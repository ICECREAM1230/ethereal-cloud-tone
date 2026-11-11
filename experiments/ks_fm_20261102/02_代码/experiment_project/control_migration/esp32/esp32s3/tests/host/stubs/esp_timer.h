#pragma once
#include <stdint.h>
typedef void *esp_timer_handle_t;
typedef struct {void (*callback)(void *); const char *name; void *arg;} esp_timer_create_args_t;
int64_t esp_timer_get_time(void);
int esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *timer);
int esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period);
