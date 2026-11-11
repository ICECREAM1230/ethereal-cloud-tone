// Copyright (c) 2026 ICECREAM1230
// ble_midi.h — MIDI over Bluetooth LE (peripheral, standard MIDI service)
//
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct midi_feedback_owner_guard;

// Bring up the NimBLE peripheral hosting the standard MIDI 1.0 over
// BLE service and separate private App control service. The complete GAP
// name remains "FPGA_Bluetooth"; scans carry the shortened "FPGA_BT" name.
// MIDI RX removes BLE framing and
// queues original MIDI bytes on raw source 2; engine_link alone sends SPI.
// Call once at boot after raw_midi_init and engine_link_init.
void ble_midi_init(void);

// Serial console key 'p': log stack state and re-trigger advertising
// (safe from any task). Part of the shared console dispatcher.
void ble_midi_console_status(void);

// Parameter-feedback direction. Epoch binds queued output to exactly one BLE
// connection; a reconnect can never inherit an old packet.
bool ble_midi_feedback_link(uint64_t *epoch, size_t *packet_max);
bool ble_midi_feedback_notify(uint64_t epoch,const uint8_t *packet,size_t length,
                              const struct midi_feedback_owner_guard *owner);
bool ble_midi_feedback_sync_pending(uint64_t *epoch);
void ble_midi_feedback_sync_complete(uint64_t epoch);

#ifdef __cplusplus
}
#endif
