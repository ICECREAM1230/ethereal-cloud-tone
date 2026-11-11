#include "midi_feedback_guard.h"

#include <string.h>

midi_feedback_owner_guard_t midi_feedback_owner_guard_capture(
    const patch_t *patch, int32_t cutoff_offset, int32_t resonance_offset,
    uint32_t context_epoch)
{
    return (midi_feedback_owner_guard_t){
        .patch=*patch, .cutoff_offset=cutoff_offset,
        .resonance_offset=resonance_offset, .context_epoch=context_epoch};
}

bool midi_feedback_owner_guard_matches(const midi_feedback_owner_guard_t *guard,
                                       const patch_t *patch,
                                       int32_t cutoff_offset,
                                       int32_t resonance_offset,
                                       uint32_t context_epoch)
{
    return guard->context_epoch == context_epoch &&
        guard->cutoff_offset == cutoff_offset &&
        guard->resonance_offset == resonance_offset &&
        memcmp(&guard->patch, patch, sizeof(*patch)) == 0;
}

bool midi_feedback_owner_state_matches(const midi_feedback_owner_guard_t *guard,
                                       const web_control_state_t *state)
{
    bool eligible = state->ready && !state->rendering_pending &&
        !state->mode_switching && !state->mode_recovering &&
        state->requested_engine == FM_ENGINE_DDS &&
        state->applied_engine == FM_ENGINE_DDS &&
        !(state->local.supported && (state->local.local_active ||
                                    state->local.blocked ||
                                    state->local.remote_pending));
    return eligible && midi_feedback_owner_guard_matches(
        guard, &state->patch, state->cutoff_offset, state->resonance_offset,
        state->context_epoch);
}

bool midi_feedback_link_guard_matches(uint64_t expected_epoch,
                                      uint16_t expected_conn,
                                      uint64_t current_epoch,
                                      uint16_t current_conn,
                                      bool live, bool subscribed)
{
    return live && subscribed && expected_epoch &&
        expected_epoch == current_epoch && expected_conn == current_conn;
}
