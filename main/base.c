#include "base.h"

#include <stdio.h>

#include <string.h>
#include "sys/time.h"
#include "stdint.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "driver/gpio.h"

#include "esp_netif.h"

#define STARTUP_SETPOINT 10


static const char* TAG = "Base";

nvs_handle_t nvs_handle_ = 0;

int actual_level = 0;
int setpoint = STARTUP_SETPOINT;
int fadetime = 50;
int duty = -1;
int maxduty = -1;


volatile level_t levellut[255];

TaskHandle_t espnowtask = NULL;

struct timeval time_;


volatile char logbuffer[LOGBUFFER_SIZE];

extern int logbufferpos = 0;

void build_nvs_key_for_gpio_gain(int gpio, char* keybuf){
    sprintf(keybuf, "GPIOPIN %hhu GAIN", gpio);
}

inline int32_t _MAX(int32_t a, int32_t b) { return((a) > (b) ? a : b); }
inline int32_t _MIN(int32_t a, int32_t b) { return((a) < (b) ? a : b); }

int clamp(int in, int low, int high){
    return (in > low) ? _MIN(in, high) : low;
}

uint64_t get_system_time_us(uint64_t offset){
    gettimeofday(&time_, NULL);
    // return time_.tv_sec * 1000000LL + time_.tv_usec - offset;
    return time_.tv_usec - offset;
}

void initialise_logbuffer(){
    for (int i = 0; i < LOGBUFFER_SIZE; i++){
        logbuffer[i] = ' ';
        // ESP_LOGI(TAG, "%i -> i %i, %d", (size_t) logbuffer, i, logbuffer[i]);
    }
    
    // sprintf(logbuffer, "This 2 is a log");
}
static char tempbuffer[512];
void log_string(char* logstring){
    printf("%s\n", logstring);
    time_t rawtime;
    struct tm * timeinfo;

    time ( &rawtime );
    timeinfo = localtime ( &rawtime );
    char* timestr = asctime(timeinfo);
    strcpy(tempbuffer, timestr);
    tempbuffer[25] = ' ';
    tempbuffer[24] = ' ';
    strcpy(tempbuffer + 26, logstring);
    int len = strlen(tempbuffer);
    int buffer_remaining = LOGBUFFER_SIZE - logbufferpos - 1;
    int bytes_copied = 0;
    if (buffer_remaining)
    {
        bytes_copied = _MIN(buffer_remaining, len);
        memcpy(logbuffer + logbufferpos, tempbuffer, bytes_copied);
        // ESP_LOGI(TAG, "Copied %i into end of buffer", bytes_copied);
        logbufferpos += bytes_copied;
    }
    if (bytes_copied < len){
        logbufferpos = 0;
        
        strncpy(logbuffer + logbufferpos, tempbuffer + bytes_copied, (len - bytes_copied));
        // ESP_LOGI(TAG, "Copied %i bytes remaining into start", len- bytes_copied);
        logbufferpos += len-bytes_copied;
    }
    logbuffer[logbufferpos] = '\n';
    logbufferpos += 1;
}

// static const api_endpoint_t alarm2_hour_endpoint = {
//     .name = "alarm2_hour",
//     .array = &alarm2_hour
// };
// static const api_endpoint_t alarm3_hour_endpoint = {
//     .name = "alarm3_hour",
//     .array = &alarm3_hour

// };
// static const api_endpoint_t alarm1_min_endpoint = {
//     .name = "alarm1_min",
//     .array = &alarm1_min
// };
// static const api_endpoint_t alarm2_min_endpoint = {
//     .name = "alarm2_min",
//     .array = &alarm2_min
// };
// static const api_endpoint_t alarm3_min_endpoint = {
//     .name = "alarm3_min",
//     .array = &alarm3_min
// };
// static const api_endpoint_t alarm1_fade_endpoint = {
//     .name = "alarm1_fade",
//     .array = &alarm1_fade
// };
// static const api_endpoint_t alarm2_fade_endpoint = {
//     .name = "alarm2_fade",
//     .array = &alarm2_fade
// };
// static const api_endpoint_t alarm3_fade_endpoint = {
//     .name = "alarm3_fade",
//     .array = &alarm3_fade
// };
// static const api_endpoint_t alarm1_setpoint_endpoint = {
//     .name = "alarm1_setpoint",
//     .array = &alarm1_setpoint
// };
// static const api_endpoint_t alarm2_setpoint_endpoint = {
//     .name = "alarm2_setpoint",
//     .array = &alarm2_setpoint
// };
// static const api_endpoint_t alarm3_setpoint_endpoint = {
//     .name = "alarm3_setpoint",
//     .array = &alarm3_setpoint
// };
// static const api_endpoint_t alarm1_enable_endpoint = {
//     .name = "alarm1_enable",
//     .array = &alarm1_enable
// };
// static const api_endpoint_t alarm2_enable_endpoint = {
//     .name = "alarm2_enable",
//     .array = &alarm2_enable
// };
// static const api_endpoint_t alarm3_enable_endpoint = {
//     .name = "alarm3_enable",
//     .array = &alarm3_enable
// };
// static const api_endpoint_t default_fadetime_endpoint = {
//     .name = "default_fadetime",
//     .array = &default_fadetime
// };
// static const api_endpoint_t full_power_endpoint = {
//     .name = "full_power",
//     .array = &full_power
// };
