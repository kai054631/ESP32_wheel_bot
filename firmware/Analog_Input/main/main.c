// #include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
#include "sdkconfig.h"

static const char *TAG = "ADC_READING";

#define ADC_PIN          ADC_CHANNEL_8
#define ADC_UNIT         ADC_UNIT_1
#define ADC_BITWIDTH    ADC_BITWIDTH_12
#define ADC_ATTEN        ADC_ATTEN_DB_12

void app_main(void)
{
     int adc_value;
     adc_oneshot_unit_handle_t adc_handle;

     adc_oneshot_unit_init_cfg_t init_config = {
          .unit_id=ADC_UNIT,
          .clk_src=ADC_RTC_CLK_SRC_DEFAULT
     };
     ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config,&adc_handle));

     adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH,
        .atten = ADC_ATTEN,
     };
     ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_PIN, &config));

     while(1){
          ESP_ERROR_CHECK(adc_oneshot_read(adc_handle,ADC_PIN,&adc_value));
          ESP_LOGI(TAG,"ADC Value : %d\n",adc_value);
          vTaskDelay(1000/portTICK_PERIOD_MS);
     }
}
