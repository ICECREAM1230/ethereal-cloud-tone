#pragma once

#include "patch.h"

/* Data words for interfaces/midi_control/{dds,ks-fm}-template.md (2026-10-16).
 * SPI addresses, generation numbers, COMMIT and acknowledgement belong to the
 * transport owner. Offsets remain signed, note-independent and unclamped. */
typedef struct {
    uint32_t elements[8][8];
    uint32_t key_track, bend_range;
    uint32_t amp_depth[128], mod_depth[128];
    uint32_t ks_notes[128][2], ks_pan[128];
    uint32_t ks_header[64]; /* zero selects the original B3FF ABI */
    uint32_t fm_expression;
} midi_template_image_t;
typedef struct {
    patch_t patch;
    int32_t cutoff_offset,resonance_offset;
} midi_patch_snapshot_t;

/* Pure compilation; no hardware, allocation or active-patch mutation.
 * Returns false for null pointers, unsupported voice structure, decay outside
 * 200..8000 ms or a DDS pitch offset outside signed18. On false, discard out. */
bool midi_templates_compile(const patch_t *patch, unsigned decay_ms,
                            midi_template_image_t *out);
// One immutable owner snapshot is also the cache key. full_sub_patch requires
// a separately observed SUB1 capability; false preserves the original image.
bool midi_templates_compile_snapshot(const midi_patch_snapshot_t *snapshot,
                                    unsigned decay_ms,bool full_sub_patch,
                                    midi_template_image_t *out);
