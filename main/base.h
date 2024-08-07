#ifndef base_H
#define base_H

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/gptimer.h"
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

#define USE_DEFAULT_FADETIME 0xFFFF
#define USE_SLOW_FADETIME 0xFFFD

#define SETPOINT_SOURCE_REST 0
#define SETPOINT_SOURCE_ADC 1
#define SETPOINT_SOURCE_ESPNOW 2
#define SETPOINT_SOURCE_ALARM 4
#define SETPOINT_SOURCE_BUTTONS 3
#define SETPOINT_SOURCE_RANDOM 5

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

#define LOGBUFFER_SIZE 0x10000

extern char logbuffer[LOGBUFFER_SIZE + 16];

extern int logbufferpos;

extern int actual_level;
extern int setpoint;
extern int fadetime;

typedef struct {
    uint8_t zeroten1_lvl;
    uint8_t zeroten2_lvl;
    uint8_t dali_lvl[6];
    uint8_t espnow_lvl;
    uint8_t relay1;
    uint8_t relay2;
} level_t;

typedef struct {
    uint16_t fadetime_256ms;
    uint8_t setpoint_source;
    uint8_t setpoint;
} setpoint_notify_t;

typedef struct {
    int16_t dali[6];
    int16_t zeroten1;
    int16_t zeroten2;
    int16_t espnow;
} level_overrides_t;

extern volatile level_t levellut[255];


#define DALI_COMMAND_COMMISSION 1
#define DALI_COMMAND_FIND_NEW_DEVICES 4
#define DALI_COMMAND_SET_FAILSAFE_LEVEL 2
#define DALI_COMMAND_SET_POWER_ON_LEVEL 3

typedef struct {
    uint16_t time;
    int8_t edgetype;
} edge_t;

typedef struct {
    edge_t edges[64];
    uint8_t length;
}
edgeframe;

typedef struct {
    QueueHandle_t queue;
    uint8_t gpio_pin;
    gptimer_handle_t timer;
    uint32_t timeout;
    bool invert;
    edgeframe edgeframe_template;
    uint8_t edgeframe_isr_state;
    uint8_t edgeframe_isr_numedges;
    uint64_t edgeframe_tempcount;
    bool enabled;
    // uint64_t edgeframe_startcount;
} edgeframe_isr_ctx_t;

typedef struct {
    uint8_t gpio_pin;
    uint8_t state;
    uint16_t data;
    uint32_t notify_idx;
    TaskHandle_t notify_task;
    uint8_t bitpos;
    gptimer_handle_t timer;
    QueueHandle_t isr_queue;
    QueueHandle_t transmit_queue;
    uint8_t invert;
    gptimer_alarm_config_t alarmconf;
    uint16_t bitwork;
    BaseType_t taskawoken;
} dali_transmit_isr_ctx;

typedef struct {
    uint8_t number;
    gptimer_handle_t timer;
    dali_transmit_isr_ctx *ctx;
    QueueHandle_t queue;
    uint16_t frameidcounter;
} dali_transmitter_handle_t;


typedef struct {
    QueueHandle_t dali_received_frame_queue;
    QueueHandle_t dali_command_queue;
    dali_transmitter_handle_t transmitter;
    uint32_t frameidpass;
    edgeframe_isr_ctx_t* edgeframe_isr_ctx;
    TaskHandle_t mainloop_task;
    SemaphoreHandle_t bus_mutex;
} dali_transceiver_t;

typedef struct dali_transceiver_t *dali_transceiver_handle_t;

typedef struct {
    TaskHandle_t mainloop_task;
    level_overrides_t *level_overrides;
    QueueHandle_t dali_command_queue;
} networking_ctx_t;

void build_nvs_key_for_gpio_gain(int gpio, char* keybuf);

int32_t _MAX(int32_t a, int32_t b);

int32_t _MIN(int32_t a, int32_t b);

int clamp(int in, int low, int high);

uint64_t get_system_time_us(uint64_t offset);

extern SemaphoreHandle_t log_mutex;

void initialise_logbuffer();

int log_string(char *logstring, int bytes_to_log, bool addtime);

typedef struct {
    const char name[24];
    const int* array;
} api_endpoint_t;

// #define ENDPOINT_DEF(name) static const api_endpoint_t ##name_endpoint = {.name = "name",.array = &name}
#define ENDPOINT_DEC(name) const api_endpoint_t name##_endpoint

#endif // base_H

