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
#define CONFIGBIT_USE_0_10v1 0x8
#define CONFIGBIT_USE_0_10v2 0x10
#define CONFIGBIT_TRANSMIT_ESPNOW 0x20
#define CONFIGBIT_RECEIVE_ESPNOW 0x40

extern nvs_handle_t nvs_handle_;

extern TaskHandle_t espnowtask;

#define LOGBUFFER_SIZE 0x8000

extern volatile DRAM_ATTR char logbuffer[LOGBUFFER_SIZE];

extern int logbufferpos;

extern int actual_level;
extern int setpoint;
extern int fadetime;

typedef struct {
    uint8_t zeroten1_lvl;
    uint8_t zeroten2_lvl;
    uint8_t dali1_lvl;
    uint8_t dali2_lvl;
    uint8_t dali3_lvl;
    uint8_t dali4_lvl;
    uint8_t espnow_lvl;
    uint8_t relay1;
    uint8_t relay2;
} level_t;

typedef struct {
    int dali1;
    int dali2;
    int dali3;
    int dali4;
    int zeroten1;
    int zeroten2;
    int espnow;
} level_overrides_t;

extern volatile level_t levellut[255];

typedef struct {
    TaskHandle_t mainloop_task;
    level_overrides_t *level_overrides;
} networking_ctx_t;

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

