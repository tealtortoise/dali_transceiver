
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


#define DALI_FRAME_ID_DONT_NOTIFY -1

#define DALI_NOTIFY_COMPLETE_INDEX 4

void display_transmit_buffer_log_timings();

esp_err_t setup_dali_transmitter(uint8_t gpio_pin, uint8_t invert, uint16_t queuedepth, dali_transmitter_handle_t *handle);

#pragma once

#endif // ifndef DALI_TRANSMIT_H
