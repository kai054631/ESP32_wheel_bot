#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#define LED_GPIO 9
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LED_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES LEDC_TIMER_10_BIT // 10-bit resolution (0-1023)
#define LEDC_FREQUENCY 1000


#define button_gpio 46

void app_main(void)
{
     gpio_config_t
     ledc_timer_config_t ledc_timer={
          .speed_mode  =  LEDC_MODE,
          .duty_resolution = LEDC_DUTY_RES,
          .timer_num = LED_TIMER,
          .freq_hz = LEDC_FREQUENCY
     };
     ledc_timer_config(&ledc_timer);
     ledc_channel_config_t ledc_channel = {
          .gpio_num = LED_GPIO,
          .speed_mode = LEDC_MODE,
          .channel = LEDC_CHANNEL,
          .timer_sel = LED_TIMER,
          .duty = 0
     };
     ledc_channel_config(&ledc_channel);
     while(1){
          for(int duty = 0; duty <= 1023;duty += 10){
               ledc_set_duty(LEDC_MODE,LEDC_CHANNEL,duty);
               ledc_update_duty(LEDC_MODE,LEDC_CHANNEL);
               vTaskDelay(20/portTICK_PERIOD_MS);
               printf("led duty cycle : %d\n",duty);

          }
          for(int duty = 1023; duty >= 0;duty -= 10){
               ledc_set_duty(LEDC_MODE,LEDC_CHANNEL,duty);
               ledc_update_duty(LEDC_MODE,LEDC_CHANNEL);
               vTaskDelay(20/portTICK_PERIOD_MS);
               printf("led duty cycle : %d\n",duty);
               
          }
     }

}
