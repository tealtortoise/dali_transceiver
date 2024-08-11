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

#define MIN_EST_LOOPTIME_US 25000

#define LOOPTIME_TOLERANCE 10000
#define LOOPTIMES_OUT_OF_TOLERANCE_NEEDED 2

#define IDLE_UPDATE_INTERVAL_US (5 * 1000000)
#define IDLE_UPDATE_INTERVAL_US_RECV_ESPNOW (IDLE_UPDATE_INTERVAL_US + 500000)

#define HASH_LEN 32 /* SHA-256 digest length */

static const char *TAG = "main";

static const char* source_str[] = {"REST", " ADC", "ESPN", "BUTN", "ALRM", "RAND", "INIT"};

nvs_handle_t mainloop_nvs_handle;

void setup_networking(void *params)
{
    networking_ctx_t *ctx = (networking_ctx_t *)params;
    setup_wifi(ctx);
    setup_sntp(ctx->mainloop_task);
    httpd_handle_t httpd = setup_httpserver(ctx);
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
static uint64_t target_looptime = 0;
static uint64_t ideal_time_between_levels_us = 0;
static uint64_t calcreftime;

int fadetime;

void calc_tickinc_and_looptime(uint64_t looptime, int dif)
{
    if (fadetime <= 50){
        tick_inc = 254;
        target_looptime = 0;
        return;
    }
    calcreftime = esp_timer_get_time();
    ideal_time_between_levels_us = ((uint64_t) fadetime) << 10;
    tick_inc = 1;
    uint64_t intermediate = ideal_time_between_levels_us / abs(dif);
    if (intermediate > 180000000LL) intermediate = 180000000LL;
    while (tick_inc < 254) 
    {
        target_looptime = intermediate * (uint64_t)tick_inc;
        // fudge the numbers a bit for smoother fades at expense of accuracy
        if (tick_inc == 1 && (target_looptime * 1.5) >= looptime) break;
        if (tick_inc == 2 && (target_looptime * 1.3) >= looptime) break;
        if (tick_inc == 3 && (target_looptime * 1.15) >= looptime) break;
        
        if (target_looptime >= looptime) break;
        tick_inc += 1;
    }

    // ESP_LOGI(TAG, "calc: looptime %llu, tick_inc %i, %i ()", looptime, tick_inc, dif);
    ESP_LOGD(TAG, "Calc time %llu us", esp_timer_get_time() - calcreftime);
}


static int dali_addresses[6];
static level_t min_level;
static char dali_address_key_buffer[] = "dalix_address";
static uint8_t existing_dali_levels[6];

static level_overrides_t level_overrides = {
    .dali = {-1, -1, -1, -1, -1, -1},
    .zeroten1 = -1,
    .zeroten2 = -1,
    .espnow = -1
};
static uint8_t already_off[] = {0, 0, 0, 0, 0, 0};

void get_dali_addresses() 
{
    for (int i = 0; i<6; i++)
    {
        dali_address_key_buffer[4] = 'a' + (char) i;
        dali_addresses[i] = get_setting(dali_address_key_buffer);
    }
}

uint8_t transmit_setlevel_dali_channel(dali_transceiver_handle_t transceiver, int channel_num, int level, bool force_send)
{
    int address = dali_addresses[channel_num];
    int override = level_overrides.dali[channel_num];
    if (override != -1) level = level_overrides.dali[channel_num];
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

static char templogbuffer[1];
static SemaphoreHandle_t printf_mutex;

int buffer_vprint(const char *format, va_list args)
{
    int strsize;
    int result;
    strsize = vsnprintf(templogbuffer, 0, format, args);
    if (1)
    {
        char* tempbuf = malloc(strsize + 2);
        vsnprintf(tempbuf, strsize+1, format, args);
        if (tempbuf[strsize -1] != '\n'){
            tempbuf[strsize-1] = '\n';
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
    TaskHandle_t mainloop_task = params;
    int mult = (rand() & 0xF);
    uint8_t setpoint;
    while (1)
    {
        mult = (rand() & 0xF);
        setpoint = (rand() & 0xFF);

        setpoint_notify_t setp = {
            .fadetime_256ms = ((rand() & 0xFF) * mult * mult) >> 8,
            .setpoint = setpoint,
            .setpoint_source = SETPOINT_SOURCE_RANDOM,
        };
        uint32_t setpoint_struct_as_int = *((uint32_t*) &setp);
        xTaskNotifyIndexed(mainloop_task, NEW_SETPOINT_NOTIFY_IDX, setpoint_struct_as_int, eSetValueWithOverwrite);
        vTaskDelay(((rand() & 0x7) * mult * mult) + 1);
    }
}

// static bool diagnostic(void)
// {
//     ESP_LOGI(TAG, "Diagnostics (3 sec)...");
//     vTaskDelay(1000 / portTICK_PERIOD_MS);
//     return get_and_log_buttons() != 4;       ;
// }

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

void check_partitions(){
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
        ESP_LOGI(TAG, "Ota state is %i",ota_state);
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
        {
            // run diagnostic function ...
            bool diagnostic_is_ok = true; //diagnostic();
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

static RTC_NOINIT_ATTR setpoint_notify_t received_setpoint;

void app_main(void)
{
    configure_gpio();
    initialise_logbuffer();
    printf_mutex = xSemaphoreCreateMutex();
    esp_log_set_vprintf(buffer_vprint);
    ESP_LOGI(TAG, "Checking partitions...");
    check_partitions();
    // sprintf(logbuffer, "This is a log");
    ESP_LOGI(TAG, "Seeding random...");
    srand(time(NULL));
    vTaskPrioritySet(NULL, 6);
    // rotate_gpio_outputs();
    ESP_LOGI(TAG, "Setting up SPIFFS and NVS...");
    setup_nvs_spiffs_settings();
    ESP_LOGI(TAG, "Reading LUTs...");
    read_level_luts(levellut);
    TaskHandle_t randomtask;
    // xTaskCreate(random_setpointer_task, "random setpointer task", 2048, (void*) xTaskGetCurrentTaskHandle(), 7, &randomtask);

    uint8_t dip_address = read_dip_switches();
    ESP_LOGI(TAG, "Read DIP Switch address: %d", dip_address);

    setup_relays(get_setting("configbits"));
    TaskHandle_t uarttask;
    // xTaskCreate(uart_log_task, "uart loopback", 4096, NULL, 1, &uarttask);

    int startup_setpoint_lvl = get_setting("startup_level");
    
    esp_reset_reason_t reason = esp_reset_reason();
    if ((reason != ESP_RST_DEEPSLEEP) && (reason != ESP_RST_SW)) {
        ESP_LOGI(TAG, "Fresh start up detected, loading startup level %i from NVS", startup_setpoint_lvl);
        received_setpoint.setpoint = startup_setpoint_lvl;
        received_setpoint.fadetime_256ms = USE_DEFAULT_FADETIME;
        received_setpoint.setpoint_source = SETPOINT_SOURCE_INIT;
    }
    else
    {
        ESP_LOGI(TAG, "Software restart detected - keeping previous setpoint %d", received_setpoint.setpoint);
    }

    uint32_t setpoint_struct_as_int = *((uint32_t*) &received_setpoint);
    xTaskNotifyIndexed(xTaskGetCurrentTaskHandle(), NEW_SETPOINT_NOTIFY_IDX, setpoint_struct_as_int, eSetValueWithOverwrite);
    networking_ctx_t networking_ctx = {
        .mainloop_task = xTaskGetCurrentTaskHandle(),
        .dali_command_queue = NULL,
        .setpoint_struct = &received_setpoint,
        .level_overrides = &level_overrides,
    };
    TaskHandle_t networktask;
    xTaskCreate(setup_networking, "setup_networking", 4096, (void *)&networking_ctx, 2, &networktask);
    
    zeroten_handle_t pwm1;
    zeroten_handle_t pwm2;
    // ESP_ERROR_CHECK(setup_0_10v_channel(LED1_GPIO, CALIBRATION_PWM_LOG, &pwm1));
    ESP_ERROR_CHECK(setup_0_10v_channel(PWM_010v_GPIO, CALIBRATION_LOOKUP_NVS, &pwm1));
    ESP_ERROR_CHECK(setup_0_10v_channel(PWM_010v2_GPIO, CALIBRATION_LOOKUP_NVS, &pwm2));

    setup_button_interrupts(xTaskGetCurrentTaskHandle(), pwm1, pwm2);

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
    networking_ctx.dali_command_queue = dali_setup_command_queue(dali_transceiver);
    // dali_set_system_failure_level(dali_transceiver, 8, 10);
    // dali_set_system_failure_level(dali_transceiver, 9, 10);
    dali_broadcast_level_noblock(dali_transceiver, 0);

    light_adc_config_t adcconfig = {
        .notify_task = xTaskGetCurrentTaskHandle(),
        .tolerance = 10,
        .sampled_needed = 3,
        .average_window = 4,
        .current_setpoint = &received_setpoint
    };
    int resistor_level;
    // setup_adc(adcconfig);

    BaseType_t received;
    esp_err_t sent;
    int firsttime = 1;
    // actual_level = setpoint;
    // vTaskDelay(50);
    ESP_LOGI(TAG, "Preparing main loop");
    int local_setpoint_struct_as_int;
    int64_t reftime = esp_timer_get_time() - MIN_EST_LOOPTIME_US;
    uint64_t reawake_time = 0;
    ESP_LOGI(TAG, "Getting settings from NVS...");
    int configbits = get_setting("configbits");

    int default_fadetime = get_setting("default_fade");
    int looptime = MIN_EST_LOOPTIME_US;

    int updates_performed = 0;
    int fade_remaining = 0;

    bool at_setpoint;
    uint16_t actual_level = received_setpoint.setpoint;
    level_t level_el;
    int dali_broadcast;
    level_t future_el;
    uint64_t current_time;
    uint64_t actual_looptime;
    int random_looptime = 1;
    uint8_t local_setpoint = received_setpoint.setpoint;

    ESP_LOGI(TAG, "Getting DALI addresses");
    get_dali_addresses();
    uint16_t full_power = get_setting("full_power");
    int zeroten1_lvl_to_send = 0;
    int zeroten2_lvl_to_send = 0;
    int dali_levels_to_send[6];
    for (int i = 0; i< 6; i++)
    {
        dali_levels_to_send[i] = 0;
    }
    int espnow_lvl_to_send = 0;
    int lookahead;
    int idlecount = 0;
    int levellog_count = 0;
    bool force_resend = false;
    setpoint_notify_t old_setpoint = {
        .setpoint = received_setpoint.setpoint,
        .fadetime_256ms = USE_DEFAULT_FADETIME,
        .setpoint_source = SETPOINT_SOURCE_INIT
    };
    int max_lookahead;
    bool new_setpoint;
    int minlevelbits = 0;
    int future_level;
    uint8_t start_of_fade_level = 0;
    int looptime_outside_tolerance_count = 0;
    // level_t lev_el;
    
    // for (int i=0; i< 5; i++){
    //     lev_el = levellut[i];
    //     ESP_LOGI(TAG, "Level %i: %d, %d, %d", i, lev_el.zeroten1_lvl, lev_el.zeroten2_lvl, lev_el.dali1_lvl);
    // }
    level_t subslevel = {
        .zeroten1_lvl = 0,
        .zeroten2_lvl = 0,
        .dali_lvl = {0, 0, 0, 0, 0, 0},
        .espnow_lvl = 0,
        .relay1 = 0,
        .relay2 = 0,
    };
    ESP_LOGI(TAG, "Starting main loop...");
    while (1)
    {
        actual_looptime = (esp_timer_get_time() - reftime);
        if (actual_looptime > (target_looptime + LOOPTIME_TOLERANCE) || actual_looptime < (target_looptime - LOOPTIME_TOLERANCE))
        {
            looptime_outside_tolerance_count += 1;
        }
        if (looptime_outside_tolerance_count >= LOOPTIMES_OUT_OF_TOLERANCE_NEEDED)
        {
            calc_tickinc_and_looptime(actual_looptime, received_setpoint.setpoint - start_of_fade_level);
            looptime_outside_tolerance_count = 0;
        }
        while (1)
        {
            received = xTaskNotifyWaitIndexed(NEW_SETPOINT_NOTIFY_IDX, 0, 0, &local_setpoint_struct_as_int, 1);
            current_time = esp_timer_get_time();
            force_resend = received;
            if (received == pdTRUE)
            { 
                old_setpoint = received_setpoint;
                received_setpoint = *((setpoint_notify_t*) &local_setpoint_struct_as_int);
                if (received_setpoint.setpoint_source & 0x80)
                {
                    // relative setpoint
                    ESP_LOGI(TAG, "Received relative setpoint (%i)", (int)received_setpoint.setpoint);
                    received_setpoint.setpoint = clamp(old_setpoint.setpoint + (int8_t) received_setpoint.setpoint, 0, 254);
                }
                else
                {
                    received_setpoint.setpoint = clamp(received_setpoint.setpoint, 0, 254);
                }
                
                gpio_set_level(LED1_GPIO, 1);
                start_of_fade_level = actual_level;
                full_power = clamp(get_setting("full_power"), 0, 512);
                new_setpoint = true;
                // ESP_LOGI(TAG, "Received new setpoint: %d, fade: %i source %s", received_setpoint.setpoint, received_setpoint.fadetime_256ms, source_str[received_setpoint.setpoint_source]);
                random_looptime = (rand() & 127);
                tick_inc = 1;
                configbits = get_setting("configbits");
                if (received_setpoint.fadetime_256ms == USE_DEFAULT_FADETIME)
                {
                    fadetime = get_setting("default_fade");
                }
                else if (received_setpoint.fadetime_256ms == USE_SLOW_FADETIME)
                {
                    fadetime = get_setting("slow_fade");
                }
                else
                {
                    fadetime = received_setpoint.fadetime_256ms << 8;
                }
                if (fadetime < 8)
                    fadetime = 8;
                min_level = levellut[0];

                // make sure output needed later in fade are woken up
                for (future_level = actual_level; future_level != received_setpoint.setpoint; future_level += (received_setpoint.setpoint > actual_level) ? 1 : -1)
                {
                    future_el = levellut[future_level];
                    for (int ch=0; ch < sizeof(level_t); ch ++)
                    {
                        if (*(((uint8_t*) &future_el) + ch) > 0)
                        {
                            *(((uint8_t*) &min_level) + ch) = 1;
                        }
                    }
                }
                // for (int ch=0; ch < sizeof(level_t); ch ++)
                // {
                //     ESP_LOGI(TAG, "Ch %i min level %i",ch, *(((uint8_t*) &min_level) + ch));;
                // }
                if (received_setpoint.setpoint != actual_level)
                {
                    calc_tickinc_and_looptime(MIN_EST_LOOPTIME_US, received_setpoint.setpoint - start_of_fade_level);
                    looptime_outside_tolerance_count = 0;
                }
                break;
            };
            if (current_time > reawake_time)
                break;
        };
        
        reftime = esp_timer_get_time();
        fade_remaining = received_setpoint.setpoint - actual_level;
        if (fade_remaining < 0)
        {
            actual_level = _MAX(actual_level - tick_inc, received_setpoint.setpoint);
            reawake_time = reftime + target_looptime;
        }
        else if (fade_remaining > 0)
        {
            actual_level = _MIN(actual_level + tick_inc, received_setpoint.setpoint);
            reawake_time = reftime + target_looptime;
        }
        else
        {
            // we're at setpoint
            gpio_set_level(LED1_GPIO, 0);
            // reawake_time = reftime + IDLE_UPDATE_INTERVAL_US;
            force_resend = true;
            configbits = get_setting("configbits");
            if (configbits & CONFIGBIT_RECEIVE_ESPNOW)
            {
                // we don't want double awakening if we're receiving regular updates
                reawake_time = reftime + IDLE_UPDATE_INTERVAL_US_RECV_ESPNOW;
            }
            else
            {
                reawake_time = reftime + IDLE_UPDATE_INTERVAL_US;
            }
            idlecount += 1;
            min_level = levellut[0];
            get_dali_addresses();

            full_power = clamp(get_setting("full_power"), 0, 512);
            
            if (!(idlecount & 0xF))
            {
                ESP_LOGI(TAG, "Min free heap %i", heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
                ESP_LOGI(TAG, "Free heap %i", heap_caps_get_free_size(MALLOC_CAP_8BIT));
            }
            // list_tasks();
        }
        
        
        fade_remaining = received_setpoint.setpoint - actual_level;
        level_el = levellut[clamp((int)actual_level + (int)full_power - 254, 0, 254)];
        
        // ESP_LOGI(TAG, "lvl %i, DALIA %d, ovverride %i", clamp((int)actual_level + (int)full_power - 254, 0, 254), level_el.dali_lvl[0], level_overrides.dali[0]);

        for (int ch=0; ch < sizeof(level_t); ch ++)
        {
            if (*((uint8_t*) &level_el + ch) < *((uint8_t*) &min_level + ch))
            {
                *((uint8_t*) &level_el + ch) = *((uint8_t*) &min_level + ch);
            }
        }
        manage_relay_timeouts(configbits, level_el.relay1, level_el.relay2);

        // ESP_LOGI(TAG, "lvl %i, DALIA %d, ovverride %i, min_level %i", clamp((int)actual_level + (int)full_power - 254, 0, 254), level_el.dali_lvl[0], level_overrides.dali[0], min_level.dali_lvl[0]);

        if (espnowtask != NULL && (configbits & CONFIGBIT_TRANSMIT_ESPNOW))
        {
            if (level_overrides.espnow != -1)
            {
                espnow_lvl_to_send = level_overrides.espnow;
            }
            else
            {
                espnow_lvl_to_send = level_el.espnow_lvl;
            }
            xTaskNotifyIndexed(espnowtask,
                               LIGHT_LEVEL_NOTIFY_INDEX,
                               espnow_lvl_to_send,
                               eSetValueWithOverwrite);
        }

        if (configbits & CONFIGBIT_USE_0_10v1)
        {
            if (level_overrides.zeroten1 != -1)
            {
                zeroten1_lvl_to_send = level_overrides.zeroten1;
            }
            else
            {
                zeroten1_lvl_to_send = level_el.zeroten1_lvl;
            }
            set_0_10v_level(pwm1, zeroten1_lvl_to_send);
        }
        else
        {
            set_0_10v_level(pwm1, 0);
            set_0_10v_level(pwm2, 0);
        }
        if (configbits & CONFIGBIT_USE_0_10v2)
        {
            if (level_overrides.zeroten2 != -1)
            {
                zeroten2_lvl_to_send = level_overrides.zeroten2;
            }
            else
            {
                zeroten2_lvl_to_send = level_el.zeroten2_lvl;
            }
            set_0_10v_level(pwm2, zeroten2_lvl_to_send);
        }
        
        bool dali_bus_idle = true;
        for (int i = 0; i < 6; i++)
        {
            if (level_el.dali_lvl[i] > 0 || !already_off[i] || level_overrides.dali[i] > 0)
            {
                dali_bus_idle = false;
            }
        }
        if (!dali_bus_idle && (configbits & CONFIGBIT_USE_DALI) && dali_take_mutex(dali_transceiver, 0))
        {

            dali_broadcast = 0;
            for (int i = 0; i < 6; i++)
            {
                ESP_LOGD(TAG, "DALI Channel %i address is %i", i, dali_addresses[i]);
                if (dali_addresses[i] == 200)
                {
                    dali_broadcast = i;
                    break;
                }
            }
            if (dali_broadcast)
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

        if (!(levellog_count & 7))
        {
            
            ESP_LOGI(TAG, "Lvl-> SP |  SRC N | 0-10v1 0-10v2 DALIA DALIB DALIC DALID DALIE DALIF ESPN Rly1 Rly2    Fade    TLT  LT");
            ESP_LOGI(TAG, "                        %s     %s    %s    %s    %s    %s    %s    %s   %s   %s   %s",
                    (configbits & CONFIGBIT_USE_0_10v1) ? "ON" : " .",
                    (configbits & CONFIGBIT_USE_0_10v2) ? "ON" : " .",
                    ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[0] != -1) ? "ON" : " .",
                    ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[1] != -1) ? "ON" : " .",
                    ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[2] != -1) ? "ON" : " .",
                    ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[3] != -1) ? "ON" : " .",
                    ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[4] != -1) ? "ON" : " .",
                    ((configbits & CONFIGBIT_USE_DALI) && dali_addresses[5] != -1) ? "ON" : " .",
                    (configbits & CONFIGBIT_TRANSMIT_ESPNOW) ? "ON" : " .",
                    (configbits & CONFIGBIT_USE_RELAY1) ? "ON" : " .",
                    (configbits & CONFIGBIT_USE_RELAY2) ? "ON" : " .");
        }
        
        levellog_count += 1;
        ESP_LOGI(TAG,   "%3.1u->%3.1d | %s %1.i |    %3.1i    %3.1i   %3.1d   %3.1d   %3.1d   %3.1d   %3.1d   %3.1d  %3.1i  %3.1d  %3.1d %7.1i %6.1llu %3.1i",
                actual_level,
                received_setpoint.setpoint,
                source_str[received_setpoint.setpoint_source & 0xF],
                (int) new_setpoint,
                zeroten1_lvl_to_send,
                zeroten2_lvl_to_send,
                dali_levels_to_send[0],
                dali_levels_to_send[1],
                dali_levels_to_send[2],
                dali_levels_to_send[3],
                dali_levels_to_send[4],
                dali_levels_to_send[5],
                espnow_lvl_to_send,
                level_el.relay1,
                level_el.relay2,
                fadetime,
                target_looptime >> 10,
                ((int) actual_looptime) >> 10);
                
        new_setpoint = false;
    }
}
