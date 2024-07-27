#ifndef DALI_UTILS_H
#define DALI_UTILS_H

#include "dali.h"
#include <stdint.h>
#include "esp_log.h"

#include "dali_transceiver.h"

typedef struct {
    uint8_t command;
    uint8_t address;
    uint8_t value;
    uint8_t extra;
} dali_command_t;

esp_err_t dali_assign_short_addresses(dali_transceiver_handle_t handle, int start_address);

QueueHandle_t dali_setup_command_queue(dali_transceiver_handle_t handle);

#pragma once

#endif
