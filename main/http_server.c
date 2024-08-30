/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "http_server.h"
#include "freertos/timers.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "esp_app_format.h"
// #include "esp_system.h"

#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
// #include "protocol_examples_common.h"
#include "utils.c"
// #include "esp_tls_crypto.h"
#include <esp_http_server.h>
#include "esp_event.h"
#include "driver/gpio.h"
#include "base.h"
#include "esp_netif.h"
// #include "esp_tls.h"
#include "dali.h"
#include "dali_utils.h"
// #include "base.h"

#include <esp_wifi.h>
#include <esp_system.h>
#include "nvs_flash.h"
#include "html.c"
#include "http_server.h"
#include "settings.h"
#include "gpio_utils.h"
#include "http_file_handling.h"

#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN (64)

static const char *TAG = "http server";

#define REC_BUF_SIZE 256

#define NO_DIGITS_FOUND -98765413

static device_status_t *status;

static char uri[64];
static int slashes[10];
static int idx_start;
static int idx_end;
static char substrings[5][32];
static int uri_segment_count;
static int substring_count;
static int substring_ints[5];

void parse_uri(const char *uri)
{
    for (int i = 0; i < 5; i++)
    {
        substring_ints[i] = GET_SETTING_NOT_FOUND;
    }
    ESP_LOGD(TAG, "Parsing URI '%s'", uri);
    uri_segment_count = -1;
    int scanint;
    int valid;
    for (int i = 0; i <= strlen(uri); i++)
    {
        if (uri[i] == '/' || uri[i] == 0)
        {
            if (slashes[uri_segment_count] == i - 1)
            {
                // we have a double slash or EOL
                slashes[uri_segment_count] = i;
                continue;
            }
            uri_segment_count += 1;
            slashes[uri_segment_count] = i;
            // printf("found slash at %i\n", i);
            if (uri_segment_count > 4)
                break;
            if (uri_segment_count > 0)
            {
                idx_start = slashes[uri_segment_count - 1] + 1;
                idx_end = i;
                // printf("i %i, start %i, end %i\n", i, idx_start, idx_end);
                strncpy(substrings[uri_segment_count - 1], uri + idx_start, idx_end - idx_start);
                substrings[uri_segment_count - 1][idx_end - idx_start] = 0;
                valid = sscanf(substrings[uri_segment_count - 1], "%i", &scanint);
                if (valid)
                {
                    substring_ints[uri_segment_count - 1] = scanint;
                }
            }
        }
    }
    // printf("Found %i slashes\n", slashcount);
    substring_count = uri_segment_count;
    // for (int i=0; i< substring_count; i++)
    // {
    //     ESP_LOGI(TAG, "Substring %i is '%s' (int %i)", i, substrings[i], substring_ints[i]);
    // }
}

static esp_err_t nvs_get_handler(httpd_req_t *req)
{
    parse_uri(req->uri);
    int value = GET_SETTING_NOT_FOUND;
    // int* reg = get_endpoint_ptr();
    int index = substring_ints[2];

    if (substring_count == 2)
    {
        value = get_setting(substrings[1]);
    }
    else if (substring_count == 3 && substring_ints[2] != GET_SETTING_NOT_FOUND)
    {
        value = get_setting_indexed(substrings[1], index);
    }
    else
    {
        value = GET_SETTING_NOT_FOUND;
    }
    if (value == GET_SETTING_NOT_FOUND)
    {
        sprintf(httpd_temp_buffer, "Not found");
        // ESP_LOGI(TAG, "%s: URI '%s', setting NOT FOUND", req->uri);

        httpd_resp_set_status(req, HTTPD_404);
        httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    }
    else
    {
        sprintf(httpd_temp_buffer, "%i", value);
        // ESP_LOGI(TAG, "%s: URI '%s', returning %i", req->uri, value);
    }

    httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t nvs_put_handler(httpd_req_t *req)
{
    parse_uri(req->uri);

    int bytes = httpd_req_recv(req, httpd_temp_buffer, 255);
    httpd_temp_buffer[bytes] = 0;
    int put_data_int;
    int datarecv = sscanf(httpd_temp_buffer, "%i", &put_data_int);
    ESP_LOGI(TAG, "===== REST API Handler: received PUT at %s (%s)", req->uri, httpd_temp_buffer);
    ESP_LOGD(TAG, "Received %s (%i) %i", httpd_temp_buffer, put_data_int, datarecv);
    int value = GET_SETTING_NOT_FOUND;

    int index = substring_ints[2];

    if (datarecv != 1)
    {
        httpd_resp_set_status(req, HTTPD_400);
        sprintf(httpd_temp_buffer, "No int found");
        httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    if (substring_count == 2)
    {
        ESP_LOGD(TAG, "Setting key %s to %i", substrings[1], put_data_int);
        set_setting(substrings[1], put_data_int);
    }
    else if (substring_count == 3 && substring_ints[2] != GET_SETTING_NOT_FOUND)
    {
        ESP_LOGD(TAG, "Setting key %s/%i to %i", substrings[1], index, put_data_int);
        set_setting_indexed(substrings[1], index, put_data_int);
        // ESP_LOGI(TAG, "Setting %s index %i from %s", substrings[0], index, substrings[1]);
    }
    if (strcmp(substrings[1], "lutfile") == 0)
    {
        ESP_LOGI(TAG, "Detected change of lutfile, reloading luts...");

        networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
        read_level_luts(ctx->status->lut);
    }

    sprintf(httpd_temp_buffer, "OK");
    httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);

    networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
    xTaskNotifyIndexed(ctx->mainloop_task, NEW_SETPOINT_NOTIFY_IDX, 0, eNoAction);
    return ESP_OK;
}

static esp_err_t current_setpoint_handler(httpd_req_t *req)
{
    parse_uri(req->uri);
    if (req->method == HTTP_GET)
    {
        networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
        sprintf(httpd_temp_buffer, "%i", ctx->status->setpoint);
    }
    else if (req->method == HTTP_PUT)
    {
        int bytes = httpd_req_recv(req, httpd_temp_buffer, 254);
        httpd_temp_buffer[bytes] = 0;
        int data;
        int datarecv = sscanf(httpd_temp_buffer, "%i", &data);
        ESP_LOGI(TAG, "Setpoint handler: received PUT at %s (%s)", req->uri, httpd_temp_buffer);
        ESP_LOGD(TAG, "Received %s (%i) %i", httpd_temp_buffer, data, datarecv);
        if (data >= 0 && data <= 255)
        {
            int fade;
            if (substring_count == 2 && (strcmp(substrings[1], "slow") == 0))
            {
                fade = USE_SLOW_FADETIME;
            }
            else
            {
                fade = USE_DEFAULT_FADETIME;
            }
            networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
            ctx->status->fadetime_ms = fade;
            ctx->status->setpoint = data;
            ctx->status->setpoint_source = SETPOINT_SOURCE_REST;
            // uint32_t setpoint_struct_as_int = *((uint32_t*) &setp);
            xTaskNotifyIndexed(ctx->mainloop_task, NEW_SETPOINT_NOTIFY_IDX, SETPOINT_SOURCE_REST, eSetValueWithOverwrite);
            ESP_LOGI(TAG, "Set new setpoint %i", data);
            sprintf(httpd_temp_buffer, "OK");
        }
        else
        {
            ESP_LOGW(TAG, "=== Bad setpoint %i", data);
            httpd_resp_set_status(req, HTTPD_400);
            ESP_LOGI(TAG, "Received %s (%i) %i", httpd_temp_buffer, data, datarecv);
            sprintf(httpd_temp_buffer, "Setpoint must be 0 <= sp <= 254 (or 255 for power off)");
        }
    }
    httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t otaupdate(httpd_req_t *req)
{
    // return httpd_resp_send_500(req);
    // char* buffer = malloc(1024);
    parse_uri(req->uri);
    int bytes;
    ESP_LOGI(TAG, "Received POST OTAUpdate %i bytes", req->content_len);

    esp_err_t err;
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG, "Starting OTA example");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running)
    {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08" PRIx32 ", but running from offset 0x%08" PRIx32,
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08" PRIx32 ")",
             running->type, running->subtype, running->address);

    update_partition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%" PRIx32,
             update_partition->subtype, update_partition->address);
    assert(update_partition != NULL);

    esp_app_desc_t new_app_info;
    bool image_header_was_checked = false;
    int binary_file_length = 0;
    int leds = 0;

    while (1)
    {
        bytes = httpd_req_recv(req, httpd_temp_buffer, _MIN(req->content_len, BUF_SIZE));
        if (bytes > 1)
        {
            ESP_LOGI(TAG, "Recv %i bytes", bytes);
            // printf(".");
            // fflush(stdout);
            if (image_header_was_checked == false)
            {
                esp_app_desc_t new_app_info;
                if (bytes > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))
                {
                    // check current version with downloading
                    memcpy(&new_app_info, &httpd_temp_buffer[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                    ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                    esp_app_desc_t running_app_info;
                    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK)
                    {
                        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
                    }

                    const esp_partition_t *last_invalid_app = esp_ota_get_last_invalid_partition();
                    esp_app_desc_t invalid_app_info;
                    if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK)
                    {
                        ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
                    }
                    image_header_was_checked = true;

                    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                        return httpd_resp_send_500(req);
                    }
                    ESP_LOGI(TAG, "esp_ota_begin succeeded");

                    gpio_set_level(LED1_GPIO, (leds & 8) > 0);
                    gpio_set_level(LED2_GPIO, (leds & 16) > 0);
                }
                else
                {
                    ESP_LOGE(TAG, "received package is not fit len");
                    return httpd_resp_send_500(req);
                }
            }
            err = esp_ota_write(update_handle, (const void *)httpd_temp_buffer, bytes);
            if (err != ESP_OK)
            {
                return httpd_resp_send_500(req);
            }
            binary_file_length += bytes;
            ESP_LOGD(TAG, "Written image length %d", binary_file_length);
            leds += 1;
            gpio_set_level(LED1_GPIO, (leds & 16) > 0);
            gpio_set_level(LED2_GPIO, ((leds + 8) & 16) > 0);
        }
        else
        {
            ESP_LOGI(TAG, "Recv returned 0 bytes");
            sprintf(httpd_temp_buffer, "OK");
            httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);

            err = esp_ota_end(update_handle);
            if (err != ESP_OK)
            {
                if (err == ESP_ERR_OTA_VALIDATE_FAILED)
                {
                    ESP_LOGE(TAG, "Image validation failed, image is corrupted");
                }
                ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
                return httpd_resp_send_500(req);
            }

            err = esp_ota_set_boot_partition(update_partition);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
                return httpd_resp_send_500(req);
            }
            if (strcmp(substrings[1], "stale") == 0)
            {
                ESP_LOGI(TAG, "Setting stale flag");
                status->stale_flag = STATUS_STALE_FLAG;
                // esp_system_abort("Just wanted to restart properly");
            }
            esp_restart();
            return ESP_OK;
        }
        vTaskDelay(1);
    }

    sprintf(httpd_temp_buffer, "OK");
    httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    // free(buffer);
    return ESP_OK;
}

static TimerHandle_t delaytimer;
static int timercount;

static void poweroff(TimerHandle_t timer)
{
    ESP_LOGI(TAG, "Timer expired -> Turning power off");
    status->power_on = 0;
    xTaskNotifyIndexed(status->mainloop_task, NEW_SETPOINT_NOTIFY_IDX, SETPOINT_SOURCE_REST, eSetValueWithOverwrite);
}

static esp_err_t rest_power_handler(httpd_req_t *req)
{
    bool put = req->method == HTTP_PUT;
    networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
    bool notify = true;
    BaseType_t success = pdTRUE;
    if (put)
    {
        parse_uri(req->uri);
        int put_data_int = -2;
        int bytes = httpd_req_recv(req, httpd_temp_buffer, 255);
        httpd_temp_buffer[bytes] = 0;
        int datarecv = sscanf(httpd_temp_buffer, "%i", &put_data_int);

        int level = clamp(put_data_int, 0, 254);
        ESP_LOGI(TAG, "=== Received PUT at %s (%s) -> %i", req->uri, httpd_temp_buffer, put_data_int);
        if (substring_count == 3)
        {
            if (strcmp(substrings[2], "setlevel") == 0)
            {
                ESP_LOGI(TAG, "Settng power_on to %i", level);
                ctx->status->setpoint = level;
                ctx->status->fadetime_ms = 0;
                ctx->status->setpoint_source = SETPOINT_SOURCE_REST;
                ctx->status->power_on = 1;
            }
            else if (strcmp(substrings[2], "delay") == 0)
            {
                if (put_data_int < 1000)
                {
                    poweroff(delaytimer);
                }
                else
                {
                    ESP_LOGI(TAG, "Starting turn-off timer for %i ms", put_data_int);
                    if (delaytimer == 0)
                    {
                        delaytimer = xTimerCreate("poweroff-delay", pdMS_TO_TICKS(put_data_int), false, &timercount, poweroff);
                    }
                    else
                    {
                        success = xTimerChangePeriod(delaytimer, pdMS_TO_TICKS(put_data_int), 0);
                    }
                    success = (success == pdTRUE) && xTimerStart(delaytimer, 0);
                    if (success != pdTRUE)
                    {
                        httpd_resp_send_500(req);
                        return ESP_FAIL;
                    }
                    ESP_LOGI(TAG, "Started!");

                    // if (ctx->status->setpoint > 15)
                    // ctx->status->setpoint = _MAX(ctx->status->setpoint >> 2, 15);
                    // ctx->status->fadetime_ms = 1000;
                    // ctx->status->setpoint_source = SETPOINT_SOURCE_REST;
                    // xTaskNotifyIndexed(ctx->status->mainloop_task, NEW_SETPOINT_NOTIFY_IDX, SETPOINT_SOURCE_REST, eSetValueWithOverwrite);
                }

                notify = false;
            }
            else
            {
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        }
        else
        {
            if (put_data_int > 0)
            {
                ESP_LOGI(TAG, "Turning power on at existing level");
                ctx->status->power_on = 1;
            }
            else
            {

                ESP_LOGI(TAG, "Turning power off");
                ctx->status->power_on = 0;
            }
        }

        sprintf(httpd_temp_buffer, "OK");
        if (notify)
        {
            xTaskNotifyIndexed(ctx->status->mainloop_task, NEW_SETPOINT_NOTIFY_IDX, SETPOINT_SOURCE_REST, eSetValueWithOverwrite);
            if (delaytimer != 0)
            {
                success = xTimerStop(delaytimer, 0);
                if (success != pdTRUE)
                {
                    httpd_resp_send_500(req);
                }
            }
        }
    }
    else
    {
        if (delaytimer != 0 && xTimerIsTimerActive(delaytimer) && ctx->status->power_on)
        {
            sprintf(httpd_temp_buffer, "Timer on");
        }
        else
        {
            sprintf(httpd_temp_buffer, "%i", ctx->status->power_on);
        }
    }
    httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t rest_channel_override_handler(httpd_req_t *req)
{
    parse_uri(req->uri);
    bool put = req->method == HTTP_PUT;
    int put_data_int = -1;

    ESP_LOGI(TAG, "===== Received GET at %s", req->uri);
    const char *channelname = substrings[2];
    int16_t *override_ptr;
    if (put)
    {

        int bytes = httpd_req_recv(req, httpd_temp_buffer, 255);
        httpd_temp_buffer[bytes] = 0;
        int datarecv = sscanf(httpd_temp_buffer, "%i", &put_data_int);
        ESP_LOGI(TAG, "===== Received PUT at %s (%s)", req->uri, httpd_temp_buffer);
        ESP_LOGD(TAG, "Received %s (%i) %i", httpd_temp_buffer, put_data_int, datarecv);
        ESP_LOGI(TAG, "Trying to set override for channel %s: %i", channelname, put_data_int);
    }
    char daliname[] = "DALIX";
    const char *chname;
    networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
    if (strncmp(channelname, "dali", 4) == 0)
    {
        int8_t channel_num = channelname[4] - 'a';
        if (channel_num >= 0 && channel_num < DALI_CHANNELS)
        {
            ESP_LOGI(TAG, "Getting override pointer for DALI %i", channel_num);
            override_ptr = &ctx->status->level_overrides.dali[channel_num];
            chname = daliname;
            daliname[4] = 'A' + (char)channel_num;
            ESP_LOGI(TAG, "chname is %s", chname);
        }
        else
        {
            ESP_LOGI(TAG, "Didn't recognise DALI channel %s", channelname);
            return httpd_resp_send_404(req);
        }
    }
    else if (strcmp(channelname, "espnow") == 0)
    {
        override_ptr = &ctx->status->level_overrides.espnow;
        chname = "ESPNOW";
    }
    else if (strcmp(channelname, "zeroten1") == 0)
    {
        override_ptr = &ctx->status->level_overrides.zeroten1;
        chname = "0-10v 1";
    }
    else if (strcmp(channelname, "zeroten2") == 0)
    {
        override_ptr = &ctx->status->level_overrides.zeroten2;
        chname = "0-10v 2";
    }
    else
    {
        ESP_LOGI(TAG, "Didn't recognise channel %s", channelname);
        return httpd_resp_send_404(req);
    }
    if (put)
    {
        *override_ptr = (int16_t)put_data_int;
        sprintf(httpd_temp_buffer, "OK");
        ESP_LOGI(TAG, "PUT: Set %s to %i", chname, put_data_int);
        xTaskNotifyIndexed(ctx->mainloop_task, NEW_SETPOINT_NOTIFY_IDX, 0, eNoAction);
    }
    else
    {
        sprintf(httpd_temp_buffer, "%i", *override_ptr);
        ESP_LOGI(TAG, "GET: %s is %i", chname, *override_ptr);
    }

    httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t view_luts(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    sprintf(httpd_temp_buffer, "Lvl 0-10v1 0-10v2 DALIA DALIB DALIC DALID DALIE DALIF DALIG DALIH ESPNOW Rly1 Rly2   R   G   B\n");
    httpd_resp_sendstr_chunk(req, httpd_temp_buffer);
    networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
    static_assert(DALI_CHANNELS == 8, "DALI Channels != 8");
    for (int i = 0; i <= 254; i++)
    {
        level_t lev = ctx->status->lut[i];
        sprintf(httpd_temp_buffer, "%3.1i    %3.1d    %3.1d   %3.1d   %3.1d   %3.1d   %3.1d   %3.1d   %3.1d   %3.1d   %3.1d    %3.1d  %3.1d  %3.1d %3.1d %3.1d %3.1d\n",
                i,
                lev.zeroten1_lvl,
                lev.zeroten2_lvl,
                lev.dali_lvl[0],
                lev.dali_lvl[1],
                lev.dali_lvl[2],
                lev.dali_lvl[3],
                lev.dali_lvl[4],
                lev.dali_lvl[5],
                lev.dali_lvl[6],
                lev.dali_lvl[7],
                lev.espnow_lvl,
                lev.relay1,
                lev.relay2,
                lev.r,
                lev.g,
                lev.b);
        httpd_resp_sendstr_chunk(req, httpd_temp_buffer);
    }
    httpd_resp_send_chunk(req, httpd_temp_buffer, 0);
    return ESP_OK;
}

static esp_err_t restart(httpd_req_t *req)
{
    esp_restart();
    return ESP_OK;
}

static esp_err_t logbuffer_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send_chunk(req, logbuffer + logbufferpos, LOGBUFFER_SIZE - logbufferpos);
    httpd_resp_send_chunk(req, logbuffer, logbufferpos);
    httpd_resp_send_chunk(req, logbuffer, 0);
    return ESP_OK;
}

static void send_block_plaintext(httpd_req_t *req, char *buf, int start, int end)
{
    if ((end - start) <= 0)
        return;

    char inbyte;
    for (int bytepos = start; bytepos < end; bytepos++)
    {
        bool send = false;
        inbyte = buf[bytepos];
        if (inbyte == '\n')
        {
            send = true;
        }

        else if (inbyte >= 32)
        {
            send = true;
        }
        if (send)
        {
            httpd_temp_buffer[bytepos - start] = inbyte;
        }
        else
        {
            httpd_temp_buffer[bytepos - start] = 32;
        }
    }
    httpd_resp_send_chunk(req, httpd_temp_buffer, end - start);
}

static esp_err_t logbuffer_plaintext_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    int blockstart = logbufferpos;
    int blocksize = _MIN(1024, LOGBUFFER_SIZE);
    while (1)
    {
        send_block_plaintext(req, logbuffer, blockstart, blockstart + blocksize);
        blockstart += blocksize;
        if ((blockstart + blocksize) >= LOGBUFFER_SIZE)
        {
            send_block_plaintext(req, logbuffer, blockstart, LOGBUFFER_SIZE);
            break;
        }
    }
    blockstart = 0;
    while (1)
    {
        send_block_plaintext(req, logbuffer, blockstart, blockstart + blocksize);
        blockstart += blocksize;
        if ((blockstart + blocksize) >= logbufferpos)
        {
            send_block_plaintext(req, logbuffer, blockstart, logbufferpos);
            break;
        }
    }
    httpd_resp_send_chunk(req, logbuffer, 0);
    return ESP_OK;
}

static esp_err_t deal_with_command_response(httpd_req_t *req)
{
    dali_command_return_t retr;
    uint32_t intretr;
    BaseType_t received = xTaskNotifyWaitIndexed(DALI_COMMAND_RETURN_INDEX, 0, 0, &intretr, pdMS_TO_TICKS(30000));
    memcpy(&retr, &intretr, sizeof(dali_command_return_t));

    if (received != pdTRUE)
    {
        httpd_resp_set_status(req, HTTPD_408);
        httpd_resp_send(req, "Timed out", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_NOT_FINISHED;
    }
    if (retr.err)
    {
        sprintf(httpd_temp_buffer, "Error during command (%u)", retr.err);
        httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    sprintf(httpd_temp_buffer, "%i", retr.value);
    httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t commission(httpd_req_t *req)
{
    networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
    dali_command_t command = {
        .command = DALI_COMMAND_COMMISSION,
        .address = 0,
        .notify_task = xTaskGetCurrentTaskHandle()};
    if (ctx->dali_command_queue != NULL)
    {
        xQueueSend(ctx->dali_command_queue, &command, pdMS_TO_TICKS(5000));
        return deal_with_command_response(req);
    }
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

static esp_err_t reset_factory(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Resetting NVS!!!");
    nvs_flash_erase_partition("nvs2");
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t dali_commands_handler(httpd_req_t *req)
{
    networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
    parse_uri(req->uri);
    bool post = req->method == HTTP_POST;
    bool get = req->method == HTTP_GET;
    int data = -1;
    int address = substring_ints[2];
    if (post)
    {
        int bytes = httpd_req_recv(req, httpd_temp_buffer, 255);
        httpd_temp_buffer[bytes] = 0;
        int datarecv = sscanf(httpd_temp_buffer, "%i", &data);
        ESP_LOGI(TAG, "===== Received POST at %s (%s)", req->uri, httpd_temp_buffer);
        ESP_LOGD(TAG, "Received %s (%i) %i", httpd_temp_buffer, data, datarecv);
    }
    else if (get)
    {
        ESP_LOGI(TAG, "===== Received GET at %s, address %i", req->uri, address);
    }
    else
    {
        return ESP_FAIL;
    }
    uint8_t cmd = 255;
    dali_command_t command = {
        .command = cmd,
        .address = address,
        .value = post ? data : 0,
        .notify_task = xTaskGetCurrentTaskHandle()};
    bool not_allowed = false;
    if (strcmp(substrings[1], "power-on-level") == 0)
    {
        if (post)
        {
            command.command = DALI_COMMAND_SET_POWER_ON_LEVEL;
            ESP_LOGI(TAG, "Setting power on level for address %d to %d", command.address, command.value);
        }
        else
        {
            command.command = DALI_COMMAND_GET_POWER_ON_LEVEL;
            ESP_LOGI(TAG, "Getting power on level for address %d", command.address);
        }
    }
    else if (strcmp(substrings[1], "failsafe-level") == 0)
    {
        if (get)
        {
            not_allowed = true;
        }
        else
        {
            command.command = DALI_COMMAND_SET_FAILSAFE_LEVEL;
            ESP_LOGI(TAG, "Setting failsafe level for address %d to %d", command.address, command.value);
        }
    }
    else if (strcmp(substrings[1], "fade-time") == 0)
    {
        if (post)
        {
            command.command = DALI_COMMAND_SET_FADE_TIME;
            ESP_LOGI(TAG, "Setting fade time for address %d to %d", command.address, command.value);
        }
        else
        {
            command.command = DALI_COMMAND_GET_FADE_TIME;
            ESP_LOGI(TAG, "Querying fade time for address %d to %d", command.address, command.value);
        }
    }
    else if (strcmp(substrings[1], "commission") == 0)
    {
        if (get)
        {
            not_allowed = true;
        }
        else
        {
            command.command = DALI_COMMAND_COMMISSION;
            ESP_LOGI(TAG, "Commissioning all devices assigning short address from %d", command.value);
        }
    }
    else if (strcmp(substrings[1], "find-new-devices") == 0)
    {
        if (get)
        {
            not_allowed = true;
        }
        else
        {
            command.command = DALI_COMMAND_FIND_NEW_DEVICES;
            ESP_LOGI(TAG, "Finding new devices - assigning short addresses from %d", command.value);
        }
    }
    else if (strcmp(substrings[1], "add-to-group") == 0)
    {
        if (get)
        {
            not_allowed = true;
        }
        else
        {
            command.command = DALI_COMMAND_ADD_TO_GROUP;
            ESP_LOGI(TAG, "Adding short addr %d to group %d", command.address, command.value);
        }
    }
    else if (strcmp(substrings[1], "reset-device") == 0)
    {
        if (get)
        {
            not_allowed = true;
        }
        else
        {
            command.command = DALI_COMMAND_RESET_DEVICE;
            ESP_LOGI(TAG, "Resetting device addr %d to default settings", command.address);
        }
    }
    else
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    if (not_allowed)
    {
        httpd_resp_set_status(req, "405 Method Not Allowed");
        httpd_resp_send(req, "Does not support GET", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    if (ctx->dali_command_queue != NULL)
    {
        xQueueSend(ctx->dali_command_queue, &command, pdMS_TO_TICKS(1000));
        return deal_with_command_response(req);
    }
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

static const httpd_uri_t files = {
    .uri = "/?*",
    .method = HTTP_GET,
    .handler = file_handler,
    .user_ctx = NULL};
static const httpd_uri_t file_upload = {
    .uri = "/spiffs/?*",
    .method = HTTP_POST,
    .handler = file_uploader,
    .user_ctx = NULL};
static const httpd_uri_t file_del = {
    .uri = "/spiffs/?*",
    .method = HTTP_DELETE,
    .handler = file_uploader,
    .user_ctx = NULL};
static const httpd_uri_t get_setpoint = {
    .uri = "/setpoint?*",
    .method = HTTP_GET,
    .handler = current_setpoint_handler,
    .user_ctx = NULL};
static const httpd_uri_t put_setpoint = {
    .uri = "/setpoint?*",
    .method = HTTP_PUT,
    .handler = current_setpoint_handler,
    .user_ctx = NULL};
static const httpd_uri_t rest_get = {
    .uri = "/nvs/?*",
    .method = HTTP_GET,
    .handler = nvs_get_handler,
    .user_ctx = NULL};
static const httpd_uri_t log_get = {
    .uri = "/logterm?*",
    .method = HTTP_GET,
    .handler = logbuffer_handler,
    .user_ctx = NULL};
static const httpd_uri_t log_get_plaintext = {
    .uri = "/log",
    .method = HTTP_GET,
    .handler = logbuffer_plaintext_handler,
    .user_ctx = NULL};
static const httpd_uri_t rest_put = {
    .uri = "/nvs/?*",
    .method = HTTP_PUT,
    .handler = nvs_put_handler,
    .user_ctx = NULL};
static const httpd_uri_t rest_put_channel_level = {
    .uri = "/api/channel/?*",
    .method = HTTP_PUT,
    .handler = rest_channel_override_handler,
    .user_ctx = NULL};
static const httpd_uri_t rest_get_channel_level = {
    .uri = "/api/channel/?*",
    .method = HTTP_GET,
    .handler = rest_channel_override_handler,
    .user_ctx = NULL};
static const httpd_uri_t get_power_on_endpoint = {
    .uri = "/api/power",
    .method = HTTP_GET,
    .handler = rest_power_handler,
    .user_ctx = NULL};
static const httpd_uri_t put_power_on_endpoint = {
    .uri = "/api/power?*",
    .method = HTTP_PUT,
    .handler = rest_power_handler,
    .user_ctx = NULL};
static const httpd_uri_t view_luts_endpoint = {
    .uri = "/luts",
    .method = HTTP_GET,
    .handler = view_luts,
    .user_ctx = NULL};
static const httpd_uri_t restart_endpoint = {
    .uri = "/restart",
    .method = HTTP_POST,
    .handler = restart,
    .user_ctx = NULL};
static const httpd_uri_t dali_command_post_endpoint = {
    .uri = "/dali/*?",
    .method = HTTP_POST,
    .handler = dali_commands_handler,
    .user_ctx = NULL};
static const httpd_uri_t dali_command_get_endpoint = {
    .uri = "/dali/*?",
    .method = HTTP_GET,
    .handler = dali_commands_handler,
    .user_ctx = NULL};
static const httpd_uri_t reset_to_factory_endpoint = {
    .uri = "/factory",
    .method = HTTP_POST,
    .handler = reset_factory,
    .user_ctx = NULL};
static const httpd_uri_t post_ota = {
    .uri = "/otaupdate*?",
    .method = HTTP_POST,
    .handler = otaupdate,
    .user_ctx = NULL};

static void nullfree(void *c)
{
}
static httpd_handle_t start_webserver(networking_ctx_t *ctx)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.global_user_ctx = ctx;
    config.global_user_ctx_free_fn = nullfree;
    config.max_open_sockets = 13;
    config.max_uri_handlers = 21;
    config.lru_purge_enable = true;
    status = ctx->status;
    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &get_setpoint);
        httpd_register_uri_handler(server, &put_setpoint);
        httpd_register_uri_handler(server, &get_power_on_endpoint);
        httpd_register_uri_handler(server, &put_power_on_endpoint);
        httpd_register_uri_handler(server, &post_ota);
        httpd_register_uri_handler(server, &view_luts_endpoint);
        httpd_register_uri_handler(server, &reset_to_factory_endpoint);
        httpd_register_uri_handler(server, &dali_command_post_endpoint);
        httpd_register_uri_handler(server, &dali_command_get_endpoint);
        httpd_register_uri_handler(server, &restart_endpoint);
        httpd_register_uri_handler(server, &rest_put_channel_level);
        httpd_register_uri_handler(server, &rest_get_channel_level);
        httpd_register_uri_handler(server, &rest_get);
        httpd_register_uri_handler(server, &rest_put);
        httpd_register_uri_handler(server, &log_get);
        httpd_register_uri_handler(server, &log_get_plaintext);
        httpd_register_uri_handler(server, &files);
        httpd_register_uri_handler(server, &file_upload);
        httpd_register_uri_handler(server, &file_del);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    esp_err_t e = httpd_stop(server);
    vTaskDelay(2000);
    return e;
}

static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    handler_ctx_t *ctx = (handler_ctx_t *)arg;
    if (ctx->server)
    {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(ctx->server) == ESP_OK)
        {
            ctx->server = NULL;
        }
        else
        {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    handler_ctx_t *ctx = (handler_ctx_t *)arg;
    if (ctx->server == NULL)
    {
        ESP_LOGI(TAG, "Starting webserver");
        ctx->server = start_webserver(ctx->networking_ctx);
    }
}

httpd_handle_t setup_httpserver(networking_ctx_t *networking_ctx)
{
    static httpd_handle_t server = NULL;
    handler_ctx_t *handler_ctx = malloc(sizeof(handler_ctx_t));
    handler_ctx->server = server;
    handler_ctx->networking_ctx = networking_ctx;
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, handler_ctx));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, handler_ctx));

    handler_ctx->server = start_webserver(networking_ctx);
    return server;
}
