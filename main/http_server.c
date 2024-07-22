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
#include "esp_netif.h"
// #include "esp_tls.h"
#include "dali.h"

#if !CONFIG_IDF_TARGET_LINUX
#include <esp_wifi.h>
#include <esp_system.h>
#include "nvs_flash.h"
#include "base.h"
#include "html.c"
#include "http_server.h"
#include "settings.h"
// #include "esp_eth.h"
#endif  // !CONFIG_IDF_TARGET_LINUX

#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN  (64)

/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */

static const char *TAG = "http server";

#define BUF_SIZE 0x7FFF

#define REC_BUF_SIZE 256

#define NO_DIGITS_FOUND -98765413

#define NUM_ENDPOINTS 17
static char responsebuffer[BUF_SIZE];
static char recbuffer[REC_BUF_SIZE];

extern int setpoint;


#define OTABUFSIZE 0x7FFF
static char ota_write_data[OTABUFSIZE + 1] = { 0 };


api_endpoint_t registers[NUM_ENDPOINTS];

void populate_registers(){
}

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

int* get_endpoint_ptr(){
    bool nomatch;
    int* reg;
    for (int i=0; i < NUM_ENDPOINTS; i++){
        // registers[i].name[15] = 0;
        // ESP_LOGI(TAG, "Looking for register '%s' with '%s'", substrings[0], registers[i].name);
        nomatch = strcmp(substrings[0], registers[i].name);
        if (!nomatch){
            ESP_LOGI(TAG, "Matched %s", substrings[0]);
            reg = registers[i].array;
            return reg;
        }
    }
    ESP_LOGI(TAG, "Didn't find any known endpoint");
    return 0;
}

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
        // ESP_LOGI(TAG, "Looking for %s index %i from %s", substrings[0], index, substrings[1]);
    }
    else
    {
        value = GET_SETTING_NOT_FOUND;
    }
    if (value == GET_SETTING_NOT_FOUND) {
        sprintf(responsebuffer, "Not found");
        httpd_resp_set_status(req, HTTPD_404);
        httpd_resp_send(req, responsebuffer, HTTPD_RESP_USE_STRLEN);
    }
    else
    {
        sprintf(responsebuffer, "%i", value);
    }
    httpd_resp_send(req, responsebuffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t rest_put_handler(httpd_req_t *req){
    parse_uri(req->uri);

    int bytes = httpd_req_recv(req, recbuffer, 255);
    recbuffer[bytes] = 0;
    int data;
    int datarecv = sscanf(recbuffer, "%i", &data);
    ESP_LOGI(TAG, "===== Received PUT at %s (%s)", req->uri, recbuffer);
    ESP_LOGD(TAG, "Received %s (%i) %i", recbuffer, data, datarecv);
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
        sprintf(responsebuffer, "Not found");
        httpd_resp_set_status(req, HTTPD_404);
        httpd_resp_send(req, responsebuffer, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    else if (datarecv != 1)
    {
        httpd_resp_set_status(req, HTTPD_400);
        sprintf(responsebuffer, "No int found");
        httpd_resp_send(req, responsebuffer, HTTPD_RESP_USE_STRLEN);
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
    sprintf(responsebuffer, "OK");
    httpd_resp_send(req, responsebuffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* An HTTP GET handler */
static esp_err_t setpoint_get_handler(httpd_req_t *req)
{
    int ns = get_int_from_uri(req->uri);
    if ((ns >= 0) && (ns <=254)) {
        sprintf(responsebuffer, response, ns);
    }
    else 
    {
        sprintf(responsebuffer, response, 0);
    };
    // req->sess_ctx;
    networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
    setpoint = ns;
    xTaskNotifyIndexed(ctx->mainloop_task, SETPOINT_SLEW_NOTIFY_INDEX, USE_DEFAULT_FADETIME, eSetValueWithOverwrite);

    httpd_resp_send(req, responsebuffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t current_setpoint_handler(httpd_req_t *req){
    sprintf(responsebuffer, "%i", setpoint);
    httpd_resp_send(req, responsebuffer, HTTPD_RESP_USE_STRLEN);
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
        bytes = httpd_req_recv(req, ota_write_data, _MIN(req->content_len, OTABUFSIZE));
        if (bytes > 1) 
        {
            ESP_LOGI(TAG, "Recv %i bytes", bytes);
            // printf(".");
            // fflush(stdout);
            if (image_header_was_checked == false) {
                esp_app_desc_t new_app_info;
                if (bytes > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                    // check current version with downloading
                    memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
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
            err = esp_ota_write( update_handle, (const void *)ota_write_data, bytes);
            if (err != ESP_OK) {
                return httpd_resp_send_500(req);
            }
            binary_file_length += bytes;
            ESP_LOGD(TAG, "Written image length %d", binary_file_length);
        }
        else
        {
            ESP_LOGI(TAG, "Recv returned 0 bytes");
            sprintf(responsebuffer, "OK");
            httpd_resp_send(req, responsebuffer, HTTPD_RESP_USE_STRLEN);

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
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
            return ESP_OK;
            break;
        }
        vTaskDelay(1);
    }
    
    sprintf(responsebuffer, "OK");
    httpd_resp_send(req, responsebuffer, HTTPD_RESP_USE_STRLEN);
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
            
        int bytes = httpd_req_recv(req, recbuffer, 255);
        recbuffer[bytes] = 0;
        int datarecv = sscanf(recbuffer, "%i", &data);
        ESP_LOGI(TAG, "===== Received PUT at %s (%s)", req->uri, recbuffer);
        ESP_LOGD(TAG, "Received %s (%i) %i", recbuffer, data, datarecv);
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
        sprintf(responsebuffer, "OK");
        ESP_LOGI(TAG, "PUT: Set %s to %i", chname, data);
        xTaskNotifyIndexed(ctx->mainloop_task, SETPOINT_SLEW_NOTIFY_INDEX, USE_DEFAULT_FADETIME, eSetValueWithOverwrite);
    }
    else
    {
        sprintf(responsebuffer, "%i", *override_ptr);
        ESP_LOGI(TAG, "GET: %s is %i", chname, *override_ptr);
    }
    
    httpd_resp_send(req, responsebuffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const char* index_filename = "/spiffs/index.html";
static const char* alarm_filename = "/spiffs/alarms.html";
static const char* channels_filename = "/spiffs/channels.html";
static const char* spiffsfolder = "/spiffs";
// static const char* filenamebuf = "                         ";

static esp_err_t file_handler(httpd_req_t *req){
    char* uri = req->uri;
    char filename[64];
    if (strcmp(uri, "/") == 0) {
        strcpy(filename, index_filename);
    }
    else if (strcmp(uri, "/setup/") == 0)
    {
        strcpy(filename, alarm_filename);
    }
    else if (strcmp(uri, "/ch/") == 0)
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


    fseek(in_file, 0, SEEK_END);
    long fsize = ftell(in_file);
    fseek(in_file, 0, SEEK_SET);

    if (fsize > BUF_SIZE - 1) {
        fclose(in_file);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Buffer too small");
        return ESP_ERR_NO_MEM;
    }
    
    
    fread(responsebuffer, fsize, 1, in_file);
    fclose(in_file);

    responsebuffer[fsize] = 0;

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
    httpd_resp_send(req, responsebuffer, fsize);
    return ESP_OK;

}

static char logbuffercopy[LOGBUFFER_SIZE];

static esp_err_t logbuffer_handler(httpd_req_t *req){
    // initialise_logbuffer();
    
    // for (int i = 0; i < LOGBUFFER_SIZE; i++){
        // logbuffer[i] = 'a';
    // }
    
    // responsebuffer[fsize] = 0;
    // logbuffer[10] = 0;
    int bytes_at_end = (LOGBUFFER_SIZE - logbufferpos - 1);
    memcpy(logbuffercopy, logbuffer + logbufferpos, bytes_at_end);
    memcpy(logbuffercopy + bytes_at_end, logbuffer, LOGBUFFER_SIZE - bytes_at_end);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, logbuffercopy,LOGBUFFER_SIZE);
    return ESP_OK;

}

static const httpd_uri_t hello = {
    .uri       = "/level/?*",
    .method    = HTTP_GET,
    .handler   = setpoint_get_handler,
    .user_ctx  = "Hello World!"
};

static const httpd_uri_t files = {
    .uri       = "/?*",
    .method    = HTTP_GET,
    .handler   = file_handler,
    .user_ctx  = NULL
};
static const httpd_uri_t get_setpoint = {
    .uri       = "/setpoint/current/",
    .method    = HTTP_GET,
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
    .uri       = "/log/",
    .method    = HTTP_GET,
    .handler   = logbuffer_handler,
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
static const httpd_uri_t post_ota = {
    .uri       = "/otaupdate/",
    .method    = HTTP_POST,
    .handler   = otaupdate,
    .user_ctx  = NULL
};
// static const httpd_uri_t set_alarm = {
//     .uri       = "/setpoint/current/",
//     .method    = HTTP_GET,
//     .handler   = current_setpoint_handler,
//     .user_ctx  = NULL
// };

/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /hello and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /hello or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /hello). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
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
    config.max_uri_handlers = 10;
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &hello);
        httpd_register_uri_handler(server, &post_ota);
        httpd_register_uri_handler(server, &rest_put_channel_level);
        httpd_register_uri_handler(server, &rest_get_channel_level);
        httpd_register_uri_handler(server, &get_setpoint);
        httpd_register_uri_handler(server, &rest_get);
        httpd_register_uri_handler(server, &rest_put);
        httpd_register_uri_handler(server, &log_get);
        httpd_register_uri_handler(server, &files);
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

    
    // FILE *in_file  = fopen("/spiffs/html.html", "r"); // read only

    // if (in_file == NULL)
    // {  
        // ESP_LOGE(TAG, "Error! Could not open file");
    // }
    // char line[1000];
    // fgets( line, 1000, in_file );
    // printf(line);
    // vTaskDelay(10000000);

    // ESP_ERROR_CHECK(esp_event_loop_create_default());

    // ESP_ERROR_CHECK(example_connect());

    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
     * and re-start it upon connection.
     */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, ctx));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, ctx));
    populate_registers();
    server = start_webserver(extractx);
    return server;
}
