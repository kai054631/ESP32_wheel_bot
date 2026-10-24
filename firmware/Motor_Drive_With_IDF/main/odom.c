#include "odom.h"
#include "math.h"
void odom_update(odom_t *o,motor_t *left,motor_t *right){
     float distance=(left->dist+right->dist)/2;             //distance of a step
     float dtheta=(right->dist-left->dist)/TRACK_WIDTH;     //theta of a step

     o->theta = o->theta + dtheta;           //updated theta

     o->x += distance * cosf(o->theta);
     o->y += distance * sinf(o->theta);
     



}