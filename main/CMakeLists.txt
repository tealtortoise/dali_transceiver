idf_component_register(SRCS "rgb_led.c" "settings.c" "dali_edgeframe_parser.c" "dali_rmt_receiver.c" "dali_transmit.c" "edgeframe_logger.c" "http_server.c" "wifi.c" "dali_transceiver_main.c" "dali_encoder.c" "dali.c" "dali_transceiver.c" "dali_utils.c" "0-10v.c" "espnow.c" "ledflash.c" "gpio_utils.c" "uart.c" "adc.c" "base.c" "buttons.c" "realtime.c" "settings.h"
                       INCLUDE_DIRS "." REQUIRES esp_http_server driver esp_wifi nvs_flash esp_event esp_adc vfs spiffs esp_netif esp_timer app_update)

                    #    REQUIRES(driver)