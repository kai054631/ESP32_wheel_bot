#include "motor.h"

float motor_kp = 0.5, motor_ki = 1.0;

void motor_drive(motor_t *m, int value)
{
     if (value > 0) // FORWARD
     {
          if (value >= 100)
          {
               value = 100;
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
          if (value <= -100)
          {
               value = -100;
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

void motor_update(motor_t *m)
{
     int count = 0;
     float limit_integral =10;
     // calculate tick per rev
     ESP_ERROR_CHECK(pcnt_unit_get_count(m->unit, &count));
     m->delta = count - m->prev;
     if (m->delta > ENC_HIGH_LIMIT / 2)
     {
          m->delta -= ENC_HIGH_LIMIT;
     }
     else if (m->delta < -ENC_HIGH_LIMIT / 2)
     {
          m->delta += ENC_HIGH_LIMIT;
     }
     // convert tick per rev to rpm and velocity
     float speed = m->delta / DT;       // 20ms period
     float rev = speed / TICKS_PER_REV; // 11ppr*4 time pcnt*(1:45)ratio
     m->rpm = rev * 60;
     // float velo = m->rpm * 3.14 * 0.065 / 60; // rpm*pi*65mm wheel diameter
     m->prev = count;
     float ff = (m->target_rpm + 9.9) / 2.01;
     if (m->target_rpm != 0)
     {
          // close loop control
          float error = m->target_rpm - m->rpm;
          m->integral = m->integral + error * DT;
          if (m->integral >= limit_integral)
          {
               m->integral = limit_integral;
          }
          else if (m->integral <= -limit_integral)
          {
               m->integral = -limit_integral;
          }
          m->pwm_out = ff + (motor_kp * error) + (motor_ki * m->integral);
          if (m->pwm_out >= 100)
          {
               m->pwm_out = 100;
          }
          else if (m->pwm_out <= -100)
          {
               m->pwm_out = -100;
          }
          else if (m->pwm_out > 0 && m->pwm_out < 15)
          {
               m->pwm_out = 15;
          }
          motor_drive(m, m->pwm_out);
     }
     else
     {
          m->integral = 0;
          m->pwm_out = 0;
          motor_drive(m, m->pwm_out);
     }
}