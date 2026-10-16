#include "motor.h"


void motor_drive(motor_t *m, int value)
{
     if (value > 0) // FORWARD
     {
          if(value>=100){
               value=100;
          }
          int duty = value * 10.23;
          gpio_set_level(m->in1, 1);
          gpio_set_level(m->in2, 0);
          ledc_set_duty(PWM_MODE, m->pwm_channel, duty);
          ledc_update_duty(PWM_MODE, m->pwm_channel);
          // printf(" duty cycle B : %d\n", duty);
     }
     else if (value < 0) // BACKWARD
     {
          if(value<=-100){
               value=-100;
          }
          int duty = -value * 10.23;
          gpio_set_level(m->in1, 0);
          gpio_set_level(m->in2, 1);
          ledc_set_duty(PWM_MODE, m->pwm_channel, duty);
          ledc_update_duty(PWM_MODE, m->pwm_channel);
          // printf(" duty cycle B : -%d\n", duty);
     }
     else // STOP
     {
          gpio_set_level(m->in1, 0);
          gpio_set_level(m->in2, 0);
          ledc_set_duty(PWM_MODE, m->pwm_channel, 0);
          ledc_update_duty(PWM_MODE, m->pwm_channel);
          // printf(" Motor B Stop\n");
     }
};