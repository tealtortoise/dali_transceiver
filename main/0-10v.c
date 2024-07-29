
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <esp_clk_tree.h>
#include "driver/ledc.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "base.h"
#include "0-10v.h"

#define PWM_0_10v_FREQUENCY 10000
#define MAX_LEDC_CHANNELS 6

#define GENERIC_CAL_GAIN 0.8541
#define GENERIC_CAL_OFF_VOLTAGE 0.27
#define GENERIC_CAL_START_VOLTAGE 1.5
#define GENERIC_CAL_FINISH_VOLTAGE 9.1
#define GENERIC_CAL_FULL_VOLTAGE 10.0

static const char *TAG = "0-10v driver";

static QueueHandle_t ledc_channel_queue;

static int8_t pwm_resolution = -1;

#ifdef CONFIG_IDF_TARGET_ESP32S3

#define CLOCK_SOURCE SOC_MOD_CLK_APB
#define LEDC_CLOCK_SOURCE LEDC_APB_CLK
#endif // config target
#ifdef CONFIG_IDF_TARGET_ESP32C6

#define CLOCK_SOURCE SOC_MOD_CLK_PLL_F80M
#define LEDC_CLOCK_SOURCE LEDC_USE_PLL_DIV_CLK
#endif // config target

uint64_t starttime;
uint64_t et;

uint32_t get_pwm_duty(uint8_t level, uint16_t lut[]){
    return lut[level];
}

void built_lut(uint16_t lut[], int calibration, double gain){
    int max_value = (1 << pwm_resolution) - 1;
    lut[0] = 0;
    lut[254] = max_value;
    switch (calibration)
    {
        case CALIBRATION_PWM_LOG:
            double mult = (max_value) / (pow(1.027, 254.0));
            for (int i=1; i < 254; i++){
                lut[i] = clamp(pow(1.027, i-1) * mult - 1, 0, 0xffff);
            }
            break;
        case CALIBRATION_LOOKUP_NVS:
        case CALIBRATION_GENERIC_LOG_ELDOLED:
            ESP_LOGI(TAG, "Using gain %f for LUT", gain);
            double x;
            double diff = GENERIC_CAL_FINISH_VOLTAGE - GENERIC_CAL_START_VOLTAGE;
            double voltage;
            double dutydouble;
            for (int i=0; i < 255; i++){
                if (i == 0){
                    voltage = GENERIC_CAL_OFF_VOLTAGE;
                }
                else if (i == 254)
                {
                    voltage = GENERIC_CAL_FULL_VOLTAGE;
                }
                else
                {
                    x = ((double) i - 1.0) / 253.0;
                    voltage = x * diff + GENERIC_CAL_START_VOLTAGE;
                }
                dutydouble = voltage / 10.0 * gain;
                lut[i] = clamp(dutydouble * max_value, 0, 0xffff);
                if (i <3 || i > 251) {
                    ESP_LOGI(TAG, "0-10v Cal Input %d -> voltage %f, doubleduty %f, lut %u", i, voltage, dutydouble, lut[i]);
                }
            }
            break;
        default:
            for (int i=1; i < 254; i++){
                lut[i] = clamp(i * max_value / 254, 0, 0xffff);
            }
            break;
    }
}

void prepare_pwm_for_0_10v() {
    ledc_channel_queue = xQueueCreate(MAX_LEDC_CHANNELS, sizeof(uint8_t));
    for (int i = 0; i < MAX_LEDC_CHANNELS; i++) {
        xQueueSend(ledc_channel_queue, (uint8_t*) &i, 1);
    }
    uint32_t clkspeed;
    esp_clk_tree_src_get_freq_hz(CLOCK_SOURCE, ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT, &clkspeed);
    pwm_resolution = ledc_find_suitable_duty_resolution(clkspeed, PWM_0_10v_FREQUENCY);
}

esp_err_t setup_0_10v_channel(uint8_t gpio_pin, int calibration, zeroten_handle_t *handle){
    if (pwm_resolution == -1) prepare_pwm_for_0_10v();
    uint8_t channelnum;
    BaseType_t success = xQueueReceive(ledc_channel_queue, &channelnum,0);
    if (!success) return ESP_FAIL;

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = pwm_resolution,
        .freq_hz = PWM_0_10v_FREQUENCY,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_CLOCK_SOURCE,
    };

    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel_confstruct = {
        .channel    = channelnum,
        .duty       = 0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num   = gpio_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel  = LEDC_TIMER_0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_confstruct));

    double gain = GENERIC_CAL_GAIN;
    if (calibration == CALIBRATION_LOOKUP_NVS) {
        // ESP_ERROR_CHECK(nvs_set_i32(nvs_handle, "testint", 123));
        char key[24];
        build_nvs_key_for_gpio_gain(gpio_pin, key);
        ESP_LOGI(TAG, "NVS Key '%s'", key);
        int64_t channelgain_intrep;
        double channelgain;
        esp_err_t err = nvs_get_i64(nvs_handle_, key, &channelgain_intrep);
        if (err == ESP_OK) {
            channelgain = *(double*) &channelgain_intrep;
            ESP_LOGI(TAG, "Found gain %f in NVS for GPIO %d", channelgain, gpio_pin);
            gain = channelgain;
        }
        else
        {
            ESP_LOGI(TAG, "No gain found in NVS for GPIO %d, using generic %f", gpio_pin, gain);
        }
    }

    zeroten_handle_ *handlestruct = malloc(sizeof(zeroten_handle_));
    handlestruct->ledc_channel = channelnum;
    handlestruct->pwm_resolution = pwm_resolution;
    handlestruct->gpio_pin = gpio_pin;
    built_lut(handlestruct->lut, calibration, gain);

    *handle = (zeroten_handle_t) handlestruct;
    return ESP_OK;
}

esp_err_t set_0_10v_level(zeroten_handle_t handle, uint8_t level) {
    if (level == 255) return ESP_ERR_INVALID_ARG;
    zeroten_handle_* zhandle = (zeroten_handle_ *) handle;
    uint32_t duty = get_pwm_duty(level, zhandle->lut);
    esp_err_t returnval = ledc_set_duty(LEDC_LOW_SPEED_MODE, zhandle->ledc_channel, duty);
    // ESP_LOGI(TAG, "level %d -> duty %lu (res %i)", level, duty, pwm_resolution );
    returnval = returnval | ledc_update_duty(LEDC_LOW_SPEED_MODE, zhandle->ledc_channel);
    if (returnval) {
        ESP_LOGE(TAG, "0-10v LEDC set duty error: %u", level);
        return returnval;
    }
    return ESP_OK;
}

esp_err_t disable_pwm_channel(zeroten_handle_t handle){
    zeroten_handle_* zhandle = (zeroten_handle_ *) handle;
    return ledc_stop(LEDC_LOW_SPEED_MODE, zhandle->ledc_channel, 0);
    // return ESP_OK;
}