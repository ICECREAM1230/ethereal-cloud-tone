#include "midi_feedback_transport.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static midi_feedback_owner_guard_t owner(uint32_t context, uint8_t volume)
{
    patch_t patch;
    patch_default(&patch);
    patch.volume = volume;
    return midi_feedback_owner_guard_capture(&patch, 10, -20, context);
}

static void late_success_is_consumed_without_duplicate_enqueue(void)
{
    midi_feedback_transport_t transport = {0};
    const uint8_t packet[] = {0x80,0x81,0xb0,7,100};
    midi_feedback_owner_guard_t guard = owner(7, 100);
    uint32_t token = 0;
    assert(midi_feedback_transport_begin(&transport, 11, 3, packet,
           sizeof packet, &guard, &token) == MIDI_FEEDBACK_TRANSPORT_ENQUEUE);
    uint32_t original = token;
    assert(midi_feedback_transport_begin(&transport, 11, 3, packet,
           sizeof packet, &guard, &token) == MIDI_FEEDBACK_TRANSPORT_WAIT);
    assert(token == original);
    assert(midi_feedback_transport_mark_running(&transport, original));
    assert(midi_feedback_transport_complete(&transport, original, 0));
    assert(midi_feedback_transport_begin(&transport, 11, 3, packet,
           sizeof packet, &guard, &token) == MIDI_FEEDBACK_TRANSPORT_SENT);
    assert(transport.phase == MIDI_FEEDBACK_TRANSPORT_EMPTY);
}

static void old_completion_cannot_wake_or_complete_new_token(void)
{
    midi_feedback_transport_t transport = {0};
    const uint8_t first[] = {1}, second[] = {2};
    midi_feedback_owner_guard_t guard = owner(2, 90);
    uint32_t old_token, new_token;
    assert(midi_feedback_transport_begin(&transport, 4, 8, first, 1,
           &guard, &old_token) == MIDI_FEEDBACK_TRANSPORT_ENQUEUE);
    assert(midi_feedback_transport_mark_running(&transport, old_token));
    assert(midi_feedback_transport_complete(&transport, old_token, -1));
    assert(midi_feedback_transport_begin(&transport, 4, 8, second, 1,
           &guard, &new_token) == MIDI_FEEDBACK_TRANSPORT_ENQUEUE);
    assert(new_token != old_token);
    assert(!midi_feedback_transport_complete(&transport, old_token, 0));
    assert(transport.phase == MIDI_FEEDBACK_TRANSPORT_QUEUED);
    assert(transport.token == new_token);
}

static void queued_owner_change_is_rejected_by_callback_guard(void)
{
    midi_feedback_transport_t transport = {0};
    const uint8_t packet[] = {3};
    midi_feedback_owner_guard_t queued = owner(5, 80);
    midi_feedback_owner_guard_t changed = owner(6, 81);
    uint32_t token;
    assert(midi_feedback_transport_begin(&transport, 6, 2, packet, 1,
           &queued, &token) == MIDI_FEEDBACK_TRANSPORT_ENQUEUE);
    assert(!midi_feedback_transport_owner_matches(&transport, &changed));
    assert(midi_feedback_transport_owner_matches(&transport, &queued));
}

int main(void)
{
    late_success_is_consumed_without_duplicate_enqueue();
    old_completion_cannot_wake_or_complete_new_token();
    queued_owner_change_is_rejected_by_callback_guard();
    puts("MIDI feedback transport: late completion, token ownership and callback owner fence PASS");
    return 0;
}
