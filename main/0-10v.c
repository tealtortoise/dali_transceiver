
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <esp_clk_tree.h>
#include "driver/ledc.h"
#include "esp_log.h"
#include "base.h"
#include "0-10v.h"

#define PWM_0_10v_FREQUENCY 10000
// #define OFF_DUTY_8_BIT 7
// #define LOW_DUTY_8_BIT 14
// #define HIGH_DUTY_8_BIT 250
#define OFF_DUTY_8_BIT 0
#define LOW_DUTY_8_BIT 0
#define HIGH_DUTY_8_BIT 255
#define MAX_LEDC_CHANNELS 6

#define LUT_SIZE 256
static DRAM_ATTR uint8_t lut_x[LUT_SIZE];
static DRAM_ATTR uint16_t lut_y[LUT_SIZE];

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

uint32_t get_pwm_duty(uint16_t level, bool non_linearise){
    // level range 0 - 0xffff: 0 is off
    // if (level == 0) return OFF_DUTY_8_BIT << (pwm_resolution - 8);
    starttime = get_system_time_us(0);
    int intermediate_level = lut_y[(level >> 8)];
    et = get_system_time_us(starttime);
    ESP_LOGI(TAG, "get_PWM_DUTY took %llu us", et);
    // ESP_LOGI(TAG, "level %i -> %i", level, intermediate_level);

    // const uint16_t mult = (HIGH_DUTY_8_BIT - LOW_DUTY_8_BIT) << 8;
    // uint32_t intermediate_duty = (LOW_DUTY_8_BIT << 16) + intermediate_level * mult;
    // return intermediate_duty >> (32 -  pwm_resolution);
    return intermediate_level;
}

void build_log_lookups(){
    int x;
    float mult = ((1 << pwm_resolution) - 1) / powf(1.027, 255);
    for (int i=0; i < LUT_SIZE; i++){

        x = i * (256 / LUT_SIZE);
        lut_x[i] = x;
        lut_y[i] = clamp(powf(1.027, x) * mult, 0, 0xffff);
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
    build_log_lookups();
}

esp_err_t setup_0_10v_channel(uint8_t gpio_pin, uint16_t initial_level, ledc_channel_t *ledc_channel){
    if (pwm_resolution == -1) prepare_pwm_for_0_10v();
    uint8_t channelnum;
    BaseType_t success = xQueueReceive(ledc_channel_queue, &channelnum,0);
    if (!success) return ESP_FAIL;

    *ledc_channel = channelnum;

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = pwm_resolution,
        .freq_hz = PWM_0_10v_FREQUENCY,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_CLOCK_SOURCE,
    };

    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel_confstruct = {
        .channel    = *ledc_channel,
        .duty       = get_pwm_duty(initial_level, false),
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num   = gpio_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel  = LEDC_TIMER_0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_confstruct));
    return ESP_OK;
}

esp_err_t set_0_10v_level(uint8_t channel, uint16_t level, bool non_linearise) {
    uint16_t duty = get_pwm_duty(level, non_linearise);
    esp_err_t returnval = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ESP_LOGI(TAG, "level %u -> duty %u (res %i)", level, duty, pwm_resolution );
    returnval = returnval | ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
    if (returnval) {
        ESP_LOGE(TAG, "0-10v LEDC set duty error: %u", level);
        return returnval;
    }
    return ESP_OK;
}
