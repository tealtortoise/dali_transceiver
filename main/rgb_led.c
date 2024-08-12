
#include "rgb_led.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"
#include "gpio_utils.h"

#include "esp_log.h"

static const char *TAG = "rgb-led";

static led_strip_handle_t led_strip;



void rgb_led_task(void *params)
{
    device_status_t* status = (device_status_t*) params;
    uint8_t r=0;
    uint8_t g=0;
    uint8_t b=0;
    while (1)
    {
        led_strip_set_pixel(led_strip, 0, r, g, b);
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(10));
        if (0 && !status->power_on)
        {
            r = 50; g = 10; b = 0;
            continue;
        }
        r = status->lut[status->setpoint].r;
        g = status->lut[status->setpoint].g;
        b = status->lut[status->setpoint].b;
    }
}

void setup_rgb_led(device_status_t *status)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = 1, // at least one LED on board
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_LOGI(TAG, "Setting up RMT for LED strip...");
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_set_pixel(led_strip, 0, 0, 0, 0);
    led_strip_refresh(led_strip);
    xTaskCreate(rgb_led_task, "RGBLED", 4096, (void*) status, 1, NULL);
}