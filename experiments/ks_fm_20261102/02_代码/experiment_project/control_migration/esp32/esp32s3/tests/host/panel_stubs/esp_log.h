#pragma once
#include <assert.h>
#include <stdio.h>
// Consume log arguments for format/unused-variable checks, without printing.
#define ESP_LOGI(tag,...) do { (void)(tag); if (0) printf(__VA_ARGS__); } while (0)
#define ESP_LOGW(tag,...) ESP_LOGI(tag,__VA_ARGS__)
#define ESP_LOGE(tag,...) ESP_LOGI(tag,__VA_ARGS__)
#define ESP_ERROR_CHECK(expression) do { assert((expression)==0); } while (0)
