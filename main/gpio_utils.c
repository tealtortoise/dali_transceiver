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

int get_and_log_buttons(){
    int but1 = gpio_get_level(BUT1_GPIO);
    int but2 = gpio_get_level(BUT2_GPIO);
    int but3 = gpio_get_level(BUT3_GPIO);
    char* state = but1 ? NOT_PRESSED_MESSAGE : PRESSED_MESSAGE;
    ESP_LOGI(TAG, "Button 1 is %s", state);
    state = but2 ? NOT_PRESSED_MESSAGE : PRESSED_MESSAGE;
    ESP_LOGI(TAG, "Button 2 is %s", state);
    state = but3 ? NOT_PRESSED_MESSAGE : PRESSED_MESSAGE;
    ESP_LOGI(TAG, "Button 3 is %s", state);
    return 7 ^ (but1 | (but2 << 1) | (but3 << 2));
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
            get_and_log_buttons();
            vTaskDelay(240);
            dip_address = read_dip_switches();
            ESP_LOGI(TAG, "Read DIP Switch address: %d", dip_address);
        }
        state = 1 - state;
    }
}

gpio_config_t output_config = {
    .pin_bit_mask = OUTPIUT_PIN_MAP,
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};

gpio_config_t input_config = {
    .pin_bit_mask = INPUT_PIN_MAP,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
};

void configure_output_pin(uint8_t pin, uint8_t state){
    output_config.pin_bit_mask = 1 << pin;
    ESP_ERROR_CHECK(gpio_config(&output_config));
    ESP_ERROR_CHECK(gpio_set_drive_capability(pin, GPIO_DRIVE_CAP_0));
    gpio_set_level(pin, state);
}

void configure_input_pin(uint8_t pin, uint8_t wpu, uint8_t intr){
    input_config.pin_bit_mask = 1 << pin;
    input_config.pull_up_en = wpu;
    input_config.intr_type = intr;
    ESP_ERROR_CHECK(gpio_config(&input_config));
}

void configure_gpio(){
    gpio_install_isr_service(0);
    configure_output_pin(RELAY1_GPIO, 0);
    configure_output_pin(RELAY2_GPIO, 0);
    configure_output_pin(LED1_GPIO, 0);
    configure_output_pin(LED2_GPIO, 0);
    configure_output_pin(PWM_010v_GPIO, 0);
    configure_output_pin(PWM_010v2_GPIO, 0);
    configure_output_pin(TX_GPIO, 1);

    configure_input_pin(RX_GPIO, 0, GPIO_INTR_POSEDGE);
    configure_input_pin(AIN_GPIO, 0, GPIO_INTR_DISABLE);
    configure_input_pin(EXT1_GPIO, 0, GPIO_INTR_DISABLE);

    configure_input_pin(BUT1_GPIO, 1, GPIO_INTR_DISABLE);
    configure_input_pin(BUT2_GPIO, 1, GPIO_INTR_DISABLE);
    configure_input_pin(BUT3_GPIO, 1, GPIO_INTR_DISABLE);
    configure_input_pin(DIP1_GPIO, 1, GPIO_INTR_DISABLE);
    configure_input_pin(DIP2_GPIO, 1, GPIO_INTR_DISABLE);
    configure_input_pin(DIP3_GPIO, 1, GPIO_INTR_DISABLE);
    configure_input_pin(DIP4_GPIO, 1, GPIO_INTR_DISABLE);
}
