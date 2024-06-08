/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

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
#include "html.c"
// #include "esp_eth.h"
#endif  // !CONFIG_IDF_TARGET_LINUX

#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN  (64)

/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */

static const char *HTAG = "http server";

static char *responsebuffer[4096];

typedef struct {
    httpd_handle_t server;
    httpd_ctx *extra_ctx;
} handler_ctx;

/* An HTTP GET handler */
static esp_err_t hello_get_handler(httpd_req_t *req)
{
    char numberlevel[5];
    int digits = 0;
    int ns = 0;
    int urilen = strnlen(req->uri, 20);
    uint8_t byte;
    if (urilen > 7) {
        for (int i = 0; i <5; i++){
            byte = req->uri[7 + i];
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
            char *end;
            ns = strtol(numberlevel, &end, 10);
            ESP_LOGI(HTAG, "Found digits in URI %s", numberlevel);
        }
    }
    
    if (digits > 0 && (ns >= 0) && (ns <=254)) {
        sprintf(responsebuffer, response, ns);
    }
    else 
    {
        sprintf(responsebuffer, response, 0);
    };
    // req->sess_ctx;
    httpd_ctx *ctx = httpd_get_global_user_ctx(req->handle);
    // QueueHandle_t lightingqueue = ctx->queue;
    ctx->level = ns;
    xTaskNotifyIndexed(ctx->task, 0, ns, eSetValueWithOverwrite);
    // int o = (int)httpd_get_global_user_ctx(req->handle);
    // ESP_LOGI(HTAG, "ctxval %u", o);
    // xQueueOverwrite(lightingqueue, &ns);

    httpd_resp_send(req, responsebuffer, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t hello = {
    .uri       = "/level/?*",
    .method    = HTTP_GET,
    .handler   = hello_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = "Hello World!"
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
    ESP_LOGI(HTAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(HTAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &hello);
        // httpd_register_uri_handler(server, &echo);
        // httpd_register_uri_handler(server, &ctrl);
        return server;
    }

    ESP_LOGI(HTAG, "Error starting server!");
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
    handler_ctx *ctx = (handler_ctx*) event_data;
    // httpd_handle_t* server = (httpd_handle_t*) arg;
    if (ctx->server) {
        ESP_LOGI(HTAG, "Stopping webserver");
        if (stop_webserver(ctx->server) == ESP_OK) {
            ctx->server = NULL;
        } else {
            ESP_LOGE(HTAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    handler_ctx *ctx = (handler_ctx*) event_data;
    // httpd_handle_t* server = (httpd_handle_t*) arg;
    if (ctx->server == NULL) {
        ESP_LOGI(HTAG, "Starting webserver");
        ctx->server = start_webserver(ctx->extra_ctx);
    }
}
#endif // !CONFIG_IDF_TARGET_LINUX

httpd_handle_t setup_httpserver(httpd_ctx *extractx)
{
    static httpd_handle_t server = NULL;
    handler_ctx ctx = {
        .server = server,
        .extra_ctx = extractx
    };
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());

    // ESP_ERROR_CHECK(example_connect());

    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
     * and re-start it upon connection.
     */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &ctx));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &ctx));

    server = start_webserver(extractx);
    return server;
}
