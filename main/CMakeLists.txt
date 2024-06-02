idf_component_register(SRCS "wifi.c" "dali_transceiver_main.c" "dali_encoder.c"
                       INCLUDE_DIRS "." REQUIRES esp_http_server driver esp_wifi nvs_flash esp_event)

                    #    REQUIRES(driver)