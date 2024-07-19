#include <math.h>
#include <time.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/ledc.h"


#include "base.h"
// #include "edgeframe_logger.h"
// #include "dali_transmit.h"
#include "0-10v.h"
// #include "dali_edgeframe_parser.h"
// #include "dali.h"
#include "dali_transceiver.h"
#include "dali_utils.h"
// #include "dali_utils.c"
// #include "dali_rmt_receiver.c"
// #ifdef IS_PRIMARY
#include "wifi.h"
#include "http_server.h"
// #endif // IS_PRIMARY
#include "base.h"
#include "ledflash.h"
#include "gpio_utils.h"
#include "uart.h"
#include "buttons.h"
#include "adc.h"
#include "realtime.h"
#include "settings.h"
#define RESOLUTION_HZ     10000000

#define MIN_EST_LOOPTIME_US 25000

#define LOOPTIME_TOLERANCE 2000

#define MAX_UPDATE_INTERVAL 5000000

#define MAX_UPDATE_PERIOD_US 1500000


static const char *TAG = "main loop";

nvs_handle_t mainloop_nvs_handle;

void setup_networking(void* params){
    TaskHandle_t mainlooptask = (TaskHandle_t) params;
    setup_wifi();
    setup_sntp(mainlooptask);
    httpd_ctx httpdctx = {
        .mainloop_task = mainlooptask,
    };
    httpd_handle_t httpd = setup_httpserver(&httpdctx);
    while (1){
        vTaskDelay(portMAX_DELAY);
    }
}

void list_tasks(){
    
    char buffer[4096];
    vTaskList(buffer);
    printf(buffer);
}

static int tick_inc = 1;
static uint64_t target_looptime = 0;
static uint64_t ideal_time_between_levels_us = 0;

void calc_tickinc_and_looptime(uint64_t looptime) {
    ideal_time_between_levels_us = fadetime << 2;
    target_looptime = ideal_time_between_levels_us * (uint64_t) tick_inc;
    while (target_looptime < looptime) {
        tick_inc += 1;
        target_looptime = ideal_time_between_levels_us * tick_inc;
        // ESP_LOGI(TAG, "Trying %i tick inc, %i target looptime", tick_inc, target_looptime);
    }
    ESP_LOGI(TAG, "calc: looptime %llu, tick_inc %i", looptime, tick_inc);
}


static char templogbuffer[128];
void primary(){
    setup_relays(get_setting("configbits"));
    TaskHandle_t networktask;   
    xTaskCreate(setup_networking, "setup_networking", 4096, xTaskGetCurrentTaskHandle(), 2, &networktask);

    zeroten_handle_t pwm1;
    zeroten_handle_t pwm2;
    ESP_ERROR_CHECK(setup_0_10v_channel(LED1_GPIO, CALIBRATION_PWM_LOG, &pwm1));
    ESP_ERROR_CHECK(setup_0_10v_channel(PWM_010v_GPIO, CALIBRATION_LOOKUP_NVS, &pwm2));

    setup_button_interrupts(xTaskGetCurrentTaskHandle(), pwm1, pwm2);

    dali_transceiver_config_t transceiver_config = dali_transceiver_sensible_default_config;
    transceiver_config.invert_input = DALI_DONT_INVERT;
    transceiver_config.invert_output = DALI_DONT_INVERT;
    transceiver_config.transmit_queue_size_frames = 1;
    transceiver_config.receive_queue_size_frames = 0;
    transceiver_config.receive_gpio_pin = RX_GPIO   ;
    transceiver_config.transmit_gpio_pin = TX_GPIO;
    transceiver_config.parser_config.forward_frame_action = DALI_PARSER_ACTION_LOG;

    dali_transceiver_handle_t dali_transceiver;
    ESP_ERROR_CHECK(dali_setup_transceiver(transceiver_config, &dali_transceiver));

    // dali_set_system_failure_level(dali_transceiver, 8, 10);
    // dali_set_system_failure_level(dali_transceiver, 9, 10);
    dali_broadcast_level_noblock(dali_transceiver, 30);

    BaseType_t received;
    esp_err_t sent;
    int firsttime = 1;
    // actual_level = setpoint;
    // vTaskDelay(50);
    ESP_LOGI(TAG, "Starting main loop");
    int local_fadetime = fadetime;
    int64_t reftime = esp_timer_get_time() - MIN_EST_LOOPTIME_US;
    uint64_t reawake_time = esp_timer_get_time() + 5000000;
    int configbits = get_setting("configbits");
    
    int default_fadetime = get_setting("default_fade");
    int looptime = MIN_EST_LOOPTIME_US;

    int updates_performed = 0;
    int fade_remaining = 0;
    
    bool at_setpoint;
    int actual_level = setpoint;
    uint64_t current_time;
    uint64_t actual_looptime;
    int random_looptime = 1;
    int local_setpoint = setpoint;
    while (1) {
        actual_looptime = (esp_timer_get_time() - reftime);
        if (actual_looptime > (target_looptime + LOOPTIME_TOLERANCE))
        {
            calc_tickinc_and_looptime(actual_looptime);
        };
        while (1) {
            received = xTaskNotifyWaitIndexed(SETPOINT_SLEW_NOTIFY_INDEX, 0, 0, &local_fadetime, 1);
            local_setpoint = clamp(setpoint, 0, 254);
            current_time = esp_timer_get_time();
            if (received == pdTRUE){
                random_looptime = (rand() & 127);
                tick_inc = 1;
                configbits = get_setting("configbits");
                fadetime = (local_fadetime == USE_DEFAULT_FADETIME) ? get_setting("default_fade") : local_fadetime;
                if (fadetime < 8) fadetime = 8;
                calc_tickinc_and_looptime(MIN_EST_LOOPTIME_US);
                break;
            };
            if (current_time > reawake_time) break;
        };
        reftime = esp_timer_get_time();
        fade_remaining = local_setpoint - actual_level;
        if (fade_remaining < 0){
            actual_level = _MAX(actual_level - tick_inc, local_setpoint);
            reawake_time = reftime + target_looptime;
        }
        else if (fade_remaining > 0)
        {
            actual_level = _MIN(actual_level + tick_inc, local_setpoint);
            reawake_time = reftime + target_looptime;
        }
        else
        {
            // we're at setpoint
            reawake_time = reftime + MAX_UPDATE_INTERVAL;
            ESP_LOGI(TAG, "State: Idle. Waiting for new setpoint.");
            configbits = get_setting("configbits");
            sprintf(templogbuffer, "Config: Relay1: %i, Relay2: %i, DALI: %i, ESPNOW %i, 0-10v 1: %i, 0-10v 2:%i",
                (configbits & CONFIGBIT_USE_RELAY1) > 0,
                (configbits & CONFIGBIT_USE_RELAY2) > 0,
                (configbits & CONFIGBIT_USE_DALI) > 0,
                (configbits & CONFIGBIT_USE_ESPNOW) > 0,
                (configbits & CONFIGBIT_USE_0_10v1) > 0,
                (configbits & CONFIGBIT_USE_0_10v2) > 0);
            printf(templogbuffer);
            log_string(templogbuffer);
                // list_tasks();
        }

        fade_remaining = local_setpoint - actual_level;
        manage_relay_timeouts(configbits, actual_level, actual_level);

        if (espnowtask != NULL && (configbits & CONFIGBIT_USE_ESPNOW))
        {
            xTaskNotifyIndexed(espnowtask,
                SETPOINT_SLEW_NOTIFY_INDEX,
                actual_level,
                eSetValueWithOverwrite);
        }
            
        if (configbits & CONFIGBIT_USE_0_10v1){
            ESP_ERROR_CHECK(set_0_10v_level(pwm1, actual_level));
        }
        if (configbits & CONFIGBIT_USE_0_10v2)
        {
            ESP_ERROR_CHECK(set_0_10v_level(pwm2, actual_level));
        }
        // for (int i=8; i<13; i+=1) {
        if (configbits & CONFIGBIT_USE_DALI){
            sent = dali_broadcast_level_noblock(dali_transceiver, actual_level);
        }
        sprintf(templogbuffer, "%s: Snt %i->%i Fade %i Intvl %llu", TAG, actual_level, local_setpoint, fadetime, target_looptime);
        printf(templogbuffer);
        log_string(templogbuffer);
        // ESP_LOGI(TAG, "Setp %i, tick_inc %i, tgt_ltm %llu ftime %i, leveltme %llu", setpoint, tick_inc, target_looptime, fadetime, ideal_time_between_levels_us);
        // ESP_LOGI(TAG, "Curr %i, Actual lptm %llu", actual_level, actual_looptime);
    }
    // while (1) {
    //     at_setpoint = setpoint16 == level16bit;
    //     if (at_setpoint) {
    //         // looptime = GUESSED_LOOPTIME_US;
    //         reftime = esp_timer_get_time();
    //     }
    //     else
    //     {
    //         newtime = esp_timer_get_time();
    //         looptime = newtime - reftime;
    //         reftime = newtime;
    //     }
    //     last_level16 = level16bit;
    //     last_level = actual_level;
    //     while (1) {
    //         received = xTaskNotifyWaitIndexed(SETPOINT_SLEW_NOTIFY_INDEX, 0, 0, &local_fadetime, 1);
    //         reftime = esp_timer_get_time();
    //         if (received == pdTRUE || reftime > reawake_time) break;
    //     }
    //     // if (received) reftime = esp_timer_get_time();
    //     if (received || firsttime) {
    //         new_setpoint_time = reftime;
    //         updates_performed = 0;
    //         if (setpoint > 254){
    //             ESP_LOGE(TAG, "Unknown level %i", setpoint);
    //             setpoint = 0;
    //             continue;
    //         }
    //         fadetime = (local_fadetime == USE_DEFAULT_FADETIME) ? default_fadetime : local_fadetime;
    //         if (fadetime < 8) fadetime = 8;
        
    //         ESP_LOGI(TAG, "Received new setpoint '%i', fade time %i ms", setpoint, fadetime);
    //         updates_performed = 0;
    //         // ESP_LOGI(TAG, "Actual_level %i, tick_inc %i, wait_tick %i", actual_level, tick_increment_16, wait_ticks);
    //     }
    //     time_since_last_update = reftime - updatetime;
    //     setpoint16 = setpoint << 15;
    //     if ((reftime - last_log_time) > (at_setpoint ? 5000000 : 200000)) {
    //         configbits = get_setting("configbits");
    //         default_fadetime = get_setting("default_fade");
    //         last_log_time = reftime;
    //         ESP_LOGI(TAG, "Config: Relay1: %i, Relay2: %i, DALI: %i, ESPNOW %i, 0-10v 1: %i, 0-10v 2:%i",
    //             (configbits & CONFIGBIT_USE_RELAY1) > 0,
    //             (configbits & CONFIGBIT_USE_RELAY2) > 0,
    //             (configbits & CONFIGBIT_USE_DALI) > 0,
    //             (configbits & CONFIGBIT_USE_ESPNOW) > 0,
    //             (configbits & CONFIGBIT_USE_0_10v1) > 0,
    //             (configbits & CONFIGBIT_USE_0_10v2) > 0);
    //         if ((configbits & CONFIGBIT_USE_0_10v1) == 0){
    //             disable_pwm_channel(pwm1);
    //             disable_pwm_channel(pwm2);
    //         }
    //         else if ((configbits & CONFIGBIT_USE_0_10v2) == 0)
    //         { 
    //             disable_pwm_channel(pwm2);
    //         }
    //         if (level16bit == setpoint16) {
    //             ESP_LOGI(TAG, "Status: At setpoint (%i/%i) looptime %i", setpoint, setpoint16, looptime);
    //             display_transmit_buffer_log_timings();
    //         }
    //         else
    //         {
    //             time_since_last_setpoint = reftime - new_setpoint_time;
    //             profile_reftime = esp_timer_get_time();
    //             updated_per_sec = ((float) (updates_performed * 1000000)) / ((uint32_t) time_since_last_setpoint);
    //             // ESP_LOGI(TAG, "Status: Fading to setpoint (%i/%i), currently %i/%i, %f updates/s,  looptime %i, tick inc %i, fade time %i ms, time_since_update %llums", setpoint, setpoint16, actual_level, level16bit , updated_per_sec,looptime, tick_increment_16, fadetime, time_since_last_update / 1000 );
    //             ESP_LOGI(TAG, "Status: Fading to setpoint (/), currently /, updates/s,  looptime , tick inc , fade time  ms, time_since_update Fading to setpoint (/), currently /, updates/s,  looptime , tick inc , fade time  ms, time_since_update ms" );
    //             ESP_LOGI(TAG, "Message took %llu us", esp_timer_get_time() - profile_reftime);
    //             last_update = reftime;
    //             // updates_performed = 0;
    //         }
    //     }
        

    //     if (level16bit == setpoint16){
    //         // idling
    //         wait_ticks = pdMS_TO_TICKS(100);
    //     }
    //     else
    //     {
    //         wait_ticks = (fadetime > 1800000) ? pdMS_TO_TICKS(100) : pdMS_TO_TICKS(20);
    //     }
    //     // if ((fadetime > 180000)) ESP_LOGI(TAG, "%i, %i %i %i", level16bit, tick_increment_16, wait_ticks, looptime);
    //     tick_increment_16 = (looptime << 13) / fadetime;
    //     // ESP_LOGI(TAG, "New wait ticks %i, tick inc %i, loop time %i us", wait_ticks, tick_increment_16, looptime);
    //     setpoint16 = setpoint << 15;
    //     fade_remaining_16 = level16bit - setpoint16;

    //     if ((fade_remaining_16 != 0) && abs(fade_remaining_16) < tick_increment_16) {
    //         tick_increment_16 = abs(fade_remaining_16);
    //     }

    //     if (fade_remaining_16 < 0){
    //         level16bit += tick_increment_16;
    //     }
    //     else if (fade_remaining_16 > 0)
    //     {
    //         level16bit -= tick_increment_16;
    //     }

    //     // actual_level = (level16bit * get_setting("full_power") + 0x5FFF) >> 23;
    //     actual_level = (level16bit * 256 + 0x5FFF) >> 23;
    //     if (last_level == actual_level && time_since_last_update < MAX_UPDATE_PERIOD_US)
    //     {
    //         if (wait_ticks < 3) wait_ticks = 3;
    //         // vTaskDelay(pdMS_TO_TICKS(10));
    //     }
    //     else 
    //     {
    //         if (espnowtask != NULL && (configbits & CONFIGBIT_USE_ESPNOW))
    //         {
    //             xTaskNotifyIndexed(espnowtask,
    //                 SETPOINT_SLEW_NOTIFY_INDEX,
    //                 actual_level,
    //                 eSetValueWithOverwrite);
    //         }
            
    //         updatetime = reftime;
    //         // ESP_LOGI(TAG, "Actual_level %i, 16 %i, tick_inc %i, wait_tick %i, looptime %i us, updatetime %llu", actual_level, level16bit ,tick_increment_16, wait_ticks, looptime, updatetime - last_update);
    //         if ( at_setpoint ) {
    //             printf(".\n");
    //             fflush(stdout);
    //         }
    //         manage_relay_timeouts(configbits, actual_level, actual_level);
    //             // ESP_ERROR_CHECK(set_0_10v_level(pwm1, actual_level));
    //             // ESP_ERROR_CHECK(set_0_10v_level(pwm2, actual_level));

    //         if (configbits & CONFIGBIT_USE_0_10v1){
    //             ESP_ERROR_CHECK(set_0_10v_level(pwm1, actual_level));
    //         }
    //         if (configbits & CONFIGBIT_USE_0_10v2)
    //         {
    //             ESP_ERROR_CHECK(set_0_10v_level(pwm2, actual_level));
    //         }
    //         // for (int i=8; i<13; i+=1) {
    //         if (configbits & CONFIGBIT_USE_DALI){
    //             sent = dali_broadcast_level_noblock(dali_transceiver, actual_level);
    //         }
    //         updates_performed += 1;
    //         reawake_time = esp_timer_get_time() + 43000;
    //         // }
    //     };
    //     firsttime = 0;
    // };
}

void random_setpointer_task(void *params){
    TaskHandle_t mainloop_task = params;
    int mult = (rand() & 0xF);
    while (1){
        mult = (rand() & 0xF);
        setpoint = (rand() & 0xFF);
        xTaskNotifyIndexed(mainloop_task, SETPOINT_SLEW_NOTIFY_INDEX, (rand() & 0xFF)* mult *mult, eSetValueWithOverwrite);
        vTaskDelay(((rand() & 0x7) * mult *mult) + 1);
    }
}

void app_main(void)
{
    initialise_logbuffer();
    // sprintf(logbuffer, "This is a log");
    srand(time(NULL));
    vTaskPrioritySet(NULL, 6);
    configure_gpio();
    setup_nvs_spiffs_settings();
    TaskHandle_t randomtask;
    // xTaskCreate(random_setpointer_task, "random setpointer task", 2048, (void*) xTaskGetCurrentTaskHandle(), 7, &randomtask);
    // set_setting("alarmhour", 3, 1000);
    ESP_LOGI(TAG, "%i", get_setting_indexed("alarmhour", 3));
    // vTaskDelay(10000);
    // setup_uart();
    uint8_t dip_address = read_dip_switches();
    ESP_LOGI(TAG, "Read DIP Switch address: %d", dip_address);

    light_adc_config_t adcconfig = {
        .notify_task = xTaskGetCurrentTaskHandle(),
        .tolerance = 10,
        .sampled_needed = 3,
        .average_window = 4,
    };
    int resistor_level;
    int received;
    setup_adc(adcconfig);

    // while(1){
        // received = xTaskNotifyWaitIndexed(LIGHT_LEVEL_NOTIFY_INDEX, 0, 0, &resistor_level, 100);
        // if (received) ESP_LOGI(TAG, "Received resistor level %i", resistor_level);

    // }
    // rotate_gpio_outputs_forever();
    // setup_flash_led(LED1_GPIO, 2000);
    // setup_flash_led(LED2_GPIO, 2020);
    // setup_flash_led(RELAY1_GPIO, 7400);
    // setup_flash_led(RELAY2_GPIO, 7550);
    if (dip_address == 0){
        primary();
    }
    else {
        primary();
    }

}
