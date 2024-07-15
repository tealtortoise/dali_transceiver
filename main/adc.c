
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "adc.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "base.h"
#include "gpio_utils.h"


#define HIGH_RESISTANCE 20000
#define VOLTAGE_CAP 3230

#define SIGN_POSITIVE (1)
#define SIGN_NEGATIVE (0)
#define SIGN_UNDEFINED (2)

static const char* TAG = "ADC";


static int adc_raw[2][10];
static int raw_cal_voltage[2][10];  


int resistance_to_level(int resistance){
    int raw = sqrt(resistance * 0xFFFF / HIGH_RESISTANCE);
    return clamp(raw, 0, 254);
    // return min((int) (-log2f((float) (resistance + 1) / (float) HIGH_RESISTANCE) * 16.0), 254);
}

void adc_task(void *params) {
    light_adc_config_t *config = (light_adc_config_t*) params;

    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc_handle));
    
    adc_oneshot_chan_cfg_t adc_chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, AIN_GPIO, &adc_chan_config));
    
    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = AIN_GPIO,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle));
    
    int accepted_voltage = 0;
    int current_light_level = 0;
    int last_sent_setpoint = 0;
    int potential_new_setpoint = 0;
    int last_sign = -1;
    int sign = SIGN_UNDEFINED;
    int samples_different = 0;
    int cal_voltage;
    int difference;
    int accumulator = 0;
    int average_voltage;
    int resistor = 0;
    int capped = 0;
    bool update = false;
    uint64_t us_per_cycle;
    // struct timeval time;
    // gettimeofday(&time, NULL);
    // uint64_t start_us = (int64_t)time.tv_sec * 1000000L + (int64_t)time.tv_usec;
    uint64_t cycles = 0;
    while (1){
        vTaskDelay(1);
        
        cycles += 1;
        if ((cycles & 0xFFF) == 0){
            accepted_voltage = 0;
        }
        
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, AIN_GPIO, &adc_raw[0][0]));
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, adc_raw[0][0], &raw_cal_voltage[0][0]));
        cal_voltage = raw_cal_voltage[0][0];

        // ESP_LOGI(TAG, "ADC Raw/Cali Voltage: %d mV / %d mV (current %i mV, samples_different %i)", adc_raw[0][0], cal_voltage, accepted_voltage, samples_different);
        difference = cal_voltage - accepted_voltage;
        
        // ESP_LOGI(TAG,"voltage %i , %i, %i, %i",cal_voltage, samples_different, difference, accumulator);
        if (abs(difference) < config->tolerance){
            samples_different = 0;
            accumulator = 0;
            last_sign = SIGN_UNDEFINED;
            continue;
        }
        samples_different += 1;
        if (samples_different >= config->sampled_needed){
            samples_different = -config->average_window;
        }
        if (samples_different < 0) {
            // we're averaging for a new value to update with
            accumulator += cal_voltage;
            if (samples_different == -1){
                // averaging window completed
                average_voltage = min(accumulator / (config->average_window), VOLTAGE_CAP);
                resistor =   (average_voltage * 1000) / (VOLTAGE_CAP + 1 - average_voltage);
                potential_new_setpoint = resistance_to_level(min(resistor, HIGH_RESISTANCE));
                
                // ESP_LOGI(TAG, "Found different value %i, resistor %i -> level %i (S %i, %i)", average_voltage, resistor, potential_new_setpoint, sign, last_sign);

                // get current light level
                // ESP_LOGI(TAG, "Current setpoint seems to be %i, last sent %i", setpoint, last_sent_setpoint);
                if (last_sent_setpoint == setpoint || setpoint == -1){
                    // last update was probably from ADC so can just update light level
                    update = true;
                }
                else
                {
                    // updated elsewhere, wait for sweep past current value
                    sign = potential_new_setpoint > setpoint;
                    // ESP_LOGI(TAG, "ADC value not consistent with current light level (S %i %i)", sign, last_sign);
                    if ((last_sign + sign) == 1){
                        // we've switched signs so likely passed the current level so we can update safely
                        update = true;
                    }
                    else
                    {
                        //we try again next time
                        last_sign = sign;
                    }
                }
                if (update && potential_new_setpoint != setpoint && average_voltage < VOLTAGE_CAP) {
                    setpoint = potential_new_setpoint;
                    // ESP_LOGI(TAG,"UPDATED ! voltage %i , samples_different %i, difference %i, acc %i, setpoint %i", cal_voltage, samples_different, difference, accumulator, setpoint);
                    xTaskNotifyIndexed(config->notify_task, SETPOINT_SLEW_NOTIFY_INDEX, USE_DEFAULT_FADETIME, eSetValueWithOverwrite);
                    last_sent_setpoint = potential_new_setpoint;
                    last_sign = SIGN_UNDEFINED;
                }
                if (average_voltage >= VOLTAGE_CAP &&
                    cycles < (config->sampled_needed + config->average_window + 1)) 
                    {
                        ESP_LOGW(TAG, "ADC voltage cap reached - not updating setpoint from now on");
                    }
                update = false;
                accepted_voltage = average_voltage;
                samples_different = 0;
                accumulator = 0;
                }
        }
    }
}


void setup_adc(light_adc_config_t config){
    TaskHandle_t adc_task_handle;

    light_adc_config_t *copyconfig = malloc(sizeof(light_adc_config_t));
    memcpy(copyconfig, &config, sizeof(light_adc_config_t));
    xTaskCreate(adc_task, "ADC Monitor",2048, copyconfig, 1, &adc_task_handle);
    
}