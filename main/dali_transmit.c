//
// Created by thea on 5/29/24.
//


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "dali.h"


static const char *TTAG = "dali_transit";


static const uint8_t DALI_ISR_STATE_IDLE = 0;
static const uint8_t DALI_ISR_STATE_STARTED = 1;
static const uint8_t DALI_ISR_STATE_BITPREPARE = 2;
static const uint8_t DALI_ISR_STATE_BITDATA = 3;
static const uint8_t DALI_ISR_STATE_ENDDATA = 4;
static const uint8_t DALI_ISR_STATE_STOP = 5;


typedef struct {
    uint8_t gpio_pin;
    uint16_t data;
    uint8_t state;
    uint8_t bitpos;
    gptimer_handle_t timer;
    QueueHandle_t isr_queue;
    QueueHandle_t transmit_queue;
    uint8_t invert;
    gptimer_alarm_config_t alarmconf;
    uint16_t bitwork;
} dali_transmit_isr_ctx;

typedef struct {
    uint8_t number;
    gptimer_handle_t timer;
    dali_transmit_isr_ctx *ctx;
    QueueHandle_t queue;
} dali_transmitter_handle_t;


bool IRAM_ATTR dali_transmit_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    dali_transmit_isr_ctx *ctx = (dali_transmit_isr_ctx *) user_ctx;
    switch (ctx->state) {
        case DALI_ISR_STATE_BITPREPARE: {
            gpio_set_level(ctx->gpio_pin, 1 - ctx->bitwork);
            ctx->state = DALI_ISR_STATE_BITDATA;
            break;
        }
        case DALI_ISR_STATE_BITDATA: {
            gpio_set_level(ctx->gpio_pin, ctx->bitwork);
            if (ctx->bitpos < 15) {
                ctx->bitpos += 1;
                ctx->state = DALI_ISR_STATE_BITPREPARE;

                ctx->bitwork = ctx->invert ^ ((ctx->data >> (15 - ctx->bitpos)) & 1);
            }
            else
            {
                ctx->state = DALI_ISR_STATE_ENDDATA;
            }
            break;
        }
        case DALI_ISR_STATE_STARTED: {
            gpio_set_level(ctx->gpio_pin, 1 - ctx->invert);
            BaseType_t received = xQueuePeekFromISR(ctx->isr_queue, (uint16_t*) &ctx->data);
            assert(received == pdTRUE);
            ctx->state = DALI_ISR_STATE_BITPREPARE;
            ctx->bitwork = ctx->invert ^ ((ctx->data >> (15 - ctx->bitpos)) & 1);
            break;
        }
        case DALI_ISR_STATE_ENDDATA: {
            gpio_set_level(ctx->gpio_pin, 1 - ctx->invert);
            ctx->alarmconf.alarm_count = 833 * 3;
            gptimer_set_alarm_action(ctx->timer, &ctx->alarmconf);
            ctx->state = DALI_ISR_STATE_STOP;
            break;
        }
        case DALI_ISR_STATE_STOP: {
            gptimer_stop(ctx->timer);
            xQueueReset(ctx->isr_queue);
            ctx->state = DALI_ISR_STATE_IDLE;
            // ESP_DRAM_LOGI(TTAG, "Call count %u", callcount);
            break;
        }
        case DALI_ISR_STATE_IDLE: {
            ESP_DRAM_LOGI(TTAG, "How have we ended up here ctxdata: %u", ctx->data);
            ESP_ERROR_CHECK(ESP_ERR_NOT_ALLOWED);
            break;
        }

    }
    return true;
}

void dali_transmit_queue_receiver_task(dali_transmit_isr_ctx *ctx) {
    dali_forward_frame_t frame;
    while (1) {
        BaseType_t received = xQueueReceive(ctx->transmit_queue, &frame, portMAX_DELAY);
        if (received == pdTRUE) {
            uint16_t data = frame.secondbyte;
            data = data | (frame.firstbyte << 8);
            ESP_LOGI(TTAG, "Sending f: %d, s: %d to ISR", frame.firstbyte, frame.secondbyte);
            BaseType_t success = xQueueSendToBack(ctx->isr_queue, &data, pdMS_TO_TICKS(1000));
            if (success != pdTRUE) {
            ESP_ERROR_CHECK(ESP_ERR_TIMEOUT);
            }
            assert(ctx->state == DALI_ISR_STATE_IDLE);
            ctx -> bitpos = 0;
            ctx->alarmconf.alarm_count = 416;
            gptimer_set_alarm_action(ctx->timer, &ctx->alarmconf);
            gpio_set_level(ctx->gpio_pin, ctx->invert);
            ctx->state = DALI_ISR_STATE_STARTED;
            // gptimer_set_raw_count(ctx->timer, 0);
            gptimer_start(ctx->timer);
        }
    };

};

//

esp_err_t setup_dali_transmitter(uint8_t gpio_pin, uint16_t queuedepth, dali_transmitter_handle_t *handle) {
    // setup queue
    QueueHandle_t isrqueue = xQueueCreate(1, sizeof(uint16_t));
    QueueHandle_t transmitqueue = xQueueCreate(queuedepth, sizeof(dali_forward_frame_t));

    // setup gpio
    gpio_config_t gpioconf = {
        // .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pin_bit_mask = 1 << gpio_pin
    };
    gpio_config(&gpioconf);
    gpio_set_drive_capability(gpio_pin,GPIO_DRIVE_CAP_3);

    // setup gptimer
    gptimer_config_t gptimerconfig = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    gptimer_handle_t gptimer;
    gptimer_new_timer(&gptimerconfig, &gptimer);

    gptimer_alarm_config_t alarmconf = {
        .alarm_count = 416,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    gptimer_set_alarm_action(gptimer, &alarmconf);


    // allocate internal DRAM ctx
    dali_transmit_isr_ctx *ctx = heap_caps_malloc(sizeof(dali_transmit_isr_ctx),
        MALLOC_CAP_INTERNAL);

    ctx->gpio_pin = gpio_pin;
    ctx->data = 0;
    ctx->bitpos = 0;
    ctx->state = DALI_ISR_STATE_IDLE;
    ctx->isr_queue = isrqueue;
    ctx->transmit_queue = transmitqueue;
    ctx->timer = gptimer;
    ctx->invert = 1;
    ctx->alarmconf = alarmconf;

    gptimer_event_callbacks_t cbs = {
        .on_alarm = dali_transmit_isr,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, ctx));

    handle->timer = gptimer;
    handle->ctx = ctx;
    handle->queue = transmitqueue;

    gptimer_enable(gptimer);
    xTaskCreate((void *)dali_transmit_queue_receiver_task, "dali_transmit_queue_receiver_task", 2400, ctx, 1, NULL);

    return ESP_OK;
}


