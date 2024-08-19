#ifndef SETTINGS_H
#define SETTINGS_H

#include "esp_err.h"
#include "base.h"

esp_err_t set_setting_indexed(const char *name, int element, int value);

esp_err_t set_setting(const char *name, int value);

int get_setting(const char *name);

int get_setting_indexed(const char *name, int element);

esp_err_t setup_nvs_spiffs_settings();

esp_err_t read_level_luts(level_t lut[]);


#endif // SETTINGS_H
#pragma once
