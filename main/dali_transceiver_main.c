#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_heap_trace.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#include "soc/uart_periph.h"

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
#include "rgb_led.h"

#define MIN_EST_LOOPTIME_MS 50

#define LOOPTIME_TOLERANCE 20
#define LOOPTIMES_OUT_OF_TOLERANCE_NEEDED 2

#define IDLE_UPDATE_INTERVAL_DURING_COOLDOWN_MS (800)
#define MAX_IDLE_UPDATE_INTERVAL_AFTER_COOLDOWN_MS (60UL * 60UL * 1000UL)
#define MINIMUM_TARGET_LOOPTIME 15

#define BALLAST_WAKE_LOOKAHEAD_MS 2000

#define IDLE_UPDATE_INTERVAL_ADDITION_US_RECV_ESPNOW (500)

#define HASH_LEN 32 /* SHA-256 digest length */

static const char *TAG = "main";

static const char *source_str[] = {"REST", " ADC", "ESPN", "BUTN", "ALRM", "RAND", "INIT"};

static const char *spaces = "   ";

nvs_handle_t mainloop_nvs_handle;

static __NOINIT_ATTR device_status_t status;

void setup_networking(void *params)
{
    // vTaskDelay(5000);
    networking_ctx_t *ctx = (networking_ctx_t *)params;
    setup_wifi(ctx);
    setup_sntp(ctx->status);
    setup_httpserver(ctx);
    while (1)
    {
        vTaskDelay(portMAX_DELAY);
    }
}

void list_tasks()
{
    char buffer[4096];
    vTaskList(buffer);
    printf(buffer);
}

static int tick_inc = 1;
static uint8_t tick_inc_array[255] = {1};
static uint32_t tlt_array[255] = {100};
static uint64_t calcreftime;

int fadetime;

uint32_t get_time_ms()
{
    return (uint32_t)(esp_timer_get_time() >> 10);
}

// void ___calc_tickinc_and_looptime(uint64_t looptime, int dif)
// {
//     if (fadetime <= 50)
//     {
//         tick_inc = 254;
//         target_looptime = 0;
//         return;
//     }
//     calcreftime = esp_timer_get_time();
//     ideal_time_between_levels_us = ((uint64_t)fadetime) << 10;
//     tick_inc = 0;
//     uint64_t intermediate = ideal_time_between_levels_us / abs(dif);
//     if (intermediate > 180000000LL)
//         intermediate = 180000000LL;
//     while (tick_inc < 254)
//     {
//         tick_inc += 1;
//         target_looptime = intermediate * (uint64_t)tick_inc;
//         // fudge the numbers a bit for smoother fades at expense of accuracy
//         if (target_looptime < MINIMUM_TARGET_LOOPTIME)
//         {
//             continue;
//         }
//         if (tick_inc == 1 && (target_looptime * 1.5) >= looptime)
//             break;
//         if (tick_inc == 2 && (target_looptime * 1.3) >= looptime)
//             break;
//         if (tick_inc == 3 && (target_looptime * 1.15) >= looptime)
//             break;

//         if (target_looptime >= looptime)
//             break;
//     }
//     // ESP_LOGI(TAG, "calc: looptime %llu, tick_inc %i, %i ()", looptime, tick_inc, dif);
//     ESP_LOGD(TAG, "Calc time %llu us", esp_timer_get_time() - calcreftime);
// }

// #define CURVE(inp) (inp * inp - (inp << 9) + 108000UL)

uint32_t curve(const uint32_t input)
{
    return input * input - (input << 9) + 108000UL;
}

void calc_fade_increments(uint32_t looptime, int start, int finish)
{
    if (fadetime <= 50)
    {
        for (int i = 0; i <= 254; i++)
        {
            tick_inc_array[i] = 254;
            tlt_array[i] = 0;
        }
        return;
    }
    if (looptime < MINIMUM_TARGET_LOOPTIME)
        looptime = MINIMUM_TARGET_LOOPTIME;
    ESP_LOGD(TAG, "Need to calculate from %lu looptime  %i fade: %i to %i", looptime, fadetime, start, finish);
    uint32_t temp_tick_inc;
    uint32_t intermediate_tick_inc;
    uint32_t looptime_ms = looptime;
    uint32_t temp_tlt;
    uint64_t starttime = esp_timer_get_time();
    uint32_t tempcurve;
    uint32_t avg_fade_pos = (start + finish) >> 1;

    // curve(90) is selected to fine tune overall fade time
    uint32_t prop_fadetime = (((uint64_t)fadetime * (uint64_t)curve(avg_fade_pos)) / ((uint64_t)curve(90) * (uint64_t)abs(finish - start))) << 8;
    // ESP_LOGI(TAG, "prop fadetime %lu", prop_fadetime);
    // uint32_t prop_fadetime = ((fadetime * 1000) / (1000  * abs(finish - start));
    bool avoid_overflow = prop_fadetime > 0x7FFFFF;
    for (int i = _MIN(start, finish); i <= _MAX(start, finish); i++)
    {
        tempcurve = curve(i);
        if (avoid_overflow)
        {
            temp_tick_inc = 1;
            temp_tlt = ((prop_fadetime * temp_tick_inc) / tempcurve) << 8;
        }
        else
        {
            intermediate_tick_inc = looptime_ms * (tempcurve >> 8) / prop_fadetime;
            temp_tick_inc = intermediate_tick_inc + 1;
            temp_tlt = (prop_fadetime * (temp_tick_inc << 8)) / tempcurve;
        }

        tick_inc_array[i] = temp_tick_inc;
        tlt_array[i] = temp_tlt;
        // if (1 && (i & 0x7) == 0)
        // {
        // ESP_LOGI(TAG, "Calculated index %i: Inc %lu, TLT %lu tc %lu", i, temp_tick_inc, temp_tlt, tempcurve);
        // }
    }
    // ESP_LOGI(TAG, "Calculation took %llu us", esp_timer_get_time() - starttime);
}

static int dali_addresses[6];
static char dali_address_key_buffer[] = "dalix_address";
static uint8_t existing_dali_levels[6];

static uint8_t already_off[] = {0, 0, 0, 0, 0, 0};

void get_dali_addresses()
{
    for (int i = 0; i < 6; i++)
    {
        dali_address_key_buffer[4] = 'a' + (char)i;
        dali_addresses[i] = get_setting(dali_address_key_buffer);
    }
}

int configbits;

int count_dali_channels()
{
    if ((configbits & CONFIGBIT_USE_DALI) == 0)
        return 0;
    int count = 0;
    for (int i = 0; i < 6; i++)
    {
        if (dali_addresses[i] != -1)
            count += 1;
        if (dali_addresses[i] == 200)
            return 1;
    }
    return count;
}

int estimate_looptime()
{
    return _MAX(count_dali_channels() * 25 - 5, 10);
}

uint8_t transmit_setlevel_dali_channel(dali_transceiver_handle_t transceiver, int channel_num, int level, bool force_send)
{
    int address = dali_addresses[channel_num];
    int override = status.level_overrides.dali[channel_num];
    if (override != -1)
        level = status.level_overrides.dali[channel_num];
    if (!status.power_on)
        level = 0;
    already_off[channel_num] = (level == 0);
    if (address == -1)
    {
        already_off[channel_num] = 1;
        return 0;
    }
    if (!force_send && level == existing_dali_levels[channel_num])
    {
        // value isn't new
        return level;
    }
    existing_dali_levels[channel_num] = level;
    ESP_LOGD(TAG, "(DALI %i) Sending DALI value %i, to %i", channel_num, address, level);
    if (address >= 0 && address <= 63)
    {
        dali_set_level_noblock(transceiver, address, level, pdMS_TO_TICKS(130));
        return level;
    }
    if (address >= 100 && address <= 115)
    {
        dali_set_level_group_noblock(transceiver, address - 100, level, pdMS_TO_TICKS(130));
        return level;
    }
    if (address == 200)
    {
        dali_broadcast_level_noblock(transceiver, level);
        return level;
    }
    ESP_LOGE(TAG, "Unknown DALI Channel %i", address);
    return 0;
}

static char tinybuffer[1];
static SemaphoreHandle_t printf_mutex;

int buffer_vprint(const char *format, va_list args)
{
    int strsize;
    strsize = vsnprintf(tinybuffer, 0, format, args);
    if (1)
    {
        char *tempbuf = malloc(strsize + 2);
        vsnprintf(tempbuf, strsize + 1, format, args);
        if (tempbuf[strsize - 1] != '\n')
        {
            tempbuf[strsize - 1] = '\n';
            tempbuf[strsize] = 0;
        }
        int ret = log_string(tempbuf, strsize, true);
        fputs(tempbuf, stdout);
        free(tempbuf);
        return ret;
    }
    return 0;
}

void random_setpointer_task(void *params)
{
    device_status_t *status = (device_status_t *)params;
    int mult = (rand() & 0xF);
    uint8_t setpoint;
    while (1)
    {
        mult = (rand() & 0xF);
        setpoint = (rand() & 0xFF);

        status->fadetime_ms = ((rand() & 0xFF) * mult * mult);
        status->setpoint = setpoint;
        status->setpoint_source = SETPOINT_SOURCE_RANDOM;
        xTaskNotifyIndexed(status->mainloop_task, NEW_SETPOINT_NOTIFY_IDX, SETPOINT_SOURCE_RANDOM, eSetValueWithOverwrite);
        vTaskDelay(((rand() & 0x7) * mult * mult) + 1);
    }
}

static void print_sha256(const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i)
    {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s: %s", label, hash_print);
}

dali_transceiver_handle_t setup_dali(networking_ctx_t *networking_ctx)
{
    dali_transceiver_config_t transceiver_config = dali_transceiver_sensible_default_config;
    transceiver_config.invert_input = DALI_DONT_INVERT;
    transceiver_config.invert_output = DALI_DONT_INVERT;
    transceiver_config.transmit_queue_size_frames = 1;
    transceiver_config.receive_queue_size_frames = 16;
    transceiver_config.enable_receiving = false;
    transceiver_config.receive_gpio_pin = RX_GPIO;
    transceiver_config.transmit_gpio_pin = TX_GPIO;
    transceiver_config.parser_config.forward_frame_action = DALI_PARSER_ACTION_LOG;
    transceiver_config.parser_config.mangled_frame_action = DALI_PARSER_ACTION_LOG_AND_RECORD;

    dali_transceiver_handle_t dali_transceiver;
    ESP_ERROR_CHECK(dali_setup_transceiver(transceiver_config, &dali_transceiver));
    networking_ctx->dali_command_queue = dali_setup_command_queue(dali_transceiver);
    dali_broadcast_level_noblock(dali_transceiver, 0);
    return dali_transceiver;
}

void check_partitions()
{
    uint8_t sha_256[HASH_LEN] = {0};
    esp_partition_t partition;

    // get sha256 digest for the partition table
    partition.address = ESP_PARTITION_TABLE_OFFSET;
    partition.size = ESP_PARTITION_TABLE_MAX_LEN;
    partition.type = ESP_PARTITION_TYPE_DATA;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for the partition table: ");

    // get sha256 digest for bootloader
    partition.address = ESP_BOOTLOADER_OFFSET;
    partition.size = ESP_PARTITION_TABLE_OFFSET;
    partition.type = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    esp_err_t getstate = esp_ota_get_state_partition(running, &ota_state) == ESP_OK;
    ESP_LOGI(TAG, "Get ota state result %i", getstate);
    if (getstate)
    {
        ESP_LOGI(TAG, "Ota state is %i", ota_state);
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
        {
            // run diagnostic function ...
            bool diagnostic_is_ok = true; // diagnostic();
            if (diagnostic_is_ok)
            {
                ESP_LOGI(TAG, "Diagnostics completed successfully! Continuing execution ...");
                esp_ota_mark_app_valid_cancel_rollback();
            }
            else
            {
                ESP_LOGE(TAG, "Diagnostics failed! Start rollback to the previous version ...");
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
        }
    }
}

level_t get_level_el(uint8_t level, device_status_t status, uint16_t full_power){
    if (status.actual_level == 0)
    {
        // we need to always ensure zero is off regardless of full_power setting
        return status.lut[0];
    }
    else
    {
        return status.lut[clamp((int)level + (int)full_power - 254, 0, 254)];
    }
}

static networking_ctx_t networking_ctx;

void app_main(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    configure_gpio();
    bool stale = (status.stale_flag == STATUS_STALE_FLAG);
    bool reinit = stale || ((reason != ESP_RST_DEEPSLEEP) && (reason != ESP_RST_SW));
    if (reinit)
    {
        initialise_logbuffer();
    }
    printf_mutex = xSemaphoreCreateMutex();
    esp_log_set_vprintf(buffer_vprint);
    ESP_LOGI(TAG, "Checking partitions...");
    check_partitions();
    ESP_LOGI(TAG, "Seeding random...");
    srand(time(NULL));
    // test_flexclamp();
    vTaskPrioritySet(NULL, 6);
    // rotate_gpio_outputs();

    ESP_LOGI(TAG, "Setting up SPIFFS and NVS...");
    setup_nvs_spiffs_settings();

    ESP_LOGI(TAG, "Reading LUTs...");
    read_level_luts(status.lut);

    // TaskHandle_t randomtask;
    // xTaskCreate(random_setpointer_task, "random setpointer task", 2048, (void*) &status, 7, &randomtask);

    uint8_t dip_address = read_dip_switches();
    ESP_LOGI(TAG, "Read DIP Switch address: %d", dip_address);

    setup_relays(get_setting("configbits"));
    int startup_setpoint_lvl = get_setting("startup_level");

    status.mainloop_task = xTaskGetCurrentTaskHandle();
    if (reinit)
    {
        if (stale)
        {
            ESP_LOGI(TAG, "Status marked as stale - re-initialising");
        }
        else
        {
            ESP_LOGI(TAG, "Fresh start up detected, loading startup level %i from NVS", startup_setpoint_lvl);
        }
        status.setpoint = startup_setpoint_lvl;
        status.fadetime_ms = USE_DEFAULT_FADETIME;
        status.setpoint_source = SETPOINT_SOURCE_INIT;
        status.power_on = 1;
        status.actual_level = startup_setpoint_lvl;
        for (int i = 0; i < 6; i++)
        {
            status.level_overrides.dali[i] = -1;
        }
        status.level_overrides.espnow = -1;
        status.level_overrides.zeroten1 = -1;
        status.level_overrides.zeroten2 = -1;
        status.stale_flag = 0;
    }
    else
    {
        ESP_LOGI(TAG, "Software restart detected - no stale flag - keeping previous setpoint %d", status.setpoint);
    }

    setup_rgb_led(&status);
    xTaskNotifyIndexed(xTaskGetCurrentTaskHandle(), NEW_SETPOINT_NOTIFY_IDX, SETPOINT_SOURCE_INIT, eSetValueWithOverwrite);

    TaskHandle_t networktask;
    networking_ctx.mainloop_task = xTaskGetCurrentTaskHandle();
    networking_ctx.dali_command_queue = NULL;
    networking_ctx.status = &status;
    xTaskCreate(setup_networking, "setup_networking", 4096, (void *)&networking_ctx, 2, &networktask);

    zeroten_handle_t pwm1;
    zeroten_handle_t pwm2;
    ESP_ERROR_CHECK(setup_0_10v_channel(PWM_010v_GPIO, CALIBRATION_LOOKUP_NVS, &pwm1));
    ESP_ERROR_CHECK(setup_0_10v_channel(PWM_010v2_GPIO, CALIBRATION_LOOKUP_NVS, &pwm2));

    setup_button_interrupts(&status, pwm1, pwm2);

    dali_transceiver_handle_t dali_transceiver = setup_dali(&networking_ctx);

    // light_adc_config_t adcconfig = {
    //     .notify_task = xTaskGetCurrentTaskHandle(),
    //     .tolerance = 11,
    //     .sampled_needed = 3,
    //     .average_window = 4,
    //     .status = &status};
    // int resistor_level;
    // setup_adc(adcconfig);

    BaseType_t received;
    esp_err_t sent;
    int firsttime = 1;
    ESP_LOGI(TAG, "Preparing main loop");
    uint32_t recv_value;
    uint32_t reftime = get_time_ms();
    uint64_t reawake_time = 0;
    ESP_LOGI(TAG, "Getting settings from NVS...");

    int default_fadetime = get_setting("default_fade");
    configbits = get_setting("configbits");
    int looptime = MIN_EST_LOOPTIME_MS;

    int updates_performed = 0;
    int fade_remaining = 0;

    bool at_setpoint;
    uint8_t level_el_array[sizeof(level_t)];
    int dali_broadcast;
    level_t future_el;
    uint32_t current_time;
    uint32_t actual_looptime;
    int random_looptime = 1;
    uint8_t local_setpoint = status.setpoint;

    ESP_LOGI(TAG, "Getting DALI addresses");
    get_dali_addresses();
    uint16_t full_power = get_setting("full_power");
    int zeroten1_lvl_to_send = 0;
    int zeroten2_lvl_to_send = 0;
    int dali_levels_to_send[6];
    for (int i = 0; i < 6; i++)
    {
        dali_levels_to_send[i] = 0;
    }
    int espnow_lvl_to_send = 0;
    int relay1_lvl_to_send = 0;
    int relay2_lvl_to_send = 0;
    char espnow_str[] = "   ";
    char zeroten1_str[] = "   ";
    char zeroten2_str[] = "   ";
    char relay1_str[] = "   ";
    char relay2_str[] = "   ";
    char dali_str[6][4] = {
        "   ",
        "   ",
        "   ",
        "   ",
        "   ",
        "   ",
    };
    int lookahead;
    int idlecount = 0;
    int levellog_count = 0;
    bool force_resend = false;
    device_status_t old_status = status;
    int max_lookahead;
    bool new_setpoint;
    int minlevelbits = 0;
    uint32_t idle_reawake_interval = 1000;
    uint32_t cooldown_reawake_interval = 1000;
    uint32_t max_idle_reawake_interval = 1000;
    uint32_t cooldown_duration = get_setting("idle_cooldown");
    int idle_sends = 0;
    uint32_t idle_start_time = reftime;
    int future_level;
    uint8_t min_level_array[sizeof(level_t)] = { 0 };
    uint8_t start_of_fade_level = 0;
    int looptime_outside_tolerance_count = 0;

    ESP_LOGI(TAG, "Starting main loop...");
    while (1)
    {
        //
        // do loop timing housekeeping
        //
        actual_looptime = (get_time_ms() - reftime);
        if (fade_remaining != 0 && actual_looptime > MINIMUM_TARGET_LOOPTIME)
        {
            if (actual_looptime > (tlt_array[status.actual_level] + LOOPTIME_TOLERANCE) || actual_looptime < (tlt_array[status.actual_level] - LOOPTIME_TOLERANCE))
            {
                looptime_outside_tolerance_count += 1;
            }
            if (looptime_outside_tolerance_count >= LOOPTIMES_OUT_OF_TOLERANCE_NEEDED)
            {
                // calc_tickinc_and_looptime(actual_looptime, status.setpoint - start_of_fade_level);
                calc_fade_increments(actual_looptime, start_of_fade_level, status.setpoint);

                looptime_outside_tolerance_count = 0;
            }
        }
        while (1)
        {
            //s
            // spin until next tick or new setpoint received 
            //

            received = xTaskNotifyWaitIndexed(NEW_SETPOINT_NOTIFY_IDX, 0, 0, &recv_value, 1);
            current_time = get_time_ms();
            force_resend = received;
            if (received == pdTRUE)
            {
                old_status = status;
                status.setpoint = clamp(status.setpoint, 0, 254);

                // signal new setpoint
                gpio_set_level(LED1_GPIO, 1);
                start_of_fade_level = status.actual_level;
                full_power = clamp(get_setting("full_power"), 0, 512);
                max_idle_reawake_interval = _uMAX(((uint32_t)get_setting("idle_intvl_ms")), 200UL);
                cooldown_duration = _uMAX(((uint32_t)get_setting("idle_cooldown")), 0UL);
                new_setpoint = true;
                // ESP_LOGI(TAG, "Received new setpoint: %d, fade: %lu source %s", status.setpoint, status.fadetime_ms, source_str[status.setpoint_source]);
                random_looptime = (rand() & 127);
                tick_inc = 1;
                configbits = get_setting("configbits");
                switch (status.fadetime_ms){
                    case USE_DEFAULT_FADETIME:
                        fadetime = get_setting("default_fade");
                        break;
                    case USE_SLOW_FADETIME:
                        fadetime = get_setting("slow_fade");
                        break;
                    default:
                        fadetime = status.fadetime_ms;
                }
                if (fadetime < 8) 
                    // it's basically no fade lets avoid any divide by zeros
                    fadetime = 8;

                if (status.setpoint != status.actual_level)
                {
                    calc_fade_increments(estimate_looptime(), start_of_fade_level, status.setpoint);
                    looptime_outside_tolerance_count = 0;
                }
                break;
            };
            if (current_time > reawake_time)
                break;
        };

        /////
        // process tick -> move actual_level fading towards setpoint
        /////
        reftime = get_time_ms();

        // reset lookahead array to be paranoid
        memcpy(min_level_array, &status.lut[0], sizeof(level_t));

        fade_remaining = status.setpoint - status.actual_level;
        if (fade_remaining != 0)
        {
            // process fade tick
            //

            // look ahead to rewake ballasts ahead of time
            int end_lookahead_level;
            uint32_t time_acc = 0;
            int sign = fade_remaining > 0 ? 1 : -1;
            int lvl = status.actual_level;
            int loops = 0;
            while (lvl != status.setpoint)
            {
                loops += 1;
                if (loops > 255)
                {
                    ESP_LOGE(TAG, "Something went wrong in fade search loop. Loop count timeout.");
                    // zero array
                    memcpy(min_level_array, &status.lut[0], sizeof(level_t));
                    break;
                }
                ESP_LOGD(TAG, "Looking at lvl %d. Adding %lu to acc (%lu). Ticking %d", lvl, tlt_array[lvl], time_acc, tick_inc_array[lvl]);

                // check to see if we've gone far enough ahead
                time_acc += tlt_array[lvl];
                if (time_acc > BALLAST_WAKE_LOOKAHEAD_MS) break;
                // get test level
                int next_lvl_unclamped = lvl + sign * (int)tick_inc_array[lvl];
                lvl =  flexclamp(next_lvl_unclamped, status.actual_level, status.setpoint);
                

                // get iterable copy so we avoid type punning shenanigans
                uint8_t future_level_array[sizeof(level_t)];
                level_t future_el = get_level_el(lvl, status, full_power);
                memcpy(future_level_array, &future_el, sizeof(level_t));

                // iterate over array to check for used channels
                for (int ch = 0; ch < sizeof(level_t); ch++)
                {
                    if (future_level_array[ch] > 0)
                        // ensure channel does not switch completely off
                        min_level_array[ch] = 1;
                }
            }
            
            // prepare next loop
            reawake_time = reftime + tlt_array[status.actual_level];
            idle_sends = 0;
            idle_start_time = reftime;

            // tick increment
            int act = status.actual_level;
            int inc = (int)tick_inc_array[status.actual_level] * sign;
            // we need to make sure we never overshoot
            status.actual_level = flexclamp(act + inc, act, status.setpoint);
        }
        else
        {
            // we're at setpoint
            //

            // signal end of fade
            gpio_set_level(LED1_GPIO, 0);

            // Resend all levels just to be sure
            force_resend = true;

            configbits = get_setting("configbits");

            // calculate reawake intervals
            if ((reftime - idle_start_time) < cooldown_duration)
            {
                idle_reawake_interval = _uMIN(IDLE_UPDATE_INTERVAL_DURING_COOLDOWN_MS, max_idle_reawake_interval);
            }
            else
            {
                idle_reawake_interval = _uMIN(MAX_IDLE_UPDATE_INTERVAL_AFTER_COOLDOWN_MS, max_idle_reawake_interval);
            }
            // set reawake time
            reawake_time = reftime + (uint64_t)idle_reawake_interval;
            ESP_LOGD(TAG, "Idle reawake %lu reftime %lu idle_start %lu cd %lu", idle_reawake_interval, reftime, idle_start_time, cooldown_duration);

            if (configbits & CONFIGBIT_RECEIVE_ESPNOW)
            {
                // we don't want double awakening if we're receiving regular updates so we add a bit extra to accommodate network latency
                reawake_time += IDLE_UPDATE_INTERVAL_ADDITION_US_RECV_ESPNOW;
            }

            // reset min_level array
            memcpy(min_level_array, &status.lut[0], sizeof(level_t));
            // refresh local data from NVS
            get_dali_addresses();
            full_power = clamp(get_setting("full_power"), 0, 512);

            idlecount += 1;
            idle_sends += 1;
            if (!(idlecount & 0x3F))
            {
                // log RAM situation every 1000 loops
                ESP_LOGI(TAG, "Min free heap %i", heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
                ESP_LOGI(TAG, "Free heap %i", heap_caps_get_free_size(MALLOC_CAP_8BIT));
            }
        }

        /////
        // process sends for tick
        /////
        level_t level_el = get_level_el(status.actual_level, status, full_power);

        // ensure minimum levels
        memcpy(level_el_array, &level_el, sizeof(level_t));
        for (int ch = 0; ch < sizeof(level_t); ch++)
        {
            if (level_el_array[ch] < min_level_array[ch])
                level_el_array[ch] = min_level_array[ch];
        }
        memcpy(&level_el, &level_el_array, sizeof(level_t));

        // RELAYS
        relay1_lvl_to_send = status.power_on ? level_el.relay1 : 0;
        relay2_lvl_to_send = status.power_on ? level_el.relay2 : 0;
        manage_relay_timeouts(configbits, relay1_lvl_to_send, relay2_lvl_to_send);

        // ESPNOW
        if (espnowtask != NULL && (configbits & CONFIGBIT_TRANSMIT_ESPNOW))
        {
            espnow_lvl_to_send = (status.level_overrides.espnow == -1) ? level_el.espnow_lvl : status.level_overrides.espnow;
            if (!status.power_on)
                espnow_lvl_to_send = 0;

            xTaskNotifyIndexed(espnowtask,
                               LIGHT_LEVEL_NOTIFY_INDEX,
                               espnow_lvl_to_send,
                               eSetValueWithOverwrite);
        }

        // 0-10V
        if (configbits & CONFIGBIT_USE_0_10v1)
        {
            int16_t override = status.level_overrides.zeroten1;
            zeroten1_lvl_to_send = (override == -1) ? level_el.zeroten1_lvl : override;
            if (!status.power_on)
            {
                zeroten1_lvl_to_send = 0;
            }
            set_0_10v_level(pwm1, zeroten1_lvl_to_send);
            
            if (configbits & CONFIGBIT_USE_0_10v2)
            {
                int16_t override = status.level_overrides.zeroten2;
                zeroten2_lvl_to_send = (override == -1) ? level_el.zeroten2_lvl : override;
                if (!status.power_on)
                {
                    zeroten2_lvl_to_send = 0;
                }
                set_0_10v_level(pwm2, zeroten2_lvl_to_send);
            }
            else
            {
                set_zero_duty_pwm_channel(pwm2);
            }
        }
        else
        {
            // we can't use channel 2 without channel 1 so stop both
            set_zero_duty_pwm_channel(pwm1);
            set_zero_duty_pwm_channel(pwm2);
        }


        // DALI

        // if we're using DALI use mutex to make sure we're uninterruped by any dali config commands from the REST API
        if ((configbits & CONFIGBIT_USE_DALI) && dali_take_mutex(dali_transceiver, 0))
        {
            // check for any DALI broadcast commands -> we don't need to bother with short addresses
            dali_broadcast = -1;
            for (int i = 0; i < 6; i++)
            {
                ESP_LOGD(TAG, "DALI Channel %i address is %i", i, dali_addresses[i]);
                if (dali_addresses[i] == 200)
                {
                    dali_broadcast = i;
                    break;
                }
            }
            if (dali_broadcast >= 0)
            {
                dali_levels_to_send[dali_broadcast] = transmit_setlevel_dali_channel(dali_transceiver, dali_broadcast, level_el.dali_lvl[dali_broadcast], force_resend);
            }
            else
            {
                for (int i = 0; i < 6; i++)
                {
                    dali_levels_to_send[i] = transmit_setlevel_dali_channel(dali_transceiver, i, level_el.dali_lvl[i], force_resend);
                }
            }
            dali_give_mutex(dali_transceiver);
        }

        /////
        // logging for tick
        /////
        levellog_count += 1;

        // Write levels to individual strings
        if (configbits & CONFIGBIT_USE_0_10v1)
            snprintf(zeroten1_str, 4, "%3.1i", zeroten1_lvl_to_send);
        if (configbits & CONFIGBIT_USE_0_10v2)
            snprintf(zeroten2_str, 4, "%3.1i", zeroten2_lvl_to_send);
        for (int i = 0; i < 6; i++)
        {
            if ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[i] != -1)
                snprintf(dali_str[i], 4, "%3.1i", dali_levels_to_send[i]);
        }
        if (configbits & CONFIGBIT_TRANSMIT_ESPNOW)
            snprintf(espnow_str, 4, "%3.1i", espnow_lvl_to_send);
        if (configbits & CONFIGBIT_USE_RELAY1)
            snprintf(relay1_str, 4, "%3.1i", relay1_lvl_to_send);
        if (configbits & CONFIGBIT_USE_RELAY2)
            snprintf(relay2_str, 4, "%3.1i", relay2_lvl_to_send);

        if ((configbits & (CONFIGBIT_USE_RELAY1 | CONFIGBIT_USE_RELAY2)))
        {
            // We are using relays so bother logging them
            if (!(levellog_count & 7))
            {
                // log legend every 8 logs
                ESP_LOGI(TAG, "Lvl-> SP PWR | SRCE N | 0-10v1 0-10v2 DALIA DALIB DALIC DALID DALIE DALIF ESPN Rly1 Rly2    Fade    TLT   LT Wake");
            }
            ESP_LOGI(TAG, "%3.1u->%3.1d %s | %s %1.i |    %s    %s   %s   %s   %s   %s   %s   %s  %s  %s  %s %7.1i %6.1lu %4.1i  %3.1lu",
                     status.actual_level,
                     status.setpoint,
                     (status.power_on) ? " ON" : "OFF",
                     source_str[status.setpoint_source & 0xF],
                     (int)new_setpoint,
                     (configbits & CONFIGBIT_USE_0_10v1) ? zeroten1_str : spaces,
                     (configbits & CONFIGBIT_USE_0_10v2) ? zeroten2_str : spaces,
                     ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[0] != -1) ? dali_str[0] : spaces,
                     ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[1] != -1) ? dali_str[1] : spaces,
                     ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[2] != -1) ? dali_str[2] : spaces,
                     ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[3] != -1) ? dali_str[3] : spaces,
                     ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[4] != -1) ? dali_str[4] : spaces,
                     ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[5] != -1) ? dali_str[5] : spaces,
                     (configbits & CONFIGBIT_TRANSMIT_ESPNOW) ? espnow_str : spaces,
                     (configbits & CONFIGBIT_USE_RELAY1) ? relay1_str : spaces,
                     (configbits & CONFIGBIT_USE_RELAY2) ? relay2_str : spaces,
                     fadetime,
                     tlt_array[status.actual_level],
                     ((int)actual_looptime),
                     idle_reawake_interval >> 10);
        }
        else
        {
            // We're not using relay so don't bother logging them
            if (!(levellog_count & 7))
            {
                // log legend every 8 logs
                ESP_LOGI(TAG, "Lvl-> SP PWR | SRCE N | 0-10v1 0-10v2 DALIA DALIB DALIC DALID DALIE DALIF ESPN     Fade    TLT   LT Wake");
            }
            ESP_LOGI(TAG, "%3.1u->%3.1d %s | %s %1.i |    %s    %s   %s   %s   %s   %s   %s   %s  %s  %7.1i %6.1lu %4.1i  %3.1lu",
                     status.actual_level,
                     status.setpoint,
                     (status.power_on) ? " ON" : "OFF",
                     source_str[status.setpoint_source & 0xF],
                     (int)new_setpoint,
                     (configbits & CONFIGBIT_USE_0_10v1) ? zeroten1_str : spaces,
                     (configbits & CONFIGBIT_USE_0_10v2) ? zeroten2_str : spaces,
                     ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[0] != -1) ? dali_str[0] : spaces,
                     ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[1] != -1) ? dali_str[1] : spaces,
                     ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[2] != -1) ? dali_str[2] : spaces,
                     ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[3] != -1) ? dali_str[3] : spaces,
                     ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[4] != -1) ? dali_str[4] : spaces,
                     ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[5] != -1) ? dali_str[5] : spaces,
                     (configbits & CONFIGBIT_TRANSMIT_ESPNOW) ? espnow_str : spaces,
                     fadetime,
                     tlt_array[status.actual_level],
                     ((int)actual_looptime),
                     idle_reawake_interval >> 10);
        }
        new_setpoint = false;
    }
}
