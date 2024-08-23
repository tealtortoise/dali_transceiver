#ifndef HTTP_FILE_H
#define HTTP_FILE_H

#include <esp_http_server.h>
#include <esp_log.h>

#include "base.h"
#include "settings.h"

#define BUF_SIZE 0x3000

#define MAX_SAFE_INFIX_FILE_SIZE 512

#define MAX_INFIX_REPLACEMENTS 10

static char httpd_temp_buffer[BUF_SIZE];
static char httpd_temp_file_buffer[BUF_SIZE];

esp_err_t file_handler(httpd_req_t* req);

esp_err_t file_uploader(httpd_req_t* req);

#endif // HTTP_FILE_H
