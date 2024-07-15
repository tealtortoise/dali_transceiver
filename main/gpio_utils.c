#include "gpio_utils.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "GPIO_UTILS";

uint8_t read_dip_switches(){
    return gpio_get_level(DIP1_GPIO) | 
        (gpio_get_level(DIP2_GPIO) << 1) |
        (gpio_get_level(DIP3_GPIO) << 2) |
        (gpio_get_level(DIP4_GPIO) << 3);
}

void log_buttons(){
    int but1 = gpio_get_level(BUT1_GPIO);
    int but2 = gpio_get_level(BUT2_GPIO);
    int but3 = gpio_get_level(BUT3_GPIO);
    char* state = but1 ? NOT_PRESSED_MESSAGE : PRESSED_MESSAGE;
    ESP_LOGI(TAG, "Button 1 is %s", state);
    state = but2 ? NOT_PRESSED_MESSAGE : PRESSED_MESSAGE;
    ESP_LOGI(TAG, "Button 2 is %s", state);
    state = but3 ? NOT_PRESSED_MESSAGE : PRESSED_MESSAGE;
    ESP_LOGI(TAG, "Button 3 is %s", state);

}

void rotate_gpio_outputs_forever(){
    int state = 1;
    int output_idx = 0;
    uint8_t dip_address;
    while (1) {
        for (int i=0; i < OUTPUT_PIN_COUNT; i++){
            ESP_LOGI(TAG, "Setting %s (%i) to %i", OUTPUT_PIN_NAMES[i], OUTPUT_PINS[i], state);
            gpio_set_level(OUTPUT_PINS[i], state);
            ESP_LOGI(TAG, "DALI Monitor level %i",gpio_get_level(RX_GPIO));
            vTaskDelay(2);
            vTaskDelay(240);
            log_buttons();
            vTaskDelay(240);
            dip_address = read_dip_switches();
            ESP_LOGI(TAG, "Read DIP Switch address: %d", dip_address);
        }
        state = 1 - state;
    }
}