#include "gpio_utils.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "GPIO_UTILS";

#define RELAY_TURNOFF_DELAY_S 20
#define RELAY_LOCKOUT_TIME_S 5


inline int32_t _MAX(int32_t a, int32_t b) { return((a) > (b) ? a : b); }
inline int32_t _MIN(int32_t a, int32_t b) { return((a) < (b) ? a : b); }

uint8_t read_dip_switches(){
    return gpio_get_level(DIP1_GPIO) | 
        (gpio_get_level(DIP2_GPIO) << 1) |
        (gpio_get_level(DIP3_GPIO) << 2) |
        (gpio_get_level(DIP4_GPIO) << 3);
}

typedef struct {
    int configbits;
    int level1;
    int level2;
    int turnoff_delay;
} relay_timeout_data_t;

static relay_timeout_data_t timeout_data;

#define CONFIGBIT_USE_RELAY1 0x1
#define CONFIGBIT_USE_RELAY2 0x2

void relay_timeout_task(void* params) {
    int counter1 = 0;
    int counter2 = 0;
    int offcounter1 = 0;
    int offcounter2 = 0;
    int last1 = 0;
    int current1 = 0;
    int last2 = 0;
    int current2 = 0;
    int configbits;
    while (1) {
        configbits = timeout_data.configbits;
        offcounter1 = _MAX(offcounter1 - 1, 0);
        offcounter2 = _MAX(offcounter2 - 1, 0);
        if (!timeout_data.level1) {
            counter1 = counter1 > 0 ? counter1 - 1 : 0;
        }
        else {
            counter1 = RELAY_TURNOFF_DELAY_S << 2;
        };
        if (!timeout_data.level2) {
            counter2 = counter2 > 0 ? counter2 - 1 : 0;
        }
        else
        {
            counter2 = RELAY_TURNOFF_DELAY_S << 2;
        }
        if (!(configbits & CONFIGBIT_USE_RELAY1)) counter1 = 0;
        if (!(configbits & CONFIGBIT_USE_RELAY2)) counter2 = 0;
        last1 = current1;
        last2 = current2;
        current1 = counter1 > 0 && !offcounter1;
        current2 = counter2 > 0 && !offcounter2;
        gpio_set_level(RELAY1_GPIO, current1);
        gpio_set_level(RELAY2_GPIO, current2);
        // ESP_LOGI(TAG, "%i, %i, %i, %i", counter1, counter2,  current1,current2);
        if (current1 < last1) {
            ESP_LOGI(TAG, "Relay 1 Now OFF");
            offcounter1 = RELAY_LOCKOUT_TIME_S << 2;
        };
        if (current2 < last2) {
            ESP_LOGI(TAG, "Relay 2 Now OFF");
            offcounter2 = RELAY_LOCKOUT_TIME_S << 2;
        };
        if (current1 > last1) ESP_LOGI(TAG, "Relay 1 Now ON");
        if (current2 > last2) ESP_LOGI(TAG, "Relay 2 Now ON");
        vTaskDelay(pdMS_TO_TICKS(269));
    }
}

void manage_relay_timeouts(int configbits, int level1, int level2){
    timeout_data.level1 = level1;
    timeout_data.level2 = level2;
    timeout_data.configbits = configbits;
}

void setup_relays(int configbits){
    TaskHandle_t task_;
    timeout_data.configbits = configbits;
    timeout_data.level1 = 0;
    timeout_data.level2 = 0;
    timeout_data.turnoff_delay = RELAY_TURNOFF_DELAY_S;
    xTaskCreate(relay_timeout_task, "relaytimeout", 2400, NULL, 1, &task_);
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
