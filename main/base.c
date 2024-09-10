#include "base.h"

#include <stdio.h>

#include <string.h>
#include "sys/time.h"
#include "stdint.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "driver/gpio.h"

#include "esp_netif.h"

#define STARTUP_SETPOINT 0


static const char* TAG = "Base";

nvs_handle_t nvs_handle_ = 0;

int actual_level = 0;
int duty = -1;
int maxduty = -1;

__NOINIT_ATTR char logbuffer[LOGBUFFER_SIZE + 16];

// volatile level_t levellut[255];

TaskHandle_t espnowtask = NULL;

struct timeval time_;

// extern char logbuffer[LOGBUFFER_SIZE + 16];

extern __NOINIT_ATTR int logbufferpos = 0;

void build_nvs_key_for_gpio_gain(int gpio, char* keybuf){
    sprintf(keybuf, "GPIOPIN %hhu GAIN", gpio);
}


SemaphoreHandle_t log_mutex;

inline int32_t _MAX(int32_t a, int32_t b) { return((a) > (b) ? a : b); }
inline int32_t _MIN(int32_t a, int32_t b) { return((a) < (b) ? a : b); }
inline uint32_t _uMIN(uint32_t a, uint32_t b) { return((a) < (b) ? a : b); }
inline uint32_t _uMAX(uint32_t a, uint32_t b) { return((a) > (b) ? a : b); }

int clamp(int in, int low, int high){
    return (in > low) ? _MIN(in, high) : low;
}

int flexclamp(int in, int bound_a, int bound_b){
    if (bound_a > bound_b && in >= bound_b && in <= bound_a) return in;
    if (bound_a > bound_b && in >= bound_b) return bound_a;
    if (bound_a > bound_b) return bound_b;
    if (in <= bound_b && in >= bound_a) return in;
    if (in <= bound_b) return bound_a;
    return bound_b;
}

void test_flexclamp(){
    while (1)
    {
        int8_t a = rand() & 0xFF;
        int8_t b = rand() & 0xFF;
        int8_t c = rand() & 0xFF;

        ESP_LOGI(TAG, "testing %d >= %d >= %d -> %d", a, c, b, flexclamp(c, a, b));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

uint32_t uclamp(uint32_t in, uint32_t low, uint32_t high){
    return (in > low) ? _uMIN(in, high) : low;
}

uint64_t get_system_time_us(uint64_t offset){
    gettimeofday(&time_, NULL);
    // return time_.tv_sec * 1000000LL + time_.tv_usec - offset;
    return time_.tv_usec - offset;
}

void initialise_logbuffer(){
    log_mutex = xSemaphoreCreateMutex();
    logbufferpos = 0;
    for (int i = 0; i < LOGBUFFER_SIZE; i++){
        logbuffer[i] = ' ';
        // ESP_LOGI(TAG, "%i -> i %i, %d", (size_t) logbuffer, i, logbuffer[i]);
    }
    
    // sprintf(logbuffer, "This 2 is a log");
}
static char tempbuffer[64];

static uint64_t last_log = 0;
static uint64_t nowtime = 0;


int log_string(const char* logstring, int bytes_to_log, bool addtime){
    int added = 0;
    if (addtime) {
        nowtime = esp_timer_get_time();
        if ((nowtime - last_log) > 1000000) {
            time_t rawtime;
            struct tm * timeinfo;

            time ( &rawtime );
            timeinfo = localtime ( &rawtime );
            const char* timestr = asctime(timeinfo);
            strncpy(tempbuffer, timestr, 24);
            tempbuffer[24] = '\n';
            // tempbuffer[24] = ' ';
            last_log = nowtime;
            added += log_string(tempbuffer, 25, false);
        }
    }
    int len = bytes_to_log;
    int buffer_remaining = LOGBUFFER_SIZE - logbufferpos - 1;
    int bytes_copied = 0;
    if (buffer_remaining > 0)
    {
        bytes_copied = _MIN(buffer_remaining, len);
        if ((logbuffer + logbufferpos + bytes_copied) > (logbuffer + LOGBUFFER_SIZE)) {
            ESP_ERROR_CHECK(ESP_ERR_NOT_ALLOWED);
        }
        memcpy(logbuffer + logbufferpos, logstring, bytes_copied);
        // ESP_LOGI(TAG, "Copied %i into end of buffer", bytes_copied);
        logbufferpos += bytes_copied;
    }
    if (bytes_copied < len){
        logbufferpos = 0;
        
        if ((logbuffer + logbufferpos + (len - bytes_copied)) > (logbuffer + LOGBUFFER_SIZE)) {
            ESP_ERROR_CHECK(ESP_ERR_NOT_ALLOWED);
        }
        strncpy(logbuffer + logbufferpos, logstring + bytes_copied, (len - bytes_copied));
        // ESP_LOGI(TAG, "Copied %i bytes remaining into start", len- bytes_copied);
        logbufferpos += len - bytes_copied;
    }
    if (logbufferpos > (LOGBUFFER_SIZE - 1))
    {
        logbufferpos = 0;
    }
    return added + bytes_to_log;
}


bool overrides_active(device_status_t *status)
{
    for (int i=0; i < DALI_CHANNELS; i++){
        if (status->level_overrides.dali[i] > -1)
        {
            return true;
        }
    }
    if (status->level_overrides.zeroten1 > -1)
    {
        return true;
    }
    if (status->level_overrides.zeroten2 > -1)
    {
        return true;
    }
    if (status->level_overrides.espnow > -1)
    {
        return true;
    }
    return false;
}
