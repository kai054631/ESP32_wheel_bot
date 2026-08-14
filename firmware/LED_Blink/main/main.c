#include <stdio.h>  //standard c library for Printf 
#include "freertos/FreeRTOS.h"     //provide free RTOS type and function
#include "freertos/task.h"    //for vTaskDelay function
#include "driver/gpio.h" // include the funtion required to configure and control gpio
#include "sdkconfig.h"   //include the project configuration file

#define LED_GPIO 9

void app_main(void)

{    //Void setup   (configure pin)
     gpio_reset_pin(LED_GPIO);     //clear previous configuration
     gpio_set_direction(LED_GPIO,GPIO_MODE_OUTPUT);
     
     //loop to blink led
     while(1)
     {
          printf("LED ON\n");
          gpio_set_level(LED_GPIO,1);
          vTaskDelay(1000/portTICK_PERIOD_MS);

          printf("LED OFF\n");
          gpio_set_level(LED_GPIO,0);
          vTaskDelay(100/portTICK_PERIOD_MS);
     }

}
