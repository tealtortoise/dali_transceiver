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
#define NEW_SETPOINT_NOTIFY_IDX 1
#define LIGHT_LEVEL_NOTIFY_INDEX 0

#define SUSPEND_MAIN_LOOP_LEVEL 0xFFFF
#define RESUME_MAIN_LOOP_LEVEL 0xFFF0

#define USE_DEFAULT_FADETIME 0xFFFFFFFF
#define USE_SLOW_FADETIME 0xFFFFFFFE

#define SETPOINT_SOURCE_REST 0
#define SETPOINT_SOURCE_ADC 1
#define SETPOINT_SOURCE_ESPNOW 2
#define SETPOINT_SOURCE_ALARM 4
#define SETPOINT_SOURCE_BUTTONS 3
#define SETPOINT_SOURCE_RANDOM 5
#define SETPOINT_SOURCE_INIT 6

#define GET_SETTING_NOT_FOUND 0x80000000

#define CONFIGBIT_USE_RELAY1 0x1
#define CONFIGBIT_USE_RELAY2 0x2
#define CONFIGBIT_USE_DALI 0x4
#define CONFIGBIT_USE_0_10v1 0x8
#define CONFIGBIT_USE_0_10v2 0x10
#define CONFIGBIT_TRANSMIT_ESPNOW 0x20
#define CONFIGBIT_RECEIVE_ESPNOW 0x40

#define ALARM_TYPE_UNIVERSAL 0
#define ALARM_TYPE_UP_ONLY 1
#define ALARM_TYPE_DOWN_ONLY 2

#define DALI_MAX_EDGEFRAME_LENGTH 40

#define DALI_CHANNELS 8

#define STATUS_STALE_FLAG 0x12345678

extern nvs_handle_t nvs_handle_;

extern TaskHandle_t espnowtask;

#define LOGBUFFER_SIZE 0x10000

extern char logbuffer[LOGBUFFER_SIZE + 16];

extern int logbufferpos;

typedef struct
{
    uint8_t zeroten1_lvl;
    uint8_t zeroten2_lvl;
    uint8_t dali_lvl[DALI_CHANNELS];
    uint8_t espnow_lvl;
    uint8_t relay1;
    uint8_t relay2;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    float power;
} level_t;

typedef struct
{
    uint8_t fadetime_float; // Fadetime == ((x & 0xF) * (1 << ((x >> 4) & 0xF)) ) ms
    uint8_t setpoint_source;
    uint8_t setpoint;
} setpoint_notify_t;

typedef struct
{
    int16_t dali[DALI_CHANNELS];
    int16_t zeroten1;
    int16_t zeroten2;
    int16_t espnow;
} level_overrides_t;

typedef struct
{
    uint8_t setpoint;
    uint8_t setpoint_source;
    uint8_t power_on;
    uint8_t actual_level;
    uint32_t fadetime_ms;
    TaskHandle_t mainloop_task;
    level_overrides_t level_overrides;
    level_t channel_levels;
    level_t lut[255];
    uint32_t stale_flag;
} device_status_t;

// extern volatile level_t levellut[255];

#define DALI_COMMAND_COMMISSION 1
#define DALI_COMMAND_FIND_NEW_DEVICES 4
#define DALI_COMMAND_SET_FAILSAFE_LEVEL 2
#define DALI_COMMAND_SET_POWER_ON_LEVEL 3
#define DALI_COMMAND_GET_POWER_ON_LEVEL 0x43
#define DALI_COMMAND_SET_FADE_TIME 5
#define DALI_COMMAND_GET_FADE_TIME 0x45
#define DALI_COMMAND_ADD_TO_GROUP 6
#define DALI_COMMAND_RESET_DEVICE 7

typedef struct
{
    uint16_t time;
    int8_t edgetype;
} edge_t;

typedef struct
{
    edge_t edges[DALI_MAX_EDGEFRAME_LENGTH];
    uint8_t length;
} edgeframe;

typedef struct
{
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

typedef struct
{
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

typedef struct
{
    uint8_t number;
    gptimer_handle_t timer;
    dali_transmit_isr_ctx *ctx;
    QueueHandle_t queue;
    uint16_t frameidcounter;
} dali_transmitter_handle_t;

typedef struct
{
    QueueHandle_t dali_received_frame_queue;
    QueueHandle_t dali_command_queue;
    dali_transmitter_handle_t transmitter;
    uint32_t frameidpass;
    edgeframe_isr_ctx_t *edgeframe_isr_ctx;
    TaskHandle_t mainloop_task;
    SemaphoreHandle_t bus_mutex;
} dali_transceiver_t;

typedef struct dali_transceiver_t *dali_transceiver_handle_t;

typedef struct
{
    TaskHandle_t mainloop_task;
    level_overrides_t *level_overrides;
    QueueHandle_t dali_command_queue;
    device_status_t *status;
    SemaphoreHandle_t logstring_mutex;
    char *logstring;
    char *logheader;
} networking_ctx_t;

void build_nvs_key_for_gpio_gain(int gpio, char *keybuf);

int32_t _MAX(int32_t a, int32_t b);

int32_t _MIN(int32_t a, int32_t b);

uint32_t _uMIN(uint32_t a, uint32_t b);

uint32_t _uMAX(uint32_t a, uint32_t b);

int clamp(int in, int low, int high);

int flexclamp(int in, int a, int b);

void test_flexclamp();

uint32_t uclamp(uint32_t in, uint32_t low, uint32_t high);

uint64_t get_system_time_us(uint64_t offset);

extern SemaphoreHandle_t log_mutex;

void initialise_logbuffer();

int log_string(const char *logstring, int bytes_to_log, bool addtime);

bool overrides_active(device_status_t *status);

typedef struct
{
    const char name[24];
    const int *array;
} api_endpoint_t;

#endif // base_H
