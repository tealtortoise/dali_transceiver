#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "base.h"
#include <esp_http_server.h>

typedef struct {
    httpd_handle_t server;
    networking_ctx_t *extra_ctx;
} handler_ctx;

httpd_handle_t setup_httpserver(networking_ctx_t *extractx);

#endif // HTTP_SERVER_H

