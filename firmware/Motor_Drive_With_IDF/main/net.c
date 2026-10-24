#include "net.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#include "wifi_secret.h" // WIFI_SSID, WIFI_PASS (copy of wifi_secret.h.example)

#define TCP_PORT 3333
#define MSG_LEN 160
#define TX_QUEUE_LEN 24
#define RX_QUEUE_LEN 64

static const char *TAG = "net";

typedef struct
{
     uint8_t len;
     char text[MSG_LEN];
} tx_msg_t;

static QueueHandle_t tx_q;
static QueueHandle_t rx_q;
static volatile int client_connected = 0;

// ---------------------------------------------------------------- Wi-Fi ----

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
     if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
     {
          esp_wifi_connect();
     }
     else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
     {
          ESP_LOGW(TAG, "wifi lost, retrying");
          esp_wifi_connect(); // keep trying forever
     }
     else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
     {
          ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
          ESP_LOGI(TAG, "got ip: " IPSTR "  -> nc " IPSTR " %d", IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.ip), TCP_PORT);
     }
}

static void wifi_start(void)
{
     esp_err_t err = nvs_flash_init(); // Wi-Fi driver keeps its calibration in NVS
     if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
     {
          ESP_ERROR_CHECK(nvs_flash_erase());
          err = nvs_flash_init();
     }
     ESP_ERROR_CHECK(err);

     ESP_ERROR_CHECK(esp_netif_init());
     ESP_ERROR_CHECK(esp_event_loop_create_default());
     esp_netif_create_default_wifi_sta();

     wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
     ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
     ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
     ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

     wifi_config_t cfg = {0};
     strncpy((char *)cfg.sta.ssid, WIFI_SSID, sizeof(cfg.sta.ssid) - 1);
     strncpy((char *)cfg.sta.password, WIFI_PASS, sizeof(cfg.sta.password) - 1);
     cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // WPA2 or better

     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
     ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
     ESP_ERROR_CHECK(esp_wifi_start());
     ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE)); // no modem sleep: lower latency
}

// ------------------------------------------------------------ TCP server ----

// Send everything, or return -1 (peer gone / stalled).
static int send_all(int sock, const char *p, int len)
{
     while (len > 0)
     {
          int n = send(sock, p, len, 0);
          if (n <= 0)
          {
               return -1;
          }
          p += n;
          len -= n;
     }
     return 0;
}

// Serve one client until it disconnects.
static void serve_client(int sock)
{
     struct timeval rx_to = {.tv_sec = 0, .tv_usec = 20 * 1000}; // recv waits <= 20 ms
     struct timeval tx_to = {.tv_sec = 1, .tv_usec = 0};         // a stuck send gives up
     int on = 1;
     int idle = 5, intvl = 2, cnt = 3; // detect a dead peer in ~11 s
     setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rx_to, sizeof(rx_to));
     setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tx_to, sizeof(tx_to));
     setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
     setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
     setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
     setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
     setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));

     xQueueReset(tx_q); // old lines are stale
     xQueueReset(rx_q);
     client_connected = 1;

     while (1)
     {
          tx_msg_t m;
          while (xQueueReceive(tx_q, &m, 0) == pdTRUE)
          {
               if (send_all(sock, m.text, m.len) < 0)
               {
                    goto done;
               }
          }

          uint8_t buf[32];
          int n = recv(sock, buf, sizeof(buf), 0); // waits at most 20 ms
          if (n > 0)
          {
               for (int i = 0; i < n; i++)
               {
                    xQueueSend(rx_q, &buf[i], 0); // full: drop
               }
          }
          else if (n == 0)
          {
               break; // peer closed
          }
          else if (errno != EAGAIN && errno != EWOULDBLOCK)
          {
               break; // real error
          }
     }
done:
     client_connected = 0;
     ESP_LOGI(TAG, "client left");
}

static void tcp_task(void *arg)
{
     while (1)
     {
          int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
          if (listener < 0)
          {
               vTaskDelay(pdMS_TO_TICKS(1000));
               continue;
          }
          int on = 1;
          setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

          struct sockaddr_in addr = {
              .sin_family = AF_INET,
              .sin_port = htons(TCP_PORT),
              .sin_addr.s_addr = htonl(INADDR_ANY),
          };
          if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listener, 1) != 0)
          {
               ESP_LOGE(TAG, "bind/listen failed, retry");
               close(listener);
               vTaskDelay(pdMS_TO_TICKS(1000));
               continue;
          }
          ESP_LOGI(TAG, "listening on port %d", TCP_PORT);

          while (1)
          {
               int sock = accept(listener, NULL, NULL); // blocks until a client comes
               if (sock < 0)
               {
                    break; // rebuild the listener
               }
               ESP_LOGI(TAG, "client connected");
               serve_client(sock);
               shutdown(sock, 0);
               close(sock);
          }
          close(listener);
     }
}

// ------------------------------------------------------------ public API ----

void net_init(void)
{
     tx_q = xQueueCreate(TX_QUEUE_LEN, sizeof(tx_msg_t));
     rx_q = xQueueCreate(RX_QUEUE_LEN, sizeof(uint8_t));
     wifi_start();
     // core 1: keep the network away from the control loop (app_main runs on core 0)
     xTaskCreatePinnedToCore(tcp_task, "tcp", 4096, NULL, 3, NULL, 1);
}

int net_printf(const char *fmt, ...)
{
     if (!client_connected)
     {
          return 0;
     }
     tx_msg_t m;
     va_list ap;
     va_start(ap, fmt);
     int n = vsnprintf(m.text, sizeof(m.text), fmt, ap);
     va_end(ap);
     if (n < 0)
     {
          return 0;
     }
     m.len = (n >= (int)sizeof(m.text)) ? sizeof(m.text) - 1 : n; // truncated line
     return xQueueSend(tx_q, &m, 0) == pdTRUE ? m.len : 0;       // never waits
}

int net_getc(void)
{
     uint8_t c;
     if (!rx_q)
     {
          return -1; // net_init() not called yet
     }
     return xQueueReceive(rx_q, &c, 0) == pdTRUE ? c : -1;
}

int net_connected(void)
{
     return client_connected;
}
