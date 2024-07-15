#ifndef base_H
#define base_H

#include "stdint.h"

#ifndef min
    #define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#define RESISTOR_CHANGE_NOTIFY_INDEX 2
#define SETPOINT_SLEW_NOTIFY_INDEX 1
#define LIGHT_LEVEL_NOTIFY_INDEX 0

#define SUSPEND_MAIN_LOOP_LEVEL 0xFFFF
#define RESUME_MAIN_LOOP_LEVEL 0xFFF0

#define USE_DEFAULT_FADETIME 0xFFFFFFFF

extern int actual_level;
extern int setpoint;
extern int fadetime;
extern int default_fadetime;

extern int alarm1_hour;
extern int alarm1_min;
extern int alarm1_fade;
extern int alarm1_setpoint;
extern int alarm1_enable;
extern int alarm2_hour;
extern int alarm2_min;
extern int alarm2_fade;
extern int alarm2_setpoint;
extern int alarm2_enable;
extern int alarm3_hour;
extern int alarm3_min;
extern int alarm3_fade;
extern int alarm3_setpoint;
extern int alarm3_enable;
extern int full_power;


void build_nvs_key_for_gpio_gain(int gpio, char* keybuf);

int clamp(int in, int low, int high);

uint64_t get_system_time_us(uint64_t offset);

#endif // base_H

