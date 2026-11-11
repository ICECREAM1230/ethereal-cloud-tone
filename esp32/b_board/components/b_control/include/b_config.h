#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "harp_core.h"

#define B_CONFIG_BLOB_SIZE 224u
#define B_CONFIG_V2_SIZE 100u
#define B_CONFIG_MAGIC_V3 UINT32_C(0x42524833)

typedef struct {
    /* Public channel uses 1..16; the MIDI status nibble uses channel - 1. */
    int32_t channel, notes[12], gains[12], velocity[4];
    int32_t gate, volume, transpose, fine, cutoff, resonance;
    int32_t polarity, debounce, enc_edges, enc_reverse, route, pins[4][3];
} b_config_t;

typedef enum {
    B_FIELD_CHANNEL, B_FIELD_NOTES, B_FIELD_GAINS, B_FIELD_VELOCITY,
    B_FIELD_GATE, B_FIELD_VOLUME, B_FIELD_TRANSPOSE, B_FIELD_FINE,
    B_FIELD_CUTOFF, B_FIELD_RESONANCE, B_FIELD_POLARITY, B_FIELD_DEBOUNCE,
    B_FIELD_ENC_EDGES, B_FIELD_ENC_REVERSE, B_FIELD_ROUTE, B_FIELD_PINS,
    B_FIELD_COUNT
} b_field_t;

typedef struct {
    uint32_t fields;
    b_config_t values;
    /* -1 means the complete 4 x 3 pins; 0..3 serves legacy harp encpins. */
    int8_t pins_row;
} b_patch_t;

void b_config_defaults(b_config_t *out);
bool b_config_valid(const b_config_t *config);
/* On rejection, out is unchanged (including when out == current). */
bool b_config_patch(const b_config_t *current, const b_patch_t *, b_config_t *out);
bool b_config_equal(const b_config_t *, const b_config_t *);
bool b_config_needs_maintenance(const b_config_t *, const b_config_t *);
/* Little-endian canonical v3; a successful decode may migrate old 100-byte v2.
 * Defaults fill fields absent from v2. Bad blobs leave out/migrated unchanged. */
size_t b_config_encode(const b_config_t *, uint8_t *out, size_t capacity);
bool b_config_decode(const uint8_t *blob, size_t size, const b_config_t *defaults,
                     b_config_t *out, bool *migrated);
