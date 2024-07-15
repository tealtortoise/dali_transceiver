
#include "buttons.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_attr.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_vfs.h"
#include "esp_vfs_dev.h"
#include "driver/usb_serial_jtag.h"
#include "esp_system.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "gpio_utils.h"
#include "base.h"

static const char* TAG = "Buttons";

typedef struct {
    zeroten_handle_t pwm1;
    zeroten_handle_t pwm2;
    TaskHandle_t mainloop_task;
} button_task_ctx_t;

#define REPEAT_DELAY_MS 200
#define BUF_SIZE 128

#define ECHO_TEST_TXD (UART_PIN_NO_CHANGE)
#define ECHO_TEST_RXD (UART_PIN_NO_CHANGE)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

#define ECHO_UART_PORT_NUM      (UART_NUM_0)
#define ECHO_UART_BAUD_RATE     (115200)
#define ECHO_TASK_STACK_SIZE    (2048)

static const char *CALIBRATE_COMMAND_1 = "PWM channel 1 duty-cycle at calibration state\n";
static const char *CALIBRATE_COMMAND_2 = "PWM channel 2 duty-cycle at calibration state\n";
static const char *ENTER_VALUE_COMMAND = "Please enter measured voltage:\n";
static const char *ENTER_VALUE_COMMAND2 = "(Enter 0.0 to cancel)\n";

bool DRAM_ATTR but1_debounce_lock = false;
bool DRAM_ATTR but2_debounce_lock = false;
bool DRAM_ATTR but3_debounce_lock = false;

TaskHandle_t DRAM_ATTR button_task_handle;

bool button1_isr(void *params){
    BaseType_t task_awoken = false;
    xTaskNotifyFromISR(button_task_handle, 1, eSetValueWithOverwrite, &task_awoken);
    return task_awoken == pdTRUE;
};

bool button2_isr(void *params){
    BaseType_t task_awoken = false;
    xTaskNotifyFromISR(button_task_handle, 2, eSetValueWithOverwrite, &task_awoken);
    return task_awoken == pdTRUE;
};

bool button3_isr(void *params){
    BaseType_t task_awoken = false;
    xTaskNotifyFromISR(button_task_handle, 3, eSetValueWithOverwrite, &task_awoken);
    return task_awoken == pdTRUE;
};

void run_calibration(button_task_ctx_t *ctx){
    // xTaskNotifyIndexed(ctx->mainloop_task, SETPOINT_SLEW_NOTIFY_INDEX, SUSPEND_MAIN_LOOP_LEVEL, eSetValueWithOverwrite);
    vTaskSuspend(ctx->mainloop_task);
    usb_serial_jtag_driver_config_t serialconfig = {
        .rx_buffer_size = 1024,
        .tx_buffer_size = 1024
    };
    usb_serial_jtag_driver_install(&serialconfig);
    esp_vfs_usb_serial_jtag_use_driver();
    
    zeroten_handle_* zhandle1 = (zeroten_handle_ *) ctx->pwm1;
    zeroten_handle_* zhandle2 = (zeroten_handle_ *) ctx->pwm2;
    esp_err_t returnval = ledc_set_duty(LEDC_LOW_SPEED_MODE, zhandle1->ledc_channel, 3000);
    returnval = returnval | ledc_update_duty(LEDC_LOW_SPEED_MODE, zhandle1->ledc_channel);
    returnval = ledc_set_duty(LEDC_LOW_SPEED_MODE, zhandle2->ledc_channel, 3000);
    returnval = returnval | ledc_update_duty(LEDC_LOW_SPEED_MODE, zhandle2->ledc_channel);

    if (zhandle1->pwm_resolution == -1) {
        ESP_LOGE(TAG, "Doesn't look like LEDC set up!");
        vTaskResume(ctx->mainloop_task);
        // xTaskNotifyIndexed(ctx->mainloop_task, SETPOINT_SLEW_NOTIFY_INDEX, RESUME_MAIN_LOOP_LEVEL, eSetValueWithOverwrite);
        return;
    }

    ESP_ERROR_CHECK(nvs_flash_init());
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open("nvs", NVS_READWRITE, &nvs_handle));

    vTaskDelay(pdMS_TO_TICKS(1000));

    double voltage;
    int read = 0;
    int gpio;
    char key[24];
    for (int i = 0; i < 2; i++){
        printf(i ? CALIBRATE_COMMAND_2 : CALIBRATE_COMMAND_1);
        printf(ENTER_VALUE_COMMAND);
        printf(ENTER_VALUE_COMMAND2);
        gpio = i ? zhandle2->gpio_pin : zhandle1->gpio_pin;
        while (1) {
            read = scanf("%lf", &voltage);
            if (read) break;
            ESP_LOGI(TAG,"Invalid response");
            vTaskDelay(100);
        }

        ESP_LOGI(TAG, "Received voltage %f", voltage);
        vTaskDelay(100);
        if (voltage == 0.0){
            ESP_LOGI(TAG, "Received 0.0 -> cancelling....");
            
            vTaskDelay(1000);
            // nvs_close(nvs_handle);
            // usb_serial_jtag_driver_uninstall();
            vTaskResume(ctx->mainloop_task);
            // xTaskNotifyIndexed(ctx->mainloop_task, SETPOINT_SLEW_NOTIFY_INDEX, RESUME_MAIN_LOOP_LEVEL, eSetValueWithOverwrite);            
            return;
        }
        double testduty = 3000.0 / (1 << zhandle1->pwm_resolution);
        double gain_correction = testduty * 10.0 / voltage;
        ESP_LOGI(TAG, "Gain for PIN %i found as %f", gpio, gain_correction);
        build_nvs_key_for_gpio_gain(gpio, key);
        ESP_LOGI(TAG, "NVS Key: '%s'", key);
        
        // ESP_ERROR_CHECK(nvs_set_i64(nvs_handle, key, *(int64_t*) &gain_correction));
        ESP_LOGI(TAG, "DIDNT Set gain in NVS to %f", gain_correction);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "Restarting in 5 seconds....");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();

}

void button_monitor_task(void *params){
    button_task_ctx_t *ctx = (button_task_ctx_t*) params;
    // run_calibration(ctx);
    BaseType_t received;
    uint32_t value;
    int but1_value;
    int but2_value;
    int but3_value;
    int but1_counter = 0;
    int but2_counter = 0;
    int but3_counter = 0;
    int counter = 0;
    bool updated = false;
    int wait_ms = 30;
    while (1){
        received = xTaskNotifyWait(0, 0, &value, pdMS_TO_TICKS(wait_ms));
        if (1) {
            but1_value = 1 - gpio_get_level(BUT1_GPIO);
            but2_value = 1 - gpio_get_level(BUT2_GPIO);
            but3_value = 1 - gpio_get_level(BUT3_GPIO);

            if (but1_value  && (but1_counter == 0 || but1_counter > (REPEAT_DELAY_MS * 2 / wait_ms))) {
                setpoint = clamp(setpoint - 4, 0, 254);
                updated = true;
            }
            else if (but2_value && (but2_counter == 0 || but2_counter > (REPEAT_DELAY_MS * 2 / wait_ms)))
            {
                setpoint = clamp(setpoint + 4, 0, 254);
                updated = true;
            }
            else if (but3_counter > (5000 / 10)) {
                ESP_LOGI(TAG, "Calibration Started.....");
                run_calibration(ctx);
                updated =true;
            }
            if (updated) {
                xTaskNotifyIndexed(ctx->mainloop_task, SETPOINT_SLEW_NOTIFY_INDEX, USE_DEFAULT_FADETIME, eSetValueWithOverwrite);
                updated = false;
            }
            but1_counter = but1_value ? but1_counter + 1 : 0;
            but2_counter = but2_value ? but2_counter + 1 : 0;
            but3_counter = but3_value ? but3_counter + 1 : 0;
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
            xTaskNotifyStateClear(button_task_handle);
        }

    }
}

void setup_button_interrupts(TaskHandle_t mainlooptask, zeroten_handle_t pwm1, zeroten_handle_t pwm2){
    uint8_t buttons[] = {BUT1_GPIO, BUT2_GPIO, BUT3_GPIO};
    for (int i =0; i <= 2; i++){
        gpio_set_intr_type(buttons[i], GPIO_INTR_NEGEDGE);
    }

    button_task_ctx_t *task_ctx = malloc(sizeof(button_task_ctx_t));
    task_ctx->mainloop_task = mainlooptask;
    task_ctx->pwm1 = pwm1;
    task_ctx->pwm2 = pwm2;

    xTaskCreate(button_monitor_task, "button-monitor-task", 3048, (void*) task_ctx, 5, &button_task_handle);

    ESP_ERROR_CHECK(gpio_isr_handler_add(BUT1_GPIO, button1_isr, (void*) mainlooptask));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUT2_GPIO, button2_isr, (void*) mainlooptask));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUT3_GPIO, button3_isr, (void*) mainlooptask));

};