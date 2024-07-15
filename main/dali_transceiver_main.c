#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/ledc.h"
#include "base.h"
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
#include "wifi.h"
#include "http_server.h"
// #endif // IS_PRIMARY
#include "espnow.h"
#include "base.h"
#include "ledflash.h"
#include "gpio_utils.h"
#include "uart.h"
#include "buttons.h"
#include "adc.h"
#include "realtime.h"

#define RESOLUTION_HZ     10000000

#define GUESSED_LOOPTIME_US 33000


static const char *TAG = "main loop";

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

static volatile DRAM_ATTR uint8_t strobestate = 0;

bool IRAM_ATTR strobetimer_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx){
    if (strobestate) {
        strobestate = 0;
    } else {
        strobestate = 1;
    }
    gpio_set_level(STROBE_GPIO, strobestate);
    return false;
}

void primary(){
    setup_wifi();
    // setup_sntp();
    httpd_ctx httpdctx = {
        .mainloop_task = xTaskGetCurrentTaskHandle()
    };
    httpd_handle_t httpd = setup_httpserver(&httpdctx);
    
    // TaskHandle_t espnow_send_task;
    // ESP_ERROR_CHECK(setup_espnow_common(&espnow_send_task, xTaskGetCurrentTaskHandle()));

    zeroten_handle_t pwm1;
    zeroten_handle_t pwm2;
    ESP_ERROR_CHECK(setup_0_10v_channel(LED1_GPIO, CALIBRATION_PWM_LOG, &pwm1));
    ESP_ERROR_CHECK(setup_0_10v_channel(PWM_010v_GPIO, CALIBRATION_LOOKUP_NVS, &pwm2));

    setup_button_interrupts(xTaskGetCurrentTaskHandle(), pwm1, pwm2);

    dali_transceiver_config_t transceiver_config = dali_transceiver_sensible_default_config;
    transceiver_config.invert_input = DALI_DONT_INVERT;
    transceiver_config.invert_output = DALI_DONT_INVERT;
    transceiver_config.receive_gpio_pin = RX_GPIO   ;
    transceiver_config.transmit_gpio_pin = TX_GPIO;
    transceiver_config.parser_config.forward_frame_action = DALI_PARSER_ACTION_IGNORE;

    dali_transceiver_handle_t dali_transceiver;
    ESP_ERROR_CHECK(dali_setup_transceiver(transceiver_config, &dali_transceiver));

    // dali_set_system_failure_level(dali_transceiver, 8, 10);
    // dali_set_system_failure_level(dali_transceiver, 9, 10);
    dali_broadcast_level(dali_transceiver, 30);

    BaseType_t received;
    esp_err_t sent;
    int firsttime = 1;
    actual_level = setpoint;
    vTaskDelay(50);
    ESP_LOGI(TAG, "Starting main loop");
    int wait_ticks = 10;
    int last_level = -1;
    int last_level16 = -1;
    int tick_increment_16 = 1 << 8;
    int local_fadetime = fadetime;
    int fade_remaining_16 = 0;
    int setpoint16 = setpoint << 8;
    int level16bit = setpoint16;
    int64_t reftime = esp_timer_get_time() - GUESSED_LOOPTIME_US;
    int64_t newtime;
    int looptime = GUESSED_LOOPTIME_US;
    while (1) {
        
        if (last_level16 == level16bit) {
            // looptime = GUESSED_LOOPTIME_US;
            reftime = esp_timer_get_time();
        }
        else
        {
            newtime = esp_timer_get_time();
            looptime = newtime - reftime;
            reftime = newtime;
        }
        
        last_level16 = level16bit;
        last_level = actual_level;
        received = xTaskNotifyWaitIndexed(SETPOINT_SLEW_NOTIFY_INDEX, 0, 0, &local_fadetime, wait_ticks);
        setpoint16 = setpoint << 15;
        fadetime = (local_fadetime == USE_DEFAULT_FADETIME) ? default_fadetime : local_fadetime;
        if (fadetime < 8) fadetime = 8;
        // ESP_LOGI(TAG, "Received %i, setpoint16 %i ,wait_ticks %i, fadetime %i", received, setpoint16, wait_ticks, fadetime);
        

        if (level16bit == setpoint16){
            // idling
            wait_ticks = pdMS_TO_TICKS(100);
        }
        else
        {
            wait_ticks = (fadetime > 1800000) ? pdMS_TO_TICKS(100) : 0;
        }
        // if ((fadetime > 180000)) ESP_LOGI(TAG, "%i, %i %i %i", level16bit, tick_increment_16, wait_ticks, looptime);
        tick_increment_16 = (looptime << 13) / fadetime;
        // ESP_LOGI(TAG, "New wait ticks %i, tick inc %i, loop time %i us", wait_ticks, tick_increment_16, looptime);
        if (received || firsttime) {
            if (setpoint > 254){
                ESP_LOGE(TAG, "Unknown level %i", setpoint);
                continue;
            }
            ESP_LOGI(TAG, "Received new setpoint '%i', fade time %i us", setpoint, fadetime);
            // ESP_LOGI(TAG, "Actual_level %i, tick_inc %i, wait_tick %i", actual_level, tick_increment_16, wait_ticks);
        }
        setpoint16 = setpoint << 15;
        fade_remaining_16 = level16bit - setpoint16;

        if ((fade_remaining_16 != 0) && abs(fade_remaining_16) < tick_increment_16) {
            tick_increment_16 = abs(fade_remaining_16);
        }

        if (fade_remaining_16 < 0){
            level16bit += tick_increment_16;
        }
        else if (fade_remaining_16 > 0)
        {
            level16bit -= tick_increment_16;
        }

        actual_level = (level16bit * full_power + 0x5FFF) >> 23;
        if (last_level == actual_level)
        {
            if (wait_ticks < 10) wait_ticks = 10;
            // vTaskDelay(pdMS_TO_TICKS(10));
        }
        else 
        {

            // xTaskNotifyIndexed(espnow_send_task, LIGHT_LEVEL_NOTIFY_INDEX, requestlevel, eSetValueWithOverwrite);
            // gpio_set_level(RELAY1_GPIO, requestlevel > 0);
            // gpio_set_level(RELAY2_GPIO, requestlevel > 0);
            ESP_LOGI(TAG, "Actual_level %i, 16 %i, tick_inc %i, wait_tick %i, looptime %i us", actual_level, level16bit ,tick_increment_16, wait_ticks, looptime);
            ESP_ERROR_CHECK(set_0_10v_level(pwm1, actual_level));
            ESP_ERROR_CHECK(set_0_10v_level(pwm2, actual_level));
            // for (int i=8; i<13; i+=1) {
            sent = dali_broadcast_level(dali_transceiver, actual_level);
            // }
        };
        firsttime = 0;
    };
}

void secondary(){
    espnow_wifi_init();
    
    TaskHandle_t espnow_send_task;
    ESP_ERROR_CHECK(setup_espnow_common(&espnow_send_task, xTaskGetCurrentTaskHandle()));
    
    uint32_t requestlevel;
    
    setup_espnow_receiver();
    

    zeroten_handle_t pwm1;
    zeroten_handle_t pwm2;
    ESP_ERROR_CHECK(setup_0_10v_channel(LED1_GPIO, 2, &pwm1));
    ESP_ERROR_CHECK(setup_0_10v_channel(PWM_010v2_GPIO, 2, &pwm2));

    // ESP_ERROR_CHECK(gpio_install_isr_service(0));
    
    dali_transceiver_config_t transceiver_config = dali_transceiver_sensible_default_config;
    transceiver_config.receive_gpio_pin = RX_GPIO;
    transceiver_config.transmit_gpio_pin = TX_GPIO;

    dali_transceiver_handle_t dali_transceiver;
    ESP_ERROR_CHECK(dali_setup_transceiver(transceiver_config, &dali_transceiver));

    dali_broadcast_level(dali_transceiver, 30);
    dali_set_level_block(dali_transceiver, 8, 70);
    ESP_LOGI(TAG, "Query level returned %hi", dali_query_level(dali_transceiver, 9));

    uint8_t address;
    uint32_t queuepeek;
    BaseType_t success;
    // int level = 10;
    while (1){
        success = xTaskNotifyWaitIndexed(LIGHT_LEVEL_NOTIFY_INDEX, 0, 0, &requestlevel, 100);
        if (success) {
            ESP_LOGI(TAG, "Received notification of level change to %lu", requestlevel);
            success = xTaskNotifyWaitIndexed(LIGHT_LEVEL_NOTIFY_INDEX, 0, 0, &requestlevel, 0);
            ESP_LOGI(TAG, "Received notification of level change to %lu", requestlevel);

            if (requestlevel > 254) requestlevel = 254;
            ESP_ERROR_CHECK(set_0_10v_level(pwm1, requestlevel));
            for (int i=8; i<10; i+=1) {
                ESP_ERROR_CHECK(dali_set_level_block(dali_transceiver, i, requestlevel));
            }
            vTaskDelay(pdMS_TO_TICKS(4));
        }
    }
}


void app_main(void)
{
    configure_gpio();
    // setup_uart();
    uint8_t dip_address = read_dip_switches();
    ESP_LOGI(TAG, "Read DIP Switch address: %d", dip_address);

    light_adc_config_t adcconfig = {
        .notify_task = xTaskGetCurrentTaskHandle(),
        .tolerance = 10,
        .sampled_needed = 3,
        .average_window = 4,
    };
    int resistor_level;
    int received;
    setup_adc(adcconfig);

    // while(1){
        // received = xTaskNotifyWaitIndexed(LIGHT_LEVEL_NOTIFY_INDEX, 0, 0, &resistor_level, 100);
        // if (received) ESP_LOGI(TAG, "Received resistor level %i", resistor_level);

    // }
    // rotate_gpio_outputs_forever();
    // setup_flash_led(LED1_GPIO, 2000);
    // setup_flash_led(LED2_GPIO, 2020);
    // setup_flash_led(RELAY1_GPIO, 7400);
    // setup_flash_led(RELAY2_GPIO, 7550);
    if (dip_address == 0){
        primary();
    }
    else {
        primary();
    }

}
