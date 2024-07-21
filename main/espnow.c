/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
   This example shows how to use ESPNOW.
   Prepare two device, one for sending ESPNOW data and another for receiving
   ESPNOW data.
*/
#include "espnow.h"
#include "base.h"
#include "settings.h"

// #define IS_MAIN true

#define ESPNOW_MAXDELAY 512

static const char *TAG = "espnow_example";

static QueueHandle_t s_example_espnow_queue;

static uint8_t s_example_broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint16_t s_example_espnow_seq[EXAMPLE_ESPNOW_DATA_MAX] = { 0, 0 };


static void example_espnow_deinit(espnow_ctx_t *espnow_ctx);

/* WiFi should start before using ESPNOW */
void espnow_wifi_init(void)
{
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(ESPNOW_WIFI_MODE) );
    ESP_ERROR_CHECK( esp_wifi_start());
    ESP_ERROR_CHECK( esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

#if CONFIG_ESPNOW_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK( esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
#endif
}

/* ESPNOW sending or receiving callback function is called in WiFi task.
 * Users should not do lengthy operations from this task. Instead, post
 * necessary data to a queue and handle it from a lower priority task. */
static void example_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    example_espnow_event_t evt;
    example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    evt.id = EXAMPLE_ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}

static void example_espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    example_espnow_event_t evt;
    example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
    uint8_t * mac_addr = recv_info->src_addr;
    uint8_t * des_addr = recv_info->des_addr;

    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    if (IS_BROADCAST_ADDR(des_addr)) {
        /* If added a peer with encryption before, the receive packets may be
         * encrypted as peer-to-peer message or unencrypted over the broadcast channel.
         * Users can check the destination address to distinguish it.
         */
        ESP_LOGD(TAG, "Receive broadcast ESPNOW data");
    } else {
        ESP_LOGD(TAG, "Receive unicast ESPNOW data");
    }

    evt.id = EXAMPLE_ESPNOW_RECV_CB;
    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL) {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;
    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }
}

/* Parse received ESPNOW data. */
int example_espnow_data_parse(uint8_t *data, uint16_t data_len, uint8_t *state, uint16_t *seq, int *magic, uint8_t *payload)
{
    example_espnow_data_t *buf = (example_espnow_data_t *)data;
    uint16_t crc, crc_cal = 0;

    if (data_len < sizeof(example_espnow_data_t)) {
        ESP_LOGE(TAG, "Receive ESPNOW data too short, len:%d", data_len);
        return -1;
    }

    *state = buf->state;
    *seq = buf->seq_num;
    *magic = buf->magic;
    crc = buf->crc;
    buf->crc = 0;
    crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

    if (crc_cal == crc) {
        *payload = buf->payload[0];
        return buf->type;
    }

    return -1;
}

/* Prepare ESPNOW data to be sent. */
void example_espnow_data_prepare(espnow_ctx_t *espnow_ctx, uint8_t level)
{
    example_espnow_data_t *buf = (example_espnow_data_t *)espnow_ctx->buffer;

    assert(espnow_ctx->len >= sizeof(example_espnow_data_t));

    buf->type = IS_BROADCAST_ADDR(espnow_ctx->dest_mac) ? EXAMPLE_ESPNOW_DATA_BROADCAST : EXAMPLE_ESPNOW_DATA_UNICAST;
    buf->state = espnow_ctx->state;
    buf->seq_num = s_example_espnow_seq[buf->type]++;
    buf->crc = 0;
    buf->magic = espnow_ctx->magic;
    /* Fill all remaining bytes after the data with random values */
    // esp_fill_random(buf->payload, espnow_ctx->len - sizeof(example_espnow_data_t));
    buf->payload[0] = level;
    buf->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, espnow_ctx->len);
}


static void espnow_receive_queue_task(void *pvParameter)
{
    example_espnow_event_t evt;
    uint8_t recv_state = 0;
    uint16_t recv_seq = 0;
    int recv_magic = 0;
    bool is_broadcast = false;
    int ret;
    uint8_t rxlevel;


    /* Start sending broadcast ESPNOW data. */
    espnow_ctx_t *espnow_ctx = (espnow_ctx_t *)pvParameter;
    #ifdef IS_MAIN
    // if (esp_now_send(espnow_ctx->dest_mac, espnow_ctx->buffer, espnow_ctx->len) != ESP_OK) {
        // ESP_LOGE(TAG, "Send error");
        // example_espnow_deinit(espnow_ctx);
        // vTaskDelete(NULL);
    // }
    #endif // IS_MAIN
    bool configbit_recv;
    BaseType_t received;
    int cycles = 0;
    while (1) {
        if ((cycles & 0x3F) == 0) configbit_recv = (get_setting("configbits") & CONFIGBIT_RECEIVE_ESPNOW) > 0;
        received = xQueueReceive(s_example_espnow_queue, &evt, pdMS_TO_TICKS(100));
        cycles += 0;
        if (received != pdTRUE || !configbit_recv) continue;
        switch (evt.id) {
            case EXAMPLE_ESPNOW_SEND_CB:
            {
                // example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;
                // is_broadcast = IS_BROADCAST_ADDR(send_cb->mac_addr);

                // ESP_LOGD(TAG, "Send data to "MACSTR", status1: %d", MAC2STR(send_cb->mac_addr), send_cb->status);

                // if (is_broadcast && (espnow_ctx->broadcast == false)) {
                //     break;
                // }

                // if (!is_broadcast) {
                //     espnow_ctx->count--;
                //     if (espnow_ctx->count == 0) {
                //         ESP_LOGI(TAG, "Send done");
                //         example_espnow_deinit(espnow_ctx);
                //         vTaskDelete(NULL);
                //     }
                // }

                // /* Delay a while before sending the next data. */
                // if (espnow_ctx->delay > 0) {
                //     vTaskDelay(espnow_ctx->delay/portTICK_PERIOD_MS);
                // }

                // ESP_LOGI(TAG, "send data to "MACSTR"", MAC2STR(send_cb->mac_addr));

                // memcpy(espnow_ctx->dest_mac, send_cb->mac_addr, ESP_NOW_ETH_ALEN);
                // example_espnow_data_prepare(espnow_ctx);

                // /* Send the next data after the previous data is sent. */
                // if (esp_now_send(espnow_ctx->dest_mac, espnow_ctx->buffer, espnow_ctx->len) != ESP_OK) {
                //     ESP_LOGE(TAG, "Send error");
                //     example_espnow_deinit(espnow_ctx);
                //     vTaskDelete(NULL);
                // }
                ESP_LOGE(TAG, "How? Send callback somehow.");
                break;
            }
            case EXAMPLE_ESPNOW_RECV_CB:
            {
                example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

                ret = example_espnow_data_parse(recv_cb->data, recv_cb->data_len, &recv_state, &recv_seq, &recv_magic, &rxlevel);
                ESP_LOGI(TAG, "Level data received %d, type is %i", rxlevel, ret);
                xTaskNotifyIndexed(espnow_ctx->mainloop_task, SETPOINT_SLEW_NOTIFY_INDEX, USE_DEFAULT_FADETIME, eSetValueWithOverwrite);
                free(recv_cb->data);
                if (ret == EXAMPLE_ESPNOW_DATA_BROADCAST) {
                    ESP_LOGI(TAG, "Receive %dth broadcast data from: "MACSTR", len: %d", recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

                    /* If MAC address does not exist in peer list, add it to peer list. */
                    if (esp_now_is_peer_exist(recv_cb->mac_addr) == false) {
                        esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
                        if (peer == NULL) {
                            ESP_LOGE(TAG, "Malloc peer information fail");
                            // example_espnow_deinit(espnow_ctx);
                            vTaskDelete(NULL);
                        }
                        memset(peer, 0, sizeof(esp_now_peer_info_t));
                        peer->channel = CONFIG_ESPNOW_CHANNEL;
                        peer->ifidx = ESPNOW_WIFI_IF;
                        peer->encrypt = true;
                        memcpy(peer->lmk, CONFIG_ESPNOW_LMK, ESP_NOW_KEY_LEN);
                        memcpy(peer->peer_addr, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                        ESP_ERROR_CHECK( esp_now_add_peer(peer) );
                        free(peer);
                    }



                    /* Indicates that the device has received broadcast ESPNOW data. */
                    if (espnow_ctx->state == 0) {
                        espnow_ctx->state = 1;
                    }

                    /* If receive broadcast ESPNOW data which indicates that the other device has received
                     * broadcast ESPNOW data and the local magic number is bigger than that in the received
                     * broadcast ESPNOW data, stop sending broadcast ESPNOW data and start sending unicast
                     * ESPNOW data.
                     */
                    if (0 && recv_state == 1) {
                        /* The device which has the bigger magic number sends ESPNOW data, the other one
                         * receives ESPNOW data.
                         */
                        if (espnow_ctx->unicast == false && espnow_ctx->magic >= recv_magic) {
                    	    ESP_LOGI(TAG, "Start sending unicast data");
                    	    ESP_LOGI(TAG, "send data to "MACSTR"", MAC2STR(recv_cb->mac_addr));

                    	    /* Start sending unicast ESPNOW data. */
                            memcpy(espnow_ctx->dest_mac, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                            example_espnow_data_prepare(espnow_ctx, 0);
                            if (esp_now_send(espnow_ctx->dest_mac, espnow_ctx->buffer, espnow_ctx->len) != ESP_OK) {
                                ESP_LOGE(TAG, "Send error");
                                example_espnow_deinit(espnow_ctx);
                                vTaskDelete(NULL);
                            }
                            else {
                                espnow_ctx->broadcast = false;
                                espnow_ctx->unicast = true;
                            }
                        }
                    }
                }
                else if (ret == EXAMPLE_ESPNOW_DATA_UNICAST) {
                    ESP_LOGI(TAG, "Receive %dth unicast data from: "MACSTR", len: %d", recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

                    /* If receive unicast ESPNOW data, also stop sending broadcast ESPNOW data. */
                    espnow_ctx->broadcast = false;
                }
                else {
                    ESP_LOGI(TAG, "Receive error data from: "MACSTR"", MAC2STR(recv_cb->mac_addr));
                }
                break;
            }
            default:
                ESP_LOGE(TAG, "Callback type error: %d", evt.id);
                break;
        }
    }
}

void espnow_send_task(void *pvParameter){
    espnow_ctx_t *espnow_ctx = (espnow_ctx_t *)pvParameter;
    uint32_t value;
    BaseType_t received;
    example_espnow_data_t *data = (example_espnow_data_t*)espnow_ctx->buffer;
    while (1){
    received = xTaskNotifyWaitIndexed(SETPOINT_SLEW_NOTIFY_INDEX, 0, 0, &value, 201);
        if (received) {
            // ESP_LOGI(TAG, "Received data to send via ESPNOW %lu", value);
            // data->payload[0] = (uint8_t) value;
            example_espnow_data_prepare(espnow_ctx, (uint8_t) value);
            // ESP_LOG_BUFFER_HEX(TAG, espnow_ctx->buffer, espnow_ctx->len);
            esp_now_send(espnow_ctx->dest_mac, espnow_ctx->buffer, espnow_ctx->len);
        }
    }
}

esp_err_t setup_espnow_common(TaskHandle_t *sending_minitask_handle, TaskHandle_t mainloop_task)
{
    
    s_example_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(example_espnow_event_t));
    if (s_example_espnow_queue == NULL) {
        ESP_LOGE(TAG, "Create mutex fail");
        return ESP_FAIL;
    }

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK( esp_now_init() );
    
    // ESP_ERROR_CHECK( esp_now_register_send_cb(example_espnow_send_cb) );

    /* Set primary master key. */
    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK) );

    /* Add broadcast peer information to peer list. */
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, s_example_broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK( esp_now_add_peer(peer) );
    free(peer);


    /* Initialize sending parameters. */
    espnow_ctx_t *espnow_ctx = malloc(sizeof(espnow_ctx_t));
    if (espnow_ctx == NULL) {
        ESP_LOGE(TAG, "Malloc send parameter fail");
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(espnow_ctx, 0, sizeof(espnow_ctx_t));
    espnow_ctx->unicast = false;
    espnow_ctx->broadcast = true;
    espnow_ctx->state = 0;
    espnow_ctx->magic = esp_random();
    espnow_ctx->count = CONFIG_ESPNOW_SEND_COUNT;
    espnow_ctx->delay = CONFIG_ESPNOW_SEND_DELAY;
    espnow_ctx->len = sizeof(example_espnow_data_t);
    espnow_ctx->buffer = malloc(CONFIG_ESPNOW_SEND_LEN);
    espnow_ctx->mainloop_task = mainloop_task;
    if (espnow_ctx->buffer == NULL) {
        ESP_LOGE(TAG, "Malloc send buffer fail");
        free(espnow_ctx);
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memcpy(espnow_ctx->dest_mac, s_example_broadcast_mac, ESP_NOW_ETH_ALEN);
    example_espnow_data_prepare(espnow_ctx, 0);

    // TaskHandle_t temptask;

    xTaskCreate(espnow_receive_queue_task, "espnow_receive_queue_task", 8192 , espnow_ctx, 4, NULL);
    xTaskCreate(espnow_send_task, "espnow_send_task", 4096, espnow_ctx, 4, sending_minitask_handle);
    // *sending_minitask_handle = temptask;
    return ESP_OK;
}

void setup_espnow_receiver(){
    
    ESP_ERROR_CHECK( esp_now_register_recv_cb(example_espnow_recv_cb) );
}

void setup_espnow_transmitter(){

}

static void example_espnow_deinit(espnow_ctx_t *espnow_ctx)
{
    free(espnow_ctx->buffer);
    free(espnow_ctx);
    vSemaphoreDelete(s_example_espnow_queue);
    esp_now_deinit();   
}

