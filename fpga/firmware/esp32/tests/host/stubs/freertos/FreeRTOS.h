#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef unsigned TickType_t;
typedef int BaseType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY (~0u)
#define pdMS_TO_TICKS(ms) (((ms)+9)/10)
