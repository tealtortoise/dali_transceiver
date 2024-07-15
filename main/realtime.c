
#include <stdio.h>
#include <time.h>
#include "base.h"
#include "esp_log.h"
#include "realtime.h"
#include "esp_netif_sntp.h"


static const char* TAG = "SNTP";

static int alarm_minute = 41;
void rtc_alarm_task(void* params){
    time_t now;
    struct tm *tm_struct;
    int hour;
    int minute;
    int second;
    int alarm_min;
    int alarm_hour;
    int alarm_fade;
    int alarm_setpoint;
    int alarm_enable;
    TaskHandle_t mainloop_task = (TaskHandle_t) params;
    while (1)
    {
        now = time(NULL);
        tm_struct = localtime(&now);
        hour = tm_struct->tm_hour;   
        minute = tm_struct->tm_min;
        second = tm_struct->tm_sec;
        // ESP_LOGI(TAG, "Hour:min %i : %i : %i", hour, minute, second);
        for (int i = 0; i < 3; i++){
            switch (i){
                case 0: {
                    alarm_min = alarm1_min;
                    alarm_hour = alarm1_hour;
                    alarm_fade = alarm1_fade;
                    alarm_setpoint = alarm1_setpoint;
                    alarm_enable = alarm1_enable;
                    break;
                }
                case 1: {
                    alarm_min = alarm2_min;
                    alarm_hour = alarm2_hour;
                    alarm_fade = alarm2_fade;
                    alarm_setpoint = alarm2_setpoint;
                    alarm_enable = alarm2_enable;
                    break;
                }
                case 2: {
                    alarm_min = alarm3_min;
                    alarm_hour = alarm3_hour;
                    alarm_fade = alarm3_fade;
                    alarm_setpoint = alarm3_setpoint;
                    alarm_enable = alarm3_enable;
                    break;
                }
            };
            if (alarm_enable && hour == alarm_hour && minute == alarm_min && second == 0){
                ESP_LOGI(TAG, "Alarm!!!");
                setpoint = alarm_setpoint;
                xTaskNotifyIndexed(mainloop_task, SETPOINT_SLEW_NOTIFY_INDEX, alarm_fade, eSetValueWithOverwrite);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(950));
    }
}


void setup_sntp(){
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
    // time_t rawtime;
    // struct tm * timeinfo;

    // time ( &rawtime );
    // timeinfo = localtime ( &rawtime );
    // printf ( "Current local time and date: %s", asctime (timeinfo) );
    TaskHandle_t rtc_handle;
    xTaskCreate(rtc_alarm_task, "rtc_alarm_task", 2048, (void*) xTaskGetCurrentTaskHandle(), 1, &rtc_handle);
    // vTaskDelay(pdMS_TO_TICKS(10000));
}