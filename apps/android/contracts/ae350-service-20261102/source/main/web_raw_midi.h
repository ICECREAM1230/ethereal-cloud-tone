// JSON transport only; MIDI interpretation remains in the FPGA.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t token, sequence, context_epoch;
    uint8_t bytes[256];
    size_t length;
    bool has_binding;
} web_raw_midi_request_t;

// Requires source="web" and 1..256 exact integer bytes. Optional binding fields
// must all be present and nonzero. Rejects unknown/duplicate fields and trailing
// content. Reads exactly length bytes, needs no NUL terminator, clears out on
// failure, and does not perform session authorization or MIDI interpretation.
bool web_raw_midi_parse(const char *body, size_t length,
                        web_raw_midi_request_t *out);
