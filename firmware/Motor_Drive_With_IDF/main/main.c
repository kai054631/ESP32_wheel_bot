#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"

// static const char *TAG = "example";

void motorMove(int mot, int value);

#define PWMA 4 // motor A pwm pin
#define AIN2 5 // motor A phase change 2
#define AIN1 6 // motor A phase change 1

#define STBY 7 // standby pin for tb6612fng, high to run motor

#define BIN1 15 // motor B phase change 1
#define BIN2 16 // motor B phase change 2
#define PWMB 17 // motor B pwm pin

#define GND 18

#define enAA 8 // motor A phase A encoder
#define enAB 3 // motor A phase B encoder

#define enBA 46 // motor B phase A encoder
#define enBB 9  // motor B phase B encoder

#define sda 11 // gy85 pin 1
#define scl 10 // gy85 pin 2

#define MOTOR_B 0
#define MOTOR_A 1

#define PWMB_CHANNEL LEDC_CHANNEL_0
#define PWMA_CHANNEL LEDC_CHANNEL_1
#define PWM_TIMER LEDC_TIMER_0
#define PWM_MODE LEDC_LOW_SPEED_MODE
#define PWM_DUTY_RES LEDC_TIMER_10_BIT // 10-bit resolution (0-1023)
#define PWM_FREQUENCY 25000

#define ENC_HIGH_LIMIT 100
#define ENC_LOW_LIMIT -100

pcnt_unit_handle_t unitA = NULL;
pcnt_unit_handle_t unitB = NULL;

void encoders_init(){
     pcnt_unit_config_t unit_cfg = {
          .high_limit = ENC_HIGH_LIMIT,
          .low_limit = ENC_LOW_LIMIT,
     };
     ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg,&unitA));
     ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg,&unitB));

     pcnt_glitch_filter_config_t filter_cfg  = {
          .max_glitch_ns =    1000
     };
     ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(unitA,&filter_cfg));
     ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(unitB,&filter_cfg));

     pcnt_chan_config_t chanA_cfg = {
        .edge_gpio_num  = enAA,
        .level_gpio_num = enAB
    };
    pcnt_channel_handle_t chanA = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(unitA, &chanA_cfg, &chanA));

    pcnt_chan_config_t chanB_cfg = {
        .edge_gpio_num  = enBA,
        .level_gpio_num = enBB,
    };
    pcnt_channel_handle_t chanB = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(unitB, &chanB_cfg, &chanB));

    // 4x decode: every edge on A or B counts, direction set by the other
    // channel's current level
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chanA, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chanA,PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chanB, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chanB,PCNT_CHANNEL_LEVEL_ACTION_KEEP,PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_unit_enable(unitA));   // enable the unit
    ESP_ERROR_CHECK(pcnt_unit_clear_count(unitA));   // clear count to 0
    ESP_ERROR_CHECK(pcnt_unit_start(unitA));   // start counting

    ESP_ERROR_CHECK(pcnt_unit_enable(unitB));   // enable the unit
    ESP_ERROR_CHECK(pcnt_unit_clear_count(unitB));   // clear count to 0
    ESP_ERROR_CHECK(pcnt_unit_start(unitB));   // start counting
}




void motorMove(int mot, int value)
{
     if (mot == 0)//MOTOR B
     {
          gpio_set_level(GND, 0);  // pull low to create gnd for driver
          gpio_set_level(STBY, 1); // put high to enable the driver
          if(value>100){
               value=100;
          }
          if (value > 0) //FORWARD
          {
               int duty = value*10.23;
               gpio_set_level(BIN1, 1);
               gpio_set_level(BIN2, 0);
               ledc_set_duty(PWM_MODE, PWMB_CHANNEL, duty);
               ledc_update_duty(PWM_MODE, PWMB_CHANNEL);
               printf(" duty cycle B : %d\n", duty);
          }
          else if (value < 0) //BACKWARD
          {
               int duty = -value*10.23;
               gpio_set_level(BIN1, 0);
               gpio_set_level(BIN2, 1);
               ledc_set_duty(PWM_MODE, PWMB_CHANNEL, duty);
               ledc_update_duty(PWM_MODE, PWMB_CHANNEL);
               printf(" duty cycle B : -%d\n", duty);
          }
          else //STOP
          {
               gpio_set_level(BIN1, 0);
               gpio_set_level(BIN2, 0);
               ledc_set_duty(PWM_MODE, PWMB_CHANNEL, 0);
               ledc_update_duty(PWM_MODE, PWMB_CHANNEL);
               printf(" Motor B Stop\n");
          }
          return;
     }
     else if (mot == 1) //MOTOR A DRIVE
     {
          // gpio_set_level(GND, 0);  // pull low to create gnd for driver
          // gpio_set_level(STBY, 1); // put high to enable the driver
          if(value>100){
               value=100;
          }
          if (value > 0)      //FORWARD
          {
               int duty = value*10.23;
               gpio_set_level(AIN1, 1);
               gpio_set_level(AIN2, 0);
               ledc_set_duty(PWM_MODE, PWMA_CHANNEL, duty);
               ledc_update_duty(PWM_MODE, PWMA_CHANNEL);
               printf(" duty cycle A : %d\n", duty);
          }
          else if (value < 0) //BACKWARD
          {
               int duty =-value*10.23;
               gpio_set_level(AIN1, 0);
               gpio_set_level(AIN2, 1);
               ledc_set_duty(PWM_MODE, PWMA_CHANNEL, duty);
               ledc_update_duty(PWM_MODE, PWMA_CHANNEL);
               printf(" duty cycle A : -%d\n", duty);
          }
          else //STOP
          {
               gpio_set_level(AIN1, 0);
               gpio_set_level(AIN2, 0);
               ledc_set_duty(PWM_MODE, PWMA_CHANNEL, 0);
               ledc_update_duty(PWM_MODE, PWMA_CHANNEL);
               printf(" Motor A Stop\n");
          }
          return;
     }
}
void app_main(void)
{
     // UNIVERSAL PIN CONFIG
     gpio_config_t driver_conf = {
         .pin_bit_mask = (1ULL << STBY) | (1ULL << GND),
         .mode = GPIO_MODE_OUTPUT,
         .pull_up_en = GPIO_PULLUP_DISABLE,
         .pull_down_en = GPIO_PULLDOWN_DISABLE,
         .intr_type = GPIO_INTR_DISABLE};
     gpio_config(&driver_conf);

     // MOTOR B PIN CONFIG
     gpio_config_t driverB_conf = {
         .pin_bit_mask = (1ULL << BIN1) | (1ULL << BIN2),
         .mode = GPIO_MODE_OUTPUT,
         .pull_up_en = GPIO_PULLUP_DISABLE,
         .pull_down_en = GPIO_PULLDOWN_DISABLE,
         .intr_type = GPIO_INTR_DISABLE};
     gpio_config(&driverB_conf);

     // MOTOR A PIN CONFIG
     gpio_config_t driverA_conf = {
         .pin_bit_mask = (1ULL << AIN1) | (1ULL << AIN2),
         .mode = GPIO_MODE_OUTPUT,
         .pull_up_en = GPIO_PULLUP_DISABLE,
         .pull_down_en = GPIO_PULLDOWN_DISABLE,
         .intr_type = GPIO_INTR_DISABLE};
     gpio_config(&driverA_conf);

     // TIMER CONFIG FOR MOTOR PWM
     ledc_timer_config_t motor_timer = {
         .speed_mode = PWM_MODE,
         .duty_resolution = PWM_DUTY_RES,
         .timer_num = PWM_TIMER,
         .freq_hz = PWM_FREQUENCY};
     // ledc_timer_config(&motor_timer);
     ESP_ERROR_CHECK(ledc_timer_config(&motor_timer));
     // PWM CHANNEL CONFIG FOR MOTOR B
     ledc_channel_config_t motorB_channel = {
         .gpio_num = PWMB,
         .speed_mode = PWM_MODE,
         .channel = PWMB_CHANNEL,
         .timer_sel = PWM_TIMER,
         .duty = 0};
     // ledc_channel_config(&motorB_channel);
     ESP_ERROR_CHECK(ledc_channel_config(&motorB_channel));

     // PWM CHANNEL CONFIG FOR MOTOR A
     ledc_channel_config_t motorA_channel = {
         .gpio_num = PWMA,
         .speed_mode = PWM_MODE,
         .channel = PWMA_CHANNEL,
         .timer_sel = PWM_TIMER,
         .duty = 0};
     // ledc_channel_config(&motorA_channel);
     ESP_ERROR_CHECK(ledc_channel_config(&motorA_channel));

     gpio_set_level(GND, 0);  // pull low to create gnd for driver
     gpio_set_level(STBY, 1); // put high to enable the driver
     encoders_init();
     int countA=0;
     int countB=0;
     while (1)
     {
          // ESP_ERROR_CHECK(pcnt_unit_get_count(unitA,&countA));
          // ESP_ERROR_CHECK(pcnt_unit_get_count(unitB,&countB));
          // printf("MOTOR A : %d    MOTOR B : %d\n",countA,countB);
          // vTaskDelay(200/portTICK_PERIOD_MS);

          // motorMove(MOTOR_B, 80);
          motorMove(MOTOR_A, 20);
          vTaskDelay(3000 / portTICK_PERIOD_MS);
          // motorMove(MOTOR_B, 0);
          // motorMove(MOTOR_A, 0);
          vTaskDelay(1000 / portTICK_PERIOD_MS);
     }
}
