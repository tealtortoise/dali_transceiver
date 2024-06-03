

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "dali.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"

#define RECEIVE_DOUBLE_BIT_THRESHOLD 600

static const char* RRTAG = "dali_parser";

typedef struct {
    QueueHandle_t framequeue;
    rmt_channel_handle_t channel;
    dali_frame_t outgoing;
    dali_rmt_received_frame_t template;
    rmt_receive_config_t receive_config;
} dali_rmt_receiver_ctx;

static bool IRAM_ATTR example_rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    dali_rmt_receiver_ctx *ctx = (dali_rmt_receiver_ctx*) user_data;
    ctx->template.num_symbols = edata->num_symbols;
    // memcpy(ctx->template.received_symbols, edata->received_symbols,
        // sizeof(rmt_symbol_word_t) * RMT_RECEIVE_BUFFER_SIZE_SYMBOLS);
    xQueueSendFromISR(ctx->framequeue, &ctx->template, &high_task_wakeup);

    rmt_receive(ctx->channel,
        &ctx->template.received_symbols,
        sizeof(rmt_symbol_word_t) * RMT_RECEIVE_BUFFER_SIZE_SYMBOLS,
        &ctx->receive_config);
    // ESP_DRAM_LOGI(RRTAG, "ISR %u", edata->num_symbols);
    return high_task_wakeup == pdTRUE;
}

void rmt_queue_log_task(void* params){
    QueueHandle_t queue = (QueueHandle_t) params;
    dali_rmt_received_frame_t receivestruct;
    uint64_t manchester_word;
    uint8_t manchester_bits[64];
    uint32_t final_word;
    uint8_t bit_position;
    uint8_t firstbyte;
    uint8_t secondbyte;
    bool a;
    bool b;
    bool error;

    vTaskDelay(pdMS_TO_TICKS(5000));
    while (1) {

        bool received = xQueueReceive(queue, &receivestruct, 101);
        // if (received) ESP_LOGI(RRTAG, "received %u", receivestruct.num_symbols);

        if (received) {
            error = false;
            for (int round = 0;round < 2; round++) {
                // round 1 repeats round 0 but with logging in case of errors

                // reset variables
                manchester_word = 0;
                bit_position = 0;
                for (int i=0; i< 64; i++) {
                    manchester_bits[i] = 0;
                }
                if (round > 0) {
                    ESP_LOGI(RRTAG, "Received RMT message...");
                }
                rmt_symbol_word_t *symbols = receivestruct.received_symbols;
                for (int i = 0; i < receivestruct.num_symbols; i++) {
                    rmt_symbol_word_t symbol = symbols[i];
                    if (round > 0) {
                        ESP_LOGI(RRTAG, "   Received RMT frame %u: %u, %u; %u",
                            symbol.level0,
                            symbol.duration0,
                            symbol.level1,
                            symbol.duration1);
                    }
                    manchester_word = manchester_word | ((uint64_t)symbol.level0 << bit_position);
                    manchester_bits[bit_position] = symbol.level0;
                    bit_position += 1;
                    if (symbol.duration0 > RECEIVE_DOUBLE_BIT_THRESHOLD) {
                        // duration encompasses 2 manchester half-bits so
                        manchester_word = manchester_word | ((uint64_t)symbol.level0 << bit_position);
                        manchester_bits[bit_position] = symbol.level0;
                        bit_position += 1;
                    }
                    manchester_word = manchester_word | ((uint64_t)symbol.level1 << bit_position);
                    manchester_bits[bit_position] = symbol.level1;
                    bit_position += 1;
                    if (symbol.duration1 > RECEIVE_DOUBLE_BIT_THRESHOLD) {
                        // duration encompasses 2 manchester half-bits
                        manchester_word = manchester_word | ((uint64_t)symbol.level1 << bit_position);
                        manchester_bits[bit_position] = symbol.level1;
                        bit_position += 1;
                    }

                }

                if (round > 0) {
                    char bitstring[50];
                    int spaceadd = 0;
                    int bitsroundedto8 = ((bit_position >> 3) << 3) + 8;
                    for (int i = 0; i<bitsroundedto8;i++) {
                        if (i > 0 && ((bitsroundedto8 - i) % 8 == 0)) {
                            bitstring[i + spaceadd] = 32;
                            spaceadd += 1;
                        }
                        bitstring[i + spaceadd] = ((manchester_word >> (bitsroundedto8 - 1 - i)) & 1) ? 49 : 48;
                    }
                    bitstring[40 + spaceadd] = 0;
                    ESP_LOGI(RRTAG, "Bitstring %s", bitstring);
                    ESP_LOGI(RRTAG, "");
                }

                final_word = 0;
                a = 0;
                b = 0;
                for (uint8_t i = 0; i <16; i += 1) {
                    a = manchester_bits[i * 2 + 2];
                    b = manchester_bits[i * 2 + 3];
                    if (a != b) {
                        final_word = final_word | ((1 - b) << (15-i));
                    }
                    else {
                        ESP_LOGE(RRTAG, "Bit error");
                        error = true;
                    }
                }
                secondbyte = final_word & 0xFF;
                firstbyte = (final_word & 0xFF00) >> 8;
                if (round >= 0) {
                    ESP_LOGI(RRTAG, "DALI RMT Received first byte %u", firstbyte);
                    ESP_LOGI(RRTAG, "DALI RMT Received second byte %u", secondbyte);
                }
                if (!error) break;
                // if (firstbyte == receivestruct.outgoing.firstbyte && secondbyte == receivestruct.outgoing.secondbyte) {
                    // ESP_LOGI(RRTAG, "First byte matches");
                    // ESP_LOGI(RRTAG, "Second byte matches");
                    // break;
                // }
            }
        }

    }
}


QueueHandle_t setup_rmt_dali_receiver(uint8_t gpio_pin, uint8_t queuedepth) {
    ESP_LOGI(RRTAG, "create RMT RX channel");
    rmt_rx_channel_config_t rx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,
        .mem_block_symbols = RMT_RECEIVE_BUFFER_SIZE_SYMBOLS, // amount of RMT symbols that the channel can store at a time
        .gpio_num = gpio_pin,
    };
    rmt_channel_handle_t rx_channel;
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_channel_cfg, &rx_channel));

    // ESP_LOGI(TAG, "register RX done callback");
    QueueHandle_t receive_queue = xQueueCreate(queuedepth, sizeof(dali_rmt_received_frame_t));
    assert(receive_queue);
    // rmt_symbol_word_t *rmt_symbol_buffer = malloc(RMT_RECEIVE_BUFFER_SIZE_SYMBOLS * sizeof(rmt_symbol_word_t));
    dali_rmt_receiver_ctx *ctx = heap_caps_malloc(sizeof(dali_rmt_receiver_ctx), MALLOC_CAP_INTERNAL);

    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = example_rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel, &cbs, ctx));

    ctx->channel = rx_channel;
    ctx->framequeue = receive_queue;

    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 1250,
        .signal_range_max_ns = 12000000,
    };
    ctx->receive_config = receive_config;

    ESP_ERROR_CHECK(rmt_enable(ctx->channel));
    ESP_ERROR_CHECK(rmt_receive(ctx->channel,
        ctx->template.received_symbols,
        sizeof(rmt_symbol_word_t) * RMT_RECEIVE_BUFFER_SIZE_SYMBOLS,
        &ctx->receive_config));
    return receive_queue;
}

void setup_rmt_log_task(QueueHandle_t queue) {
    xTaskCreate((TaskFunction_t *) rmt_queue_log_task, "rmt_queue_log_task", 9128, queue, 1, NULL);
}