#include <stdio.h>  //standard c library for Printf 
#include "freertos/FreeRTOS.h"     //provide free RTOS type and function
#include "freertos/task.h"    //for vTaskDelay function
#include "driver/gpio.h" // include the funtion required to configure and control gpio
#include "sdkconfig.h"   //include the project configuration file

#define LED_GPIO 9
#define switch_GPIO 46
void app_main(void)

{    //   Void setup  
     //   (configure pin)
     gpio_config_t led_conf ={
          .pin_bit_mask = (1ULL<< LED_GPIO),
          .mode = GPIO_MODE_OUTPUT,
          .pull_up_en = GPIO_PULLUP_DISABLE,
          .pull_down_en = GPIO_PULLDOWN_DISABLE,
          .intr_type = GPIO_INTR_DISABLE
     };
     gpio_config(&led_conf);
     gpio_config_t switch_conf ={
          .pin_bit_mask = (1ULL<< switch_GPIO),
          .mode = GPIO_MODE_INPUT,
          .pull_up_en = GPIO_PULLUP_ENABLE,
          .pull_down_en = GPIO_PULLDOWN_DISABLE,
          .intr_type = GPIO_INTR_DISABLE
     };
     gpio_config(&switch_conf);

     //loop to blink led
     while(1)
     {
          if(gpio_get_level(switch_GPIO)==1)
          {
               printf("LED 1 ON\n");
               gpio_set_level(LED_GPIO,1);
          }else
          {
               printf("LED 1 OFF\n");
               gpio_set_level(LED_GPIO,0);
          }
          
          vTaskDelay(200/portTICK_PERIOD_MS);
          // gpio_set_level(LED_GPIO2,1);
          // vTaskDelay(1000/portTICK_PERIOD_MS);
     }

}
