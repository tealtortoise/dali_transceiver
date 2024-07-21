
#include "settings.h"
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "base.h"
#include "gpio_utils.h"
#include "settings.h"

// static char keybuffer[16];

static const char *TAG = "Settings";

#define COUNTDOWN_START 4
#define SETTINGS_NO_INDEX -1
static int commit_countdown = -1;

static SemaphoreHandle_t nvs_mutex;

esp_err_t generate_key_indexed(char* keybuffer, char* name, int element){
    // ESP_LOGI(TAG, "generating key from %s %i", name, element);
    if (element == SETTINGS_NO_INDEX) {
        sprintf(keybuffer, "%s", name);
    }
    else
    {
        sprintf(keybuffer, "%s%i", name, element);
    }
    // if (keybuffer[15] != 0) {
        // ESP_LOGE(TAG, "Key too long (> 15)");
        // return ESP_ERR_NOT_ALLOWED;
    // }
    return ESP_OK;
}

esp_err_t generate_key(char* buffer, char* name){
    return generate_key_indexed(buffer, name, -1);
}

esp_err_t set_setting_indexed(char* name, int element, int value) {
    char keybuffer[16];
    if (value == GET_SETTING_NOT_FOUND) return ESP_ERR_INVALID_STATE;
    // BaseType_t mutex_taken = xSemaphoreTake(nvs_mutex, pdMS_TO_TICKS(5000));
    // if (mutex_taken == pdTRUE){
    if (1){
        ESP_ERROR_CHECK(generate_key_indexed(keybuffer, name, element));
        ESP_LOGD(TAG, "Setting Key: %s to %i", keybuffer, value);
        // ESP_LOG_BUFFER_HEX(TAG, keybuffer, 16);
        esp_err_t err = nvs_set_i32(nvs_handle_, keybuffer, value);
        commit_countdown = COUNTDOWN_START;
        // xSemaphoreGive(nvs_mutex);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "nvs_set_i32 didn't return ESP_OK (%i)", err);
            return err;
        }
    }
    else
    {
        ESP_LOGE(TAG, "FAILED TO OBTAIN MUTEX!!!");
        return ESP_ERR_NOT_FINISHED;
    }
    return ESP_OK;
};

esp_err_t set_setting(char* name, int value) {
    return set_setting_indexed(name, -1, value);
};

int get_setting_indexed(char* name, int element) {
    char keybuffer[16];
    // ESP_LOGI(TAG, "Getting Key: %s", keybuffer);
    int out;
    esp_err_t success = 1;
    // BaseType_t mutex_taken = xSemaphoreTake(nvs_mutex, pdMS_TO_TICKS(5000));
    // if (mutex_taken == pdTRUE){
    if (1){
        ESP_ERROR_CHECK(generate_key_indexed(keybuffer, name, element));
        success = nvs_get_i32(nvs_handle_, keybuffer, &out);
        if (success == ESP_OK){
            ESP_LOGD(TAG, "Read %s -> %i", keybuffer, out);
        }
        // xSemaphoreGive(nvs_mutex);
        if (success == ESP_OK){
            return out;
        }
    }
    else
    {
        ESP_LOGE(TAG, "FAILED TO OBTAIN MUTEX!!!");
        return ESP_ERR_NOT_FINISHED;
    }
    return GET_SETTING_NOT_FOUND;
};

int get_setting(char* name) {
    return get_setting_indexed(name, -1);
};

static char linebuffer[128];
static char key[16];
static int outputint;

void commit_task(void* params){
    while(1) {
        commit_countdown  = (commit_countdown > -1) ? (commit_countdown - 1) : -1;
        if (commit_countdown == 0){
            // BaseType_t mutex_taken = xSemaphoreTake(nvs_mutex, pdMS_TO_TICKS(2000));
            // if (mutex_taken == pdTRUE) {
            if (1) {
                nvs_commit(nvs_handle_);
                // xSemaphoreGive(nvs_mutex);
                ESP_LOGI(TAG, "Committed NVS");
            }
            else
            {
                ESP_LOGE(TAG, "FAILED TO OBTAIN MUTEX!!");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t setup_nvs_spiffs_settings(){
    // keybuffer[15] = 0;
    nvs_mutex = xSemaphoreCreateMutex();
    esp_vfs_spiffs_conf_t spiffsconf = {
        .base_path = "/spiffs",
        .format_if_mount_failed = false,
        .max_files = 5,
        .partition_label = NULL,
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&spiffsconf));
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    err = nvs_flash_init_partition("nvs2");
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase_partition("nvs2"));
        err = nvs_flash_init_partition("nvs2");
    }
    ESP_ERROR_CHECK( err );
    ESP_ERROR_CHECK(nvs_open_from_partition("nvs2", "mulberry", NVS_READWRITE, &nvs_handle_));

    FILE *settingfile = fopen("/spiffs/default_settings.csv", "r");
    int commapos = -1;
    char* out; 
    int existing_int;

    esp_err_t key_find_result;
    nvs_type_t nvstype;
    BaseType_t mutex_taken;
    bool nvs_updated = false;
    // ESP_LOGW(TAG, "Resetting NVS!!!");
    // nvs_close(nvs_handle_);
    // ESP_ERROR_CHECK(nvs_flash_erase());
    // ESP_ERROR_CHECK(nvs_flash_init());
    // ESP_ERROR_CHECK(nvs_open("nvs", NVS_READWRITE, &nvs_handle_));
    bool force_update = get_and_log_buttons() == 5;
    if (force_update) {
        ESP_LOGW(TAG, "Resetting NVS!!!");
        nvs_flash_erase();
    }
    while (1){
        commapos = -1;
        out = fgets(linebuffer, 64, settingfile);
        // sprintf(linebuffer, "test,0");
        // printf(linebuffer);
        for (int i = 1; i <= 16; i++){
            if (linebuffer[i] == ',') {
                commapos = i;
                break;
            }
        }
        if (commapos == -1) {
            ESP_LOGE(TAG, "Could't find comma in line"); 
            fclose(settingfile);
            return ESP_ERR_NOT_FOUND;
        }
        // ESP_LOGI(TAG, "Found comma at pos %i in %s", commapos, linebuffer);
        strncpy(key, linebuffer, commapos);
        key[commapos] = (char) 0;
        sscanf(linebuffer + commapos + 1, "%i", &outputint);
        // ESP_LOGI(TAG,"Key %s = %i",key,outputint);
        key_find_result = nvs_find_key(nvs_handle_, key, &nvstype);
        if (key_find_result == ESP_OK && !force_update)
        {
            // ESP_LOGI(TAG, "Key '%s' already in NVS", key);
            mutex_taken = xSemaphoreTake(nvs_mutex, pdMS_TO_TICKS(5000));
            if (mutex_taken == pdTRUE){
                ESP_ERROR_CHECK(nvs_get_i32(nvs_handle_, key, &existing_int));
                xSemaphoreGive(nvs_mutex);
            }
            else
            {
                ESP_LOGE(TAG, "FAILED TO OBTAIN MUTEX!!!");
                existing_int = GET_SETTING_NOT_FOUND;
            }
        }
        else
        {   
            if (force_update) {
                ESP_LOGI(TAG, "Force updating '%s' -> setting to %i", key, outputint);
            }
            else
            {
                ESP_LOGI(TAG, "Couldn't find key '%s' -> setting to %i", key, outputint);
            }
            mutex_taken = xSemaphoreTake(nvs_mutex, pdMS_TO_TICKS(5000));
            if (mutex_taken == pdTRUE){
                ESP_ERROR_CHECK(nvs_set_i32(nvs_handle_, key, outputint));
                nvs_updated = true;
                xSemaphoreGive(nvs_mutex);
            }
            else
            {
                ESP_LOGE(TAG, "FAILED TO OBTAIN MUTEX!!!");
            }
        }
        if (out == NULL) break;
    }

    fclose(settingfile);
    if (nvs_updated) {
    }
    TaskHandle_t task;
    xTaskCreate(commit_task, "nvs commit task", 2048, NULL, 1, &task);
    return ESP_OK;
}


esp_err_t read_level_luts(level_t lut[]){
    FILE *lutfile = fopen("/spiffs/levelluts.csv", "r");
    if (lutfile == NULL)
    {
        ESP_LOGE(TAG, "couldn't find levelluts.csv");
        ESP_ERROR_CHECK(ESP_ERR_NOT_FOUND);
    }
    int commapos = -1;
    int celllen = -1;
    int lastcommapos = -1;
    char* out; 
    int existing_int;
    char cellbuffer[16];
    int cell_int;
    int column_idx;
    int row_idx;
    char* lutlinebuffer = malloc(128);
    size_t levet_t_size = sizeof(level_t);
    while (1){
        commapos = -1;
        lastcommapos = -1;
        column_idx = 0;
        row_idx = 0;
        cell_int = -1;
        out = fgets(linebuffer, 128, lutfile);
        linebuffer[40] = 0;
        // sprintf(linebuffer, "test,0");

        printf(linebuffer);
        if (linebuffer[0] == '#') continue;

        for (int i = 1; i <= 64; i++){
            if (linebuffer[i] == ',' || linebuffer[i] == 13 || linebuffer[i] == 10 || linebuffer[i] == 0) {
                commapos = i;
                celllen = i - lastcommapos - 1;
                if (celllen == 0) break;
                memcpy(cellbuffer, linebuffer + lastcommapos + 1, celllen);
                cellbuffer[celllen] = 0;
                sscanf(cellbuffer, "%i", &cell_int);
                if (column_idx == 0)
                {
                    row_idx = cell_int;
                    if (row_idx > 254 || row_idx < 0)
                    {
                        ESP_LOGE(TAG, "Invalid row is CSV LUT (%i)", row_idx);
                        break;
                    }
                }
                else
                {
                    uint8_t * byt = (size_t) lut + levet_t_size * row_idx + column_idx - 1;
                    *byt = (uint8_t) cell_int;
                
                }
                // printf("Row %i Cell %i contents '%s' data %i\n", row_idx, column_idx, cellbuffer, cell_int);
                column_idx += 1;
                lastcommapos = commapos;
                if (linebuffer[i] == 0)
                {
                    break;
                }
                // ESP_LOGI(TAG, "%d, %d", lut[4].dali1_lvl, lut[5].dali2_lvl);
            }
        }
        // vTaskDelay(500);
        if (commapos == -1) {
            ESP_LOGE(TAG, "Could't find comma in line"); 
            fclose(lutfile);
            return ESP_ERR_NOT_FOUND;
        }
        // ESP_LOGI(TAG, "Found comma at pos %i in %s", commapos, linebuffer);
        if (out == NULL) break;
    }

    fclose(lutfile);
    return ESP_OK;
}

