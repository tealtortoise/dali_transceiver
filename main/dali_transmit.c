//
// Created by thea on 5/29/24.
//

#include <assert.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "dali.h"
#include "dali_transmit.h"
#include "dali_edgeframe_parser.h"


static const char *TTAG = "dali_transit";

typedef struct {
    uint16_t data;
    TaskHandle_t notify_task_handle;
    uint32_t notify_idx;
} dali_transmit_isr_job;

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
            // BaseType_t received = xQueuePeekFromISR(ctx->isr_queue,  (dali_transmit_isr_job*) &ctx->job);
            // ctx->data = ctx->job.data;
            // assert(received == pdTRUE);
            ctx->state = DALI_ISR_STATE_BITPREPARE;
            ctx->bitwork = ctx->invert ^ ((ctx->data >> (15 - ctx->bitpos)) & 1);
            break;
        }
        case DALI_ISR_STATE_ENDDATA: {
            gpio_set_level(ctx->gpio_pin, 1 - ctx->invert);
            ctx->alarmconf.alarm_count = 833 * 4;
            gptimer_set_alarm_action(ctx->timer, &ctx->alarmconf);
            ctx->state = DALI_ISR_STATE_STOP;
            break;
        }
        case DALI_ISR_STATE_STOP: {
            ctx->alarmconf.alarm_count = 11000;
            gptimer_set_alarm_action(ctx->timer, &ctx->alarmconf);
            ctx->state = DALI_ISR_STATE_SETTLING;
            // ESP_DRAM_LOGI(TTAG, "Call count %u", callcount);
            break;
        }
        case DALI_ISR_STATE_SETTLING: {
            gptimer_stop(ctx->timer);
            // xQueueReset(ctx->isr_queue);
            ctx->state = DALI_ISR_STATE_IDLE;
            xTaskNotifyIndexedFromISR(
                ctx->notify_task,
                ctx->notify_idx,
                0,
                eSetValueWithOverwrite,
                &ctx->taskawoken);
            break;
        }
        case DALI_ISR_STATE_IDLE: {
            ESP_DRAM_LOGI(TTAG, "How have we ended up here ctxdata: %u", ctx->data);
            ESP_ERROR_CHECK(ESP_ERR_NOT_ALLOWED);
            break;
        }

    }
    return ctx->taskawoken == pdTRUE;
}

void dali_transmit_queue_receiver_task(dali_transmit_isr_ctx *ctx) {
    dali_frame_t frame;
    dali_transmit_job job;
    uint32_t returnval;
    TaskHandle_t thistask = xTaskGetCurrentTaskHandle();
    ctx->notify_task = xTaskGetCurrentTaskHandle();
    BaseType_t success;
    ctx->notify_idx = 0;
    uint16_t tempdata;
            // success = xTaskNotifyWaitIndexed(123, 0, 0, &returnval, 10);
    while (1) {
        // vTaskDelay(50);
        BaseType_t received = xQueueReceive(ctx->transmit_queue, &job, portMAX_DELAY);
        if (received == pdTRUE) {
            tempdata = job.frame.secondbyte;
            ctx->data = tempdata | (job.frame.firstbyte << 8);
            // ESP_LOGI(TTAG, "Sending f: %d, s: %d to ISR", frame.firstbyte, frame.secondbyte);
            assert(ctx->state == DALI_ISR_STATE_IDLE);
            ctx -> bitpos = 0;
            ctx->alarmconf.alarm_count = 416;
            gptimer_set_alarm_action(ctx->timer, &ctx->alarmconf);
            gpio_set_level(ctx->gpio_pin, ctx->invert);
            ctx->state = DALI_ISR_STATE_STARTED;
            // gptimer_set_raw_count(ctx->timer, 0);
            gptimer_start(ctx->timer);
            success = xTaskNotifyWaitIndexed(0, 0, 0, &returnval, pdMS_TO_TICKS(100));
            if (job.frameid != DALI_FRAME_ID_DONT_NOTIFY) {
                xTaskNotifyIndexed(job.notify_task, DALI_NOTIFY_COMPLETE_INDEX, job.frameid, eSetValueWithOverwrite);
            }
            
            if (success != pdTRUE) ESP_LOGE(TTAG, "Dali transmit timer_isr didn't return in time");
            if (ctx->state != DALI_ISR_STATE_IDLE) ESP_LOGE(TTAG, "ISR state not IDLE");
        }
    };

};

//

esp_err_t setup_dali_transmitter(uint8_t gpio_pin, uint8_t invert, uint16_t queuedepth, dali_transmitter_handle_t *handle) {
    // setup queue
    QueueHandle_t isrqueue = xQueueCreate(1, sizeof(dali_transmit_isr_job));
    QueueHandle_t transmitqueue = xQueueCreate(queuedepth, sizeof(dali_transmit_job));

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
    ctx->invert = invert;
    ctx->alarmconf = alarmconf;

    gptimer_event_callbacks_t cbs = {
        .on_alarm = dali_transmit_isr,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, ctx));

    handle->timer = gptimer;
    handle->ctx = ctx;
    handle->queue = transmitqueue;
    handle->frameidcounter = 1;

    gptimer_enable(gptimer);
    xTaskCreate((void *)dali_transmit_queue_receiver_task, "dali_transmit_queue_receiver_task", 2400, ctx, 1, NULL);

    return ESP_OK;
}
