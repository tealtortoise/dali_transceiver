#ifndef SETTINGS_H
#define SETTINGS_H

#include "esp_err.h"

esp_err_t set_setting_indexed(char *name, int element, int value);

esp_err_t set_setting(char *name, int value);

int get_setting(char *name);

int get_setting_indexed(char *name, int element);

esp_err_t setup_nvs_spiffs_settings();

#endif // SETTINGS_H
#pragma once
