// Private control service, separate from standard BLE MIDI. The MIDI module
// remains the sole NimBLE host/GAP owner and forwards lifecycle events here.
#pragma once
#include "esp_err.h"
struct ble_gap_event;
struct ble_gatt_svc_def;

// Call after nimble_port_init, before registering services/starting the host.
// Only a small persistent host mailbox is initialized; no worker/decoder yet.
esp_err_t ble_control_init(void);
const struct ble_gatt_svc_def *ble_control_services(void);
void ble_control_gap_event(const struct ble_gap_event *event);
void ble_control_host_reset(void);
