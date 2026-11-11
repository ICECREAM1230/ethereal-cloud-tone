#ifndef BLE_MIDI_UNFRAMER_H
#define BLE_MIDI_UNFRAMER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One instance per BLE connection/epoch; reset on ingress loss or reconnect. */
typedef struct {
    bool in_sysex;
} ble_midi_unframer_t;

void ble_midi_unframer_init(ble_midi_unframer_t *state);
void ble_midi_unframer_reset(ble_midi_unframer_t *state);
/* Remove RP-052 ATT framing and preserve MIDI message meaning on a classic
 * byte stream. Restore a channel status only at the first omitted-status message
 * after System Common (including same-packet SysEx); ordinary Running Status
 * and MIDI values remain unchanged. Running Status is packet-local; only SysEx
 * spans packets.
 * Each restored status replaces that message's mandatory timestamp byte, so the
 * output is at most packet_len-1 bytes (the ATT header is always removed).
 * Returns false on malformed framing, partial non-SysEx messages, null arguments,
 * or insufficient capacity. Failure clears state, sets *out_len=0 when provided,
 * and leaves out untouched. Caller must flush its source on failure.
 * Input/output buffers must not overlap. No timestamp scheduling is performed.
 */
bool ble_midi_unframer_feed_packet(ble_midi_unframer_t *state,
                                   const uint8_t *packet, size_t packet_len,
                                   uint8_t *out, size_t out_capacity,
                                   size_t *out_len);

#endif
