// voice_alloc's source-aware FM allocation; no SPI calls in this module.
#pragma once
#include "fm_engine.h"
bool fm_voice_on(uint8_t source,uint8_t channel,uint32_t token,uint8_t note,uint8_t velocity);
void fm_voice_off(uint8_t source,uint8_t channel,uint32_t token,uint8_t note);
void fm_voice_sustain(uint8_t source,uint8_t channel,bool down);
void fm_voice_bend(uint8_t source,uint8_t channel,uint16_t bend);
void fm_voice_cc(uint8_t source,uint8_t channel,uint8_t cc,uint8_t value);
void fm_voice_web_expression(void);
void fm_voice_release_owner(uint32_t token);
void fm_voice_all_notes_off(uint8_t source,uint8_t channel);
void fm_voice_panic(void);
