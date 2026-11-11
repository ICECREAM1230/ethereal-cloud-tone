#pragma once
#include "platform.h"
typedef struct {
    uint32_t processed_seq;
    bool quiet,usb,ble;
    uint32_t selected,usb_sent,ble_sent,usb_retries,ble_retries,ble_errors;
} b_router_report_t;
esp_err_t b_router_start(bool usb_initialized,bool ble_initialized);
void b_router_publish(const harp_target_t *,harp_route_t,uint32_t seq);
void b_router_snapshot(b_router_report_t *);
