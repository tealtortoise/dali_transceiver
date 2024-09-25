
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "base.h"

#include "espnow.h"
#include "wifi.h"
#include "gpio_utils.h"
#include "ble_wifi_prov.h"

// #define LIGHTING_WIFI_SSID "vodafone10BD78"
// #define LIGHTING_WIFI_PASSWORD "9Crabstickerponcho"

// // #define LIGHTING_WIFI_SSID "Verity"
// // #define LIGHTING_WIFI_PASSWORD "f39e27cc"

// /* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "wifi station";

static int s_disconnect_count = 0;
static int s_retry_num = 0;
static TaskHandle_t wifi_reconnect_handle;

static int received;
static uint32_t value;

void __wifi_reconnect_task(void *params)
{
    while (1)
    {
        received = xTaskNotifyWait(0, 0, &value, portMAX_DELAY);
        if (received == pdTRUE)
        {
            ESP_LOGI(TAG, "Received notification of WiFi disconnect.");
            if (value <= 3)
            {
                // just reconnect
            }
            else if (value <= 5)
            {
                ESP_LOGI(TAG, "Waiting 10 seconds before reconnecting...");
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            else if (value <= 10)
            {
                vTaskDelay(pdMS_TO_TICKS(30000));
                ESP_LOGI(TAG, "Waiting 30 seconds before reconnecting...");
            }
            else
            {
                vTaskDelay(pdMS_TO_TICKS(60000));
                ESP_LOGI(TAG, "Waiting 60 seconds before reconnecting...");
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            ESP_LOGI(TAG, "Reconnecting...");
            esp_wifi_connect();
        }
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        s_disconnect_count += 1;
        gpio_set_level(LED2_GPIO, 0);
        xTaskNotify(wifi_reconnect_handle, s_disconnect_count, eSetValueWithOverwrite);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        gpio_set_level(LED2_GPIO, 1);
        s_disconnect_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// void wifi_setup_main_operation_events(EventGroupHandle_t wifi_event_group)
// {
//     s_wifi_event_group = wifi_event_group;

//     // ESP_ERROR_CHECK(esp_netif_init());

//     // ESP_ERROR_CHECK(esp_event_loop_create_default());
//     // esp_netif_create_default_wifi_sta();

//     // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     // ESP_ERROR_CHECK(esp_wifi_init(&cfg));

//     esp_event_handler_instance_t instance_any_id;
//     esp_event_handler_instance_t instance_got_ip;
//     ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
//                                                         ESP_EVENT_ANY_ID,
//                                                         &event_handler,
//                                                         NULL,
//                                                         &instance_any_id));
//     ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
//                                                         IP_EVENT_STA_GOT_IP,
//                                                         &event_handler,
//                                                         NULL,
//                                                         &instance_got_ip));

//     xTaskCreate(wifi_reconnect_task, "Wifi Reconnect", 4096, NULL, 1, &wifi_reconnect_handle);

//     // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
//     // ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
//     // ESP_ERROR_CHECK(esp_wifi_start());

//     // ESP_LOGI(TAG, "wifi_init_sta finished.");

//     /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
//      * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
//     EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
//                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
//                                            pdFALSE,
//                                            pdFALSE,
//                                            portMAX_DELAY);

//     /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
//      * happened. */
//     if (bits & WIFI_CONNECTED_BIT)
//     {
//         ESP_LOGI(TAG, "connected to ap");
//     }
//     else if (bits & WIFI_FAIL_BIT)
//     {
//         ESP_LOGI(TAG, "Failed to connect");
//     }
//     else
//     {
//         ESP_LOGE(TAG, "UNEXPECTED EVENT");
//     }
// }

