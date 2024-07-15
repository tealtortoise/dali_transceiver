
#ifndef DALI_TRANSMIT_H
#define DALI_TRANSMIT_H

#include <stdint.h>
#include "esp_log.h"
#include "dali.h"

static const uint8_t DALI_ISR_STATE_IDLE = 0;
static const uint8_t DALI_ISR_STATE_STARTED = 1;
static const uint8_t DALI_ISR_STATE_BITPREPARE = 2;
static const uint8_t DALI_ISR_STATE_BITDATA = 3;
static const uint8_t DALI_ISR_STATE_ENDDATA = 4;
static const uint8_t DALI_ISR_STATE_STOP = 5;
static const uint8_t DALI_ISR_STATE_SETTLING = 6;


typedef struct {
    uint8_t gpio_pin;
    uint8_t state;
    uint16_t data;
    uint32_t notify_idx;
    TaskHandle_t notify_task;
    uint8_t bitpos;
    gptimer_handle_t timer;
    QueueHandle_t isr_queue;
    QueueHandle_t transmit_queue;
    uint8_t invert;
    gptimer_alarm_config_t alarmconf;
    uint16_t bitwork;
    BaseType_t taskawoken;
} dali_transmit_isr_ctx;

typedef struct {
    uint8_t number;
    gptimer_handle_t timer;
    dali_transmit_isr_ctx *ctx;
    QueueHandle_t queue;
    uint16_t frameidcounter;
} dali_transmitter_handle_t;

#define DALI_FRAME_ID_DONT_NOTIFY -1

#define DALI_NOTIFY_COMPLETE_INDEX 4


esp_err_t setup_dali_transmitter(uint8_t gpio_pin, uint8_t invert, uint16_t queuedepth, dali_transmitter_handle_t *handle);



#pragma once

#endif // ifndef DALI_TRANSMIT_H
