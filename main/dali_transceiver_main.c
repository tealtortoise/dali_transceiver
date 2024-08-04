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

#define LOOPTIME_TOLERANCE 2000

#define IDLE_UPDATE_INTERVAL_US (1 * 1000000)
#define IDLE_UPDATE_INTERVAL_US_RECV_ESPNOW (IDLE_UPDATE_INTERVAL_US + 500000)

#define HASH_LEN 32 /* SHA-256 digest length */

static const char *TAG = "main";

static const char* source_str[] = {"REST", " ADC", "ESPN", "BUTN", "ALRM", "RAND"};

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

static char uartbuf[2048];
void uart_log_task(void *params)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(1, 512 * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(1, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(1, 15, 9, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // esp_rom_gpio_connect_out_signal(9, UART_PERIPH_SIGNAL(0, SOC_UART_TX_PIN_IDX), false, false);
    int len;
    while (1)
    {
        // vTaskDelay(1000);
        len = uart_read_bytes(1, uartbuf, (2048 - 1), pdMS_TO_TICKS(2000));
        // Write data back to the UART
        // uart_write_bytes(0, (const char *) buffer, len);
        if (len) {
            // uartbuf[len] = 0;

            log_string(uartbuf, len, false);
        }
        else
        {
            // ESP_LOGI(TAG, "No bytes received");
            // log_string("no data oh noes :(");
        }
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

void calc_tickinc_and_looptime(uint64_t looptime)
{
    ideal_time_between_levels_us = fadetime << 2;
    target_looptime = ideal_time_between_levels_us * (uint64_t)tick_inc;
    while (target_looptime < looptime)
    {
        tick_inc += 1;
        target_looptime = ideal_time_between_levels_us * tick_inc;
        // ESP_LOGI(TAG, "Trying %i tick inc, %i target looptime", tick_inc, target_looptime);
    }
    ESP_LOGD(TAG, "calc: looptime %llu, tick_inc %i", looptime, tick_inc);
}

void transmit_setlevel_dali_channel(dali_transceiver_handle_t transceiver, int channel, int level)
{
    if (channel >= 0 && channel <= 63)
    {
        dali_set_level_noblock(transceiver, channel, level, pdMS_TO_TICKS(130));
        return;
    }
    if (channel >= 100 && channel <= 115)
    {
        dali_set_level_group_noblock(transceiver, channel - 100, level, pdMS_TO_TICKS(130));
        return;
    }
    if (channel == 200)
    {
        dali_broadcast_level_noblock(transceiver, level);
        return;
    }
    if (channel == -1)
        return;
    ESP_LOGE(TAG, "Unknown DALI Channel %i", channel);
    return;
}

static char templogbuffer[0x4000];
static SemaphoreHandle_t printf_mutex;
int buffer_vprint(const char *format, va_list args)
{  
    int result;
    if (xSemaphoreTake(printf_mutex, pdMS_TO_TICKS(1000)) == pdTRUE){

        int strsize = vsnprintf(templogbuffer, 0, format, args);
        
        char* tempbuf = malloc(strsize + 2);
        vsnprintf(tempbuf, strsize, format, args);
        if (tempbuf[strsize -1] != '\n'){
            tempbuf[strsize-1] = '\n';
            tempbuf[strsize] = 0;
        }
        log_string(tempbuf, strsize, true);
        fputs(tempbuf, stdout);
        xSemaphoreGive(printf_mutex);
        free(tempbuf);
    }
    
    result = 0;
    // BaseType_t success = xSemaphoreTake(log_mutex, pdMS_TO_TICKS(1000));
    // if (success == pdTRUE) {
        // log_string(templogbuffer, strlen(templogbuffer), false);
        // xSemaphoreGive(log_mutex);
    // }
    // va_end(args);

    return result;
}

void mainloop()
{
    setup_relays(get_setting("configbits"));
    TaskHandle_t uarttask;
    // xTaskCreate(uart_log_task, "uart loopback", 4096, NULL, 1, &uarttask);

    level_overrides_t level_overrides = {
        .dali1 = -1,
        .dali2 = -1,
        .dali3 = -1,
        .dali4 = -1,
        .zeroten1 = -1,
        .zeroten2 = -1,
        .espnow = -1};

    networking_ctx_t networking_ctx = {
        .mainloop_task = xTaskGetCurrentTaskHandle(),
        .dali_command_queue = NULL,
        .level_overrides = &level_overrides};
    TaskHandle_t networktask;
    xTaskCreate(setup_networking, "setup_networking", 4096, (void *)&networking_ctx, 2, &networktask);
    
    gpio_set_level(LED2_GPIO, 1);
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
    transceiver_config.receive_queue_size_frames = 0;
    transceiver_config.receive_gpio_pin = RX_GPIO;
    transceiver_config.transmit_gpio_pin = TX_GPIO;
    transceiver_config.parser_config.forward_frame_action = DALI_PARSER_ACTION_LOG;

    dali_transceiver_handle_t dali_transceiver;
    ESP_ERROR_CHECK(dali_setup_transceiver(transceiver_config, &dali_transceiver));
    networking_ctx.dali_command_queue = dali_setup_command_queue(dali_transceiver);
    // dali_set_system_failure_level(dali_transceiver, 8, 10);
    // dali_set_system_failure_level(dali_transceiver, 9, 10);
    dali_broadcast_level_noblock(dali_transceiver, 0);

    BaseType_t received;
    esp_err_t sent;
    int firsttime = 1;
    // actual_level = setpoint;
    // vTaskDelay(50);
    ESP_LOGI(TAG, "Starting main loop");
    int local_setpoint_struct_as_int;
    int64_t reftime = esp_timer_get_time() - MIN_EST_LOOPTIME_US;
    uint64_t reawake_time = esp_timer_get_time() + 5000000;
    int configbits = get_setting("configbits");

    int default_fadetime = get_setting("default_fade");
    int looptime = MIN_EST_LOOPTIME_US;

    int updates_performed = 0;
    int fade_remaining = 0;

    bool at_setpoint;
    uint16_t actual_level = setpoint;
    level_t level_el;
    level_t future_el;
    uint64_t current_time;
    uint64_t actual_looptime;
    int random_looptime = 1;
    uint8_t local_setpoint = setpoint;

    int dali1_address = get_setting("dali1_channel");
    int dali2_address = get_setting("dali2_channel");
    int dali3_address = get_setting("dali3_channel");
    int dali4_address = get_setting("dali4_channel");
    uint16_t full_power = get_setting("full_power");
    int zeroten1_lvl_to_send = setpoint;
    int zeroten2_lvl_to_send = setpoint;
    int dali1_lvl_to_send = setpoint;
    int dali2_lvl_to_send = setpoint;
    int dali3_lvl_to_send = setpoint;
    int dali4_lvl_to_send = setpoint;
    int espnow_lvl_to_send = setpoint;
    uint8_t already_off[] = {0,0,0,0};
    int lookahead;
    int idlecount = 0;
    int levellog_count = 0;
    int max_lookahead;
    bool new_setpoint;
    int minlevelbits = 0;
    int future_level;
    level_t min_level;
    setpoint_notify_t received_setpoint;
    bool dali_broadcast = false;
    // level_t lev_el;
    
    // for (int i=0; i< 5; i++){
    //     lev_el = levellut[i];
    //     ESP_LOGI(TAG, "Level %i: %d, %d, %d", i, lev_el.zeroten1_lvl, lev_el.zeroten2_lvl, lev_el.dali1_lvl);
    // }
    level_t subslevel = {
        .zeroten1_lvl = 0,
        .zeroten2_lvl = 0,
        .dali1_lvl = 0,
        .dali2_lvl = 0,
        .dali3_lvl = 0,
        .dali4_lvl = 0,
        .espnow_lvl = 0,
        .relay1 = 0,
        .relay2 = 0,
    };
    while (1)
    {
        actual_looptime = (esp_timer_get_time() - reftime);
        if (actual_looptime > (target_looptime + LOOPTIME_TOLERANCE))
        {
            calc_tickinc_and_looptime(actual_looptime);
        };
        while (1)
        {
            received = xTaskNotifyWaitIndexed(SETPOINT_SLEW_NOTIFY_INDEX, 0, 0, &local_setpoint_struct_as_int, 1);
            current_time = esp_timer_get_time();
            if (received == pdTRUE)
            { 
                received_setpoint = *((setpoint_notify_t*) &local_setpoint_struct_as_int);
                received_setpoint.setpoint = clamp(received_setpoint.setpoint, 0, 254);
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

                calc_tickinc_and_looptime(MIN_EST_LOOPTIME_US);
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
            // reawake_time = reftime + IDLE_UPDATE_INTERVAL_US;
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
            dali1_address = get_setting("dali1_address");
            dali2_address = get_setting("dali2_address");
            dali3_address = get_setting("dali3_address");
            dali4_address = get_setting("dali4_address");

            full_power = clamp(get_setting("full_power"), 0, 512);
            
            if (!(idlecount & 0xF))
            {
                ESP_LOGI(TAG, "Min free heap %i", heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
                ESP_LOGI(TAG, "Free heap %i", heap_caps_get_free_size(MALLOC_CAP_8BIT));
            }
        
            // list_tasks();
        }

        fade_remaining = received_setpoint.setpoint - actual_level;
        level_el = levellut[actual_level + full_power - 254];
        
        for (int ch=0; ch < sizeof(level_t); ch ++)
        {
            if (*((uint8_t*) &level_el + ch) < *((uint8_t*) &min_level + ch))
            {
                *((uint8_t*) &level_el + ch) = *((uint8_t*) &min_level + ch);
            }
        }
        manage_relay_timeouts(configbits, level_el.relay1, level_el.relay2);

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
            ESP_ERROR_CHECK(set_0_10v_level(pwm1, zeroten1_lvl_to_send));
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
            ESP_ERROR_CHECK(set_0_10v_level(pwm2, zeroten2_lvl_to_send));
        }
        // for (int i=8; i<13; i+=1) {
        if (configbits & CONFIGBIT_USE_DALI && dali_take_mutex(dali_transceiver, 0))
        {
            if (level_overrides.dali1 != -1)
            {
                dali1_lvl_to_send = level_overrides.dali1;
            }
            else
            {
                dali1_lvl_to_send = level_el.dali1_lvl;
            }
            if (dali1_lvl_to_send || !already_off[0])
            {
                transmit_setlevel_dali_channel(dali_transceiver, dali1_address, dali1_lvl_to_send);
                already_off[0] = !dali1_lvl_to_send;
            }

            dali_broadcast = dali1_address == 200;
            if (!dali_broadcast)
            {

                if (level_overrides.dali2 != -1)
                {
                    dali2_lvl_to_send = level_overrides.dali2;
                }
                else
                {
                    dali2_lvl_to_send = level_el.dali2_lvl;
                }
                if (level_overrides.dali3 != -1)
                {
                    dali3_lvl_to_send = level_overrides.dali3;
                }
                else
                {
                    dali3_lvl_to_send = level_el.dali3_lvl;
                }
                if (level_overrides.dali4 != -1)
                {
                    dali4_lvl_to_send = level_overrides.dali4;
                }
                else
                {
                    dali4_lvl_to_send = level_el.dali4_lvl;
                }
                if (dali2_lvl_to_send || !already_off[1])
                {
                    transmit_setlevel_dali_channel(dali_transceiver, dali2_address, dali2_lvl_to_send);
                    already_off[1] = !dali2_lvl_to_send;
                }
                if (dali3_lvl_to_send || !already_off[2])
                {
                    transmit_setlevel_dali_channel(dali_transceiver, dali3_address, dali3_lvl_to_send);
                    already_off[2] = !dali3_lvl_to_send;
                }
                if (dali3_lvl_to_send || !already_off[3])
                {
                    transmit_setlevel_dali_channel(dali_transceiver, dali4_address, dali4_lvl_to_send);
                    already_off[3] = !dali4_lvl_to_send;
                }
            }
            dali_give_mutex(dali_transceiver);
        }
        // sprintf(templogbuffer, "%s: Snt %i->%i Fade %i Intvl %llu", TAG, actual_level, received_setpoint.setpoint, fadetime, target_looptime);
        // log_string(templogbuffer);

        // ESP_LOGI(TAG, "Lvl %i->%i { 0-10v1 %d, 0-10v2 %d, DALI1 %d, `DALI2 %d, DALI3 %d, DALI4 %d, ESPNOW %d, Rly1 %d, Rly2 %d LpTm %i",
                    //   "Lvl -> SP | 0-10v1 0-10v2 DALI1 DALI2 DALI3 DALI4 ESPNOW, Rly1 Rly2 Looptime"
        if (!(levellog_count & 7))
        {
            ESP_LOGI(TAG, "Lvl-> SP |  SRC N | 0-10v1 0-10v2 DALI1 DALI2 DALI3 DALI4 ESPN Rly1 Rly2     Fade  Looptime");
            ESP_LOGI(TAG, "                        %s     %s    %s    %s    %s    %s   %s   %s   %s",
                    (configbits & CONFIGBIT_USE_0_10v1) ? "ON" : " .",
                    (configbits & CONFIGBIT_USE_0_10v2) ? "ON" : " .",
                    ((configbits & CONFIGBIT_USE_DALI) && dali1_address != -1) ? "ON" : " .",
                    ((configbits & CONFIGBIT_USE_DALI) && dali2_address != -1) ? "ON" : " .",
                    ((configbits & CONFIGBIT_USE_DALI) && dali3_address != -1) ? "ON" : " .",
                    ((configbits & CONFIGBIT_USE_DALI) && dali4_address != -1) ? "ON" : " .",
                    (configbits & CONFIGBIT_TRANSMIT_ESPNOW) ? "ON" : " .",
                    (configbits & CONFIGBIT_USE_RELAY1) ? "ON" : " .",
                    (configbits & CONFIGBIT_USE_RELAY2) ? "ON" : " .");
        }
        levellog_count += 1;
        ESP_LOGI(TAG,   "%3.1u->%3.1d | %s %1.i |    %3.1i    %3.1i   %3.1i   %3.1i   %3.1i   %3.1i  %3.1i  %3.1d  %3.1d %8.1i  %8.i",
                actual_level,
                received_setpoint.setpoint,
                source_str[received_setpoint.setpoint_source],
                (int) new_setpoint,
                zeroten1_lvl_to_send,
                zeroten2_lvl_to_send,
                dali1_lvl_to_send,
                dali2_lvl_to_send,
                dali3_lvl_to_send,
                dali4_lvl_to_send,
                espnow_lvl_to_send,
                level_el.relay1,
                level_el.relay2,
                fadetime,
                (int)actual_looptime);
                
        new_setpoint = false;
    }
}

void random_setpointer_task(void *params)
{
    TaskHandle_t mainloop_task = params;
    int mult = (rand() & 0xF);
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
        xTaskNotifyIndexed(mainloop_task, SETPOINT_SLEW_NOTIFY_INDEX, setpoint_struct_as_int, eSetValueWithOverwrite);
        vTaskDelay(((rand() & 0x7) * mult * mult) + 1);
    }
}

static bool diagnostic(void)
{
    ESP_LOGI(TAG, "Diagnostics (3 sec)...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    return get_and_log_buttons() != 4;       ;
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
            bool diagnostic_is_ok = diagnostic();
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

void app_main(void)
{
    ESP_LOGI(TAG, "It worked (three times)!!!!");

    configure_gpio();
    gpio_set_level(LED1_GPIO, 1);
    initialise_logbuffer();
    printf_mutex = xSemaphoreCreateMutex();
    esp_log_set_vprintf(buffer_vprint);

    check_partitions();
    // sprintf(logbuffer, "This is a log");
    srand(time(NULL));
    vTaskPrioritySet(NULL, 6);
    // rotate_gpio_outputs();
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
    // setup_adc(adcconfig);

    // while(1){
    // received = xTaskNotifyWaitIndexed(LIGHT_LEVEL_NOTIFY_INDEX, 0, 0, &resistor_level, 100);
    // if (received) ESP_LOGI(TAG, "Received resistor level %i", resistor_level);

    // }
    // rotate_gpio_outputs_forever();
    // setup_flash_led(LED1_GPIO, 2000);
    // setup_flash_led(LED2_GPIO, 2020);
    // setup_flash_led(RELAY1_GPIO, 7400);
    // setup_flash_led(RELAY2_GPIO, 7550);
    mainloop();
}
