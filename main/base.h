#ifndef base_H
#define base_H

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "nvs_flash.h"
#include "nvs.h"

// #ifndef min
    // #define min(a,b) ((a) < (b) ? (a) : (b))
// #endif

#define RESISTOR_CHANGE_NOTIFY_INDEX 2
#define SETPOINT_SLEW_NOTIFY_INDEX 1
#define LIGHT_LEVEL_NOTIFY_INDEX 0

#define SUSPEND_MAIN_LOOP_LEVEL 0xFFFF
#define RESUME_MAIN_LOOP_LEVEL 0xFFF0

#define USE_DEFAULT_FADETIME 0xFFFFFFFF

#define GET_SETTING_NOT_FOUND 0x80000000


#define CONFIGBIT_USE_RELAY1 0x1
#define CONFIGBIT_USE_RELAY2 0x2
#define CONFIGBIT_USE_DALI 0x4
#define CONFIGBIT_USE_ESPNOW 0x8
#define CONFIGBIT_USE_0_10v1 0x10
#define CONFIGBIT_USE_0_10v2 0x20

extern nvs_handle_t nvs_handle_;

extern TaskHandle_t espnowtask;

#define LOGBUFFER_SIZE 0xFFFF

extern volatile char logbuffer[LOGBUFFER_SIZE];

extern int logbufferpos;

extern int actual_level;
extern int setpoint;
extern int fadetime;

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

int32_t _MAX(int32_t a, int32_t b);

int32_t _MIN(int32_t a, int32_t b);

int clamp(int in, int low, int high);

uint64_t get_system_time_us(uint64_t offset);

void initialise_logbuffer();

void log_string(char *logstring);

typedef struct {
    const char name[24];
    const int* array;
} api_endpoint_t;

// #define ENDPOINT_DEF(name) static const api_endpoint_t ##name_endpoint = {.name = "name",.array = &name}
#define ENDPOINT_DEC(name) const api_endpoint_t name##_endpoint

// #define ALARMDEF()

// ENDPOINT_DEC(alarm1_hour);
// ENDPOINT_DEC(alarm2_hour);
// ENDPOINT_DEC(alarm3_hour);
// ENDPOINT_DEC(alarm1_enable);
// ENDPOINT_DEC(alarm2_enable);
// ENDPOINT_DEC(alarm3_enable);
// ENDPOINT_DEC(alarm1_min);
// ENDPOINT_DEC(alarm2_min);
// ENDPOINT_DEC(alarm3_min);
// ENDPOINT_DEC(alarm1_fade);
// ENDPOINT_DEC(alarm2_fade);
// ENDPOINT_DEC(alarm3_fade);
// ENDPOINT_DEC(full_power);
// ENDPOINT_DEC(default_fadetime);


#endif // base_H

