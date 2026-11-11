#pragma once
#include "engine_link.h"
// PLL-derived rate, not an audio measurement. DDS UQ4.10 is not Hz.
#define KS_MIDI_MIN 19u
#define KS_MAX_LENGTH 4096u
#define KS_SAMPLE_RATE (50000000.0 * 233.0 / (158.0 * 768.0))
bool ks_params_note(uint8_t note, uint8_t velocity, unsigned decay_ms,
                    uint8_t volume, int8_t pan, uint32_t seed, engine_ks_note_t *out);
uint16_t ks_params_loss(uint8_t note, unsigned decay_ms);
void ks_params_gains(uint8_t volume, int8_t pan, uint16_t *left, uint16_t *right);
