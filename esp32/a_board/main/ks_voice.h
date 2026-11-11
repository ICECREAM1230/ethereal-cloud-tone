#pragma once
#include "patch.h"
#include "engine_link.h"
// MIDI source and channel identify a key owner; web tokens have a separate
// namespace so equal pitches from two inputs cannot release each other.
void ks_voice_on(uint8_t source, uint8_t channel, uint32_t web_token, uint8_t note, uint8_t velocity);
void ks_voice_off(uint8_t source, uint8_t channel, uint32_t web_token, uint8_t note);
void ks_voice_sustain(uint8_t source, uint8_t channel, bool down);
void ks_voice_release_owner(uint32_t web_token);
void ks_voice_all_notes_off(void);
void ks_voice_panic(void);
void ks_voice_expression(void);
void ks_voice_decay(unsigned milliseconds);
unsigned ks_voice_decay_ms(void);
void ks_voice_poll(void);
