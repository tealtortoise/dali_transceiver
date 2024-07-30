/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "http_server.h"
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

#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN  (64)

/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */

static const char *TAG = "http server";

#define BUF_SIZE 0x1000

#define REC_BUF_SIZE 256

#define NO_DIGITS_FOUND -98765413

static char httpd_temp_buffer[BUF_SIZE];

char filename[128];

static char templogbuffer[1024];

extern int setpoint;

int get_int_from_uri(char* uri){
    char numberlevel[5];
    int digits = 0;
    int ns = 0;
    int urilen = strnlen(uri, 20);
    uint8_t byte;
    if (urilen > 7) {
        for (int i = 0; i <5; i++){
            byte = uri[7 + i];
            if ( byte >= '0' && byte <= '9'){
                digits += 1;
                numberlevel[i] = byte;
            }
            else
            {
                numberlevel[i] = 0;
                break;
            }
        }
        if (digits > 0){
            ns = atoi(numberlevel);
            // ESP_LOGI(TAG, "Found digits in URI %s", numberlevel);
        }
    }
    if (digits > 0) return ns;
    return NO_DIGITS_FOUND;
}


static char uri[64];
static int slashes[10];
static int idx_start;
static int idx_end;
static char substrings[5][32];
static int slashcount;
static int substring_count;
static int substring_ints[5];

void parse_uri(char* uri){
    for (int i = 0; i < 5; i++)
    {
        substring_ints[i] = GET_SETTING_NOT_FOUND;
    }
    // printf("URI: %s\n", uri);
    slashcount = 0;
    int scanint;
    int valid;
    for (int i = 0; i<strlen(uri); i++){
        if (uri[i] == '/'){
            slashes[slashcount] = i;
            slashcount += 1;
            // printf("found slash at %i\n", i);
            if (slashcount >4) break;
            if (slashcount > 2){
                idx_start = slashes[slashcount - 2] + 1;
                idx_end = i;
                // printf("i %i, start %i, end %i\n", i, idx_start, idx_end);
                strncpy(substrings[slashcount - 3], uri + idx_start, idx_end - idx_start);
                substrings[slashcount - 3][idx_end - idx_start] = 0;
                valid = sscanf(substrings[slashcount - 3], "%i", &scanint);
                if (valid) {
                    substring_ints[slashcount - 3] = scanint;
                }
            }
        }
    }
    // printf("Found %i slashes\n", slashcount);
    substring_count = slashcount - 2;
    // printf("substr1 %s\n", substrings[0]);
    // printf("substr2 %s\n", substrings[1]);
    // printf("substr3 %s\n", substrings[2]);
    // printf("subint1 %i\n", substring_ints[0]);
    // printf("subint2 %i\n", substring_ints[1]);
    // printf("subint3 %i\n", substring_ints[2]);
}

// int* get_endpoint_ptr(){
//     bool nomatch;
//     int* reg;
//     for (int i=0; i < NUM_ENDPOINTS; i++){
//         // registers[i].name[15] = 0;
//         // ESP_LOGI(TAG, "Looking for register '%s' with '%s'", substrings[0], registers[i].name);
//         nomatch = strcmp(substrings[0], registers[i].name);
//         if (!nomatch){
//             ESP_LOGI(TAG, "Matched %s", substrings[0]);
//             reg = registers[i].array;
//             return reg;
//         }
//     }
//     ESP_LOGI(TAG, "Didn't find any known endpoint");
//     return 0;
// }

static esp_err_t rest_get_handler(httpd_req_t *req){
    parse_uri(req->uri);
    int value = GET_SETTING_NOT_FOUND;
    // int* reg = get_endpoint_ptr();
    int index = substring_ints[1];
        
    if (substring_count == 1) {
        value = get_setting(substrings[0]);
    }
    else if (substring_count == 2 && substring_ints[1] != GET_SETTING_NOT_FOUND)
    {
        value = get_setting_indexed(substrings[0], index);
    }
    else
    {
        value = GET_SETTING_NOT_FOUND;
    }
    if (value == GET_SETTING_NOT_FOUND) {
        sprintf(httpd_temp_buffer, "Not found");
        sprintf(templogbuffer,"%s: URI '%s', setting NOT FOUND", TAG, req->uri);

        httpd_resp_set_status(req, HTTPD_404);
        httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    }
    else
    {
        sprintf(httpd_temp_buffer, "%i", value);
        sprintf(templogbuffer,"%s: URI '%s', returning %i", TAG, req->uri, value);
    }
    
    // log_string(templogbuffer);
    httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t rest_put_handler(httpd_req_t *req){
    parse_uri(req->uri);

    int bytes = httpd_req_recv(req, httpd_temp_buffer, 255);
    httpd_temp_buffer[bytes] = 0;
    int data;
    int datarecv = sscanf(httpd_temp_buffer, "%i", &data);
    ESP_LOGI(TAG, "===== Received PUT at %s (%s)", req->uri, httpd_temp_buffer);
    ESP_LOGD(TAG, "Received %s (%i) %i", httpd_temp_buffer, data, datarecv);
    int value = GET_SETTING_NOT_FOUND;
    int exists = 0;
    int existing = GET_SETTING_NOT_FOUND;
    // int* reg = get_endpoint_ptr();
    // int wait = rand() & 0xFF;
    // ESP_LOGI(TAG, "waiting %i", wait);
    // vTaskDelay(wait);
    int index = substring_ints[1];

    if (substring_count == 1) {
        // existing = get_setting(substrings[0]);
    }
    else if (substring_count == 2 && index != GET_SETTING_NOT_FOUND)
    {
        // existing = get_setting_indexed(substrings[0], index);
    }
    existing = -3;
    exists = existing != GET_SETTING_NOT_FOUND;
    ESP_LOGD(TAG, "Existing = %i", existing);
    
    if (exists == 0) {
        ESP_LOGI(TAG, "Returning 404");
        sprintf(httpd_temp_buffer, "Not found");
        httpd_resp_set_status(req, HTTPD_404);
        httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    else if (datarecv != 1)
    {
        httpd_resp_set_status(req, HTTPD_400);
        sprintf(httpd_temp_buffer, "No int found");
        httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    if (existing != data){
        if (substring_count == 1) {
            ESP_LOGD(TAG, "Setting key %s to %i", substrings[0], data);
            set_setting(substrings[0], data);
        }
        else if (substring_count == 2 && substring_ints[1] != GET_SETTING_NOT_FOUND)
        {
            ESP_LOGD(TAG, "Setting key %s/%i to %i", substrings[0], substring_ints[1], data);
            set_setting_indexed(substrings[0], index, data);
            // ESP_LOGI(TAG, "Setting %s index %i from %s", substrings[0], index, substrings[1]);
        }
    }
    else
    {
        ESP_LOGI(TAG, "URI '%s' unchanged (%i)", req->uri, existing);
    }
    
    if (strcmp(substrings[0], "lutfile") == 0){
        ESP_LOGI(TAG, "Detected change of lutfile, reloading luts...");
        read_level_luts(levellut);
    }
    
    sprintf(httpd_temp_buffer, "OK");
    httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* An HTTP GET handler */
static esp_err_t setpoint_get_handler(httpd_req_t *req)
{
    int ns = get_int_from_uri(req->uri);
    if ((ns >= 0) && (ns <=254)) {
        sprintf(httpd_temp_buffer, response, ns);
    }
    else 
    {
        sprintf(httpd_temp_buffer, response, 0);
    };
    // req->sess_ctx;
    networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
    setpoint = ns;
    ESP_LOGI(TAG, "Wifi Notify %i", ns);
    xTaskNotifyIndexed(ctx->mainloop_task, SETPOINT_SLEW_NOTIFY_INDEX, USE_DEFAULT_FADETIME, eSetValueWithOverwrite);

    httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t current_setpoint_handler(httpd_req_t *req){
    if (req->method == HTTP_GET) {
        sprintf(httpd_temp_buffer, "%i", setpoint);
    }
    else if (req->method == HTTP_PUT){
        int bytes = httpd_req_recv(req, httpd_temp_buffer, 255);
        httpd_temp_buffer[bytes] = 0;
        int data;
        int datarecv = sscanf(httpd_temp_buffer, "%i", &data);
        ESP_LOGI(TAG, "===== Received PUT at %s (%s)", req->uri, httpd_temp_buffer);
        ESP_LOGD(TAG, "Received %s (%i) %i", httpd_temp_buffer, data, datarecv);
        if (data >= 0 && data <= 254){
            setpoint = data;
            networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
            xTaskNotifyIndexed(ctx->mainloop_task, SETPOINT_SLEW_NOTIFY_INDEX, USE_DEFAULT_FADETIME, eSetValueWithOverwrite);
            ESP_LOGI(TAG, "Set new setpoint %i", setpoint);
            sprintf(httpd_temp_buffer, "OK");
        }
        else
        {
            ESP_LOGI(TAG, "Bad setpoint %i", data);
            httpd_resp_set_status(req, HTTPD_400_BAD_REQUEST);
            sprintf(httpd_temp_buffer, "Setpoint must be 0 <= sp <= 254");
        }
    }
    httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t otaupdate(httpd_req_t *req){
    // return httpd_resp_send_500(req);
    // char* buffer = malloc(1024);
    int bytes;
    ESP_LOGI(TAG, "Received POST OTAUpdate %i bytes", req->content_len);
    
    
    esp_err_t err;
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG, "Starting OTA example");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08"PRIx32", but running from offset 0x%08"PRIx32,
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08"PRIx32")",
             running->type, running->subtype, running->address);


    update_partition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%"PRIx32,
             update_partition->subtype, update_partition->address);
    assert(update_partition != NULL);

    esp_app_desc_t new_app_info;
    bool image_header_was_checked = false;
    int binary_file_length = 0;
    while (1){
        bytes = httpd_req_recv(req, httpd_temp_buffer, _MIN(req->content_len, BUF_SIZE));
        if (bytes > 1) 
        {
            ESP_LOGI(TAG, "Recv %i bytes", bytes);
            // printf(".");
            // fflush(stdout);
            if (image_header_was_checked == false) {
                esp_app_desc_t new_app_info;
                if (bytes > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                    // check current version with downloading
                    memcpy(&new_app_info, &httpd_temp_buffer[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                    ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                    esp_app_desc_t running_app_info;
                    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
                        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
                    }

                    const esp_partition_t* last_invalid_app = esp_ota_get_last_invalid_partition();
                    esp_app_desc_t invalid_app_info;
                    if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) {
                        ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
                    }
                    image_header_was_checked = true;

                    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                        return httpd_resp_send_500(req);
                    }
                    ESP_LOGI(TAG, "esp_ota_begin succeeded");
                } else {
                    ESP_LOGE(TAG, "received package is not fit len");
                    return httpd_resp_send_500(req);
                }
            }
            err = esp_ota_write( update_handle, (const void *)httpd_temp_buffer, bytes);
            if (err != ESP_OK) {
                return httpd_resp_send_500(req);
            }
            binary_file_length += bytes;
            ESP_LOGD(TAG, "Written image length %d", binary_file_length);
        }
        else
        {
            ESP_LOGI(TAG, "Recv returned 0 bytes");
            sprintf(httpd_temp_buffer, "OK");
            httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);

            err = esp_ota_end(update_handle);
            if (err != ESP_OK) {
                if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                    ESP_LOGE(TAG, "Image validation failed, image is corrupted");
                }
                ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
                return httpd_resp_send_500(req);
            }
            

            err = esp_ota_set_boot_partition(update_partition);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
                return httpd_resp_send_500(req);
            }
            ESP_LOGI(TAG, "OTA Update complete -> restart system!");
            vTaskDelay(pdMS_TO_TICKS(4000));
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

static esp_err_t rest_put_channel_handler(httpd_req_t *req){
    parse_uri(req->uri);
    bool put = req->method == HTTP_PUT;
    int data = -1;
    
    char* channelname = substrings[1];
    int* override_ptr;
    if (put) {
            
        int bytes = httpd_req_recv(req, httpd_temp_buffer, 255);
        httpd_temp_buffer[bytes] = 0;
        int datarecv = sscanf(httpd_temp_buffer, "%i", &data);
        ESP_LOGI(TAG, "===== Received PUT at %s (%s)", req->uri, httpd_temp_buffer);
        ESP_LOGD(TAG, "Received %s (%i) %i", httpd_temp_buffer, data, datarecv);
        ESP_LOGI(TAG, "Trying to set override for channel %s: %i", channelname, data);
    }
    char* chname;
    networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
    if (strcmp(channelname, "dali1") == 0){
        override_ptr = &ctx->level_overrides->dali1;
        chname = "DALI1";
    }
    else if (strcmp(channelname, "dali2") == 0)
    {
        override_ptr = &ctx->level_overrides->dali2;
        chname = "DALI2";
    }
    else if (strcmp(channelname, "dali3") == 0)
    {
        override_ptr = &ctx->level_overrides->dali3;
        chname = "DALI3";
    }
    else if (strcmp(channelname, "dali4") == 0)
    {
        override_ptr = &ctx->level_overrides->dali4;
        chname = "DALI4";
    }
    else if (strcmp(channelname, "espnow") == 0)
    {
        override_ptr = &ctx->level_overrides->espnow;
        chname = "ESPNOW";
    }
    else if (strcmp(channelname, "zeroten1") == 0)
    {
        override_ptr = &ctx->level_overrides->zeroten1;
        chname = "0-10v 1";
    }
    else if (strcmp(channelname, "zeroten2") == 0)
    {
        override_ptr = &ctx->level_overrides->zeroten2;
        chname = "0-10v 2";
    }
    else
    {
        ESP_LOGI(TAG, "Didn't recognise channel %s", channelname);
        return httpd_resp_send_404(req);
    }
    if (put)
    {
        *override_ptr = data;
        sprintf(httpd_temp_buffer, "OK");
        ESP_LOGI(TAG, "PUT: Set %s to %i", chname, data);
        xTaskNotifyIndexed(ctx->mainloop_task, SETPOINT_SLEW_NOTIFY_INDEX, USE_DEFAULT_FADETIME, eSetValueWithOverwrite);
    }
    else
    {
        sprintf(httpd_temp_buffer, "%i", *override_ptr);
        ESP_LOGI(TAG, "GET: %s is %i", chname, *override_ptr);
    }
    
    httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const char* index_filename = "/spiffs/index.html";
static const char* alarm_filename = "/spiffs/alarms.html";
static const char* channels_filename = "/spiffs/channels.html";
static const char* spiffsfolder = "/spiffs";
// static const char* filenamebuf = "                         ";

static esp_err_t file_handler(httpd_req_t *req){
    char* uri = req->uri;
    if (strcmp(uri, "/") == 0) {
        strcpy(filename, index_filename);
    }
    else if (strcmp(uri, "/setup") == 0)
    {
        strcpy(filename, alarm_filename);
    }
    else if (strcmp(uri, "/ch") == 0)
    {
        strcpy(filename, channels_filename);
    }
    else
    {
        strcpy(filename, spiffsfolder);
        strcat(filename, uri);
    }
    ESP_LOGI(TAG, "Looking for '%s'", filename);
    FILE *in_file  = fopen(filename, "r"); // read only

    if (in_file == NULL)
    {  
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Can't find file");
        return ESP_ERR_NOT_FOUND;
    }


    int urilen = strlen(uri);
    if (strcmp(uri + urilen - 4, ".ico") == 0)
    {
        httpd_resp_set_type(req, "image/x-icon");
        httpd_resp_set_hdr(req, "cache-control", "max-age=300");
    }
    if (strcmp(uri + urilen - 4, ".png") == 0)
    {
        httpd_resp_set_type(req, "image/png");
        httpd_resp_set_hdr(req, "cache-control", "max-age=300");
    }
    if (strcmp(uri + urilen - 5, ".html") == 0)
    {
        httpd_resp_set_type(req, "text/html");
    }
    if (strcmp(uri + urilen - 4, ".css") == 0)
    {
        httpd_resp_set_type(req, "text/css");
        httpd_resp_set_hdr(req, "cache-control", "max-age=30");
    }

    fseek(in_file, 0, SEEK_END);
    long fsize = ftell(in_file);
    fseek(in_file, 0, SEEK_SET);

    if (fsize > BUF_SIZE - 1) {
        int sent_bytes = 0;
        int bytes_to_send;
        while (1) {
            bytes_to_send = _MIN(BUF_SIZE, fsize - sent_bytes);
            if (bytes_to_send > 0) fread(httpd_temp_buffer, bytes_to_send, 1, in_file);
            httpd_resp_send_chunk(req, httpd_temp_buffer, bytes_to_send);
            
            if (bytes_to_send == 0) break;
            sent_bytes += bytes_to_send;
        }
        // fclose(in_file);
        // httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Buffer too small");
        // return ESP_ERR_NO_MEM;
    }
    else
    {
        fread(httpd_temp_buffer, fsize, 1, in_file);
        httpd_resp_send(req, httpd_temp_buffer, fsize);
    }
    fclose(in_file);

    // httpd_temp_buffer[fsize] = 0;

    // httpd_resp_send(req, httpd_temp_buffer, fsize);
    return ESP_OK;
}

static esp_err_t file_uploader(httpd_req_t *req){
    char* uri = req->uri;
    
    // sprintf(templogbuffer,"%s: POST URI: '%s'", TAG, req->uri);
    
    // log_string(templogbuffer);
    
    strcpy(filename, spiffsfolder);
    strcat(filename, uri + 7);
    if (req->method == HTTP_POST)
    {
        ESP_LOGI(TAG, "POST at URI %s",uri);
    }
    else if (req->method == HTTP_DELETE)
    {
        ESP_LOGI(TAG, "DELETE at URI %s",uri);
        ESP_LOGI(TAG, "Filename '%s'",filename);
        
        FILE *f = fopen(filename, "r");
        if (f == NULL)
        {
            sprintf(httpd_temp_buffer, "Error deleting '%s'", filename);
            httpd_resp_set_status(req, HTTPD_404);
        }
        else
        {
            fclose(f);
            remove(filename);
            sprintf(httpd_temp_buffer, "Deleted '%s'", filename);
        }
        httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    else
    {
        httpd_resp_send_500(req);
    }
    // sprintf(templogbuffer,"%s: POST at filename '%s' - access == %i", TAG, filename, access(filename, F_OK));
    // log_string(templogbuffer);
    
    // if (access(filename, F_OK) != 0){
    //     httpd_resp_send_404(req);
    //     return ESP_FAIL;
    // }
    int bytes_recv;
    int bytes_tot = 0;
    FILE *f = fopen(filename, "w");
    while (1) {
        bytes_recv = httpd_req_recv(req, httpd_temp_buffer, BUF_SIZE);
        if (bytes_recv == 0) break;
        fwrite(httpd_temp_buffer, 1, bytes_recv, f);
        bytes_tot += bytes_recv;
    }
    fclose(f);
    sprintf(httpd_temp_buffer, "Uploaded %i bytes to '%s'", bytes_tot, filename);
    httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    if (strncmp(filename, "/spiffs/levellut", 16) == 0){
        read_level_luts(levellut);
    }
    return ESP_OK;
}

static esp_err_t view_luts(httpd_req_t* req){
    
    httpd_resp_set_type(req, "text/plain");
    level_t lev;
    for (int i = 0; i<= 254; i++){
        lev = levellut[i];
        sprintf(httpd_temp_buffer, "LUT Level %i: 0-10v1: %d, 0-10v2: %d, DALI1: %d, DALI2: %d, DALI3: %d, DALI4: %d, ESPNOW: %d, Rly1: %d, Rly2: %d\n", i,
             lev.zeroten1_lvl,
             lev.zeroten2_lvl,
             lev.dali1_lvl,
             lev.dali2_lvl,
             lev.dali3_lvl,
             lev.dali4_lvl,
             lev.espnow_lvl,
             lev.relay1,
             lev.relay2
             );
        httpd_resp_sendstr_chunk(req, httpd_temp_buffer);
    }
    httpd_resp_send_chunk(req, httpd_temp_buffer, 0);
    return ESP_OK;
}

static esp_err_t restart(httpd_req_t* req){
    esp_restart();
    return ESP_OK;
}


static char logbuffercopy[LOGBUFFER_SIZE];

static esp_err_t logbuffer_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send_chunk(req, logbuffer + logbufferpos, LOGBUFFER_SIZE - logbufferpos);
    httpd_resp_send_chunk(req, logbuffer, logbufferpos);
    httpd_resp_send_chunk(req, logbuffer, 0);
    return ESP_OK;
}

static void send_block_plaintext(httpd_req_t *req, char* buf, int start, int end){
    if ((end - start) <= 0) return;

    char inbyte;
    bool send;
    for (int bytepos = start; bytepos < end; bytepos++){
        send = false;
        inbyte = buf[bytepos];
        if (inbyte == '\n')
        {
            send = true;
        }
        
        else if (inbyte >= 32)
        {
            send = true;
        }
        if (send){
            httpd_temp_buffer[bytepos - start] = inbyte;
        }
        else
        {
            httpd_temp_buffer[bytepos - start] = 32;
        }
    }
    httpd_resp_send_chunk(req, httpd_temp_buffer, end - start);
}

static esp_err_t logbuffer_plaintext_handler(httpd_req_t *req){
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


    // httpd_resp_send_chunk(req, logbuffer + logbufferpos, LOGBUFFER_SIZE - logbufferpos);
    // httpd_resp_send_chunk(req, logbuffer, logbufferpos);
    httpd_resp_send_chunk(req, logbuffer, 0);
    return ESP_OK;
}

static esp_err_t commission(httpd_req_t* req){
    networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
    
    ESP_LOGI(TAG, "HTTPD Command queue %i", (int) ctx->dali_command_queue);
    ESP_LOGI(TAG, "HTTPD Command queue %i", (int) ctx->dali_command_queue);
    ESP_LOGI(TAG, "HTTPD Command queue %i", (int) ctx->dali_command_queue);
    dali_command_t command = {
        .command = DALI_COMMAND_COMMISSION,
        .address = 0,
    };
    if (ctx->dali_command_queue != NULL) 
    {
        xQueueSend(ctx->dali_command_queue, &command, pdMS_TO_TICKS(1000));
        httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

static esp_err_t power_on_level(httpd_req_t* req){
    networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
    parse_uri(req->uri);
    bool put = req->method == HTTP_PUT;
    int data = -1;
    
    char* channelname = substring_ints[1];
    int* override_ptr;
    if (put) {
            
        int bytes = httpd_req_recv(req, httpd_temp_buffer, 255);
        httpd_temp_buffer[bytes] = 0;
        int datarecv = sscanf(httpd_temp_buffer, "%i", &data);
        ESP_LOGI(TAG, "===== Received PUT at %s (%s)", req->uri, httpd_temp_buffer);
        ESP_LOGD(TAG, "Received %s (%i) %i", httpd_temp_buffer, data, datarecv);
    }
    uint8_t cmd = 255;
    dali_command_t command = {
        .command = cmd,
        .address = channelname,
        .value = data
    };
    if (strcmp(substrings[0], "set-power-on-level") == 0)
    {
        command.command = DALI_COMMAND_SET_POWER_ON_LEVEL;
        ESP_LOGI(TAG, "Setting power on level for address %d to %d", command.address, command.value);
    }
    else if (strcmp(substrings[0], "set-failsafe-level") == 0)
    {
        command.command = DALI_COMMAND_SET_FAILSAFE_LEVEL;
        ESP_LOGI(TAG, "Setting failsafe level for address %d to %d", command.address, command.value);
    }
    else
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    if (ctx->dali_command_queue != NULL) 
    {
        xQueueSend(ctx->dali_command_queue, &command, pdMS_TO_TICKS(1000));
        httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

// static const httpd_uri_t hello = {
//     .uri       = "/level/?*",
//     .method    = HTTP_GET,
//     .handler   = setpoint_get_handler,
//     .user_ctx  = "Hello World!"
// };

static const httpd_uri_t files = {
    .uri       = "/?*",
    .method    = HTTP_GET,
    .handler   = file_handler,
    .user_ctx  = NULL
};
static const httpd_uri_t file_upload = {
    .uri       = "/spiffs/?*",
    .method    = HTTP_POST,
    .handler   = file_uploader,
    .user_ctx  = NULL
};
static const httpd_uri_t file_del = {
    .uri       = "/spiffs/?*",
    .method    = HTTP_DELETE,
    .handler   = file_uploader,
    .user_ctx  = NULL
};
static const httpd_uri_t get_setpoint = {
    .uri       = "/setpoint",
    .method    = HTTP_GET,
    .handler   = current_setpoint_handler,
    .user_ctx  = NULL
};
static const httpd_uri_t put_setpoint = {
    .uri       = "/setpoint",
    .method    = HTTP_PUT,
    .handler   = current_setpoint_handler,
    .user_ctx  = NULL
};
static const httpd_uri_t rest_get = {
    .uri       = "/api/?*",
    .method    = HTTP_GET,
    .handler   = rest_get_handler,
    .user_ctx  = NULL
};
static const httpd_uri_t log_get = {
    .uri       = "/logterm",
    .method    = HTTP_GET,
    .handler   = logbuffer_handler,
    .user_ctx  = NULL
};
static const httpd_uri_t log_get_plaintext = {
    .uri       = "/log",
    .method    = HTTP_GET,
    .handler   = logbuffer_plaintext_handler,
    .user_ctx  = NULL
};
static const httpd_uri_t rest_put = {
    .uri       = "/api/?*",
    .method    = HTTP_PUT,
    .handler   = rest_put_handler,
    .user_ctx  = NULL
};
static const httpd_uri_t rest_put_channel_level = {
    .uri       = "/api/channel/?*",
    .method    = HTTP_PUT,
    .handler   = rest_put_channel_handler,
    .user_ctx  = NULL
};
static const httpd_uri_t rest_get_channel_level = {
    .uri       = "/api/channel/?*",
    .method    = HTTP_GET,
    .handler   = rest_put_channel_handler,
    .user_ctx  = NULL
};
static const httpd_uri_t view_luts_endpoint = {
    .uri       = "/luts",
    .method    = HTTP_GET,
    .handler   = view_luts,
    .user_ctx  = NULL
};
static const httpd_uri_t restart_endpoint = {
    .uri       = "/restart",
    .method    = HTTP_POST,
    .handler   = restart,
    .user_ctx  = NULL
};
static const httpd_uri_t commission_endpoint = {
    .uri       = "/dali/commission",
    .method    = HTTP_POST,
    .handler   = commission,
    .user_ctx  = NULL
};
static const httpd_uri_t power_on_level_endpoint = {
    .uri       = "/dali/set-power-on-level/*?",
    .method    = HTTP_PUT,
    .handler   = power_on_level,
    .user_ctx  = NULL
};
static const httpd_uri_t failsafe_level_endpoint = {
    .uri       = "/dali/set-failsafe-level/*?",
    .method    = HTTP_PUT,
    .handler   = power_on_level,
    .user_ctx  = NULL
};
static const httpd_uri_t post_ota = {
    .uri       = "/otaupdate",
    .method    = HTTP_POST,
    .handler   = otaupdate,
    .user_ctx  = NULL
};

esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (strcmp("/hello", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/hello URI is not available");
        /* Return ESP_OK to keep underlying socket open */
        return ESP_OK;
    } else if (strcmp("/echo", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/echo URI is not available");
        /* Return ESP_FAIL to close underlying socket */
        return ESP_FAIL;
    }
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}

static httpd_handle_t start_webserver(networking_ctx_t *ctx)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.global_user_ctx = ctx;
    config.max_open_sockets = 10;
    config.max_uri_handlers = 18;
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        // httpd_register_uri_handler(server, &hello);
        httpd_register_uri_handler(server, &get_setpoint);
        httpd_register_uri_handler(server, &put_setpoint);
        httpd_register_uri_handler(server, &post_ota);
        httpd_register_uri_handler(server, &view_luts_endpoint);
        httpd_register_uri_handler(server, &commission_endpoint);
        httpd_register_uri_handler(server, &power_on_level_endpoint);
        httpd_register_uri_handler(server, &failsafe_level_endpoint);
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
        // httpd_register_uri_handler(server, &echo);
        // httpd_register_uri_handler(server, &ctrl);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

#if !CONFIG_IDF_TARGET_LINUX
static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    handler_ctx *ctx = (handler_ctx*) arg;
    // httpd_handle_t* server = (httpd_handle_t*) arg;
    if (ctx->server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(ctx->server) == ESP_OK) {
            ctx->server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    handler_ctx *ctx = (handler_ctx*) arg;
    // httpd_handle_t* server = (httpd_handle_t*) arg;
    if (ctx->server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        ctx->server = start_webserver(ctx->extra_ctx);
    }
}
#endif // !CONFIG_IDF_TARGET_LINUX

httpd_handle_t setup_httpserver(networking_ctx_t *extractx)
{
    static httpd_handle_t server = NULL;
    handler_ctx *ctx = malloc(sizeof(handler_ctx));
    ctx->server = server;
    ctx->extra_ctx = extractx;
    ESP_ERROR_CHECK(nvs_flash_init());

    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
     * and re-start it upon connection.
     */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, ctx));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, ctx));
    server = start_webserver(extractx);
    return server;
}
