// Bidirectional subtractive parameter CC mapping.
// The project-specific laws are shared by the owner and feedback exporters so
// an emitted value always decodes to the same stable control value.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "patch.h"

enum {
    PARAM_CC_D_RENDER = 1u,
    PARAM_CC_D_ENV = 2u,
    PARAM_CC_D_CUT = 4u,
    PARAM_CC_D_LFO = 8u,
    PARAM_CC_D_GAIN = 16u,
    PARAM_CC_D_ENV2 = 32u,
    PARAM_CC_D_LFO2 = 64u,
    PARAM_CC_RESONANCE_WRITE = 1u << 16,
};

typedef struct {
    uint8_t cutoff_coarse, cutoff_fine;
    int32_t cutoff_offset, resonance_offset;
} parameter_cc_state_t;

typedef void (*parameter_cc_emit_fn)(uint8_t cc, uint8_t value, void *user);

parameter_cc_state_t parameter_cc_state_from_patch(const patch_t *patch,
                                                    int32_t cutoff_offset,
                                                    int32_t resonance_offset);
uint32_t parameter_cc_apply(patch_t *patch, parameter_cc_state_t *state,
                            uint8_t cc, uint8_t value);
bool parameter_cc_read(const patch_t *patch, const parameter_cc_state_t *state,
                       uint8_t cc, uint8_t *value);
size_t parameter_cc_dump(const patch_t *patch,
                         const parameter_cc_state_t *state,
                         parameter_cc_emit_fn emit, void *user);
