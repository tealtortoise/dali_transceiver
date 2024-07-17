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

#define BUF_SIZE 0x3FFF

#define REC_BUF_SIZE 256

#define NO_DIGITS_FOUND -98765413

#define NUM_ENDPOINTS 17
static char responsebuffer[BUF_SIZE];
static char recbuffer[REC_BUF_SIZE];

extern int setpoint;


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
    ESP_LOGI(TAG, "===== Received PUT at %s", req->uri);

    int bytes = httpd_req_recv(req, recbuffer, 255);
    recbuffer[bytes] = 0;
    int data;
    int datarecv = sscanf(recbuffer, "%i", &data);
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
static esp_err_t hello_get_handler(httpd_req_t *req)
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
    httpd_ctx *ctx = httpd_get_global_user_ctx(req->handle);
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

static const char* index_filename = "/spiffs/index.html";
static const char* alarm_filename = "/spiffs/alarms.html";
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

    httpd_resp_send(req, responsebuffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;

}

static const httpd_uri_t hello = {
    .uri       = "/level/?*",
    .method    = HTTP_GET,
    .handler   = hello_get_handler,
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
static const httpd_uri_t rest_put = {
    .uri       = "/api/?*",
    .method    = HTTP_PUT,
    .handler   = rest_put_handler,
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

static httpd_handle_t start_webserver(httpd_ctx *ctx)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.global_user_ctx = ctx;
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &hello);
        httpd_register_uri_handler(server, &get_setpoint);
        httpd_register_uri_handler(server, &rest_get);
        httpd_register_uri_handler(server, &rest_put);
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

httpd_handle_t setup_httpserver(httpd_ctx *extractx)
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
