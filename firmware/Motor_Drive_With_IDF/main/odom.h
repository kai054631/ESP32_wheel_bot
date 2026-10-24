#pragma once
#include "motor.h"

#define TRACK_WIDTH 0.1832

typedef struct{
     float x;
     float y;
     float theta;
}odom_t;

void odom_update(odom_t *o,motor_t *left,motor_t *right);