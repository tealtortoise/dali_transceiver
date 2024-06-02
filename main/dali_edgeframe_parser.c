//
// Created by thea on 5/29/24.
//

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
// #include "edgeframe_logger.c"

static const char* PTAG = "dali_parser";

#define EDGEDECODE_STATE_INIT 0
#define EDGEDECODE_STATE_CHECKSTART 1
#define EDGEDECODE_STATE_BITREADY 2
#define EDGEDECODE_STATE_END 3

#define DALI_TIMING_IDEA_HALF_BIT_US 417
#define DALI_TIMING_TOLERANCE_US 150

bool check_if_half_period(uint16_t time) {
    return time < DALI_TIMING_IDEA_HALF_BIT_US + DALI_TIMING_TOLERANCE_US && time > DALI_TIMING_IDEA_HALF_BIT_US - DALI_TIMING_TOLERANCE_US;
}

bool check_if_full_period(uint16_t time) {
    return time < DALI_TIMING_IDEA_HALF_BIT_US * 2 + DALI_TIMING_TOLERANCE_US && time > DALI_TIMING_IDEA_HALF_BIT_US * 2 - DALI_TIMING_TOLERANCE_US;
}

void edgeframe_queue_log_task(void* params) {
    QueueHandle_t queue = (QueueHandle_t) params;
    bool debug = false;
    edgeframe receivedframe;
    // vTaskDelay(pdMS_TO_TICKS(8000));
    while (1) {
        bool received = xQueueReceive(queue, &receivedframe, 101);
        // received = false;
        if (received) {
            ESP_LOGI(PTAG, "Received frame length %d", receivedframe.length);
            uint8_t state = 0;
            uint32_t output = 0;
            uint8_t output_bit_pos = 23;
            uint16_t first_bit_time = 9999;
            uint16_t baud_time=0;
            uint16_t baud_counter_bits_count = 0;
            uint16_t baud_counter_bits = 0;
            edge_t edge;
            uint16_t last_valid_bit_time;
            uint16_t last_valid_bit_elapsed;
            uint16_t last_edge_elapsed = 0;
            bool error = false;
            for (uint8_t i = 0; i<receivedframe.length; i++) {
                if(debug) {
                    ESP_LOGI(PTAG, "Level %d at %u us (%u us) state %d",
                        receivedframe.edges[i].edgetype,
                        receivedframe.edges[i].time,
                        receivedframe.edges[i].time - last_edge_elapsed,
                        state);
                }
                last_edge_elapsed = receivedframe.edges[i].time;
                if (error || state == EDGEDECODE_STATE_END) break;
                edge = receivedframe.edges[i];
                if (edge.edgetype == EDGETYPE_RISING) baud_time = edge.time - first_bit_time;
                switch (state) {
                    case EDGEDECODE_STATE_INIT: {
                        state = EDGEDECODE_STATE_CHECKSTART;
                        break;
                    }
                    case EDGEDECODE_STATE_CHECKSTART: {
                        if (edge.edgetype == EDGETYPE_RISING && check_if_half_period(edge.time)) {
                            state = EDGEDECODE_STATE_BITREADY;
                            last_valid_bit_time = edge.time;

                            first_bit_time = edge.time;
                        }
                        else
                        {
                            ESP_LOGE(PTAG, "Start bit error %u", i);
                            error = true;
                        }
                        break;
                    }
                    case EDGEDECODE_STATE_BITREADY: {

                        if (edge.edgetype == EDGETYPE_NONE) {
                            state = EDGEDECODE_STATE_END;
                            break;
                        }
                        last_valid_bit_elapsed = edge.time - last_valid_bit_time;
                        if (check_if_half_period(last_valid_bit_elapsed)) {
                            if (debug) ESP_LOGD(PTAG, "...Half bit %u", i);
                            // do nothing
                        }
                        else if (check_if_full_period(last_valid_bit_elapsed))
                        {
                            if (debug) ESP_LOGD(PTAG, "...Full bit %u edge %d bitpos %d", i, edge.edgetype, output_bit_pos);
                            if (edge.edgetype == EDGETYPE_RISING) {
                                output = output | (1 << output_bit_pos);
                            }
                            last_valid_bit_time = edge.time;
                            output_bit_pos -= 1;
                        }
                        else if (last_valid_bit_elapsed < 40) {
                            // assume glitch
                        }
                        else
                        {
                            ESP_LOGE(PTAG, "No bit error %u elapsed %u", i, last_valid_bit_elapsed);
                            // error = true;
                        }

                        break;
                    }
                }
            }
            int baud_bits_sub = (output >> (output_bit_pos+1)) & 1 ? 0 : 1;
            ESP_LOGD(PTAG, "Final output %lu", output);
            ESP_LOGI(PTAG, "Final output  >> 8 %lu", output >> 8);
            if (baud_time) ESP_LOGI(PTAG, "Baud rate %u", 500000 * (46 - output_bit_pos * 2 + baud_bits_sub) / baud_time);
            // if (baud_time) ESP_LOGI(PTAG, "Baud rate %u", 1000000 * (baud_counter_bits) / baud_time);
            uint8_t firstbyte = (output & 0xFF0000) >> 16;
            uint8_t secondbyte = (output & 0xFF00) >> 8;
            ESP_LOGI(PTAG, "First %d, second %d", firstbyte, secondbyte);
            char bitstring[30];
            // bitstring[24] = 0;
            int spaceadd = 0;
            for (int i = 0; i<16;i++) {
                if (i == 8) {
                    spaceadd += 1;
                    bitstring[i] = 32;
                }
                bitstring[i + spaceadd] = ((output >> (23 - i)) & 1) ? 49 : 48;
            }
            bitstring[16 + spaceadd] = 0;
            ESP_LOGI(PTAG, "Bitstring %s", bitstring);
            ESP_LOGI(PTAG, "");
                // ESP_LOGI(PTAG, "Level %d after %u us",
                    // receivedframe.edges[i].edgetype,
                    // receivedframe.edges[i].time);
            // }
        }
    }
}

void start_dali_parser(QueueHandle_t queue) {
    xTaskCreate((void *)edgeframe_queue_log_task, "edgeframe_queue_log_task",
        9128, queue, 1, NULL);
}