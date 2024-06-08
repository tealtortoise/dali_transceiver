// #include <soc/gpio_reg.h>

#define IS_PRIMARY true
#ifndef IS_PRIMARY
#define IS_SECONDARY true
#endif // ifdef is_primary
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/ledc.h"
// #include "edgeframe_logger.h"
// #include "dali_transmit.h"
#include "0-10v.h"
// #include "dali_edgeframe_parser.h"
// #include "dali.h"
#include "dali_transceiver.h"
#include "dali_utils.h"
// #include "dali_utils.c"
// #include "dali_rmt_receiver.c"
#ifdef IS_PRIMARY
#include "wifi.c"
#include "http_server.c"
#endif // IS_PRIMARY
#include "espnow.h"

#define RESOLUTION_HZ     10000000

#define TX_GPIO       18
#define STROBE_GPIO 17
#define RX_GPIO       6
#define PWM_010v_GPIO   15

static const char *TAG = "main loop";

void configure_gpio(){
    gpio_config_t strobe_gpio_config = {
        .pin_bit_mask = 1ULL << 17,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&strobe_gpio_config));
    ESP_ERROR_CHECK(gpio_set_drive_capability(STROBE_GPIO, GPIO_DRIVE_CAP_3));

    gpio_config_t gpio_txconfig = {
        .pin_bit_mask = 1ULL << TX_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_txconfig));
    ESP_ERROR_CHECK(gpio_set_drive_capability(TX_GPIO, GPIO_DRIVE_CAP_3));
    gpio_set_level(TX_GPIO, 0);

    gpio_config_t gpio_6config = {
        .pin_bit_mask = 1ULL << RX_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_6config));
    ESP_LOGI(TAG, "GPIO %u: level %u", RX_GPIO, gpio_get_level(RX_GPIO));
}

static volatile DRAM_ATTR uint8_t state = 0;

bool IRAM_ATTR strobetimer_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx){
    if (state) {
        state = 0;
    } else {
        state = 1;
    }
    gpio_set_level(STROBE_GPIO, state);
    return false;
}

gptimer_handle_t configure_strobetimer(){
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick = 1us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 8000000, // 2s
        // .alarm_count = 0x7ff0, // 2s
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,

    };
    gptimer_event_callbacks_t cbs = {
        .on_alarm = strobetimer_isr,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));
    // ESP_ERROR_CHECK(gptimer_start(gptimer));
    return gptimer;
}

#ifdef IS_PRIMARY
void primary(){
    setup_wifi();
    httpd_ctx httpdctx = {
        .mainloop_task = xTaskGetCurrentTaskHandle()
    };
    httpd_handle_t httpd = setup_httpserver(&httpdctx);
    
    TaskHandle_t espnow_send_task;
    ESP_ERROR_CHECK(setup_espnow_common(&espnow_send_task, xTaskGetCurrentTaskHandle()));

    
    BaseType_t success;
    uint32_t requestlevel;
    
    while (1) {
        success = xTaskNotifyWaitIndexed(0, 0, 0, &requestlevel, 100);
        if (success) {
            if (requestlevel > 254) requestlevel = 254;
            xTaskNotifyIndexed(espnow_send_task, 0, requestlevel, eSetValueWithOverwrite);
            
            ESP_LOGI(TAG, "Received new level notification: sent %lu to ESPNOW send task", requestlevel);
            // ESP_ERROR_CHECK(set_0_10v_level(pwm0_10v_channel1, (uint16_t)((uint16_t) requestlevel) << 8));
            // for (int i=8; i<10; i+=1) {
                // ESP_ERROR_CHECK(dali_set_level(dali_transceiver, i, requestlevel));
            // }
            vTaskDelay(pdMS_TO_TICKS(4));
        };
    };
}
#endif // is primary

#ifdef IS_SECONDARY
void secondary(){
    espnow_wifi_init();
    
    TaskHandle_t espnow_send_task;
    ESP_ERROR_CHECK(setup_espnow_common(&espnow_send_task, xTaskGetCurrentTaskHandle()));
    
    
    BaseType_t success;
    uint32_t requestlevel;
    
    setup_espnow_receiver();
    
    gptimer_handle_t strobetimer = configure_strobetimer();
    ESP_ERROR_CHECK(gptimer_start(strobetimer));

    ledc_channel_t pwm0_10v_channel1;
    ESP_ERROR_CHECK(setup_0_10v_channel(PWM_010v_GPIO, 1<<14, &pwm0_10v_channel1));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    
    dali_transceiver_config_t transceiver_config = dali_transceiver_sensible_default_config;
    transceiver_config.receive_gpio_pin = RX_GPIO;
    transceiver_config.transmit_gpio_pin = TX_GPIO;

    dali_transceiver_handle_t dali_transceiver;
    ESP_ERROR_CHECK(dali_setup_transceiver(transceiver_config, &dali_transceiver));

    dali_broadcast_level(dali_transceiver, 30);
    dali_set_level(dali_transceiver, 8, 70);
    ESP_LOGI(TAG, "Query level returned %hi", dali_query_level(dali_transceiver, 9));

    uint8_t address;
    uint32_t queuepeek;
    BaseType_t success;
    // int level = 10;
    while (1){
        success = xTaskNotifyWaitIndexed(0, 0, 0, &requestlevel, 100);
        if (success) {
            ESP_LOGI(TAG, "Received notification of level change to %lu", requestlevel);
            if (requestlevel > 254) requestlevel = 254;
            ESP_ERROR_CHECK(set_0_10v_level(pwm0_10v_channel1, (uint16_t)((uint16_t) requestlevel) << 8));
            for (int i=8; i<10; i+=1) {
                ESP_ERROR_CHECK(dali_set_level(dali_transceiver, i, requestlevel));
            }
            vTaskDelay(pdMS_TO_TICKS(4));
        }
    }
}
#endif

void app_main(void)
{
    configure_gpio();
    #ifdef IS_PRIMARY
    primary();
    #endif // IS_PRIMARY

    #ifdef IS_SECONDARY
    secondary();
    #endif

}
