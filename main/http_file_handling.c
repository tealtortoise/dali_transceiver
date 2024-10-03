#include <stdio.h>
#include <string.h>
#include "http_file_handling.h"

static const char *TAG = "http-filehandle";

static int num_infixes = 0;
static int infix_offsets[MAX_INFIX_REPLACEMENTS] = {-1};

char filename[128];
static char httpd_temp_buffer[BUF_SIZE];
static char httpd_temp_file_buffer[BUF_SIZE];

static char infix_string_buffer[128];

static const char *index_filename = "/spiffs/index.html";
static const char *alarm_filename = "/spiffs/alarms.html";
static const char *channels_filename = "/spiffs/channels.html";
static const char *fallback_infix_filename = "/spiffs/infixx";
static const char *spiffsfolder = "/spiffs";

static void find_infixes(char *buffer, FILE *existing_open_file)
{
    ESP_LOGD(TAG, "Looking for infixes in '%s'", index_filename);
    FILE *in_file;
    if (existing_open_file != NULL)
    {
        in_file = existing_open_file;
    }
    else
    {
        in_file = fopen(index_filename, "r");
    }
    if (in_file == NULL)
    {
        ESP_LOGE(TAG, "ERROR No replacement infix string positions found");
        return;
    }

    fseek(in_file, 0, SEEK_END);
    long fsize = ftell(in_file);
    fseek(in_file, 0, SEEK_SET);

    if (fsize > (BUF_SIZE - 1))
    {
        ESP_LOGE(TAG, "%s is too big at %li bytes", index_filename, fsize);
        fclose(in_file);
        return;
    }
    fread(buffer, fsize, 1, in_file);
    int infix_index = 0;
    for (int bytepos = 0; bytepos < fsize; bytepos++)
    {
        if ((buffer[bytepos] == '%') && (buffer[bytepos + 1] == 's'))
        {
            ESP_LOGD(TAG, "Found infix location at %i", bytepos);
            infix_offsets[infix_index] = bytepos;

            infix_index += 1;
            if (infix_index >= MAX_INFIX_REPLACEMENTS)
            {
                ESP_LOGW(TAG, "Reached replacement string limit of %i", MAX_INFIX_REPLACEMENTS);
                break;
            }
        }
    }
    ESP_LOGI(TAG, "Found %i infixes", infix_index);
    num_infixes = infix_index;
    if (existing_open_file == NULL)
    {
        fclose(in_file);
        return;
    }

    // return to beginning of file
    fseek(in_file, 0, SEEK_SET);
}

static int check_buffer_bounds(int pos)
{

    if ((pos + 1) > BUF_SIZE)
    {
        ESP_LOGE(TAG, "ERROR Ran out of buffer. Perhaps too long infix file?");
        return 1;
    }
    return 0;
}

static int send_file_as_chunk(httpd_req_t *req, char *filename, bool infix)
{
    ESP_LOGI(TAG, "Looking for '%s'", filename);
    FILE *in_file = fopen(filename, "r");

    if (in_file == NULL)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Can't find file");
        return -1;
    }

    fseek(in_file, 0, SEEK_END);
    long fsize = ftell(in_file);
    fseek(in_file, 0, SEEK_SET);

    if (fsize > (BUF_SIZE - MAX_SAFE_INFIX_FILE_SIZE))
    {
        ESP_LOGI(TAG, "File probably too big for buffer, sending chunks (can't replace strings)");
        int sent_bytes = 0;
        int bytes_to_send;
        while (1)
        {
            bytes_to_send = _MIN(BUF_SIZE, fsize - sent_bytes);
            if (bytes_to_send > 0)
                fread(httpd_temp_buffer, bytes_to_send, 1, in_file);
            httpd_resp_send_chunk(req, httpd_temp_buffer, bytes_to_send);

            if (bytes_to_send == 0)
                break;
            sent_bytes += bytes_to_send;
        }
        fclose(in_file);
        return 0;
    }

    // we move on to string replacement maybe

    FILE *name_file = NULL;
    if (infix)
    {
        int namefile_number = get_setting("namefile");
        char infix_filename[64] = {0};
        strcat(infix_filename, filename);
        strcat(infix_filename, ".infix");
        int infix_filename_len = strlen(infix_filename);
        infix_filename[infix_filename_len] = '0' + namefile_number;
        infix_filename_len += 1;
        infix_filename[infix_filename_len] = 0;

        ESP_LOGI(TAG, "Using infix filename '%s'", infix_filename);
        name_file = fopen(infix_filename, "r");

        if (name_file == NULL)
        {
            // no infix file -> we just send the original file
            ESP_LOGI(TAG, "Couldn't file '%s' file!", infix_filename);

            // try and open generic infix file
            strcpy(infix_filename, fallback_infix_filename);
            infix_filename[strlen(infix_filename) - 1] = '0' + namefile_number;
            ESP_LOGI(TAG, "Trying to open fallback infix file '%s'", infix_filename);
            name_file = fopen(infix_filename, "r");
            if (name_file == NULL)
            {
                ESP_LOGI(TAG, "Couldn't find fallback infix file!");
            }
        }
    }

    if (name_file == NULL)
    {
        fread(httpd_temp_buffer, fsize, 1, in_file);
        fclose(in_file);
        httpd_resp_send_chunk(req, httpd_temp_buffer, fsize);
        return 0;
    }

    // we move on to string replacement actually
    ESP_LOGI(TAG, "Found infix file. Replacing strings...");
    int ipos = 0; // input file buffer position
    int opos = 0; // output buffer position
    int total_replaces;
    int ptr_recv;
    char *ret;
    int pos_pointer_str_len;
    int last_pos_pointer = 0;
    int pos_pointer = 0;

    bool error = false;

    find_infixes(httpd_temp_file_buffer, in_file);
    total_replaces = 0;

    ESP_LOGI(TAG, "Infixing strings...");

    int infix_str_len;

    // load original file into file buffer

    fseek(name_file, 0, SEEK_SET); // make sure we're at start of file
    int bytes_copy;

    // copy original file blocks around infixes
    bool eof = false;
    for (int replace_idx = 0; replace_idx <= num_infixes; replace_idx++)
    {
        ret = fgets(infix_string_buffer, 127, name_file);
        if (ret == NULL)
        {
            total_replaces = replace_idx;
            eof = true;
            ESP_LOGD(TAG, "Reached EOF");
        }

        if (replace_idx == 0 && num_infixes == 0)
        {
            ESP_LOGD(TAG, "No replacing -> just copy file");
            memcpy(httpd_temp_buffer, httpd_temp_file_buffer, fsize);
            ipos += fsize;
            opos += fsize;
            break;
        }

        if (replace_idx == num_infixes || eof)
        {

            ESP_LOGI(TAG, "Copy EOF...");
            // copy tail end
            bytes_copy = fsize - ipos;
            if (check_buffer_bounds(opos + bytes_copy + 1))
                break;
            memcpy(httpd_temp_buffer + opos, httpd_temp_file_buffer + ipos, bytes_copy);
            opos += bytes_copy;
            break;
        }

        // copy head end / inbetween text
        ESP_LOGD(TAG, "Copy inbetween text at %i -> %i", ipos, opos);
        bytes_copy = infix_offsets[replace_idx] - ipos;

        if (check_buffer_bounds(opos + bytes_copy + 1))
            break;
        memcpy(httpd_temp_buffer + opos, httpd_temp_file_buffer + ipos, bytes_copy);
        ipos += bytes_copy;
        opos += bytes_copy;

        // prep infix
        int line_len = strlen(infix_string_buffer);
        infix_str_len = line_len;

        if (infix_string_buffer[line_len - 1] == 10 || infix_string_buffer[line_len - 1] == 13)
            infix_str_len -= 1; // we don't want the newlines

        // copy infix
        ESP_LOGD(TAG, "Copy infix '%s' at %i -> %i", infix_string_buffer, ipos, opos);

        if (check_buffer_bounds(opos + infix_str_len + 1))
            break;
        memcpy(httpd_temp_buffer + opos, infix_string_buffer, infix_str_len);
        opos += infix_str_len;
        ipos += 2; // we don't want the '%s' characters

        total_replaces = replace_idx + 1;
    }
    ESP_LOGD(TAG, "sending %i bytes", opos);
    httpd_resp_send_chunk(req, httpd_temp_buffer, opos);
    fclose(name_file);
    fclose(in_file);
    ESP_LOGD(TAG, "Send '%s' as chunks. Replaced %i strings", filename, total_replaces);
    return total_replaces;
}

esp_err_t file_handler(httpd_req_t *req)
{
    const char *uri = req->uri;
    bool is_index = false;
    bool try_infix = false;
    if (strcmp(uri, "/") == 0)
    {
        strcpy(filename, index_filename);
        is_index = true;
        try_infix = true;
    }
    else if (strcmp(uri, "/setup") == 0)
    {
        strcpy(filename, alarm_filename);
        try_infix = true;
    }
    else if (strcmp(uri, "/ch") == 0)
    {
        strcpy(filename, channels_filename);
        try_infix = true;
    }
    else
    {
        strcpy(filename, spiffsfolder);
        strcat(filename, uri);
    }

    int urilen = strlen(uri);
    if (urilen > 4 && strcmp(uri + urilen - 4, ".ico") == 0)
    {
        httpd_resp_set_type(req, "image/x-icon");
        httpd_resp_set_hdr(req, "cache-control", "max-age=300");
    }
    if (urilen > 4 && strcmp(uri + urilen - 4, ".png") == 0)
    {
        httpd_resp_set_type(req, "image/png");
        httpd_resp_set_hdr(req, "cache-control", "max-age=300");
    }
    if (urilen > 5 && strcmp(uri + urilen - 5, ".html") == 0)
    {
        httpd_resp_set_type(req, "text/html");
    }
    if (urilen > 4 && strcmp(uri + urilen - 4, ".css") == 0)
    {
        httpd_resp_set_type(req, "text/css");
        httpd_resp_set_hdr(req, "cache-control", "max-age=30");
    }
    if (urilen > 12 && strcmp(uri + urilen - 12, ".webmanifest") == 0)
    {
        httpd_resp_set_type(req, "application/manifest+json");
        httpd_resp_set_hdr(req, "cache-control", "max-age=60");
    }
    if (urilen > 3 && strcmp(uri + urilen - 3, ".js") == 0)
    {
        httpd_resp_set_type(req, "text/javascript");
        httpd_resp_set_hdr(req, "cache-control", "max-age=10");
    }

    int str_replacements = send_file_as_chunk(req, filename, try_infix);
    if (str_replacements < 0)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // signal we're all done with sending
    httpd_resp_send_chunk(req, httpd_temp_buffer, 0);
    return ESP_OK;
}

esp_err_t file_uploader(httpd_req_t *req)
{
    const char *uri = req->uri;

    strcpy(filename, spiffsfolder);
    strcat(filename, uri + 7);
    if (req->method == HTTP_POST)
    {
        ESP_LOGI(TAG, "POST at URI %s", uri);
    }
    else if (req->method == HTTP_DELETE)
    {
        ESP_LOGI(TAG, "DELETE at URI %s", uri);
        ESP_LOGI(TAG, "Filename '%s'", filename);

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

    int bytes_recv;
    int bytes_tot = 0;
    FILE *f = fopen(filename, "w");
    while (1)
    {
        bytes_recv = httpd_req_recv(req, httpd_temp_buffer, BUF_SIZE);
        if (bytes_recv == 0)
            break;
        fwrite(httpd_temp_buffer, 1, bytes_recv, f);
        bytes_tot += bytes_recv;
    }
    fclose(f);
    sprintf(httpd_temp_buffer, "Uploaded %i bytes to '%s'", bytes_tot, filename);
    httpd_resp_send(req, httpd_temp_buffer, HTTPD_RESP_USE_STRLEN);
    if (strncmp(filename, "/spiffs/levellut", 16) == 0)
    {
        networking_ctx_t *ctx = httpd_get_global_user_ctx(req->handle);
        read_level_luts(ctx->status->lut);
    }
    return ESP_OK;
}
