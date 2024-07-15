#ifndef ADH_H
#define ADC_H

#include <stdint.h>


void adc_task();

typedef struct {
    TaskHandle_t notify_task;
    int tolerance;
    int sampled_needed;
    int average_window;
} light_adc_config_t;

void setup_adc(light_adc_config_t config);

#endif // ADC_H

