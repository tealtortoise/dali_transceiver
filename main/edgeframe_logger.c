// #include <soc/gpio_reg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/gpio_filter.h"
#include "dali.h"


static const char *ETAG = "edgeframe_isr";
//
// typedef struct {
//     uint8_t firstbyte;
//     uint8_t secondbyte;
// } dali_forward_frame_t;


typedef struct {
    QueueHandle_t queue;
    uint8_t gpio_pin;
    gptimer_handle_t timer;
    uint32_t timeout;
    bool invert;
    edgeframe edgeframe_template;
    uint8_t edgeframe_isr_state;
    uint8_t edgeframe_isr_numedges;
    uint64_t edgeframe_tempcount;
    uint64_t edgeframe_startcount;
} edgeframe_isr_ctx;

#define EDGEFRAME_STATE_IDLE 0
#define EDGEFRAME_STATE_LOGGING 1

#define EDGETYPE_RISING 1
#define EDGETYPE_FALLING 0
#define EDGETYPE_NONE -1

bool IRAM_ATTR timeout_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    BaseType_t high_task_awoken = pdFALSE;
    edgeframe_isr_ctx *ctx = (edgeframe_isr_ctx*) user_ctx;

    gptimer_get_raw_count(ctx->timer, (uint64_t *)&ctx->edgeframe_tempcount);
    ctx->edgeframe_template.length = ctx->edgeframe_isr_numedges + 1;

    // add stop condition
    ctx->edgeframe_template.edges[ctx->edgeframe_isr_numedges].edgetype = EDGETYPE_NONE;
    ctx->edgeframe_template.edges[ctx->edgeframe_isr_numedges].time = ctx->edgeframe_tempcount - ctx->edgeframe_startcount;

    BaseType_t success = xQueueSendFromISR(ctx->queue, (edgeframe*) &ctx->edgeframe_template, &high_task_awoken);
    if (success != pdTRUE) ESP_DRAM_LOGE(ETAG, "Warning receive buffer full - missed Dali frame");
    // ESP_DRAM_LOGI(ETAG, "sent queue frame length %d", ctx->edgeframe_template.length);

    // reset state machine
    ctx->edgeframe_template.length = 0;
    ctx->edgeframe_isr_numedges = 0;
    ctx->edgeframe_isr_state = EDGEFRAME_STATE_IDLE;
    gpio_set_intr_type(ctx->gpio_pin, ctx->invert ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE);
    gptimer_stop(ctx->timer);

    return (high_task_awoken == pdTRUE);
}
static const DRAM_ATTR gptimer_event_callbacks_t cbs__ = {
    .on_alarm = timeout_isr,
};

void IRAM_ATTR input_edgelog_isr(void *params) {
    edgeframe_isr_ctx *ctx = (edgeframe_isr_ctx*) params;
    gptimer_get_raw_count(ctx->timer, (uint64_t*) &ctx->edgeframe_tempcount);
    switch (ctx->edgeframe_isr_state){
        case EDGEFRAME_STATE_IDLE: {
            // gptimer_get_raw_count(ctx.timer, &edgeframe_startcount);
            // edgeframe_lastcount = 0;
            gptimer_set_raw_count(ctx->timer, 0);
            gptimer_start(ctx->timer);
            gpio_set_intr_type(ctx->gpio_pin, GPIO_INTR_ANYEDGE);
            ctx->edgeframe_template.edges[0].edgetype = 1 - (uint8_t) ctx->invert;
            ctx->edgeframe_template.edges[0].time = 0;
            ctx->edgeframe_isr_numedges = 1;
            ctx->edgeframe_isr_state = EDGEFRAME_STATE_LOGGING;

            break;
        }
        case EDGEFRAME_STATE_LOGGING: {
            if (ctx->edgeframe_isr_numedges > 60) {
                // too long
                break;
            }
            int level = gpio_get_level(ctx->gpio_pin);
            ctx->edgeframe_template.edges[ctx->edgeframe_isr_numedges].edgetype = ctx->invert ? 1 - level : level;
            ctx->edgeframe_template.edges[ctx->edgeframe_isr_numedges].time = ctx->edgeframe_tempcount;// - edgeframe_startcount;

            gptimer_alarm_config_t alarm_config = {
                .alarm_count = ctx->edgeframe_tempcount + ctx->timeout,
                .flags.auto_reload_on_alarm = false,
            };
            ESP_ERROR_CHECK(gptimer_set_alarm_action(ctx->timer, &alarm_config));
            ctx->edgeframe_isr_numedges += 1;
            // edgeframe_lastcount = edgeframe_tempcount;
            break;
        }

    }
}


gptimer_handle_t configure_edgeframe_timer(edgeframe_isr_ctx *ctx){
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick = 1us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = timeout_isr,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, ctx));

    ESP_LOGI(ETAG, "Edgeframe timer Enable");
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    return gptimer;
}

QueueHandle_t start_edgelogger(uint8_t gpio, bool invert) {
    QueueHandle_t edgeframe_queue = xQueueCreate(4, sizeof(edgeframe));
    assert(edgeframe_queue);

    edgeframe_isr_ctx *ctx = heap_caps_malloc(sizeof(edgeframe_isr_ctx), MALLOC_CAP_INTERNAL);

    ctx->gpio_pin = gpio;
    ctx->queue = edgeframe_queue;
    ctx->timeout = 1600;
    ctx->invert = invert;
    ctx->timer = configure_edgeframe_timer(ctx);
    ctx->edgeframe_isr_state = EDGEFRAME_STATE_IDLE;

    gpio_set_intr_type(gpio, invert ? GPIO_INTR_POSEDGE: GPIO_INTR_NEGEDGE);
    gpio_pin_glitch_filter_config_t glitch_config = {
        .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
        .gpio_num = gpio
    };
    gpio_glitch_filter_handle_t glitch_filter;
    gpio_new_pin_glitch_filter(&glitch_config, &glitch_filter);

    ESP_ERROR_CHECK(gpio_isr_handler_add(gpio, input_edgelog_isr, (edgeframe_isr_ctx*) ctx));
    return edgeframe_queue;
}


