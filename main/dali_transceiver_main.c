#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/ledc.h"
// #include "edgeframe_logger.h"
// #include "dali_transmit.h"
#include "0-10v.h"
// #include "dali_edgeframe_parser.h"
// #include "dali.h"
#include "dali_transceiver.h"
#include "dali_utils.h"
// #include "dali_utils.c"
// #include "dali_rmt_receiver.c"
// #ifdef IS_PRIMARY
#include "wifi.c"
#include "http_server.c"
// #endif // IS_PRIMARY
#include "espnow.h"
#include "ledflash.h"
#include "buttons.h"

#define RESOLUTION_HZ     10000000
#ifdef CONFIG_IDF_TARGET_ESP32S3
#define TX_GPIO       18
#define STROBE_GPIO 17
#define RX_GPIO       6
#define PWM_010v_GPIO   15
// #define PWM_010v2_GPIO   15
#endif // config target
#ifdef CONFIG_IDF_TARGET_ESP32C6
#define TX_GPIO       18
#define STROBE_GPIO 21
#define RX_GPIO       2
#define PWM_010v_GPIO   11
#define PWM_010v2_GPIO   10
#define DIP1_GPIO 20
#define DIP2_GPIO 19
#define DIP3_GPIO 5
#define DIP4_GPIO 3
#define LED1_GPIO 21
#define LED2_GPIO 22
#define AIN_GPIO 6
#define BUT3_GPIO 15
#define BUT2_GPIO 23
#define BUT1_GPIO 9
#define EXT1_GPIO 7
#define RELAY1_GPIO 7
#define RELAY2_GPIO 8
#endif // config target

static const char *TAG = "main loop";


#define OUTPUT_PIN_COUNT 7
const int* OUTPUT_PINS = {LED1_GPIO, LED2_GPIO, PWM_010v_GPIO, PWM_010v2_GPIO, RELAY1_GPIO, RELAY2_GPIO, TX_GPIO};
const char* OUTPUT_PIN_NAMES[] = {"LED1", "LED2", "PWM1", "PWM2", "RELAY1", "RELAY2", "DALI TX"};
#define OUTPIUT_PIN_MAP ((1 << LED1_GPIO) | (1 << LED2_GPIO) | (1 << PWM_010v_GPIO) | (1 << PWM_010v2_GPIO) | (1 << TX_GPIO) | (1 << RELAY1_GPIO) | (1 << RELAY2_GPIO))
#define INPUT_PIN_MAP ((1 << RX_GPIO) | (1 << AIN_GPIO) | (1 << EXT1_GPIO))
#define INPUT_WPU_PIN_MAP ((1 << BUT1_GPIO) | (1 << BUT2_GPIO) | (1 << BUT3_GPIO) | (1 << DIP1_GPIO) | (1 << DIP2_GPIO)  | (1 << DIP3_GPIO) | (1 << DIP4_GPIO))

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

static volatile DRAM_ATTR uint8_t state = 0;

bool IRAM_ATTR strobetimer_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx){
    if (state) {
        state = 0;
    } else {
        state = 1;
    }
    gpio_set_level(STROBE_GPIO, state);
    return false;
}

uint8_t read_dip_switches(){
    return gpio_get_level(DIP1_GPIO) | 
        (gpio_get_level(DIP2_GPIO) << 1) |
        (gpio_get_level(DIP3_GPIO) << 2) |
        (gpio_get_level(DIP4_GPIO) << 3);
}

void primary(){
    setup_wifi();
    httpd_ctx httpdctx = {
        .mainloop_task = xTaskGetCurrentTaskHandle()
    };
    httpd_handle_t httpd = setup_httpserver(&httpdctx);
    
    TaskHandle_t espnow_send_task;
    ESP_ERROR_CHECK(setup_espnow_common(&espnow_send_task, xTaskGetCurrentTaskHandle()));


    ledc_channel_t pwm0_10v_channel1;
    ESP_ERROR_CHECK(setup_0_10v_channel(PWM_010v_GPIO, 1<<14, &pwm0_10v_channel1));
    ledc_channel_t pwm0_10v_channel2;
    ESP_ERROR_CHECK(setup_0_10v_channel(PWM_010v2_GPIO, 1<<14, &pwm0_10v_channel2));

    // ESP_ERROR_CHECK(gpio_install_isr_service(0));
    
    dali_transceiver_config_t transceiver_config = dali_transceiver_sensible_default_config;
    transceiver_config.receive_gpio_pin = RX_GPIO;
    transceiver_config.transmit_gpio_pin = TX_GPIO;

    dali_transceiver_handle_t dali_transceiver;
    ESP_ERROR_CHECK(dali_setup_transceiver(transceiver_config, &dali_transceiver));

    dali_broadcast_level(dali_transceiver, 30);
    dali_set_level(dali_transceiver, 8, 70);
    
    BaseType_t success;
    uint32_t requestlevel;
    
    while (1) {
        success = xTaskNotifyWaitIndexed(0, 0, 0, &requestlevel, 100);
        if (success) {
            if (requestlevel > 254) requestlevel = 254;
            xTaskNotifyIndexed(espnow_send_task, 0, requestlevel, eSetValueWithOverwrite);
            
            ESP_LOGI(TAG, "Received new level notification: sent %lu to ESPNOW send task", requestlevel);
            ESP_ERROR_CHECK(set_0_10v_level(pwm0_10v_channel1, (uint16_t)(0xFFFF -  (uint16_t) requestlevel) << 8));
            ESP_ERROR_CHECK(set_0_10v_level(pwm0_10v_channel2, (uint16_t)((uint16_t) requestlevel) << 8));
            for (int i=8; i<10; i+=1) {
                ESP_ERROR_CHECK(dali_set_level(dali_transceiver, i, requestlevel));
            }
            vTaskDelay(pdMS_TO_TICKS(4));
        };
    };
}

void secondary(){
    espnow_wifi_init();
    
    TaskHandle_t espnow_send_task;
    ESP_ERROR_CHECK(setup_espnow_common(&espnow_send_task, xTaskGetCurrentTaskHandle()));
    
    uint32_t requestlevel;
    
    setup_espnow_receiver();
    

    ledc_channel_t pwm0_10v_channel1;
    ESP_ERROR_CHECK(setup_0_10v_channel(PWM_010v_GPIO, 1<<14, &pwm0_10v_channel1));

    // ESP_ERROR_CHECK(gpio_install_isr_service(0));
    
    dali_transceiver_config_t transceiver_config = dali_transceiver_sensible_default_config;
    transceiver_config.receive_gpio_pin = RX_GPIO;
    transceiver_config.transmit_gpio_pin = TX_GPIO;

    dali_transceiver_handle_t dali_transceiver;
    ESP_ERROR_CHECK(dali_setup_transceiver(transceiver_config, &dali_transceiver));

    dali_broadcast_level(dali_transceiver, 30);
    dali_set_level(dali_transceiver, 8, 70);
    ESP_LOGI(TAG, "Query level returned %hi", dali_query_level(dali_transceiver, 9));

    uint8_t address;
    uint32_t queuepeek;
    BaseType_t success;
    // int level = 10;
    while (1){
        success = xTaskNotifyWaitIndexed(0, 0, 0, &requestlevel, 100);
        if (success) {
            ESP_LOGI(TAG, "Received notification of level change to %lu", requestlevel);
            if (requestlevel > 254) requestlevel = 254;
            ESP_ERROR_CHECK(set_0_10v_level(pwm0_10v_channel1, (uint16_t)((uint16_t) requestlevel) << 8));
            for (int i=8; i<10; i+=1) {
                ESP_ERROR_CHECK(dali_set_level(dali_transceiver, i, requestlevel));
            }
            vTaskDelay(pdMS_TO_TICKS(4));
        }
    }
}

void rotate_gpio_outputs(){
    int state = 1;
    int output_idx = 0;
    while (1) {
        for (int i=0; i < OUTPUT_PIN_COUNT; i++){
            ESP_LOGI(TAG, "Setting %s to %i", OUTPUT_PIN_NAMES[i], OUTPUT_PINS[i]);
            gpio_set_level(OUTPUT_PINS[i], state);
            vTaskDelay(2000);
        }
        state = 1 - state;
    }
}

void app_main(void)
{
    configure_gpio();
    uint8_t dip_address = read_dip_switches();
    ESP_LOGI(TAG, "Read DIP Switch address '{%d}'", dip_address);
    rotate_gpio_outputs();
    // setup_flash_led(LED1_GPIO, 2000);
    // setup_flash_led(LED2_GPIO, 2020);
    // setup_flash_led(RELAY1_GPIO, 7400);
    // setup_flash_led(RELAY2_GPIO, 7550);
    if (dip_address == 0){
        primary();
    }
    else {
        secondary();
    }

}
