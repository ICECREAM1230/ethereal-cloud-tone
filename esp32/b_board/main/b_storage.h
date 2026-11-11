#pragma once
#include "b_config.h"
#include "esp_err.h"
typedef struct { uint32_t id;esp_err_t result; } b_storage_result_t;
esp_err_t b_storage_start(const b_config_t *defaults,b_config_t *loaded,bool *available,bool *migrated);
bool b_storage_save(uint32_t id,const b_config_t *);
bool b_storage_poll(b_storage_result_t *);
