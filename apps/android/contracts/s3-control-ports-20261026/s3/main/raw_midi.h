#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "../contract/midi/midi_raw_contract.h"

enum { RAW_MIDI_OK, RAW_MIDI_UNAVAILABLE, RAW_MIDI_TIMEOUT, RAW_MIDI_REJECTED,
       RAW_MIDI_OVERFLOW, RAW_MIDI_EXPIRED, RAW_MIDI_RESET, RAW_MIDI_IO };
typedef struct {
    uint64_t session;
    uint32_t queued_bytes, sent_bytes, dropped_bytes, flushes, fault;
    bool flushing;
} raw_midi_source_state_t;
typedef struct {
    bool available, initialized, enabled, ready, inflight;
    bool input_capabilities_known, independent_usb, native_sensor_source3, stop_pending;
    uint32_t accepted, rejected, events, usb_status, fault, reset_count;
    uint32_t stop_requested, stop_completed, stop_fault;
    raw_midi_source_state_t source[4];
} raw_midi_state_t;

// Init before publishers; service is called ONLY by the existing SPI owner.
void raw_midi_init(void);
void raw_midi_request_enable(bool enabled);
// RAM-only input barrier for a local FPGA engine switch. Entering pause drops
// ESP32 queues and schedules separate Web/BLE releases (legacy images also 3), preserving USB,
// the FPGA enable setting, and explicit STOP receipts. Resume never replays.
void raw_midi_pause(bool paused);
void raw_midi_bind(uint8_t source, uint64_t session);
// Claim an idle source without stealing another producer's Running Status.
bool raw_midi_claim(uint8_t source, uint64_t session);
bool raw_midi_renew(uint8_t source, uint64_t session, int64_t expires_us);
// ALL releases ESP32 RAW sources. On legacy FPGA images FF also clears USB;
// independent_usb reports whether the FPGA can preserve USB during recovery.
void raw_midi_flush(uint8_t source);
// Explicit global STOP, including USB. RAM-only, returns a nonzero receipt.
// stop_completed advances only after the FPGA acknowledges every required
// scope. Requests may coalesce; failure leaves stop_pending and stop_fault.
uint32_t raw_midi_request_stop_all(void);
bool raw_midi_submit(uint8_t source, uint64_t session, const uint8_t *bytes,
                     size_t length, int64_t expires_us);
void raw_midi_service(int64_t deadline_us);
void raw_midi_state(raw_midi_state_t *out);
