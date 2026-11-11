#include "midi_feedback_guard.h"

#include <assert.h>
#include <stdio.h>

static void owner_context_and_controls_fence_old_jobs(void)
{
    patch_t patch;
    patch_default(&patch);
    midi_feedback_owner_guard_t guard =
        midi_feedback_owner_guard_capture(&patch, 10, -20, 7);
    assert(midi_feedback_owner_guard_matches(&guard, &patch, 10, -20, 7));
    assert(!midi_feedback_owner_guard_matches(&guard, &patch, 10, -20, 8));
    patch.volume++;
    assert(!midi_feedback_owner_guard_matches(&guard, &patch, 10, -20, 7));
    patch.volume--;
    assert(!midi_feedback_owner_guard_matches(&guard, &patch, 11, -20, 7));
}

static void reused_connection_handle_cannot_cross_epoch(void)
{
    assert(midi_feedback_link_guard_matches(11, 3, 11, 3, true, true));
    assert(!midi_feedback_link_guard_matches(11, 3, 12, 3, true, true));
    assert(!midi_feedback_link_guard_matches(11, 3, 11, 4, true, true));
    assert(!midi_feedback_link_guard_matches(11, 3, 11, 3, false, true));
    assert(!midi_feedback_link_guard_matches(11, 3, 11, 3, true, false));
}

int main(void)
{
    owner_context_and_controls_fence_old_jobs();
    reused_connection_handle_cannot_cross_epoch();
    puts("MIDI feedback guards: context/control change and reused-handle epoch race PASS");
    return 0;
}
