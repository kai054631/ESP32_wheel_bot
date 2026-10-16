#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
#include "sdkconfig.h"

// static const char *TAG = "example";

#define ADC_PIN ADC_CHANNEL_3
#define ADC_UNIT ADC_UNIT_2
#define ADC_BITWIDTH ADC_BITWIDTH_12
#define ADC_ATTEN ADC_ATTEN_DB_12

void motorMove(int mot, int value);

#define PWMA 4 // motor A pwm pin
#define AIN2 5 // motor A phase change 2
#define AIN1 6 // motor A phase change 1

#define STBY 7 // standby pin for tb6612fng, high to run motor

#define BIN1 15 // motor B phase change 1
#define BIN2 16 // motor B phase change 2
#define PWMB 17 // motor B pwm pin

#define enAA 11 // motor A phase A encoder
#define enAB 10 // motor A phase B encoder

#define enBA 13 // motor B phase A encoder
#define enBB 12 // motor B phase B encoder

#define sda 9 // gy85 pin 1
#define scl 8 // gy85 pin 2

#define MOTOR_B 0
#define MOTOR_A 1

#define PWMB_CHANNEL LEDC_CHANNEL_0
#define PWMA_CHANNEL LEDC_CHANNEL_1
#define PWM_TIMER LEDC_TIMER_0
#define PWM_MODE LEDC_LOW_SPEED_MODE
#define PWM_DUTY_RES LEDC_TIMER_10_BIT // 10-bit resolution (0-1023)
#define PWM_FREQUENCY 25000

#define ENC_HIGH_LIMIT 32767
#define ENC_LOW_LIMIT -32767

pcnt_unit_handle_t unitA = NULL;

pcnt_unit_handle_t unitB = NULL;

void encoders_init()
{
     pcnt_unit_config_t unit_cfg = {
         .high_limit = ENC_HIGH_LIMIT,
         .low_limit = ENC_LOW_LIMIT,
     };
     ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &unitA));
     ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &unitB));

     pcnt_glitch_filter_config_t filter_cfg = {
         .max_glitch_ns = 1000};
     ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(unitA, &filter_cfg));
     ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(unitB, &filter_cfg));

     //channel A
     pcnt_chan_config_t chanAA_cfg = {
         .edge_gpio_num = enAA,
         .level_gpio_num = enAB
     };
     pcnt_channel_handle_t chanAA = NULL;
     ESP_ERROR_CHECK(pcnt_new_channel(unitA, &chanAA_cfg, &chanAA));
     pcnt_chan_config_t chanAB_cfg = {
         .edge_gpio_num = enAB,
         .level_gpio_num = enAA
     };
     pcnt_channel_handle_t chanAB = NULL;
     ESP_ERROR_CHECK(pcnt_new_channel(unitA, &chanAB_cfg, &chanAB));

     //channel B
     pcnt_chan_config_t chanBA_cfg = {
         .edge_gpio_num = enBA,
         .level_gpio_num = enBB,
     };
     pcnt_channel_handle_t chanBA = NULL;
     ESP_ERROR_CHECK(pcnt_new_channel(unitB, &chanBA_cfg, &chanBA));
     pcnt_chan_config_t chanBB_cfg = {
         .edge_gpio_num = enBB,
         .level_gpio_num = enBA,
     };
     pcnt_channel_handle_t chanBB = NULL;
     ESP_ERROR_CHECK(pcnt_new_channel(unitB, &chanBB_cfg, &chanBB));
     

     // 4x decode: every edge on A or B counts, direction s et by the other
     // channel's current level
     //channel A
     ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chanAA, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
     ESP_ERROR_CHECK(pcnt_channel_set_level_action(chanAA, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
     ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chanAB, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
     ESP_ERROR_CHECK(pcnt_channel_set_level_action(chanAB, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

     ESP_ERROR_CHECK(pcnt_unit_enable(unitA));      // enable the unit
     ESP_ERROR_CHECK(pcnt_unit_clear_count(unitA)); // clear count to 0
     ESP_ERROR_CHECK(pcnt_unit_start(unitA));       // start counting

     //channel B
     ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chanBA, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
     ESP_ERROR_CHECK(pcnt_channel_set_level_action(chanBA, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
     ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chanBB, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
     ESP_ERROR_CHECK(pcnt_channel_set_level_action(chanBB, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

     ESP_ERROR_CHECK(pcnt_unit_enable(unitB));      // enable the unit
     ESP_ERROR_CHECK(pcnt_unit_clear_count(unitB)); // clear count to 0
     ESP_ERROR_CHECK(pcnt_unit_start(unitB));       // start counting
}

void motorMove(int mot, int value)
{
     if (mot == 0) // MOTOR B
     {
          gpio_set_level(STBY, 1); // put high to enable the driver
          if (value > 100)
          {
               value = 100;
          }
          if (value > 0) // FORWARD
          {
               int duty = value * 10.23;
               gpio_set_level(BIN1, 1);
               gpio_set_level(BIN2, 0);
               ledc_set_duty(PWM_MODE, PWMB_CHANNEL, duty);
               ledc_update_duty(PWM_MODE, PWMB_CHANNEL);
               // printf(" duty cycle B : %d\n", duty);
          }
          else if (value < 0) // BACKWARD
          {
               int duty = -value * 10.23;
               gpio_set_level(BIN1, 0);
               gpio_set_level(BIN2, 1);
               ledc_set_duty(PWM_MODE, PWMB_CHANNEL, duty);
               ledc_update_duty(PWM_MODE, PWMB_CHANNEL);
               // printf(" duty cycle B : -%d\n", duty);
          }
          else // STOP
          {
               gpio_set_level(BIN1, 0);
               gpio_set_level(BIN2, 0);
               ledc_set_duty(PWM_MODE, PWMB_CHANNEL, 0);
               ledc_update_duty(PWM_MODE, PWMB_CHANNEL);
               // printf(" Motor B Stop\n");
          }
          return;
     }
     else if (mot == 1) // MOTOR A DRIVE
     {
          gpio_set_level(STBY, 1); // put high to enable the driver
          if (value > 100)
          {
               value = 100;
          }
          if (value > 0) // FORWARD
          {
               int duty = value * 10.23;
               gpio_set_level(AIN1, 1);
               gpio_set_level(AIN2, 0);
               ledc_set_duty(PWM_MODE, PWMA_CHANNEL, duty);
               ledc_update_duty(PWM_MODE, PWMA_CHANNEL);
               // printf(" duty cycle A : %d\n", duty);
          }
          else if (value < 0) // BACKWARD
          {
               int duty = -value * 10.23;
               gpio_set_level(AIN1, 0);
               gpio_set_level(AIN2, 1);
               ledc_set_duty(PWM_MODE, PWMA_CHANNEL, duty);
               ledc_update_duty(PWM_MODE, PWMA_CHANNEL);
               // printf(" duty cycle A : -%d\n", duty);
          }
          else // STOP
          {
               gpio_set_level(AIN1, 0);
               gpio_set_level(AIN2, 0);
               ledc_set_duty(PWM_MODE, PWMA_CHANNEL, 0);
               ledc_update_duty(PWM_MODE, PWMA_CHANNEL);
               // printf(" Motor A Stop\n");
          }
          return;
     }
}
void app_main(void)
{
     // UNIVERSAL PIN CONFIG
     gpio_config_t driver_conf = {
         .pin_bit_mask = (1ULL << STBY),
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

     gpio_set_level(STBY, 1); // put high to enable the driver

     encoders_init();

     // BATTERY VOLTAGE CHECK CODE
     int adc_value;
     float Volt_Gain = 1.09;
     adc_oneshot_unit_handle_t adc_handle;

     adc_oneshot_unit_init_cfg_t init_config = {
         .unit_id = ADC_UNIT,
         .clk_src = ADC_RTC_CLK_SRC_DEFAULT};
     ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

     adc_oneshot_chan_cfg_t config = {
         .bitwidth = ADC_BITWIDTH,
         .atten = ADC_ATTEN,
     };
     ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_PIN, &config));

     int countA = 0;
     int prevA = 0;
     int deltaA = 0;

     int countB = 0;
     int prevB = 0;
     int deltaB = 0;
     TickType_t last_wake = xTaskGetTickCount();

     int print=0;
     int targetA_rpm=30,targetB_rpm=30;
     float ffA=(targetA_rpm + 9.9) / 2.01,ffB=(targetB_rpm + 9.9) / 2.01;
     float intergalA=0.0,intergalB=0.0;
     int pwmA_out=0,pwmB_out=0;
     float Kp=0.5,Ki=1.0;
     while (1)
     {
          int c =getchar();
          
          if (c=='1'){
               Kp+=0.05;
               printf("Kp=%.2f\n",Kp);
          }else if (c=='2'){
               Kp-=0.05;
               printf("Kp=%.2f\n",Kp);
          }else if (c=='0'){
               Kp=0;
          }
          
          ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_PIN, &adc_value));
          float battery_volt=(adc_value / 4095.0) * 18.282 * Volt_Gain;
          if (battery_volt  >= 11.2)
          {
               ESP_ERROR_CHECK(pcnt_unit_get_count(unitA, &countA));
               ESP_ERROR_CHECK(pcnt_unit_get_count(unitB, &countB));

               deltaA = countA - prevA;
               if(deltaA>ENC_HIGH_LIMIT/2){
                    deltaA-=ENC_HIGH_LIMIT;
               }else if(deltaA<-ENC_HIGH_LIMIT/2){
                    deltaA+=ENC_HIGH_LIMIT;
               }
               
               float speedA    = deltaA/0.02;   //20ms period
               float revA      =speedA/1980.0;       //11ppr*4 time pcnt*(1:45)ratio
               float rpmA      =revA*60; 
               float velA      =rpmA*3.14*0.065/60;   //rpm*pi*65mm wheel diameter 

               deltaB = countB - prevB;
               if(deltaB>=ENC_HIGH_LIMIT/2){
                    deltaB-=32767;
               }else if(deltaB<=-ENC_HIGH_LIMIT/2){
                    deltaB+=ENC_HIGH_LIMIT;
               }
               float speedB    = deltaB/0.02;   //20ms period
               float revB      =speedB/1980.0;       //11ppr*4 time pcnt*(1:45)ratio
               float rpmB      =revB*60; 
               float velB      =rpmB*3.14*0.065/60;   //rpm*pi*65mm wheel diameter 

               float errorA =targetA_rpm - rpmA;
               intergalA = intergalA+errorA*0.02;
               if(intergalA >=30){
                    intergalA=30;
               }else if(intergalA <=-30){
                    intergalA=-30;
               }
               pwmA_out = ffA+(Kp*errorA)+(Ki*intergalA);

               float errorB =targetB_rpm - rpmB;
               intergalB = intergalB+errorB*0.02;
               if(intergalB >=30){
                    intergalB=30;
               }else if(intergalB <=-30){
                    intergalB=-30;
               }
               pwmB_out = ffB+(Kp*errorB)+(Ki*intergalB);

               if(pwmA_out>=100){
                    pwmA_out=100;
               }else if(pwmA_out<=-100){
                    pwmA_out=-100;
               }else if(pwmA_out>0 && pwmA_out<=15){
                    pwmA_out=15;
               }
               motorMove(MOTOR_A, pwmA_out);

               if(pwmB_out>=100){
                    pwmB_out=100;
               }else if(pwmB_out<=-100){
                    pwmB_out=-100;
               }else if(pwmB_out>0 && pwmB_out<=15){
                    pwmB_out=15;
               }
               motorMove(MOTOR_B, pwmB_out);

               if(print>=3){
                    printf("error A :%.3f   RPM MOTOR A : %.4f    VEVLOCITY A : %.4f    VOLT:%.2f\n",errorA, rpmA, velA, battery_volt);
                    printf("error B :%.3f   RPM MOTOR B : %.4f    VEVLOCITY B : %.4f    VOLT:%.2f\n",errorB, rpmB, velB, battery_volt);
                    print=0;
               }
               print++;

               vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20));
               prevA = countA;
               prevB = countB;
          }
          else
          {
               gpio_set_level(STBY, 0);
               // printf("MOTOR A : %d    SPEED A : %d    VOLT:%.2f\n", deltaA, adc_value, battery_volt);
               printf("battery LOW VOLT:%.2f\n",battery_volt);
               vTaskDelay(1000 / portTICK_PERIOD_MS);
          }
     }
}
