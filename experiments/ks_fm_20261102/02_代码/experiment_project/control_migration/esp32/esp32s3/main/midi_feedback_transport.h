#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "midi_feedback_guard.h"

#define MIDI_FEEDBACK_TRANSPORT_PACKET_MAX 517

typedef enum {
    MIDI_FEEDBACK_TRANSPORT_EMPTY,
    MIDI_FEEDBACK_TRANSPORT_QUEUED,
    MIDI_FEEDBACK_TRANSPORT_RUNNING,
    MIDI_FEEDBACK_TRANSPORT_COMPLETE,
} midi_feedback_transport_phase_t;

typedef enum {
    MIDI_FEEDBACK_TRANSPORT_BUSY,
    MIDI_FEEDBACK_TRANSPORT_ENQUEUE,
    MIDI_FEEDBACK_TRANSPORT_WAIT,
    MIDI_FEEDBACK_TRANSPORT_SENT,
    MIDI_FEEDBACK_TRANSPORT_FAILED,
} midi_feedback_transport_action_t;

typedef struct {
    midi_feedback_transport_phase_t phase;
    uint32_t token;
    int result;
    uint64_t epoch;
    uint16_t conn;
    size_t length;
    midi_feedback_owner_guard_t owner;
    uint8_t packet[MIDI_FEEDBACK_TRANSPORT_PACKET_MAX];
} midi_feedback_transport_t;

midi_feedback_transport_action_t midi_feedback_transport_begin(
    midi_feedback_transport_t *transport, uint64_t epoch, uint16_t conn,
    const uint8_t *packet, size_t length,
    const midi_feedback_owner_guard_t *owner, uint32_t *token);
bool midi_feedback_transport_mark_running(midi_feedback_transport_t *transport,
                                          uint32_t token);
bool midi_feedback_transport_complete(midi_feedback_transport_t *transport,
                                      uint32_t token, int result);
bool midi_feedback_transport_owner_matches(
    const midi_feedback_transport_t *transport,
    const midi_feedback_owner_guard_t *owner);
