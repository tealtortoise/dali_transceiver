#include "base.h"

#include <stdio.h>
#include "sys/time.h"
#include "stdint.h"

#define STARTUP_SETPOINT 10

int actual_level = 0;
int setpoint = STARTUP_SETPOINT;
int fadetime = 50;
int duty = -1;
int maxduty = -1;
int default_fadetime = 2000;

int alarm1_hour = 0;
int alarm2_hour = 0;
int alarm3_hour = 0;
int alarm1_setpoint = 0;
int alarm2_setpoint = 0;
int alarm3_setpoint = 0;
int alarm1_min = 0;
int alarm2_min = 0;
int alarm3_min = 0;
int alarm1_fade = 20000;
int alarm2_fade = 20000;
int alarm3_fade = 20000;
int alarm1_enable = 0;
int alarm2_enable = 0;
int alarm3_enable = 0;
int full_power = 256;
struct timeval time_;

void build_nvs_key_for_gpio_gain(int gpio, char* keybuf){
    sprintf(keybuf, "GPIOPIN %hhu GAIN", gpio);
}

int clamp(int in, int low, int high){
    return (in >low) ? min(in, high) : low;
}

uint64_t get_system_time_us(uint64_t offset){
    gettimeofday(&time_, NULL);
    // return time_.tv_sec * 1000000LL + time_.tv_usec - offset;
    return time_.tv_usec - offset;
}