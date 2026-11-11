#include "ble_midi_unframer.h"

/* MMA/AMEI RP-052, BLE-MIDI 1.0 (2015-11-01), sections 7 and 8:
 * https://midi.org/midi-over-bluetooth-low-energy-ble-midi
 * Running status is local to one ATT payload. Common/real-time messages retain
 * it in BLE, but the next omitted-status message needs its own timestamp.
 * Only SysEx may span packets; every status (including EOX/real-time inside
 * SysEx) has a preceding timestamp. Outside SysEx, real-time is deinterleaved.
 * To preserve BLE message meaning on a classic MIDI byte stream, restore a
 * retained channel status after Common only when the next message omits it.
 * Timestamp clock correlation/scheduling is deliberately outside this module.
 */

void ble_midi_unframer_reset(ble_midi_unframer_t *state)
{
    if (state != NULL) {
        state->in_sysex = false;
    }
}

void ble_midi_unframer_init(ble_midi_unframer_t *state)
{
    ble_midi_unframer_reset(state);
}

static unsigned data_length(uint8_t status)
{
    if (status < 0xf0) {
        return (status & 0xe0) == 0xc0 ? 1 : 2;
    }
    if (status == 0xf1 || status == 0xf3) {
        return 1;
    }
    return status == 0xf2 ? 2 : 0;
}

static void append_byte(uint8_t *out, size_t *count, uint8_t byte)
{
    if (out != NULL) {
        out[*count] = byte;
    }
    ++*count;
}

/* First pass validates/counts without touching caller output. The second pass
 * emits only after the entire payload and capacity have been accepted. */
static bool decode_packet(bool initial_sysex, const uint8_t *packet, size_t len,
                          uint8_t *out, size_t *count, bool *final_sysex)
{
    bool sysex = initial_sysex;
    bool need_timestamp = true;
    bool restore_running = false;
    uint8_t running = 0;
    size_t i = 1;
    *count = 0;

    if (len < 2 || (packet[0] & 0xc0) != 0x80) {
        return false;
    }
    while (i < len) {
        if (sysex) {
            if (packet[i] < 0x80) {
                append_byte(out, count, packet[i++]);
                continue;
            }
            /* Here even F7 is a timestamp, never a bare EOX byte. */
            if (++i == len) {
                return false;
            }
            uint8_t status = packet[i++];
            if (status != 0xf7 && status < 0xf8) {
                return false;
            }
            append_byte(out, count, status);
            if (status == 0xf7) {
                sysex = false;
                need_timestamp = true;
            }
            continue;
        }

        bool timestamp = packet[i] >= 0x80;
        if (timestamp && ++i == len) {
            return false;
        }
        if (!timestamp && need_timestamp) {
            return false;
        }

        uint8_t status = packet[i];
        if (status >= 0x80) {
            if (!timestamp) {
                return false;
            }
            append_byte(out, count, status);
            ++i;
            if (status == 0xf0) {
                sysex = true;
                // RP-052 retains the prior channel status across same-packet
                // SysEx. A classic byte stream loses it at F0/F7 instead.
                restore_running = running != 0;
                continue;
            }
            if (status < 0xf0) {
                running = status;
                restore_running = false;
            } else if (status < 0xf8) {
                restore_running = running != 0;
            }
            need_timestamp = status >= 0xf0;
        } else {
            if (running == 0) {
                return false;
            }
            status = running;
            if (restore_running) {
                append_byte(out, count, running);
                restore_running = false;
            }
            need_timestamp = false;
        }

        unsigned needed = data_length(status);
        if (len - i < needed) {
            return false;
        }
        while (needed-- > 0) {
            /* RP-052 requires real-time outside SysEx to be deinterleaved. */
            if (packet[i] >= 0x80) {
                return false;
            }
            append_byte(out, count, packet[i++]);
        }
    }
    *final_sysex = sysex;
    return true;
}

bool ble_midi_unframer_feed_packet(ble_midi_unframer_t *state,
                                   const uint8_t *packet, size_t packet_len,
                                   uint8_t *out, size_t out_capacity,
                                   size_t *out_len)
{
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (state == NULL || packet == NULL || out == NULL || out_len == NULL) {
        ble_midi_unframer_reset(state);
        return false;
    }

    size_t count;
    bool final_sysex;
    if (!decode_packet(state->in_sysex, packet, packet_len, NULL, &count,
                       &final_sysex) || count > out_capacity) {
        ble_midi_unframer_reset(state);
        return false;
    }
    (void)decode_packet(state->in_sysex, packet, packet_len, out, &count,
                        &final_sysex);
    state->in_sysex = final_sysex;
    *out_len = count;
    return true;
}
