/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "http_server.h"
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
#include "esp_spiffs.h"
// #include "esp_tls.h"
#include "dali.h"

#if !CONFIG_IDF_TARGET_LINUX
#include <esp_wifi.h>
#include <esp_system.h>
#include "nvs_flash.h"
#include "base.h"
#include "html.c"
#include "http_server.h"
// #include "esp_eth.h"
#endif  // !CONFIG_IDF_TARGET_LINUX

#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN  (64)

/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */

static const char *TAG = "http server";

#define BUF_SIZE 8192

#define NO_DIGITS_FOUND -98765413
static char *responsebuffer[BUF_SIZE];

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


static esp_err_t file_handler(httpd_req_t *req){
    char* uri = req->uri;
    char filename[64] = "/spiffs";
    strcat(filename, uri);
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
    ESP_ERROR_CHECK(esp_netif_init());

    
    esp_vfs_spiffs_conf_t spiffsconf = {
        .base_path = "/spiffs",
        .format_if_mount_failed = false,
        .max_files = 2,
        .partition_label = NULL,
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&spiffsconf));
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

    server = start_webserver(extractx);
    return server;
}
