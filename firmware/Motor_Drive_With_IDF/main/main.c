#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"

// static const char *TAG = "example";

void motorMove(int mot, int value);

// DRV8833: no separate PWM pin per channel — PWM goes directly onto the
// two IN pins themselves (AIN1/AIN2 for motor A, BIN1/BIN2 for motor B).
// PWMA/PWMB from the old TB6612 wiring are gone; nothing drives GPIO4/17
// anymore.
#define AIN2 5 // motor A input 2 (PWM)
#define AIN1 6 // motor A input 1 (PWM)

#define STBY 7 // DRV8833 nSLEEP — high to wake the driver (same pin as before)

#define BIN1 15 // motor B input 1 (PWM)
#define BIN2 16 // motor B input 2 (PWM)

// GPIO18 fake-"GND" is gone — pins.txt already records the wire moved to a
// real GND pin on the board; the code just never caught up until now.

#define enAA 8 // motor A phase A encoder
#define enAB 3 // motor A phase B encoder

#define enBA 46 // motor B phase A encoder
#define enBB 9  // motor B phase B encoder

#define sda 11 // gy85 pin 1
#define scl 10 // gy85 pin 2

#define MOTOR_B 0
#define MOTOR_A 1

// One LEDC channel per input pin — DRV8833 needs 4 independent PWM
// channels (AIN1, AIN2, BIN1, BIN2), not 2 PWM + 4 plain-GPIO direction pins.
#define AIN1_CHANNEL LEDC_CHANNEL_0
#define AIN2_CHANNEL LEDC_CHANNEL_1
#define BIN1_CHANNEL LEDC_CHANNEL_2
#define BIN2_CHANNEL LEDC_CHANNEL_3
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




// Drive one LEDC channel's duty, zeroing the other channel on the same
// H-bridge half. DRV8833 direction is set by *which* input pin carries the
// PWM — the other pin is held low (coast-decay drive: 0/PWM, not PWM/PWM).
static void set_half_bridge(ledc_channel_t drive_chan, ledc_channel_t idle_chan, int duty)
{
     ledc_set_duty(PWM_MODE, idle_chan, 0);
     ledc_update_duty(PWM_MODE, idle_chan);
     ledc_set_duty(PWM_MODE, drive_chan, duty);
     ledc_update_duty(PWM_MODE, drive_chan);
}

void motorMove(int mot, int value)
{
     if (value > 100) value = 100;
     if (value < -100) value = -100;
     int duty = (abs(value) * 1023) / 100;

     if (mot == MOTOR_B)
     {
          if (value > 0) //FORWARD: PWM on BIN1, BIN2 held low
          {
               set_half_bridge(BIN1_CHANNEL, BIN2_CHANNEL, duty);
               printf(" duty cycle B : %d\n", duty);
          }
          else if (value < 0) //BACKWARD: PWM on BIN2, BIN1 held low
          {
               set_half_bridge(BIN2_CHANNEL, BIN1_CHANNEL, duty);
               printf(" duty cycle B : -%d\n", duty);
          }
          else //STOP (coast — both inputs low)
          {
               set_half_bridge(BIN1_CHANNEL, BIN2_CHANNEL, 0);
               printf(" Motor B Stop\n");
          }
          return;
     }
     else if (mot == MOTOR_A)
     {
          if (value > 0)      //FORWARD: PWM on AIN1, AIN2 held low
          {
               set_half_bridge(AIN1_CHANNEL, AIN2_CHANNEL, duty);
               printf(" duty cycle A : %d\n", duty);
          }
          else if (value < 0) //BACKWARD: PWM on AIN2, AIN1 held low
          {
               set_half_bridge(AIN2_CHANNEL, AIN1_CHANNEL, duty);
               printf(" duty cycle A : -%d\n", duty);
          }
          else //STOP (coast — both inputs low)
          {
               set_half_bridge(AIN1_CHANNEL, AIN2_CHANNEL, 0);
               printf(" Motor A Stop\n");
          }
          return;
     }
}
void app_main(void)
{
     // STBY (DRV8833 nSLEEP) is the only plain GPIO left — AIN1/AIN2/BIN1/BIN2
     // are claimed directly by LEDC below, not gpio_config'd here. Configuring
     // them as plain outputs too would be redundant: LEDC's GPIO-matrix
     // routing wins over gpio_config on the same pin anyway.
     gpio_config_t driver_conf = {
         .pin_bit_mask = (1ULL << STBY),
         .mode = GPIO_MODE_OUTPUT,
         .pull_up_en = GPIO_PULLUP_DISABLE,
         .pull_down_en = GPIO_PULLDOWN_DISABLE,
         .intr_type = GPIO_INTR_DISABLE};
     gpio_config(&driver_conf);

     // TIMER CONFIG FOR MOTOR PWM — shared by all 4 channels
     ledc_timer_config_t motor_timer = {
         .speed_mode = PWM_MODE,
         .duty_resolution = PWM_DUTY_RES,
         .timer_num = PWM_TIMER,
         .freq_hz = PWM_FREQUENCY};
     ESP_ERROR_CHECK(ledc_timer_config(&motor_timer));

     // One LEDC channel per DRV8833 input pin, all sharing PWM_TIMER.
     ledc_channel_config_t ain1_channel = {
         .gpio_num = AIN1, .speed_mode = PWM_MODE,
         .channel = AIN1_CHANNEL, .timer_sel = PWM_TIMER, .duty = 0};
     ESP_ERROR_CHECK(ledc_channel_config(&ain1_channel));

     ledc_channel_config_t ain2_channel = {
         .gpio_num = AIN2, .speed_mode = PWM_MODE,
         .channel = AIN2_CHANNEL, .timer_sel = PWM_TIMER, .duty = 0};
     ESP_ERROR_CHECK(ledc_channel_config(&ain2_channel));

     ledc_channel_config_t bin1_channel = {
         .gpio_num = BIN1, .speed_mode = PWM_MODE,
         .channel = BIN1_CHANNEL, .timer_sel = PWM_TIMER, .duty = 0};
     ESP_ERROR_CHECK(ledc_channel_config(&bin1_channel));

     ledc_channel_config_t bin2_channel = {
         .gpio_num = BIN2, .speed_mode = PWM_MODE,
         .channel = BIN2_CHANNEL, .timer_sel = PWM_TIMER, .duty = 0};
     ESP_ERROR_CHECK(ledc_channel_config(&bin2_channel));

     gpio_set_level(STBY, 1); // wake the DRV8833 (nSLEEP high)
     encoders_init();
     int countA = 0;
     int countB = 0;

     // DRV8833 bring-up sequence: each motor, both directions, with a stop
     // in between so you can see each transition cleanly on the bench.
     while (1)
     {
          printf("== MOTOR A forward ==\n");
          motorMove(MOTOR_A, 30);
          vTaskDelay(pdMS_TO_TICKS(2000));
          motorMove(MOTOR_A, 0);
          vTaskDelay(pdMS_TO_TICKS(1000));

          printf("== MOTOR A backward ==\n");
          motorMove(MOTOR_A, -30);
          vTaskDelay(pdMS_TO_TICKS(2000));
          motorMove(MOTOR_A, 0);
          vTaskDelay(pdMS_TO_TICKS(1000));

          printf("== MOTOR B forward ==\n");
          motorMove(MOTOR_B, 30);
          vTaskDelay(pdMS_TO_TICKS(2000));
          motorMove(MOTOR_B, 0);
          vTaskDelay(pdMS_TO_TICKS(1000));

          printf("== MOTOR B backward ==\n");
          motorMove(MOTOR_B, -30);
          vTaskDelay(pdMS_TO_TICKS(2000));
          motorMove(MOTOR_B, 0);
          vTaskDelay(pdMS_TO_TICKS(1000));

          ESP_ERROR_CHECK(pcnt_unit_get_count(unitA, &countA));
          ESP_ERROR_CHECK(pcnt_unit_get_count(unitB, &countB));
          printf("encoder counts -> A: %d   B: %d\n", countA, countB);
     }
}
