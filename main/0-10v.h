
#ifndef ZEROTEN_H
#define ZEROTEN_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/ledc.h"

#define CALIBRATION_LINEAR 1 // simple linear 0-10v ( not implemented )
#define CALIBRATION_GENERIC_LOG_ELDOLED 2 // 0-10v output designed for eldoled driver programmed with log curve
#define CALIBRATION_PWM_LOG 3 // logarithmic output designed for directly modulating LED down to 0.1%
#define CALIBRATION_LOOKUP_NVS 4 // As generic eldoled but with gain calibration if stored in flash

typedef struct {
    uint16_t lut[255];
    ledc_channel_t ledc_channel;
    int pwm_resolution;
    int gpio_pin;
} zeroten_handle_;

typedef struct zeroten_handle_* zeroten_handle_t;

esp_err_t setup_0_10v_channel(uint8_t gpio_pin, int calibration, zeroten_handle_t *handle);
esp_err_t set_0_10v_level(zeroten_handle_t handle, uint8_t level);
esp_err_t disable_pwm_channel(zeroten_handle_t handle);
#pragma once

#endif