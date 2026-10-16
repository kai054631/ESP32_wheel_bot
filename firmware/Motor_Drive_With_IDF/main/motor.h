#pragma once
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"

#define PWM_MODE LEDC_LOW_SPEED_MODE


typedef struct{
     int in1,in2;
     ledc_channel_t pwm_channel;
     pcnt_unit_handle_t unit;

     int prev;
     int delta;
     int target_rpm; 
     float integral;
     int pwm_out;
     float rpm; 
     
} motor_t;

void motor_update(motor_t *m);
void motor_drive(motor_t *m,int val);
