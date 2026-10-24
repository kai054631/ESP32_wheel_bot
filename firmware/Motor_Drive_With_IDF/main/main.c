#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
#include "sdkconfig.h"
#include "motor.h"
// static const char *TAG = "example";

#define ADC_PIN ADC_CHANNEL_3
#define ADC_UNIT ADC_UNIT_2
#define ADC_BITWIDTH ADC_BITWIDTH_12
#define ADC_ATTEN ADC_ATTEN_DB_12

#define STBY 7 // standby pin for tb6612fng, high to run motor

#define PWMB 4 // motor B pwm pin
#define BIN2 5 // motor B phase change 2
#define BIN1 6 // motor B phase change 1

#define AIN1 15 // motor A phase change 1
#define AIN2 16 // motor A phase change 2
#define PWMA 17 // motor A pwm pin

#define enBA 10 // motor B phase A encoder
#define enBB 11 // motor B phase B encoder

#define enAA 12 // motor A phase A encoder
#define enAB 13 // motor A phase B encoder

#define sda 9 // gy85 pin 1
#define scl 8 // gy85 pin 2

#define PWMB_CHANNEL LEDC_CHANNEL_0
#define PWMA_CHANNEL LEDC_CHANNEL_1

#define PWM_TIMER LEDC_TIMER_0
#define PWM_DUTY_RES LEDC_TIMER_10_BIT // 10-bit resolution (0-1023)
#define PWM_FREQUENCY 25000

pcnt_unit_handle_t unitA = NULL;
pcnt_unit_handle_t unitB = NULL;

motor_t motorA = {
    .in1 = AIN1,
    .in2 = AIN2,
    .pwm_channel = PWMA_CHANNEL};
motor_t motorB = {
    .in1 = BIN1,
    .in2 = BIN2,
    .pwm_channel = PWMB_CHANNEL};

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

     // channel A
     pcnt_chan_config_t chanAA_cfg = {
         .edge_gpio_num = enAA,
         .level_gpio_num = enAB};
     pcnt_channel_handle_t chanAA = NULL;
     ESP_ERROR_CHECK(pcnt_new_channel(unitA, &chanAA_cfg, &chanAA));
     pcnt_chan_config_t chanAB_cfg = {
         .edge_gpio_num = enAB,
         .level_gpio_num = enAA};
     pcnt_channel_handle_t chanAB = NULL;
     ESP_ERROR_CHECK(pcnt_new_channel(unitA, &chanAB_cfg, &chanAB));

     // channel B
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
     // channel A
     ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chanAA, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
     ESP_ERROR_CHECK(pcnt_channel_set_level_action(chanAA, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
     ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chanAB, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
     ESP_ERROR_CHECK(pcnt_channel_set_level_action(chanAB, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

     ESP_ERROR_CHECK(pcnt_unit_enable(unitA));      // enable the unit
     ESP_ERROR_CHECK(pcnt_unit_clear_count(unitA)); // clear count to 0
     ESP_ERROR_CHECK(pcnt_unit_start(unitA));       // start counting

     // channel B
     ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chanBA, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
     ESP_ERROR_CHECK(pcnt_channel_set_level_action(chanBA, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
     ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chanBB, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
     ESP_ERROR_CHECK(pcnt_channel_set_level_action(chanBB, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

     ESP_ERROR_CHECK(pcnt_unit_enable(unitB));      // enable the unit
     ESP_ERROR_CHECK(pcnt_unit_clear_count(unitB)); // clear count to 0
     ESP_ERROR_CHECK(pcnt_unit_start(unitB));       // start counting
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

     encoders_init();
     motorA.unit = unitA;
     motorB.unit = unitB;

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

     TickType_t last_wake = xTaskGetTickCount();

     int print = 0;
     motorA.target_rpm = 30;
     motorB.target_rpm = 30;
     while (1)
     {
          int c = getchar();
          if (c == '1')
          {
               motor_kp += 0.05;
               printf("Kp=%.2f\n", motor_kp);
          }
          else if (c == '2')
          {
               motor_kp -= 0.05;
               printf("Kp=%.2f\n", motor_kp);
          }
          else if (c == '3')
          {
               motor_ki += 0.05;
               printf("Ki=%.2f\n", motor_ki);
          }
          else if (c == '4')
          {
               motor_ki -= 0.05;
               printf("Ki=%.2f\n", motor_ki);
          }
          else if (c == '5')
          {
               motorA.target_rpm += 5;
               motorB.target_rpm += 5;
               printf("rpm=%.1f\n", motorA.target_rpm);
          }
          else if (c == '6')
          {
               motorA.target_rpm -= 5;
               motorB.target_rpm -= 5;
               printf("rpm=%.2f\n", motorA.target_rpm);
          }
          else if (c == '0')
          {
               motor_kp = 0;
               motor_ki = 0;
               motorA.target_rpm = 0;
               motorB.target_rpm = 0;
          }

          ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_PIN, &adc_value));
          float battery_volt = (adc_value / 4095.0) * 18.282 * Volt_Gain;
          if (battery_volt >= 11.2)
          {
               gpio_set_level(STBY, 1);
               motor_update(&motorA);
               motor_update(&motorB);
               if (print >= 3)
               {
                    printf("Target RPM: %.1f, RPM MOTOR A : %.4f   RPM MOTOR B : %.4f   VOLT:%.2f\n",motorA.target_rpm,motorA.rpm, motorB.rpm, battery_volt);
                    print = 0;
               }
               print++;
               vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20));
          }
          else
          {
               gpio_set_level(STBY, 0);
               // printf("MOTOR A : %d    SPEED A : %d    VOLT:%.2f\n", deltaA, adc_value, battery_volt);
               printf("battery LOW VOLT:%.2f\n", battery_volt);
               vTaskDelay(1000 / portTICK_PERIOD_MS);
          }
     }
}