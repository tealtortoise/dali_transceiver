#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "base.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_flash_partitions.h"
#include "esp_heap_trace.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "soc/uart_periph.h"
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
#include "ble_wifi_prov.h"
#include "http_server.h"
// #endif // IS_PRIMARY
#include "adc.h"
#include "base.h"
#include "buttons.h"
#include "gpio_utils.h"
#include "ledflash.h"
#include "realtime.h"
#include "rgb_led.h"
#include "settings.h"
#include "uart.h"

#define MIN_EST_LOOPTIME_MS 50

#define LOOPTIME_TOLERANCE 20
#define LOOPTIMES_OUT_OF_TOLERANCE_NEEDED 2

#define IDLE_UPDATE_INTERVAL_DURING_COOLDOWN_MS (800)
#define MAX_IDLE_UPDATE_INTERVAL_AFTER_COOLDOWN_MS (60UL * 60UL * 1000UL)
#define MINIMUM_TARGET_LOOPTIME 15

#define BALLAST_WAKE_LOOKAHEAD_MS 1600

#define IDLE_UPDATE_INTERVAL_ADDITION_US_RECV_ESPNOW (500)

#define HASH_LEN 32 /* SHA-256 digest length */

static const char *TAG = "main";

static const char *source_str[] = {"REST", " ADC", "ESPN", "BUTN",
                                   "ALRM", "RAND", "INIT"};

#define LOOPLOG_LENGTH 256

static char looplog[LOOPLOG_LENGTH];
static char looplog_headers[LOOPLOG_LENGTH];
static char looplog_template[256];

nvs_handle_t mainloop_nvs_handle;

static __NOINIT_ATTR device_status_t status;

static SemaphoreHandle_t logstring_mutex;

static esp_reset_reason_t reset_reason;
static networking_ctx_t networking_ctx;
static TaskHandle_t networktask;

static dali_transceiver_handle_t dali_transceiver;

static zeroten_handle_t pwm1;
static zeroten_handle_t pwm2;

static int tick_sign;
static BaseType_t received;
static uint32_t recv_value;
static uint32_t reftime;
static uint64_t reawake_time = 0;
static int default_fadetime;

static int fade_remaining = 0;

static uint8_t level_el_array[sizeof(level_t)];
static int dali_broadcast;
static uint32_t current_time;
static uint32_t actual_looptime;
static int random_looptime = 1;
static uint8_t local_setpoint;

static uint16_t full_power;
static int zeroten1_lvl_to_send = 0;
static int zeroten2_lvl_to_send = 0;
static int dali_levels_to_send[DALI_CHANNELS] = {0};
static int espnow_lvl_to_send = 0;
static int relay1_lvl_to_send = 0;
static int relay2_lvl_to_send = 0;
static int lookahead;
static int idlecount = 0;
static int levellog_count = 0;
static bool force_resend = false;
static device_status_t old_status;
static bool new_setpoint;
static uint32_t idle_reawake_interval = 1000;

static uint32_t max_idle_reawake_interval = 1000;
static uint32_t cooldown_duration;
static int idle_sends = 0;
static uint32_t idle_start_time;

static uint8_t min_level_array[sizeof(level_t)] = {0};
static uint8_t start_of_fade_level = 0;
static int looptime_outside_tolerance_count = 0;
static int startup_setpoint_lvl;

static level_t current_level_el;

static char tinybuffer[1];
static char vprint_buffer[1024];
static SemaphoreHandle_t printf_mutex;
static const char *logmemerror = "Unable to allocate memory for logging";

int buffer_vprint(const char *format, va_list args)
{
    if (xSemaphoreTake(printf_mutex, pdMS_TO_TICKS(200)))
    {
        int strsize;
        strsize = vsnprintf(tinybuffer, 0, format, args);
        if (1)
        {
            char *tempbuf = malloc(strsize + 2);
            if (tempbuf == NULL)
            {
                log_string(logmemerror, strlen(logmemerror), true);
                fputs(logmemerror, stdout);
                xSemaphoreGive(printf_mutex);
                return 1;
            }
            vsnprintf(tempbuf, strsize + 1, format, args);
            if (tempbuf[strsize - 1] != '\n')
            {
                tempbuf[strsize - 1] = '\n';
                tempbuf[strsize] = 0;
            }
            int ret = log_string(tempbuf, strsize, true);
            fputs(tempbuf, stdout);
            free(tempbuf);
            
            xSemaphoreGive(printf_mutex);
            return ret;
        }
    }
    return 0;
}

void setup_networking(void *params)
{
    // vTaskDelay(5000);
    networking_ctx_t *ctx = (networking_ctx_t *)params;
    wifi_provision(ctx);
    setup_sntp(ctx->status);
    setup_httpserver(ctx);
    while (1)
    {
        vTaskDelay(portMAX_DELAY);
    }
}

static char listtaskbuffer[2048];
void list_tasks()
{
    vTaskList(listtaskbuffer);
    ESP_LOGI(TAG, "%s", listtaskbuffer);
}

static int tick_inc = 1;
static uint8_t tick_inc_array[255] = {1};
static uint32_t tlt_array[255] = {100};

int fadetime;

uint32_t
get_time_ms()
{
    return (uint32_t)(esp_timer_get_time() >> 10);
}

uint32_t
fadecurve(const uint32_t input)
{
    // best option if LUT uses default DALI curve
    return input * input - (input << 9) + 108000UL;
}

uint32_t
curve_lin(const uint32_t input)
{
    // best option if LUT uses visually linear target curve
    return 60000;
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
    ESP_LOGD(TAG,
             "Need to calculate from %lu looptime  %i fade: %i to %i",
             looptime,
             fadetime,
             start,
             finish);
    uint32_t temp_tick_inc;
    uint32_t intermediate_tick_inc;
    uint32_t looptime_ms = looptime;
    uint32_t temp_tlt;
    uint32_t tempcurve;
    uint32_t avg_fade_pos = (start + finish) >> 1;

    // curve(90) is selected to fine tune overall fade time
    uint32_t prop_fadetime =
        (((uint64_t)fadetime * (uint64_t)curve_lin(avg_fade_pos)) /
         ((uint64_t)fadecurve(90) * (uint64_t)abs(finish - start)))
        << 8;
    // ESP_LOGI(TAG, "prop fadetime %lu", prop_fadetime);
    // uint32_t prop_fadetime = ((fadetime * 1000) / (1000  * abs(finish -
    // start));
    bool avoid_overflow = prop_fadetime > 0x7FFFFF;
    for (int i = _MIN(start, finish); i <= _MAX(start, finish); i++)
    {
        tempcurve = curve_lin(i);
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
        // ESP_LOGI(TAG, "Calculated index %i: Inc %lu, TLT %lu tc %lu", i,
        // temp_tick_inc, temp_tlt, tempcurve);
        // }
    }
    // ESP_LOGI(TAG, "Calculation took %llu us", esp_timer_get_time() -
    // starttime);
}

static int dali_addresses[DALI_CHANNELS];
static char dali_address_key_buffer[] = "dalix_address";
static uint8_t existing_dali_levels[DALI_CHANNELS];

static uint8_t already_off[] = {0, 0, 0, 0, 0, 0};

void get_dali_addresses()
{
    for (int i = 0; i < DALI_CHANNELS; i++)
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
    for (int i = 0; i < DALI_CHANNELS; i++)
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

uint8_t transmit_setlevel_dali_channel(dali_transceiver_handle_t transceiver,
                                       int channel_num,
                                       int level,
                                       bool force_send)
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
    ESP_LOGD(
        TAG, "(DALI %i) Sending DALI value %i, to %i", channel_num, address, level);
    if (address >= 0 && address <= 63)
    {
        dali_set_level_noblock(transceiver, address, level, pdMS_TO_TICKS(130));
        return level;
    }
    if (address >= 100 && address <= 115)
    {
        dali_set_level_group_noblock(
            transceiver, address - 100, level, pdMS_TO_TICKS(130));
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
        xTaskNotifyIndexed(status->mainloop_task,
                           NEW_SETPOINT_NOTIFY_IDX,
                           SETPOINT_SOURCE_RANDOM,
                           eSetValueWithOverwrite);
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
    dali_transceiver_config_t transceiver_config =
        dali_transceiver_sensible_default_config;
    transceiver_config.invert_input = DALI_DONT_INVERT;
    transceiver_config.invert_output = DALI_DONT_INVERT;
    transceiver_config.transmit_queue_size_frames = 1;
    transceiver_config.receive_queue_size_frames = 16;
    transceiver_config.enable_receiving = false;
    transceiver_config.receive_gpio_pin = RX_GPIO;
    transceiver_config.transmit_gpio_pin = TX_GPIO;
    transceiver_config.parser_config.forward_frame_action =
        DALI_PARSER_ACTION_LOG;
    transceiver_config.parser_config.mangled_frame_action =
        DALI_PARSER_ACTION_LOG_AND_RECORD;

    dali_transceiver_handle_t dali_transceiver;
    ESP_ERROR_CHECK(
        dali_setup_transceiver(transceiver_config, &dali_transceiver));
    networking_ctx->dali_command_queue =
        dali_setup_command_queue(dali_transceiver);
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
    esp_err_t getstate =
        esp_ota_get_state_partition(running, &ota_state) == ESP_OK;
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
                ESP_LOGI(TAG,
                         "Diagnostics completed successfully! Continuing "
                         "execution ...");
                esp_ota_mark_app_valid_cancel_rollback();
            }
            else
            {
                ESP_LOGE(TAG,
                         "Diagnostics failed! Start rollback to the previous "
                         "version ...");
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
        }
    }
}

level_t get_level_el(uint8_t level, device_status_t status, uint16_t full_power)
{
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

void initialise_status_struct()
{
    status.setpoint = startup_setpoint_lvl;
    status.fadetime_ms = USE_DEFAULT_FADETIME;
    status.setpoint_source = SETPOINT_SOURCE_INIT;
    status.power_on = 1;
    status.actual_level = startup_setpoint_lvl;
    for (int i = 0; i < DALI_CHANNELS; i++)
    {
        status.level_overrides.dali[i] = -1;
    }
    status.level_overrides.espnow = -1;
    status.level_overrides.zeroten1 = -1;
    status.level_overrides.zeroten2 = -1;
    status.stale_flag = 0;
}

void looptime_housekeeping()
{
    actual_looptime = (get_time_ms() - reftime);
    if (fade_remaining != 0 && actual_looptime > MINIMUM_TARGET_LOOPTIME)
    {
        if (actual_looptime >
                (tlt_array[status.actual_level] + LOOPTIME_TOLERANCE) ||
            actual_looptime <
                (tlt_array[status.actual_level] - LOOPTIME_TOLERANCE))
        {
            looptime_outside_tolerance_count += 1;
        }
        if (looptime_outside_tolerance_count >=
            LOOPTIMES_OUT_OF_TOLERANCE_NEEDED)
        {
            // calc_tickinc_and_looptime(actual_looptime, status.setpoint -
            // start_of_fade_level);
            calc_fade_increments(
                actual_looptime, start_of_fade_level, status.setpoint);

            looptime_outside_tolerance_count = 0;
        }
    }
}

void process_new_setpoint()
{
    old_status = status;
    status.setpoint = clamp(status.setpoint, 0, 254);

    // signal new setpoint
    gpio_set_level(LED1_GPIO, 1);
    start_of_fade_level = status.actual_level;
    full_power = clamp(get_setting("full_power"), 0, 512);
    max_idle_reawake_interval =
        _uMAX(((uint32_t)get_setting("idle_intvl_ms")), 200UL);
    cooldown_duration =
        _uMAX(((uint32_t)get_setting("idle_cooldown")), 0UL);
    new_setpoint = true;
    // ESP_LOGI(TAG, "Received new setpoint: %d, fade: %lu source
    // %s", status.setpoint, status.fadetime_ms,
    // source_str[status.setpoint_source]);
    random_looptime = (rand() & 127);
    tick_inc = 1;
    configbits = get_setting("configbits");
    switch (status.fadetime_ms)
    {
    case USE_DEFAULT_FADETIME:
        fadetime = get_setting("default_fade");
        break;
    case USE_SLOW_FADETIME:
        fadetime = get_setting("slow_fade");
        break;
    default:
        fadetime = status.fadetime_ms;
        break;
    }
    if (fadetime < 8)
        // it's basically no fade lets avoid any divide by zeros
        fadetime = 8;

    if (status.setpoint != status.actual_level)
    {
        calc_fade_increments(estimate_looptime(), start_of_fade_level, status.setpoint);
        looptime_outside_tolerance_count = 0;
    }
    else
    {
        idle_start_time = current_time;
    }
}

void lookahead_for_reawake()
{
    uint32_t time_acc = 0;
    int lvl = status.actual_level;
    int loops = 0;
    while (lvl != status.setpoint && status.fadetime_ms > 0)
    {
        loops += 1;
        if (loops > 255)
        {
            ESP_LOGE(TAG,
                     "Something went wrong in fade search loop. Loop "
                     "count timeout.");
            // zero array
            memcpy(min_level_array, &status.lut[0], sizeof(level_t));
            break;
        }

        // get test level
        int next_lvl_unclamped = lvl + tick_sign * (int)tick_inc_array[lvl];
        lvl = flexclamp(next_lvl_unclamped, status.actual_level, status.setpoint);

        if (loops < 2)
        {
            // we need to advance one level regardless of timing
            continue;
        }

        ESP_LOGD(TAG,
                 "Looking at lvl %d. Adding %lu to acc (%lu). Ticking %d",
                 lvl,
                 tlt_array[lvl],
                 time_acc,
                 tick_inc_array[lvl]);

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

        // check to see if we've gone far enough ahead
        time_acc += tlt_array[lvl];
        if (time_acc > BALLAST_WAKE_LOOKAHEAD_MS)
            break;
    }
}

void calculate_reawake_intervals()
{
    if ((reftime - idle_start_time) < cooldown_duration)
    {
        idle_reawake_interval = _uMIN(IDLE_UPDATE_INTERVAL_DURING_COOLDOWN_MS,
                                      max_idle_reawake_interval);
    }
    else
    {
        idle_reawake_interval =
            _uMIN(MAX_IDLE_UPDATE_INTERVAL_AFTER_COOLDOWN_MS,
                  max_idle_reawake_interval);
    }

    reawake_time = reftime + (uint64_t)idle_reawake_interval;
    ESP_LOGD(TAG,
             "Idle reawake %lu reftime %lu idle_start %lu cd %lu",
             idle_reawake_interval,
             reftime,
             idle_start_time,
             cooldown_duration);

    if (configbits & CONFIGBIT_RECEIVE_ESPNOW)
    {
        // we don't want double awakening if we're receiving regular
        // updates so we add a bit extra to accommodate network latency
        reawake_time += IDLE_UPDATE_INTERVAL_ADDITION_US_RECV_ESPNOW;
    }
}

void maybe_log_heap_info()
{
    if (!(idlecount & 0x3F))
    {
        // log RAM situation every 64 idles
        ESP_LOGI(TAG,
                 "Min free heap %i",
                 heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
        ESP_LOGI(TAG, "Free heap %i", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    }
    if (!(idlecount & 0xFF))
    {
        // log tasks every 256 idles
        list_tasks();
    }
}

void manage_zeroten_channel_sends()
{

    if (configbits & CONFIGBIT_USE_0_10v1)
    {
        int16_t override = status.level_overrides.zeroten1;
        zeroten1_lvl_to_send =
            (override == -1) ? current_level_el.zeroten1_lvl : override;
        if (!status.power_on)
        {
            zeroten1_lvl_to_send = 0;
        }
        set_0_10v_level(pwm1, zeroten1_lvl_to_send);

        if (configbits & CONFIGBIT_USE_0_10v2)
        {
            int16_t override = status.level_overrides.zeroten2;
            zeroten2_lvl_to_send =
                (override == -1) ? current_level_el.zeroten2_lvl : override;
            if (!status.power_on)
            {
                zeroten2_lvl_to_send = 0;
            }
            set_0_10v_level(pwm2, zeroten2_lvl_to_send);
        }
        else
        {
            ESP_ERROR_CHECK(set_zero_duty_pwm_channel(pwm2));
        }
    }
    else
    {
        // we can't use channel 2 without channel 1 so stop both
        ESP_ERROR_CHECK(set_zero_duty_pwm_channel(pwm1));
        ESP_ERROR_CHECK(set_zero_duty_pwm_channel(pwm2));
    }
}

void manage_espnow_sends()
{
    if (espnowtask != NULL && (configbits & CONFIGBIT_TRANSMIT_ESPNOW))
    {
        espnow_lvl_to_send = (status.level_overrides.espnow == -1)
                                 ? current_level_el.espnow_lvl
                                 : status.level_overrides.espnow;
        if (!status.power_on)
            espnow_lvl_to_send = 0;

        xTaskNotifyIndexed(espnowtask,
                           LIGHT_LEVEL_NOTIFY_INDEX,
                           espnow_lvl_to_send,
                           eSetValueWithOverwrite);
    }
}

void manage_dali_sends()
{
    // if we're using DALI use mutex to make sure we dont interrupt any
    // dali config commands from the REST API
    if ((configbits & CONFIGBIT_USE_DALI) && dali_take_mutex(dali_transceiver, 0))
    {
        // check for any DALI broadcast commands -> we don't need to bother
        // with short addresses
        dali_broadcast = -1;
        for (int i = 0; i < DALI_CHANNELS; i++)
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
            // do single broadcast
            dali_levels_to_send[dali_broadcast] =
                transmit_setlevel_dali_channel(dali_transceiver,
                                               dali_broadcast,
                                               current_level_el.dali_lvl[dali_broadcast],
                                               force_resend);
        }
        else
        {
            // send each channel separately
            for (int i = 0; i < DALI_CHANNELS; i++)
            {
                dali_levels_to_send[i] = transmit_setlevel_dali_channel(
                    dali_transceiver, i, current_level_el.dali_lvl[i], force_resend);
            }
        }
        dali_give_mutex(dali_transceiver);
    }
}

void log_current_status()
{
    if (xSemaphoreTake(logstring_mutex, pdMS_TO_TICKS(10)))
    {
        levellog_count += 1;

        looplog[0] = 0;
        looplog_headers[0] = 0;
        strcat(looplog_headers, "Lvl-> SP PWR | SRCE N |");
        sprintf(looplog,
                "%3.1u->%3.1d %s | %s %1.i |",
                status.actual_level,
                status.setpoint,
                (status.power_on) ? " ON" : "OFF",
                source_str[status.setpoint_source & 0xF],
                (int)new_setpoint);
        if (overrides_active(&status))
        {
            strcat(looplog_headers, " OVRIDE |");
            strcat(looplog, " ACTIVE |");
        }

        // strcat 0-10v data
        if (configbits & CONFIGBIT_USE_0_10v1)
        {
            snprintf(looplog_template, 8, "  %3.1i", zeroten1_lvl_to_send);
            strcat(looplog, looplog_template);
            strcat(looplog_headers, " 10v1");
            if (configbits & CONFIGBIT_USE_0_10v2)
            {
                snprintf(looplog_template, 8, "  %3.1i", zeroten2_lvl_to_send);
                strcat(looplog, looplog_template);
                strcat(looplog_headers, " 10v2");
            }
        }

        // strcat DALI data
        for (int i = 0; i < DALI_CHANNELS; i++)
        {
            if ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[i] != -1)
            {
                snprintf(looplog_template, 8, " %3.1i", dali_levels_to_send[i]);
                strcat(looplog, looplog_template);
                strcpy(looplog_template, " DAX");
                looplog_template[3] = 'A' + i;
                strcat(looplog_headers, looplog_template);
            }
        }
        if (configbits & CONFIGBIT_TRANSMIT_ESPNOW)
        {
            strcat(looplog_headers, " ESPN");
            snprintf(looplog_template, 8, "  %3.1i", espnow_lvl_to_send);
            strcat(looplog, looplog_template);
        }

        if (configbits & CONFIGBIT_USE_RELAY1)
        {
            strcat(looplog_headers, " RY1");
            snprintf(looplog_template, 8, " %3.1i", relay1_lvl_to_send);
            strcat(looplog, looplog_template);
        }
        if (configbits & CONFIGBIT_USE_RELAY2)
        {
            strcat(looplog_headers, " RY2");
            snprintf(looplog_template, 8, " %3.1i", relay2_lvl_to_send);
            strcat(looplog, looplog_template);
        }
        char *powerfmt;
        if (current_level_el.power < 10.0)
        {
            powerfmt = " | %5.3f";
        }
        else if (current_level_el.power < 100.0)
        {
            powerfmt = " | %5.2f";
        }
        else
        {
            powerfmt = " | %5.1f";
        }
        snprintf(looplog_template, 255, powerfmt, current_level_el.power);
        strcat(looplog, looplog_template);
        char *fadefmt;
        if (fadetime < 1000000)
        {
            fadefmt = " %5.1f";
        }
        else
        {
            fadefmt = " %5.0f";
        }
        snprintf(looplog_template, 255, fadefmt, ((double)fadetime) / 1000.0);
        strcat(looplog, looplog_template);

        snprintf(looplog_template,
                 255,
                 " %6.1lu %4.1i  %3.1lu",
                 tlt_array[status.actual_level],
                 ((int)actual_looptime),
                 idle_reawake_interval >> 10);
        strcat(looplog, looplog_template);
        strcat(looplog_headers, " |   Pwr  Fade    TLT   LT Wake");
        xSemaphoreGive(logstring_mutex);
    }

    if (!(levellog_count & 7))
    {
        // log headers every 8 lines
        ESP_LOGI(TAG, "%s", looplog_headers);
    }
    ESP_LOGI(TAG, "%s", looplog);
}

void app_main(void)
{
    reset_reason = esp_reset_reason();
    configure_gpio();
    bool stale = (status.stale_flag == STATUS_STALE_FLAG);
    bool reinit =
        stale || ((reset_reason != ESP_RST_DEEPSLEEP) && (reset_reason != ESP_RST_SW));
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
    // xTaskCreate(random_setpointer_task, "random setpointer task", 2048,
    // (void*) &status, 7, &randomtask);

    uint8_t dip_address = read_dip_switches();
    ESP_LOGI(TAG, "Read DIP Switch address: %d", dip_address);

    setup_relays(get_setting("configbits"));
    startup_setpoint_lvl = get_setting("startup_level");

    status.mainloop_task = xTaskGetCurrentTaskHandle();

    if (reinit)
    {
        if (stale)
        {
            ESP_LOGI(TAG, "Status marked as stale - re-initialising");
        }
        else
        {
            ESP_LOGI(TAG,
                     "Fresh start up detected, loading startup level %i from NVS",
                     startup_setpoint_lvl);
        }
        initialise_status_struct();
    }
    else
    {
        ESP_LOGI(TAG,
                 "Software restart detected - no stale flag - keeping previous "
                 "setpoint %d",
                 status.setpoint);
    }

    setup_rgb_led(&status);
    xTaskNotifyIndexed(xTaskGetCurrentTaskHandle(),
                       NEW_SETPOINT_NOTIFY_IDX,
                       SETPOINT_SOURCE_INIT,
                       eSetValueWithOverwrite);

    logstring_mutex = xSemaphoreCreateMutex();

    networking_ctx.mainloop_task = xTaskGetCurrentTaskHandle();
    networking_ctx.dali_command_queue = NULL;
    networking_ctx.status = &status;
    networking_ctx.logstring_mutex = logstring_mutex;
    networking_ctx.logstring = looplog;
    networking_ctx.logheader = looplog_headers;
    xTaskCreate(setup_networking,
                "setup_networking",
                4096,
                (void *)&networking_ctx,
                2,
                &networktask);

    // If we don't have Wifi provisioned we can't start as we'll run out of heap unless the BT stack is released
    xTaskNotifyWaitIndexed(PROVISIONING_DONE_INDEX, 0, 0, NULL, portMAX_DELAY);

    ESP_ERROR_CHECK(setup_0_10v_channel(
        PWM_010v_GPIO, get_setting_indexed("startup_cal", 1), &pwm1));
    ESP_ERROR_CHECK(setup_0_10v_channel(
        PWM_010v2_GPIO, get_setting_indexed("startup_cal", 2), &pwm2));

    ESP_ERROR_CHECK(setup_button_interrupts(&status, pwm1, pwm2));

    dali_transceiver = setup_dali(&networking_ctx);

    // light_adc_config_t adcconfig = {
    //     .notify_task = xTaskGetCurrentTaskHandle(),
    //     .tolerance = 11,
    //     .sampled_needed = 3,
    //     .average_window = 4,
    //     .status = &status};
    // int resistor_level;
    // setup_adc(adcconfig);

    ESP_LOGI(TAG, "Preparing main loop");
    reftime = get_time_ms();
    ESP_LOGI(TAG, "Getting settings from NVS...");

    default_fadetime = get_setting("default_fade");
    configbits = get_setting("configbits");

    local_setpoint = status.setpoint;

    full_power = get_setting("full_power");
    old_status = status;
    cooldown_duration = get_setting("idle_cooldown");
    idle_start_time = reftime;

    ESP_LOGI(TAG, "Getting DALI addresses");
    get_dali_addresses();

    list_tasks();

    ESP_LOGI(TAG, "Starting main loop...");
    while (1)
    {
        //
        // do loop timing housekeeping
        //
        looptime_housekeeping();

        while (1)
        {
            received = xTaskNotifyWaitIndexed(NEW_SETPOINT_NOTIFY_IDX, 0, 0, &recv_value, 1);
            current_time = get_time_ms();
            force_resend = received;
            if (received == pdTRUE)
            {
                process_new_setpoint();
                break;
            };
            if (current_time > reawake_time)
                break;
        };

        reftime = get_time_ms();

        // reset lookahead array to be paranoid
        memcpy(min_level_array, &status.lut[0], sizeof(level_t));

        fade_remaining = status.setpoint - status.actual_level;
        if (fade_remaining != 0)
        {
            // process fade tick
            //

            tick_sign = fade_remaining > 0 ? 1 : -1;
            lookahead_for_reawake();

            // prepare next loop
            reawake_time = reftime + tlt_array[status.actual_level];
            idle_sends = 0;
            idle_start_time = reftime;

            // tick increment
            int act = status.actual_level;
            int inc = (int)tick_inc_array[status.actual_level] * tick_sign;

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
            calculate_reawake_intervals();

            memcpy(min_level_array, &status.lut[0], sizeof(level_t)); // zero it out
            get_dali_addresses();
            full_power = clamp(get_setting("full_power"), 0, 512);

            idlecount += 1;
            idle_sends += 1;
            maybe_log_heap_info();
        }

        current_level_el = get_level_el(status.actual_level, status, full_power);

        // ensure minimum levels
        memcpy(level_el_array, &current_level_el, sizeof(level_t));
        for (int ch = 0; ch < sizeof(level_t); ch++)
        {
            if (level_el_array[ch] < min_level_array[ch])
                level_el_array[ch] = min_level_array[ch];
        }
        memcpy(&current_level_el, &level_el_array, sizeof(level_t));

        // RELAYS
        relay1_lvl_to_send = status.power_on ? current_level_el.relay1 : 0;
        relay2_lvl_to_send = status.power_on ? current_level_el.relay2 : 0;
        manage_relay_timeouts(configbits, relay1_lvl_to_send, relay2_lvl_to_send);

        manage_espnow_sends();

        manage_zeroten_channel_sends();

        manage_dali_sends();

        log_current_status();

        new_setpoint = false;
    }
}
