// Copyright (c) 2026 ICECREAM1230
// engine_link.h — the single owner of the FPGA SPI link
//
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
//
// Everything the synth engine hears goes through desired-state images.
// A 1 kHz wake advances bounded, fair live/parameter/producer batches;
// bank changes and musical gates use the FPGA's acknowledged commit state.
// Nothing else may call fpga_word_write()/fpga_swap() after initialization.
//
// Shadow discipline (implemented here, in exactly one place): after a
// swap the new shadow holds the previous generation, so each tick
// writes this tick's dirty words UNION last tick's dirty words before
// swapping — the shadow catches up on the generation it missed.
// Callers never think about banks.

#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENGINE_NUM_ELEMENTS 256
#define ENGINE_WORDS_PER_ELEMENT 7   // OSC DUTY FILTER GAIN GATE PTRS0 PTRS1
// GAIN word: volume UQ4.4 per channel (issue #40) — 0x00 = silence/
// exact mute, 0xFF = loudest; positive gain-bus values mean LOUDER.
// FILTER word: [13:0] cutoff UQ4.10 log2; [27:14] resonance UQ4.10
// log2 — octaves of Q above Butterworth (0 = Butterworth, ~1 integer
// = +6 dB of resonant peak, top of range = self-oscillation);
// [31:28] reserved.

typedef struct {
    uint8_t  elem;    // element index 0..255
    uint8_t  word;    // 0..6 (OSC through PTRS1)
    uint32_t value;
} engine_cmd_t;

// Mutes every element into the local image, writes both banks (two
// full-image writes with a swap between), and starts the 1 kHz tick
// task. Call after fpga_spi_init(). The boot image goes silent here.
// Bus pointers initialize to 0 (no modulation); the bus plan — which
// pointer targets which bus — is entirely voice_alloc's convention,
// pushed as ordinary PTRS0/PTRS1 commands at its init.
void engine_link_init(void);

// Update the desired parameter image. Address coalescing is lossless;
// false only means an invalid address or unavailable engine.
bool engine_link_send(const engine_cmd_t *cmd);

// One atomic note image. The engine first confirms GateOff was consumed by
// a complete control pass, commits these words and depths to the active bank,
// then emits GateOn. An Off cancels any queued/in-flight On for this voice.
#define ENGINE_NOTE_WORDS 40
#define ENGINE_NUM_VOICES 32
typedef struct {
    uint8_t voice, count, amp_entry, mod_entry;
    engine_cmd_t words[ENGINE_NOTE_WORDS];
    uint32_t amp_depth, mod_depth, cutoff;
} engine_note_t;
bool engine_link_note_on(const engine_note_t *note);

// Queue one live bus-base write (bus_architecture.md). Buses are not
// banked and need no swap: the write goes straight to the bus base
// register (0x0800 + bus) on the next tick. Value is signed Q8.10 in
// the low 18 bits. Bus 0 is hardwired zero and cannot be written.
bool engine_link_bus_write(uint16_t bus, uint32_t value_q810);

// ── Producer table (B4/B5) ──────────────────────────────────────────
// 128 producers x 3 words (stride 4) at 0x0100, banked like
// parameters (config is wiring): writes land in the shadow and take
// effect at the swap, with the same catch-up mirroring as the
// element image.
//   word 0 CFG:   [3:0] type (0 off, 1 LFO, 2 ADSR), [5:4] LFO shape
//                 (saw/pulse/tri/sine), [15:6] target bus,
//                 LFO:  [31:16] rate (UQ0.24 increment low bits:
//                       5.7 mHz steps, 375 Hz max)
//                 ADSR: [25:16] gate bus (level-sensitive: > 0 held)
//   word 1 RATES (ADSR), in the universal A, D, S, R order:
//                 [7:0] attack, [15:8] decay, [31:24] release as
//                 8-bit log2 RATES — increment = (16+low4) << high4
//                 in 1/16-LSB units (the level carries 4 fractional
//                 bits; that IS the four-octave down-bias, so gentle
//                 decays exist). One uniform expression, no
//                 truncation: all 256 codes are distinct equal-ratio
//                 steps — perceptually linear, so a MIDI CC maps as
//                 (cc << 1). Full-range ~44 s .. ~0.7 ms; rates, not
//                 durations — no 1/x in gateware. [23:16] SUSTAIN
//                 LEVEL, one LSB = envelope span / 256 below peak
//   word 2 DEPTH: [17:0] signed Q8.10 contribution amplitude
// The producer ADDS to the bus base: firmware's base write and the
// producer's contribution coexist on one bus (e.g. bend + vibrato).
// Amp-envelope idiom: base = full attenuation, depth NEGATIVE — the
// envelope subtracts silence.
#define ENGINE_NUM_PRODUCERS 256   // full 256-entry pass at 96 kHz
// CSP opcode = CFG[3:0], a BITMASK of enables since #145, not an enum:
//   bit 0 reads a source operand      bit 2 multiplies by DEPTH
//   bit 1 has persistent state        bit 3 accumulates onto the target
// An envelope is the instruction that watches a gate, so state+source means
// envelope and state alone means phase accumulator. These must match
// synth_pkg.sv's OPC_* exactly -- a stale value here is silent: the gateware
// decodes it as some other instruction rather than rejecting it.
#define CSP_OPC_OFF   0x0u
#define CSP_OPC_LFO   0xEu   // state + multiply + accumulate
#define CSP_OPC_ADSR  0xFu   // source (gate) + state + multiply + accumulate
#define CSP_OPC_SEND  0xDu   // source + multiply + accumulate

bool engine_link_prod_write(uint8_t entry, uint8_t word, uint32_t value);

// Cumulative rejected API commands (e.g. a GateOn bypassing the transaction).
// Valid desired-image updates cannot overflow a command queue.
uint32_t engine_link_drops(void);

// Confirmed active state for release bookkeeping; pending desired edits do
// not count as audible yet. Bus confirmation includes its live mailbox ack.
uint32_t engine_link_prod_committed(uint8_t entry, uint8_t word);
bool engine_link_bus_committed(uint16_t bus, uint32_t value);
uint32_t engine_link_release_coefficient(uint8_t entry, uint32_t *revision);

// KS01 mailbox ABI. Accepted commands and completed initialization are
// distinct; only readback from this SPI owner is published as applied state.
#define ENGINE_KS_VOICES 4
#define ENGINE_KS_CAPABILITY 0x4B533031u
#define ENGINE_KS_VERSION 0x00010004u
typedef enum { KS_NOTE_ON=1, KS_NOTE_OFF, KS_PANIC, KS_MODE, KS_GAIN, KS_LOSS } engine_ks_op_t;
typedef struct {
    uint16_t length, loss, amplitude, gain_left, gain_right;
    int16_t tuning;
    uint32_t seed;
} engine_ks_note_t;
typedef struct {
    bool available, valid, requested_mode, applied_mode, switching, busy, recovering;
    uint8_t active_mask, init_mask, release_mask, pending_mask;
    uint32_t accepted_sequence, mode_sequence, reject_count, frame_count, deadline_count;
    uint32_t started_sequence[ENGINE_KS_VOICES], note_sequence[ENGINE_KS_VOICES];
    // Sticky last fault: 1 confirmation timeout, 2 SPI readback, 3 FPGA reset.
    uint32_t fault, recovery_count, timeout_count;
} engine_ks_state_t;
bool engine_link_ks_note_on(unsigned voice, const engine_ks_note_t *note);
void engine_link_ks_note_off(unsigned voice);
void engine_link_ks_panic(void);
bool engine_link_ks_mode(bool mode);
void engine_link_ks_gain(unsigned voice, uint16_t left, uint16_t right);
void engine_link_ks_loss(unsigned voice, uint16_t loss);
void engine_link_ks_state(engine_ks_state_t *state);
// The event owner acknowledges after EVT_PANIC has cleared all key ownership.
void engine_link_owner_panic_ack(void);

#ifdef __cplusplus
}
#endif
