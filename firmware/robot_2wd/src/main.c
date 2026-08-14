#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
//esp32 esplogi print by 1 second

static const char* TAG = "MyModules";
 
int i=0;

void ESP_LOGI_PRINT(void *pvParameters){

     ESP_LOGI(TAG, "Application started successfully.");
     while(1){
          ESP_LOGI(TAG,"counter = %d",i=i+1);
          vTaskDelay(pdMS_TO_TICKS(1000));
     }
     
}
void app_main(void) {
     xTaskCreate(ESP_LOGI_PRINT,"print_esp_logi_task",2048,NULL,5,NULL);
}

