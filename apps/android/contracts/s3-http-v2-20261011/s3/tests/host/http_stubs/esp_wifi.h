#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct { unsigned num; } wifi_sta_list_t;
typedef struct { unsigned unused; } wifi_init_config_t;
typedef struct { struct {
    uint8_t ssid[32], password[64];
    unsigned ssid_len, channel, max_connection, authmode;
} ap; } wifi_config_t;
#define WIFI_EVENT_AP_STOP 1
#define WIFI_EVENT_AP_STACONNECTED 2
#define WIFI_EVENT_AP_STADISCONNECTED 3
#define WIFI_STORAGE_RAM 0
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_MODE_AP 2
#define WIFI_IF_AP 1
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *);
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int, const wifi_config_t *);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_deinit(void);
