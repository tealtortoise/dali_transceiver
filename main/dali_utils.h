#ifndef DALI_UTILS_H
#define DALI_UTILS_H

#include "dali.h"
#include <stdint.h>
#include "esp_log.h"

#include "dali_transceiver.h"

esp_err_t dali_assign_short_addresses(dali_transceiver_handle_t handle, int start_address);

#pragma once

#endif
