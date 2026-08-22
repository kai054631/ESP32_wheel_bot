/*
 * GY85_Check — one-shot hardware bring-up check for a GY-85 IMU breakout
 * (ADXL345 accelerometer + ITG3200 gyro + HMC5883L magnetometer) wired to
 * an ESP32-S3 over I2C.
 *
 * This is NOT a driver — it does the minimum needed to answer one question:
 * "is the GY-85 actually there, wired correctly, and responding as itself?"
 *
 * Steps:
 *   1. Bring up the I2C master bus on this check's own wiring (SDA=GPIO9, SCL=GPIO8) —
 *      independent of the full robot's pin map (SDA=11/SCL=10 in pins.txt).
 *   2. Scan all 7-bit addresses and log which ones ACK.
 *   3. For each of the three expected addresses, read that chip's own
 *      identification register(s) and check them against the datasheet
 *      value — this catches the case where *something* acks the address
 *      but it isn't actually the chip you think it is.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "GY85_Check";

#define I2C_SDA_GPIO   9
#define I2C_SCL_GPIO   8
#define I2C_PORT       I2C_NUM_0
#define I2C_FREQ_HZ    100000

// Known GY-85 chip addresses (7-bit)
#define ADXL345_ADDR   0x53   // accelerometer (0x1D if SDO/ALT tied high)
#define ITG3200_ADDR   0x68   // gyro
#define HMC5883L_ADDR  0x1E   // magnetometer

static i2c_master_bus_handle_t bus_handle;

// Add a device handle, do a register read, then remove the device handle.
// One-shot helper — fine for a bring-up check, not how a real driver would do it.
static esp_err_t read_regs(uint8_t dev_addr, uint8_t reg, uint8_t *out, size_t len)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev_handle;
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (err != ESP_OK) return err;

    err = i2c_master_transmit_receive(dev_handle, &reg, 1, out, len, 100);

    i2c_master_bus_rm_device(dev_handle);
    return err;
}

static void scan_bus(void)
{
    ESP_LOGI(TAG, "Scanning I2C bus (SDA=%d SCL=%d)...", I2C_SDA_GPIO, I2C_SCL_GPIO);
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        esp_err_t err = i2c_master_probe(bus_handle, addr, 50);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "  found device at 0x%02X", addr);
            found++;
        }
    }
    ESP_LOGI(TAG, "Scan done — %d device(s) responded.", found);
}

void app_main(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    vTaskDelay(pdMS_TO_TICKS(200));
    scan_bus();

    // --- ADXL345: DEVID register (0x00) must read back 0xE5 ---
    uint8_t devid = 0;
    if (read_regs(ADXL345_ADDR, 0x00, &devid, 1) == ESP_OK) {
        ESP_LOGI(TAG, "ADXL345 @0x%02X DEVID=0x%02X (expect 0xE5) -> %s",
                 ADXL345_ADDR, devid, devid == 0xE5 ? "MATCH" : "MISMATCH");
    } else {
        ESP_LOGW(TAG, "ADXL345 @0x%02X did not respond", ADXL345_ADDR);
    }

    // --- ITG3200: WHO_AM_I register (0x00) reads back its own 7-bit address directly ---
    // (bit0 mirrors the AD0 pin state; with AD0=low the whole byte reads as 0x68)
    uint8_t whoami = 0;
    if (read_regs(ITG3200_ADDR, 0x00, &whoami, 1) == ESP_OK) {
        ESP_LOGI(TAG, "ITG3200 @0x%02X WHO_AM_I=0x%02X (expect 0x%02X) -> %s",
                 ITG3200_ADDR, whoami, ITG3200_ADDR,
                 whoami == ITG3200_ADDR ? "MATCH" : "MISMATCH");
    } else {
        ESP_LOGW(TAG, "ITG3200 @0x%02X did not respond", ITG3200_ADDR);
    }

    // --- HMC5883L: identification regs 0x0A/0x0B/0x0C must read 'H','4','3' ---
    uint8_t id[3] = {0};
    bool hmc_ok = true;
    for (int i = 0; i < 3; i++) {
        if (read_regs(HMC5883L_ADDR, 0x0A + i, &id[i], 1) != ESP_OK) {
            hmc_ok = false;
        }
    }
    if (hmc_ok) {
        ESP_LOGI(TAG, "HMC5883L @0x%02X ID regs='%c%c%c' (expect 'H43') -> %s",
                 HMC5883L_ADDR, id[0], id[1], id[2],
                 (id[0]=='H' && id[1]=='4' && id[2]=='3') ? "MATCH" : "MISMATCH");
    } else {
        // 0x0D is the QMC5883L's fixed address — a pin/silkscreen-compatible clone
        // very commonly substituted for the genuine HMC5883L on cheap GY-85 boards.
        // It uses a completely different register map, so it won't answer at 0x1E
        // or understand HMC5883L registers at all.
        esp_err_t qmc_probe = i2c_master_probe(bus_handle, 0x0D, 50);
        if (qmc_probe == ESP_OK) {
            ESP_LOGW(TAG, "HMC5883L @0x%02X did not respond, but 0x0D acked -> "
                           "this board likely has a QMC5883L clone instead of a genuine "
                           "HMC5883L (same footprint, different address/registers)",
                     HMC5883L_ADDR);
        } else {
            ESP_LOGW(TAG, "HMC5883L @0x%02X did not respond, and no device at 0x0D either "
                           "-> magnetometer may not be wired/powered", HMC5883L_ADDR);
        }
    }

    ESP_LOGI(TAG, "Check complete.");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
