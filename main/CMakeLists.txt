idf_component_register(SRCS "dali_edgeframe_parser.c" "dali_rmt_receiver.c" "dali_transmit.c" "edgeframe_logger.c" "http_server.c" "wifi.c" "dali_transceiver_main.c" "dali_encoder.c"
                       INCLUDE_DIRS "." REQUIRES esp_http_server driver esp_wifi nvs_flash esp_event)

                    #    REQUIRES(driver)