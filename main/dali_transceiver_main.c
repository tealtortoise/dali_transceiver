#include <math.h>
#include <time.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
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

#define HASH_LEN 32 /* SHA-256 digest length */

static const char *TAG = "main loop";

nvs_handle_t mainloop_nvs_handle;

void setup_networking(void* params){
    networking_ctx_t *ctx = (networking_ctx_t *) params;
    setup_wifi();
    setup_sntp(ctx->mainloop_task);
    // httpd_ctx httpdctx = {
    //     .mainloop_task = mainlooptask,
    // };
    httpd_handle_t httpd = setup_httpserver(ctx);
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


void transmit_setlevel_dali_channel(dali_transceiver_handle_t transceiver, int channel, int level){
    if (channel >= 0 && channel <= 63)
    {
        dali_set_level_noblock(transceiver, channel ,level, pdMS_TO_TICKS(130));
        return;
    }
    if (channel >= 100 && channel <= 115)
    {
        dali_set_level_group_noblock(transceiver, channel - 100 ,level, pdMS_TO_TICKS(130));
        return;
    }
    if (channel == 200) {
        dali_broadcast_level_noblock(transceiver, level);
        return;
    }
    if (channel == -1) return;
    ESP_LOGE(TAG, "Unknown DALI Channel %i", channel);
    return;
}

static char templogbuffer[256];
void primary(){
    setup_relays(get_setting("configbits"));
    
    level_overrides_t level_overrides = {
        .dali1 = -1,
        .dali2 = -1,
        .dali3 = -1,
        .dali4 = -1,
        .zeroten1 = -1,
        .zeroten2 = -1,
        .espnow = -1
    };

    networking_ctx_t networking_ctx = {
        .mainloop_task = xTaskGetCurrentTaskHandle(),
        .level_overrides = &level_overrides
    };
    TaskHandle_t networktask;
    xTaskCreate(setup_networking, "setup_networking", 4096, (void*) &networking_ctx, 2, &networktask);

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
    level_t level_el;
    uint64_t current_time;
    uint64_t actual_looptime;
    int random_looptime = 1;
    int local_setpoint = setpoint;

    int dali1_channel = get_setting("dali1_channel");
    int dali2_channel = get_setting("dali2_channel");
    int dali3_channel = get_setting("dali3_channel");
    int dali4_channel = get_setting("dali4_channel");
    int zeroten1_lvl_to_send = setpoint;
    int zeroten2_lvl_to_send = setpoint;
    int dali1_lvl_to_send = setpoint;
    int dali2_lvl_to_send = setpoint;
    int dali3_lvl_to_send = setpoint;
    int dali4_lvl_to_send = setpoint;
    int espnow_lvl_to_send = setpoint;
    bool dali_broadcast = false;
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
            // sprintf(templogbuffer, "%s: State: Idle. Waiting for new setpoint.", TAG);
            // log_string(templogbuffer);
            configbits = get_setting("configbits");
            dali1_channel = get_setting("dali1_channel");
            dali2_channel = get_setting("dali2_channel");
            dali3_channel = get_setting("dali3_channel");
            dali4_channel = get_setting("dali4_channel");

            sprintf(templogbuffer, "IDLE. Config: Relay1: %i, Relay2: %i, DALI: %i, 0-10v 1: %i, 0-10v 2:%i, Send ESPNOW %i, Recv ESPNOW %i",
                (configbits & CONFIGBIT_USE_RELAY1) > 0,
                (configbits & CONFIGBIT_USE_RELAY2) > 0,
                (configbits & CONFIGBIT_USE_DALI) > 0,
                (configbits & CONFIGBIT_USE_0_10v1) > 0,
                (configbits & CONFIGBIT_USE_0_10v2) > 0,
                (configbits & CONFIGBIT_TRANSMIT_ESPNOW) > 0,
                (configbits & CONFIGBIT_RECEIVE_ESPNOW) > 0);
            // printf(templogbuffer);
            log_string(templogbuffer);
                // list_tasks();
        }

        fade_remaining = local_setpoint - actual_level;
        level_el = levellut[actual_level];
        manage_relay_timeouts(configbits, level_el.relay1, level_el.relay2);

        if (espnowtask != NULL && (configbits & CONFIGBIT_TRANSMIT_ESPNOW))
        {
            if (level_overrides.espnow != -1) {
                espnow_lvl_to_send = level_overrides.espnow;
            }
            else
            {
                espnow_lvl_to_send = level_el.espnow_lvl;
            }
            xTaskNotifyIndexed(espnowtask,
                SETPOINT_SLEW_NOTIFY_INDEX,
                espnow_lvl_to_send,
                eSetValueWithOverwrite);
        }
            
        if (configbits & CONFIGBIT_USE_0_10v1){
            if (level_overrides.zeroten1 != -1) {
                zeroten1_lvl_to_send = level_overrides.zeroten1;
            }
            else
            {
                zeroten1_lvl_to_send = level_el.zeroten1_lvl;
            }
            ESP_ERROR_CHECK(set_0_10v_level(pwm1, zeroten1_lvl_to_send));
        }
        if (configbits & CONFIGBIT_USE_0_10v2)
        {
            if (level_overrides.zeroten2 != -1) {
                zeroten2_lvl_to_send = level_overrides.zeroten2;
            }
            else
            {
                zeroten2_lvl_to_send = level_el.zeroten2_lvl;
            }
            ESP_ERROR_CHECK(set_0_10v_level(pwm2, zeroten2_lvl_to_send));
        }
        // for (int i=8; i<13; i+=1) {
        if (configbits & CONFIGBIT_USE_DALI)
        {
            if (level_overrides.dali1 != -1) {
                dali1_lvl_to_send = level_overrides.dali1;
            }
            else
            {
                dali1_lvl_to_send = level_el.dali1_lvl;
            }
            transmit_setlevel_dali_channel(dali_transceiver, dali1_channel, dali1_lvl_to_send);

            dali_broadcast = dali1_channel == 200;
            if (!dali_broadcast) {

                if (level_overrides.dali2 != -1) {
                    dali2_lvl_to_send = level_overrides.dali2;
                }
                else
                {
                    dali2_lvl_to_send = level_el.dali2_lvl;
                }
                if (level_overrides.dali3 != -1) {
                    dali3_lvl_to_send = level_overrides.dali3;
                }
                else
                {
                    dali3_lvl_to_send = level_el.dali3_lvl;
                }
                if (level_overrides.dali4 != -1) {
                    dali4_lvl_to_send = level_overrides.dali4;
                }
                else
                {
                    dali4_lvl_to_send = level_el.dali4_lvl;
                }
                transmit_setlevel_dali_channel(dali_transceiver, dali2_channel, dali2_lvl_to_send);
                transmit_setlevel_dali_channel(dali_transceiver, dali3_channel, dali3_lvl_to_send);
                transmit_setlevel_dali_channel(dali_transceiver, dali4_channel, dali4_lvl_to_send);
            }
        }
        // sprintf(templogbuffer, "%s: Snt %i->%i Fade %i Intvl %llu", TAG, actual_level, local_setpoint, fadetime, target_looptime);
        // log_string(templogbuffer);
        sprintf(templogbuffer, "%s: Lvl %i->%i { 0-10v1 %d, 0-10v2 %d, DALI1 %d, `DALI2 %d, DALI3 %d, DALI4 %d, ESPNOW %d, Relay1 %d, Relay2 %d LpTm %i", TAG, actual_level, local_setpoint,
            zeroten1_lvl_to_send,
            zeroten2_lvl_to_send,
            dali1_lvl_to_send,
            dali2_lvl_to_send,
            dali3_lvl_to_send,
            dali4_lvl_to_send,
            espnow_lvl_to_send,
            level_el.relay1,
            level_el.relay2,
            (int) actual_looptime);
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

static bool diagnostic(void)
{
    // gpio_config_t io_conf;
    // io_conf.intr_type    = GPIO_PIN_INTR_DISABLE;
    // io_conf.mode         = GPIO_MODE_INPUT;
    // io_conf.pin_bit_mask = (1ULL << CONFIG_EXAMPLE_GPIO_DIAGNOSTIC);
    // io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    // io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
    // gpio_config(&io_conf);

    ESP_LOGI(TAG, "Diagnostics (5 sec)...");
    // vTaskDelay(5000 / portTICK_PERIOD_MS);

    // bool diagnostic_is_ok = gpio_get_level(CONFIG_EXAMPLE_GPIO_DIAGNOSTIC);

    // gpio_reset_pin(CONFIG_EXAMPLE_GPIO_DIAGNOSTIC);
    // return diagnostic_is_ok;
    return true;
}


static void print_sha256 (const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s: %s", label, hash_print);
}


void app_main(void)
{
    ESP_LOGI(TAG, "It worked (twice)!!!!");
    initialise_logbuffer();

    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    // get sha256 digest for the partition table
    partition.address   = ESP_PARTITION_TABLE_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_MAX_LEN;
    partition.type      = ESP_PARTITION_TYPE_DATA;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for the partition table: ");

    // get sha256 digest for bootloader
    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // run diagnostic function ...
            bool diagnostic_is_ok = diagnostic();
            if (diagnostic_is_ok) {
                ESP_LOGI(TAG, "Diagnostics completed successfully! Continuing execution ...");
                esp_ota_mark_app_valid_cancel_rollback();
            } else {
                ESP_LOGE(TAG, "Diagnostics failed! Start rollback to the previous version ...");
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
        }
    }
    // sprintf(logbuffer, "This is a log");
    srand(time(NULL));
    vTaskPrioritySet(NULL, 6);
    configure_gpio();
    setup_nvs_spiffs_settings();
    read_level_luts(levellut);
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
