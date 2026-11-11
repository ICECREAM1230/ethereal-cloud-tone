#include "parameter_cc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t cc, value; } emitted_t;
static emitted_t emitted[64];
static size_t emitted_count;

static void capture(uint8_t cc, uint8_t value, void *user)
{
    (void)user;
    assert(emitted_count < sizeof(emitted) / sizeof(emitted[0]));
    emitted[emitted_count++] = (emitted_t){cc, value};
}

static void round_trip_all_dumped_values(void)
{
    patch_t original;
    patch_default(&original);
    parameter_cc_state_t state = parameter_cc_state_from_patch(&original, 0, 0);
    emitted_count = 0;
    assert(parameter_cc_dump(&original, &state, capture, NULL) == 38);

    patch_t rebuilt;
    patch_default(&rebuilt);
    parameter_cc_state_t rebuilt_state = parameter_cc_state_from_patch(&rebuilt, 0, 0);
    for (size_t i = 0; i < emitted_count; ++i)
        assert(parameter_cc_apply(&rebuilt, &rebuilt_state,
                                  emitted[i].cc, emitted[i].value) != 0);

    emitted_t again[64];
    size_t first_count = emitted_count;
    memcpy(again, emitted, sizeof(emitted));
    emitted_count = 0;
    assert(parameter_cc_dump(&rebuilt, &rebuilt_state, capture, NULL) == first_count);
    assert(!memcmp(again, emitted, first_count * sizeof(emitted[0])));
}

static void loaded_values_choose_stable_nearest_cc(void)
{
    patch_t patch;
    patch_default(&patch);
    patch.osc[0].duty = 311;
    patch.lfo[0].rate = 12345;
    patch.lfo[1].dest = 3;
    patch.lfo[1].depth = 777;
    parameter_cc_state_t state = parameter_cc_state_from_patch(&patch, 1379, -91);
    const uint8_t controls[] = {25, 76, 110, 71, 74, 106};
    for (size_t i = 0; i < sizeof controls; ++i) {
        uint8_t value;
        assert(parameter_cc_read(&patch, &state, controls[i], &value));
        patch_t copy = patch;
        parameter_cc_state_t copied = state;
        assert(parameter_cc_apply(&copy, &copied, controls[i], value) != 0);
        uint8_t repeated;
        assert(parameter_cc_read(&copy, &copied, controls[i], &repeated));
        assert(repeated == value);
    }
}

static void project_law_anchors_are_preserved(void)
{
    patch_t patch;
    patch_default(&patch);
    parameter_cc_state_t state = parameter_cc_state_from_patch(&patch, 0, 0);
    assert(parameter_cc_apply(&patch, &state, 7, 127) == PARAM_CC_D_GAIN);
    assert(patch.volume == 0xfe);
    assert(parameter_cc_apply(&patch, &state, 73, 127) == PARAM_CC_D_ENV);
    assert(patch.env[0].attack == 0);
    assert(parameter_cc_apply(&patch, &state, 74, 127) == PARAM_CC_D_CUT);
    assert(parameter_cc_apply(&patch, &state, 106, 127) == PARAM_CC_D_CUT);
    assert(state.cutoff_offset == 8191);
    assert(parameter_cc_apply(&patch, &state, 71, 127) ==
           PARAM_CC_RESONANCE_WRITE);
    assert((int32_t)patch.filter.resonance + state.resonance_offset == 5632);
}

static void lfo2_dump_sets_destination_before_dependent_depth(void)
{
    for (uint8_t destination = 0; destination < 4; ++destination) {
        patch_t source;
        patch_default(&source);
        parameter_cc_state_t source_state =
            parameter_cc_state_from_patch(&source, 0, 0);
        assert(parameter_cc_apply(&source, &source_state, 112,
                                  (uint8_t)(destination << 5)) != 0);
        assert(parameter_cc_apply(&source, &source_state, 110, 37) != 0);
        emitted_count = 0;
        parameter_cc_dump(&source, &source_state, capture, NULL);

        patch_t rebuilt;
        patch_default(&rebuilt);
        parameter_cc_state_t rebuilt_state =
            parameter_cc_state_from_patch(&rebuilt, 0, 0);
        size_t destination_index = emitted_count, depth_index = emitted_count;
        for (size_t i = 0; i < emitted_count; ++i) {
            if (emitted[i].cc == 112) destination_index = i;
            if (emitted[i].cc == 110) depth_index = i;
            assert(parameter_cc_apply(&rebuilt, &rebuilt_state,
                                      emitted[i].cc, emitted[i].value) != 0);
        }
        assert(destination_index < depth_index);
        assert(rebuilt.lfo[1].dest == source.lfo[1].dest);
        assert(rebuilt.lfo[1].depth == source.lfo[1].depth);
    }
}

int main(void)
{
    round_trip_all_dumped_values();
    loaded_values_choose_stable_nearest_cc();
    project_law_anchors_are_preserved();
    lfo2_dump_sets_destination_before_dependent_depth();
    puts("parameter CC: 38-value reversible dump and stable nearest readback PASS");
    return 0;
}
