#pragma once
#include <stdio.h>
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag), (void)sizeof(printf(__VA_ARGS__)))
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_ERROR_CHECK(x) ((void)(x))
