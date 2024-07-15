#include "ledflash.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

void flashtask(void* pass){
    ledflash_params_t *params = (ledflash_params_t*) pass;
    int delay = pdMS_TO_TICKS(params->periodMS);
    uint8_t state = 1;
    while (1){
        state = 1 - state;
        gpio_set_level(params->pin, state);
        vTaskDelay(delay);
    }
}

TaskHandle_t setup_flash_led(uint8_t pin, int periodMS){
    ledflash_params_t* params = malloc(sizeof(ledflash_params_t));
    params->pin = pin;
    params->periodMS = periodMS;

    TaskHandle_t handle;
    xTaskCreate(flashtask, "flashtask", 1024, params, 1, &handle);
    return handle;
}