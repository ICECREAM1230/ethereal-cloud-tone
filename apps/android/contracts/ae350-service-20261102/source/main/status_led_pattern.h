#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    STATUS_BLE_READY = 1u << 0,
    STATUS_BLE_CONNECTED = 1u << 1,
    STATUS_WIFI_READY = 1u << 2,
    STATUS_WIFI_CONNECTED = 1u << 3,
    STATUS_BLE_FAULT = 1u << 4,
    STATUS_WIFI_FAULT = 1u << 5,
} status_led_flag_t;

typedef struct {
    bool boot_complete, boot_spi_ok, engine_seen, voice_seen;
    bool engine_recovering;
    uint32_t started_ms, running_ms, engine_ms, voice_ms, flags;
} status_led_state_t;

// Electrical levels: 0 lights the LED, 1 turns it off.
typedef struct { int red, green, blue; } status_led_levels_t;
status_led_levels_t status_led_pattern(const status_led_state_t *state,
                                       uint32_t now_ms);
