//
// Created by thea on 5/29/24.
//

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "dali.h"
#include "dali_edgeframe_parser.h"
// #include "edgeframe_logger.c"

static const char* PTAG = "dali_parser";

#define EDGEDECODE_STATE_INIT 0
#define EDGEDECODE_STATE_CHECKSTART 1
#define EDGEDECODE_STATE_BITREADY 2
#define EDGEDECODE_STATE_END 3

#define DALI_TIMING_IDEA_HALF_BIT_US 417
#define DALI_TIMING_TOLERANCE_US 150

#define DALI_FORWARD_FRAME_DURATION 15411
#define DALI_BACKWARD_FRAME_DURATION 8700
#define DALI_FRAME_DURATION_TOLERANCE 1500

bool check_if_half_period(uint16_t time) {
    return time < DALI_TIMING_IDEA_HALF_BIT_US + DALI_TIMING_TOLERANCE_US && time > DALI_TIMING_IDEA_HALF_BIT_US - DALI_TIMING_TOLERANCE_US;
}

bool check_if_full_period(uint16_t time) {
    return time < DALI_TIMING_IDEA_HALF_BIT_US * 2 + DALI_TIMING_TOLERANCE_US && time > DALI_TIMING_IDEA_HALF_BIT_US * 2 - DALI_TIMING_TOLERANCE_US;
}


int get_frame_type_from_duration(uint16_t duration){
    if ((duration > DALI_FORWARD_FRAME_DURATION - DALI_FRAME_DURATION_TOLERANCE)
        && (duration < DALI_FORWARD_FRAME_DURATION + DALI_FRAME_DURATION_TOLERANCE))
        {
            return DALI_FORWARD_FRAME_TYPE;
        };
    if ((duration < DALI_BACKWARD_FRAME_DURATION + DALI_FRAME_DURATION_TOLERANCE)
        && (duration > DALI_BACKWARD_FRAME_DURATION - DALI_FRAME_DURATION_TOLERANCE))
        {
            return DALI_BACKWARD_FRAME_TYPE;
        };
    return DALI_MANGLED_FRAME;
}

void edgeframe_queue_log_task(void* params) {
    // QueueHandle_t queue = (QueueHandle_t) params;
    parsestruct *pass = (parsestruct*) params;
    QueueHandle_t edgeinputqueue = pass->edgequeue;
    QueueHandle_t dalioutputqueue = pass->daliframequeue;
    bool debug = false;
    uint16_t frame_duration;
    edgeframe receivedframe;
    dali_frame_t outputframe;
    BaseType_t sendsuccess;
    uint8_t log;
    uint8_t record;
    uint8_t frameaction;
    // vTaskDelay(pdMS_TO_TICKS(8000));
    while (1) {
        bool received = xQueueReceive(edgeinputqueue, &receivedframe, 101);
        frame_duration = receivedframe.edges[receivedframe.length - 1].time;
        outputframe.type = get_frame_type_from_duration(frame_duration);

        switch (outputframe.type)
        {
        case DALI_FORWARD_FRAME_TYPE:
            frameaction = pass->config.forward_frame_action;
            break;
        case DALI_BACKWARD_FRAME_TYPE:
            frameaction = pass->config.backward_frame_action;
            break;
        default:
            ESP_LOGE(PTAG,"Unknown frame type %d", outputframe.type);
            frameaction = DALI_PARSER_ACTION_LOG;
        }
        log = frameaction & 1;
        record = frameaction & 2;

        if (received && log) {
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
                            error = true;
                        }

                        break;
                    }
                }
            }
            int baud_bits_sub = (output >> (output_bit_pos+1)) & 1 ? 0 : 1;
            // ESP_LOGD(PTAG, "Final output %lu", output);
            // ESP_LOGI(PTAG, "Final output  >> 8 %lu", output >> 8);
            // if (baud_time) ESP_LOGI(PTAG, "Baud rate %u", );
            // if (baud_time) ESP_LOGI(PTAG, "Baud rate %u", 1000000 * (baud_counter_bits) / baud_time);
            uint8_t firstbyte = (output & 0xFF0000) >> 16;
            uint8_t secondbyte = (output & 0xFF00) >> 8;
            outputframe.firstbyte = firstbyte;
            outputframe.secondbyte = secondbyte;
            if (error) outputframe.type = DALI_MANGLED_FRAME;

            if (record || (error && (pass->config.mangled_frame_action & 2))) {
                sendsuccess = xQueueSendToBack(dalioutputqueue, &outputframe, 0);
                if (sendsuccess != pdTRUE) ESP_LOGE(PTAG, "Dali output queue full");
            }

            log_dali_frame_prefix(outputframe, record ? "ENQUEUED: " : "OBSERVED: ");
        }
    }
}


QueueHandle_t start_dali_parser(QueueHandle_t edgequeue, dali_parser_config_t config) {
    QueueHandle_t daliframequeue = xQueueCreate(16, sizeof(dali_frame_t));
    // ESP_LOGI(PTAG, "Queue handle %u", (int)&daliframequeue);
    parsestruct *pass = malloc(sizeof(parsestruct));
    // const parsestruct pass = {
    //     .edgequeue = edgequeue,
    //     .daliframequeue = daliframequeue,
    //     .ignore_forward = ignore_forward
    // };
    pass->edgequeue = edgequeue;
    pass->daliframequeue = daliframequeue;
    pass->config = config;
    // while(1){
    //     vTaskDelay(10);
    // }
    // if (sendsuccess != pdTRUE) ESP_LOGE(PTAG, "Dali output queue full");
    xTaskCreate((void *)edgeframe_queue_log_task, "edgeframe_queue_log_task",
        9128, pass, 15, NULL);
    return daliframequeue;
}