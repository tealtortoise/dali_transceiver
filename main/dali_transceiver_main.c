// #include <soc/gpio_reg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/ledc.h"
#include "edgeframe_logger.c"
#include "dali_transmit.c"
#include "0-10v.c"
#include "dali_edgeframe_parser.c"
// #include "dali_utils.c"
#include "dali_rmt_receiver.c"
// #include "wifi.c"
// #include "http_server.c"

#define RESOLUTION_HZ     10000000

#define TX_GPIO       18
#define STROBE_GPIO 17
#define RX_GPIO       6
#define PWM_010v_GPIO   15


static const char *TAG = "example";

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


void app_main(void)
{
    configure_gpio();
    QueueHandle_t lightingqueue = xQueueCreate(1, sizeof(int));
    // esp_event_handler_t lightingloop = setup_events();
    // setup_wifi();
    httpd_ctx httpdctx = {
        .queue = lightingqueue,
        .task = xTaskGetCurrentTaskHandle()
    };
    // httpd_handle_t httpd = setup_httpserver(&httpdctx);
    
    gptimer_handle_t strobetimer = configure_strobetimer();
    ESP_ERROR_CHECK(gptimer_start(strobetimer));
    ledc_channel_t pwm0_10v_channel1;
    setup_pwm_0_10v();
    dali_transmitter_handle_t dali_transmitter;
    setup_dali_transmitter(TX_GPIO, 3, &dali_transmitter);
    
    ESP_ERROR_CHECK(configure_pwm_0_10v(PWM_010v_GPIO, 1<<14, &pwm0_10v_channel1));


    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    

    QueueHandle_t edgeframe_queue = start_edgelogger(RX_GPIO, false);
    
    // QueueHandle_t daliqueue = start_dali_parser(edgeframe_queue);

    // QueueHandle_t daliqueue = start_dali_parser(edgeframe_queue, 0);
    parsestruct *ps = start_dali_parser(edgeframe_queue, 1);

    // transmit_frame(dali_transmitter, DALI_FIRSTBYTE_TERMINATE, DALI_SECONDBYTE_TERMINATE);
    
    vTaskDelay(100);
    dali_frame_t frame;
    frame.firstbyte = DALI_FIRSTBYTE_BROADCAST_LEVEL;
    frame.secondbyte = 50;
    xQueueSendToBack(dali_transmitter.queue, &frame, portMAX_DELAY);
    
    // vTaskDelay(pdMS_TO_TICKS(400));
    // provision(dali_transmitter, ps->daliframequeue);
    // vTaskDelay(pdMS_TO_TICKS(400));
    while (1){
        // BaseType_t success;
        // frame.firstbyte;
        uint8_t address;
        uint32_t queuepeek;
        BaseType_t success;
        for (int i=5; i<10; i+=1) {
            // update_0_10v_level(pwm0_10v_channel1, i *i);
            // success = xQueuePeek(lightingqueue, &queuepeek, 0);
            success = xTaskNotifyWaitIndexed(0, 0, 0, &queuepeek, pdMS_TO_TICKS(1000));
            if (success == pdTRUE){
                if (queuepeek >= 0 && queuepeek <= 63)
                {
                    address = queuepeek;
                } else {
                    ESP_LOGE(TAG, "queuepeek not valid %lu", queuepeek);
                };
            }
            frame.firstbyte = get_dali_address_byte_setlevel(i);
            frame.secondbyte = 100;
            ESP_LOGI(TAG, "address %d", i);
            xQueueSendToBack(dali_transmitter.queue, &frame, portMAX_DELAY);
            frame.secondbyte = 10;
            vTaskDelay(pdMS_TO_TICKS(400));
            xQueueSendToBack(dali_transmitter.queue, &frame, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(1000));
            // success = xQueueSendToBack(dali_transmitter_handle.queue, &frame, portMAX_DELAY);
            // vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

}
