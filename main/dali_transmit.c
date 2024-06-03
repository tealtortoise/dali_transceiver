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
#include <inttypes.h>


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
    dali_frame_t frame;
    while (1) {
        // vTaskDelay(50);
        BaseType_t received = xQueueReceive(ctx->transmit_queue, &frame, portMAX_DELAY);
        if (received == pdTRUE) {
            uint16_t data = frame.secondbyte;
            data = data | (frame.firstbyte << 8);
            // ESP_LOGI(TTAG, "Sending f: %d, s: %d to ISR", frame.firstbyte, frame.secondbyte);
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
    QueueHandle_t transmitqueue = xQueueCreate(queuedepth, sizeof(dali_frame_t));

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

BaseType_t transmit_frame(dali_transmitter_handle_t handle, uint8_t firstbyte, uint8_t secondbyte){
    dali_frame_t frame = {
        .firstbyte = firstbyte,
        .secondbyte = secondbyte
    };
    ESP_LOGI(TTAG, "Sending %d %d", firstbyte, secondbyte);
    return xQueueSendToBack(handle.queue, &frame, 0);
}


void set_24bit_address(dali_transmitter_handle_t handle, uint32_t address){
    transmit_frame(handle, DALI_FIRSTBYTE_SEARCH_HIGH, (address >> 16) & 0xff);
    vTaskDelay(pdMS_TO_TICKS(40));
    transmit_frame(handle, DALI_FIRSTBYTE_SEARCH_MID, (address >> 8) & 0xff);

    vTaskDelay(pdMS_TO_TICKS(40));
    transmit_frame(handle, DALI_FIRSTBYTE_SEARCH_LOW, (address >> 0) & 0xff);

    vTaskDelay(pdMS_TO_TICKS(40));
}

bool search_below(dali_transmitter_handle_t handle, QueueHandle_t daliqueue, uint32_t end){
    bool present = false;
    ESP_LOGI(TTAG, "Searching to %#08lx", end);
    set_24bit_address(handle, end);
    transmit_frame(handle, DALI_FIRSTBYTE_COMPARE, DALI_SECONDBYTE_COMPARE);
    vTaskDelay(pdMS_TO_TICKS(110));
    BaseType_t received = pdTRUE;
    dali_frame_t frame;
    int queued = uxQueueMessagesWaiting(daliqueue);
    if (queued > 1){
        ESP_LOGI(TTAG, "Many responses in queue");
        xQueueReset(daliqueue);
        return true;
    }
    else if (queued == 1 )
    {
        received = xQueueReceive(daliqueue, &frame, 0 );
        if (frame.firstbyte == 255)
        {
            return true;
        } 
        else 
        {
            ESP_LOGI(TTAG, "Not received expected frame %u", received);
            return true;
        }
    }
    return false;
}

static uint32_t fake[] = {0x123454, 0xF52332, 0x029876};

bool mock_search_below(dali_transmitter_handle_t handle, QueueHandle_t daliqueue, uint32_t end){
    for (int i=0; i < 3; i++){
        if (fake[i] <= end) return true;
    }
    return false;
}

void log_fakes(){
    ESP_LOGI(TTAG, "Fake addresses:");
    for (int i=0; i < 3; i++){
        if (fake[i] != 0xF0FFFFFF){
            ESP_LOGI(TTAG, "Fake address %u: %#08lx", i, fake[i]);
        }
    }
}

void remove_fake(uint32_t address){
    bool done = false;
    for (int i=0; i<3; i++){
        if (fake[i] == address) {
            fake[i] = 0xF0FFFFFF;
            done = true;
        }
    }
    if (done) {
        ESP_LOGI(TTAG, "Address %#08lx removed", address);
        log_fakes();
    }
    else
    {
        ESP_LOGE(TTAG, "NOPE none found at %#08lx", address);
        log_fakes();
    }

};
void provision(dali_transmitter_handle_t handle, QueueHandle_t daliqueue){

    ESP_LOGI(TTAG, "Provisioning");
    transmit_frame(handle, DALI_FIRSTBYTE_INITALISE, DALI_SECONDBYTE_INITIALISE_ALL);
    vTaskDelay(pdMS_TO_TICKS(40));
    transmit_frame(handle, DALI_FIRSTBYTE_INITALISE, DALI_SECONDBYTE_INITIALISE_ALL);
    vTaskDelay(pdMS_TO_TICKS(110));
    transmit_frame(handle, DALI_FIRSTBYTE_RANDOMIZE, DALI_SECONDBYTE_RANDOMIZE);
    vTaskDelay(pdMS_TO_TICKS(40));
    transmit_frame(handle, DALI_FIRSTBYTE_RANDOMIZE, DALI_SECONDBYTE_RANDOMIZE);
    vTaskDelay(pdMS_TO_TICKS(110));

    int short_address = 8;
    bool more = true;
    dali_frame_t frame;
    BaseType_t received;
    while(more ){
        ESP_LOGI(TTAG, "Staring search loop");
        uint32_t start= 0;
        uint32_t end = 0xFFFFFF;
        bool cont = true;
        bool present = false;
        bool found = false;
        uint32_t lastend = end;
        while (cont){
            vTaskDelay(10);
            ESP_LOGI(TTAG, "Searching %#08lx - %#08lx", start, end);
            present = search_below(handle, daliqueue, end);
            if (present) {
                if (start == end){
                    // singled out
                    ESP_LOGI(TTAG, "Found it! %#08lx", end);
                    // more = false;
                    found = true;

                    break;
                }
                else
                {
                    // present but not narrowed down
                    lastend = end;
                    end = start + (end - start) / 2;
                    ESP_LOGI(TTAG, "Address present, moving end down to %#08lx", end);
                }
            }
            else
            {
                // not present
                if (end == 0xFFFFFF){
                    // none exist
                    cont = false;
                    more = false;
                    found = false;
                    ESP_LOGI(TTAG, "Nothing found %#08lx", end);
                }
                else
                {
                    // lastend was better, bring start later
                    end = lastend;
                    start = end - (end - start) / 2;
                    ESP_LOGI(TTAG, "Not present, moving start to %#08lx and end back to %#08lx", start, end);
                }
            }
        }

        if (found) {
            if (start != end) ESP_LOGE(TTAG, "What??? start != end %#08lx %#08lx", start, end);

            ESP_LOGI(TTAG, "Assigning short address to %#08lx", end);
            ESP_LOGI(TTAG, "Programming short address %u", short_address);
            transmit_frame(handle, 
                DALI_FIRSTBYTE_PROGRAM_SHORT_ADDRESS, 
                get_dali_address_byte(short_address));
            vTaskDelay(200);
            received = xQueueReceive(daliqueue, &frame, 0 );
            if (received != pdTRUE) ESP_LOGE(TTAG, "No response");
            vTaskDelay(300);
            ESP_LOGI(TTAG, "Verify short address");
            transmit_frame(handle, DALI_FIRSTBYTE_VERIFY_SHORT_ADDRESS, get_dali_address_byte(short_address));
            vTaskDelay(300);
            received = xQueueReceive(daliqueue, &frame, 0 );
            if (received != pdTRUE) ESP_LOGE(TTAG, "No response");
            vTaskDelay(300);

            // received = xQueueReceive(daliqueue, &frame, 0 );
            // if (received != pdTRUE) ESP_LOGE(TTAG, "No response");

            ESP_LOGI(TTAG, "Withdraw");
            transmit_frame(handle, DALI_FIRSTBYTE_WITHDRAW, DALI_SECONDBYTE_WITHDRAW);
            vTaskDelay(200);
            short_address += 1;
            // remove_fake(end);

        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TTAG, "End provisioning");

}


    // while (1) {
    //     // ESP_LOGI(TAG, "Set dtr0 %d", 49);
    //     // set_dtr0.secondbyte = (49 << 1) + 1;
    //     // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_dtr0, sizeof(queryshortaddress), &transmit_config));
    //     // vTaskDelay(pdMS_TO_TICKS(1000));
    //     //
    //     // ESP_LOGI(TAG, "Query dtr");
    //     // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &query_dtr, sizeof(queryshortaddress), &transmit_config));
    //     // vTaskDelay(pdMS_TO_TICKS(1000));
    //     //
    //     // ESP_LOGI(TAG, "set dtr0 as short address twice %d", 49);
    //     // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &storedtr0asshortaddress, sizeof(queryshortaddress), &transmit_config));
    //     // vTaskDelay(pdMS_TO_TICKS(20));
    //     // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &storedtr0asshortaddress, sizeof(queryshortaddress), &transmit_config));
    //     // vTaskDelay(pdMS_TO_TICKS(1000));
    //     //
    //     // ESP_LOGI(TAG, "Query dtr 49");
    //     // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &query_dtr_49, sizeof(queryshortaddress), &transmit_config));
    //     // vTaskDelay(pdMS_TO_TICKS(1000));
    //     //
    //     // set_level_template.firstbyte = DALI_FIRSTBYTE_BROADCAST_LEVEL;
    //     // set_level_template.secondbyte = 10;
    //     // ESP_LOGI(TAG, "Set level %d", 10);
    //     // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    //     // vTaskDelay(pdMS_TO_TICKS(1000));
    //     set_level_template.firstbyte = (49 << 1) + 1;
    //     set_level_template.firstbyte = DALI_FIRSTBYTE_BROADCAST_LEVEL;
    //     set_level_template.secondbyte = 100;
    //     ESP_LOGI(TAG, "Set address all to level %d", 100);
    //     ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    //     vTaskDelay(pdMS_TO_TICKS(300));
    //
    //     for (int i = 0; i< 64; i++) {
    //
    //         set_level_template.firstbyte = (i << 1);
    //         // set_level_template.firstbyte = DALI_FIRSTBYTE_BROADCAST_LEVEL;
    //         set_level_template.secondbyte = 1;
    //         ESP_LOGI(TAG, "Set address %d to level 1", i);
    //         ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    //         vTaskDelay(pdMS_TO_TICKS(300));
    //     }
    //
    //     for (int i=0; i<150;i=i+20) {
    //         set_level_template.firstbyte = (49 << 1) + 1;
    //         set_level_template.firstbyte = DALI_FIRSTBYTE_BROADCAST_LEVEL;
    //         set_level_template.secondbyte = i;
    //         ESP_LOGI(TAG, "Set address 49 to level %d", i);
    //         ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    //         vTaskDelay(pdMS_TO_TICKS(300));
    //
    //         set_level_template.firstbyte = (39 << 1);
    //         set_level_template.secondbyte = 3;
    //         ESP_LOGI(TAG, "Set address 39 to level %d", 3);
    //         // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(queryshortaddress), &transmit_config));
    //         vTaskDelay(pdMS_TO_TICKS(400));
    //
    //         ESP_LOGI(TAG, "Query level");
    //         // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &querylevel, sizeof(test), &transmit_config));
    //         vTaskDelay(pdMS_TO_TICKS(400));
    //     }
    // }
    // while (1) {
    //     for (int i=0; i<7;i=i+1){
    //
    //         set_level_template.secondbyte = 1<<i; // -7;
    //         // conf.outgoing = set_level_template;
    //         // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &set_level_template, sizeof(set_level_template), &transmit_config));
    //
    //         ESP_LOGI(TAG, "Transmitting state %d", 1<<i);
    //         // ESP_LOGI(TAG, "done rx %u tx %u", rxdone, txdone);
    //
    //         // level = gpio_get_level(RX_GPIO);
    //         // gptimer_get_raw_count(strobetimer, &strobecount);
    //         // ESP_LOGI(TAG, "loop log %lu, %u: count %llu", times, level, strobecount);
    //         vTaskDelay(pdMS_TO_TICKS(1000));
    //         // ESP_ERROR_CHECK(rmt_disable(rx_channel));
    //
    //         // rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config);
    //         // conf.outgoing = test;
    //         // ESP_ERROR_CHECK(rmt_transmit(tx_channel, dali_encoder, &querylevel, sizeof(test), &transmit_config));
    //         // delay 1 second
    //         ESP_LOGI(TAG, "Transmitting querylevel");
    //
    //         // ESP_LOGI(TAG, "done rx %u tx %u", rxdone, txdone);
    //         // ESP_LOGI(TAG, "Messages waiting %u", uxQueueMessagesWaiting(receive_queue));
    //
    //         vTaskDelay(pdMS_TO_TICKS(1000));
    //         // gpio_dump_io_configuration(stdout,
    //             // (1ULL << RX_GPIO) | (1ULL << 17) | (1ULL << TX_GPIO));
    //
    //         // ESP_LOGI(TAG, "done rx %u tx %u times %lu inputtimes %lu value %lu", rxdone, txdone, times, inputtimes, value);
    //         // ESP_ERROR_CHECK(rmt_enable(rx_channel));
    //         // ESP_LOG_BUFFER_HEX(TAG, raw_symbols, 32);
    //         vTaskDelay(pdMS_TO_TICKS(600));
    //     }
    // }