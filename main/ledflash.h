

#ifndef LEDFLASH_H
#define LEDFLASH_H
#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

typedef struct {
    uint8_t pin;
    int periodMS;
} ledflash_params_t;

TaskHandle_t setup_flash_led(uint8_t pin, int periodMS);

#endif //LEDFLASH_H

