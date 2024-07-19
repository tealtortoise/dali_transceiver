
#include <stdio.h>
#include <time.h>
#include "base.h"
#include "esp_log.h"
#include "realtime.h"
#include "esp_netif_sntp.h"
#include "settings.h"

static const char* TAG = "SNTP";

static int alarm_minute = 41;
void rtc_task(void* params){

    
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&config));
    ESP_LOGI(TAG, "Started SNTP");
    ESP_ERROR_CHECK(esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)));

    time_t now;
    char strftime_buf[64];
    struct tm timeinfo;

    time(&now);
    setenv("TZ", "GMT0BST,M3.5.0/1:00:00,M10.5.0/2:00:00", 1);
    tzset();

    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in UK is: %s", strftime_buf);
    
    struct tm *tm_struct;
    int hour;
    int minute;
    int second;
    int last_minute = -1;
    int last_hour;
    int alarm_min;
    int alarm_hour;
    int alarm_fade;
    int alarm_setpoint;
    int alarm_enable;
    TaskHandle_t mainloop_task = (TaskHandle_t) params;
    while (1)
    {
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            now = time(NULL);
            tm_struct = localtime(&now);
            hour = tm_struct->tm_hour;   
            minute = tm_struct->tm_min;
            // second = tm_struct->tm_sec;
            if (last_minute != minute) break;
        }
        // ESP_LOGI(TAG, "Hour:min %i : %i : %i", hour, minute, second);
        for (int i = 0; i < 4; i++){
            
            vTaskDelay(pdMS_TO_TICKS(100));
            alarm_enable = get_setting_indexed("alarmenable", i);
            if (!alarm_enable) continue;
            alarm_hour = get_setting_indexed("alarmhour", i);
            if (hour != alarm_hour) continue;
            alarm_min = get_setting_indexed("alarmmin", i);
            if (minute != alarm_min) continue;
            ESP_LOGI(TAG, "Alarm %i !!", i);
            alarm_setpoint = get_setting_indexed("alarmsetpoint", i);
            setpoint = alarm_setpoint;
            alarm_fade = get_setting_indexed("alarmfade", i);
            xTaskNotifyIndexed(mainloop_task, SETPOINT_SLEW_NOTIFY_INDEX, alarm_fade, eSetValueWithOverwrite);
        }
        last_minute = minute;
        last_hour = hour;
    }
}


void setup_sntp(TaskHandle_t mainlooptask){
    // time_t rawtime;
    // struct tm * timeinfo;

    // time ( &rawtime );
    // timeinfo = localtime ( &rawtime );
    // printf ( "Current local time and date: %s", asctime (timeinfo) );
    TaskHandle_t rtc_handle;
    xTaskCreate(rtc_task, "rtc_task", 4096, (void*) mainlooptask, 1, &rtc_handle);
    // vTaskDelay(pdMS_TO_TICKS(10000));
}