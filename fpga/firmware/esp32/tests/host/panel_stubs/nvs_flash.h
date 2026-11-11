#pragma once
#include "esp_err.h"
#define ESP_ERR_NVS_NO_FREE_PAGES 10
#define ESP_ERR_NVS_NEW_VERSION_FOUND 11
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
