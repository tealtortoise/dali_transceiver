#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "0-10v.h"

void setup_button_interrupts(TaskHandle_t mainlooptask, zeroten_handle_t pwm1, zeroten_handle_t pwm2);

#endif // ifndef BUTTONS_H

