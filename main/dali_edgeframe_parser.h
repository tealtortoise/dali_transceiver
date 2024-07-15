
#ifndef DEP_H
#define DEP_H
#include "freertos/queue.h"
#include "dali.h"

#define DALI_PARSER_FORWARD_FRAMES_IGNORE 1
#define DALI_PARSER_FORWARD_FRAMES_LOG_ONLY 0
#define DALI_PARSER_FORWARD_FRAMES_RECORD 2

#define DALI_PARSER_ACTION_IGNORE 0
#define DALI_PARSER_ACTION_LOG 1
#define DALI_PARSER_ACTION_RECORD 2
#define DALI_PARSER_ACTION_LOG_AND_RECORD 3

typedef struct {
    uint8_t forward_frame_action;
    uint8_t backward_frame_action;
    uint8_t mangled_frame_action;
} dali_parser_config_t;

typedef struct {
    QueueHandle_t edgequeue;
    QueueHandle_t daliframequeue;
    dali_parser_config_t config;
} parsestruct;

#pragma once
#endif

QueueHandle_t start_dali_parser(QueueHandle_t edgequeue, dali_parser_config_t config);
