#ifndef GPIO_UTILS_H
#define GPIO_UTILS_H
#include <stdlib.h>
#include <stdint.h>
#include "driver/gpio.h"

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
#define PWM_010v_GPIO   10
#define PWM_010v2_GPIO   11
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
#define RELAY1_GPIO 0
#define RELAY2_GPIO 1
#endif // config target

#define PRESSED_MESSAGE "Pressed"
#define NOT_PRESSED_MESSAGE "Not Pressed"

#define OUTPUT_PIN_COUNT 7
static const int OUTPUT_PINS[] = {LED1_GPIO, LED2_GPIO, PWM_010v_GPIO, PWM_010v2_GPIO, RELAY1_GPIO, RELAY2_GPIO, TX_GPIO};
static const char* OUTPUT_PIN_NAMES[] = {"LED1", "LED2", "PWM1", "PWM2", "RELAY1", "RELAY2", "DALI TX"};
#define OUTPIUT_PIN_MAP ((1 << LED1_GPIO) | (1 << LED2_GPIO) | (1 << PWM_010v_GPIO) | (1 << PWM_010v2_GPIO) | (1 << TX_GPIO) | (1 << RELAY1_GPIO) | (1 << RELAY2_GPIO))
#define INPUT_PIN_MAP ((1 << RX_GPIO) | (1 << AIN_GPIO) | (1 << EXT1_GPIO))
#define INPUT_WPU_PIN_MAP ((1 << BUT1_GPIO) | (1 << BUT2_GPIO) | (1 << BUT3_GPIO) | (1 << DIP1_GPIO) | (1 << DIP2_GPIO)  | (1 << DIP3_GPIO) | (1 << DIP4_GPIO))


uint8_t read_dip_switches();

void manage_relay_timeouts(int configbits, int level1, int level2);

void setup_relays(int configbits);

int get_and_log_buttons();

void rotate_gpio_outputs();

void configure_gpio();

#endif // GPIO_UTILS_H

