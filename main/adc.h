#ifndef ADH_H
#define ADC_H

#include <stdint.h>

#include "base.h"

void adc_task();

typedef struct {
    TaskHandle_t notify_task;
    int tolerance;
    int sampled_needed;
    int average_window;
    setpoint_notify_t *current_setpoint;
} light_adc_config_t;

void setup_adc(light_adc_config_t config);

#endif // ADC_H

