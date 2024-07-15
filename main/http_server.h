#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <esp_http_server.h>

typedef struct {
    QueueHandle_t queue;
    TaskHandle_t mainloop_task;
} httpd_ctx;

typedef struct {
    httpd_handle_t server;
    httpd_ctx *extra_ctx;
} handler_ctx;

httpd_handle_t setup_httpserver(httpd_ctx *extractx);

#endif // HTTP_SERVER_H

