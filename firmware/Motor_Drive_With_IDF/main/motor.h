#pragma once
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include <stdio.h>

#define PWM_MODE LEDC_LOW_SPEED_MODE
#define TICKS_PER_REV 1980 //ppr(11)*gear_ratio(1:45)*decode times(4)
#define DT 0.02 //20ms per cycle

#define ENC_HIGH_LIMIT 32767
#define ENC_LOW_LIMIT -32767

extern float motor_kp, motor_ki;
typedef struct{
     int in1,in2;
     ledc_channel_t pwm_channel;
     pcnt_unit_handle_t unit;
     
     int prev;
     int delta;
     float target_rpm; 
     float integral;
     int pwm_out;
     float rpm;      
} motor_t;

void motor_drive(motor_t *m,int val);
void motor_update(motor_t *m);