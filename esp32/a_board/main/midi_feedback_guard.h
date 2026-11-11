#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "patch.h"
#include "web_control.h"

typedef struct midi_feedback_owner_guard {
    patch_t patch;
    int32_t cutoff_offset, resonance_offset;
    uint32_t context_epoch;
} midi_feedback_owner_guard_t;

midi_feedback_owner_guard_t midi_feedback_owner_guard_capture(
    const patch_t *patch, int32_t cutoff_offset, int32_t resonance_offset,
    uint32_t context_epoch);
bool midi_feedback_owner_guard_matches(const midi_feedback_owner_guard_t *guard,
                                       const patch_t *patch,
                                       int32_t cutoff_offset,
                                       int32_t resonance_offset,
                                       uint32_t context_epoch);
bool midi_feedback_owner_state_matches(const midi_feedback_owner_guard_t *guard,
                                       const web_control_state_t *state);
bool midi_feedback_link_guard_matches(uint64_t expected_epoch,
                                      uint16_t expected_conn,
                                      uint64_t current_epoch,
                                      uint16_t current_conn,
                                      bool live, bool subscribed);
