// Old control-ABI golden tests isolate the new performance service here.
// Real raw/template state machines and engine fences have separate tests.
#include "raw_midi.h"
#include "midi_template_link.h"
#include <string.h>
#ifndef REAL_RAW_MIDI
void raw_midi_state(raw_midi_state_t *out) { memset(out,0,sizeof(*out)); }
void raw_midi_service(int64_t deadline) { (void)deadline; }
void raw_midi_request_enable(bool enabled) { (void)enabled; }
void raw_midi_pause(bool paused) { (void)paused; }
#endif
void midi_template_link_init(void) { }
void midi_template_link_invalidate(void) { }
void midi_template_link_state(midi_template_link_state_t *out) { memset(out,0,sizeof(*out)); }
void midi_template_link_service(int64_t deadline,bool idle,bool available)
{ (void)deadline;(void)idle;(void)available; }
