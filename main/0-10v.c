
#include <esp_clk_tree.h>

#include "driver/ledc.h"
#include "esp_log.h"

#define PWM_0_10v_FREQUENCY 10000
#define LOW_DUTY_8_BIT 0
#define HIGH_DUTY_8_BIT 255
#define MAX_LEDC_CHANNELS 6

static const char *LTAG = "0-10v driver";

static QueueHandle_t ledc_channel_queue;

static uint8_t pwm_resolution;

uint32_t get_pwm_duty(uint16_t level){
    // level range 0 - 0xffff: 0 is off
    if (level == 0) return 0;

    const uint16_t mult = (HIGH_DUTY_8_BIT - LOW_DUTY_8_BIT) << 8;
    uint32_t intermediate = (LOW_DUTY_8_BIT << 24) + level * mult;
    return intermediate >> (32 -  pwm_resolution);
}

void setup_pwm_0_10v() {
    ledc_channel_queue = xQueueCreate(MAX_LEDC_CHANNELS, sizeof(uint8_t));
    for (int i = 0; i < MAX_LEDC_CHANNELS; i++) {
        xQueueSend(ledc_channel_queue, (uint8_t*) &i, 1);
    }
    uint32_t clkspeed;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_APB, ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT, &clkspeed);
    pwm_resolution = ledc_find_suitable_duty_resolution(clkspeed, PWM_0_10v_FREQUENCY);
}

esp_err_t configure_pwm_0_10v(uint8_t gpio_pin, uint16_t initial_level, ledc_channel_t *ledc_channel){
    uint8_t channelnum;
    BaseType_t success = xQueueReceive(ledc_channel_queue, &channelnum,0);
    if (success != pdTRUE) return ESP_FAIL;

    *ledc_channel = channelnum;

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = pwm_resolution,
        .freq_hz = PWM_0_10v_FREQUENCY,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_APB_CLK,
    };

    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel_confstruct = {
        .channel    = *ledc_channel,
        .duty       = get_pwm_duty(initial_level),
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num   = gpio_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel  = LEDC_TIMER_0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_confstruct));
    return ESP_OK;
}

void update_0_10v_level(uint8_t channel, uint16_t level) {
    uint16_t duty = get_pwm_duty(level);
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel));
}
