// Copyright (c) 2026 ICECREAM1230
// voice_alloc.c — MIDI events → voices → element parameter commands
//
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT

#include "voice_alloc.h"

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "event_bus.h"
#include "status_led.h"
#include "engine_link.h"
#include "patch.h"
#include "web_control.h"
#include "web_control_v2.h"
#include "fm_contract.h"
#include "ks_voice.h"
#include "fm_voice.h"
#include "dds_presets.h"
#include "fm_only_contract.h"
#ifndef S3_LEGACY_HOST_TEST
#include "raw_midi.h"
#include "web_raw_queue.h"
#include "midi_template_link.h"
#include "parameter_cc.h"
#include "midi_feedback.h"
#include "ble_midi.h"
#include "ks_timbre_control.h"
#include "fdc_control.h"
#include "ks_timbre_contract.h"
#include "sub_patch1.h"
#endif

#define TAG "voice_alloc"

#define VA_QUEUE_LEN   64
#define VA_TASK_STACK  6144
#define VA_TASK_PRIO   5

#define NUM_VOICES     32
#define ELEMS_PER_VOICE 8

// The active sound now lives in g_patch (patch.h, issue #69);
// WAVE_SAW / resonance / base volume / ADSR come from there. Only
// the exact-mute code stays a local constant.
// (Resonance is log2-encoded, FILTER[27:14] = octaves of Q above
// Butterworth; volume is UQ4.4, 0x00 = silence since #40.)
#define VOL_MUTE   0x00               // exact mute (special-cased in RTL)

// Master volume rides the per-voice gain-bus BASE, summed with the
// amp-envelope producer — exactly what the mod buses are for (Thor):
// one cheap bus write per voice, no swap and no element re-render,
// instead of re-baking every GAIN word. The GAIN word carries a
// FIXED per-note ceiling (VOL_REF minus velocity); g_patch.volume
// moves the gain-bus base around it. VOL_REF is the unity anchor
// (the former default volume) so a bus offset of 0 reproduces the
// old sound exactly: the RTL adds (gain_bus >>> 6) to the UQ4.4 word
// gain, so 64 bus LSB = one UQ4.4 step, and off = (vol−VOL_REF)·64
// makes word+bus == the old (vol−vel) code bit-for-bit.
#define VOL_REF    0xCF               // unity anchor = patch_default volume

// One semitone in the UQ4.10 log2 pitch (1024 LSB per octave).
#define SEMI_LSB(s)  ((int32_t)(s) * 1024 / 12)

// Unison detune spread positions (symmetric, in "steps"); the actual
// LSB offset is step * g_patch.unison_detune. The 7-wide set is the
// supersaw; the 4-wide set feeds each half of the 4+4 mode.
static const int8_t SPREAD7[7] = {-3, -2, -1, 0, 1, 2, 3};
static const int8_t SPREAD4[4] = {-3, -1, 1, 3};

// Voice lifecycle (Thor, 2026-09-01: "hitting the same key does NOT
// mean deallocating a voice with the same key"). A voice is an
// instance of a KEYSTROKE, not a key: every note-on allocates a
// fresh voice; the previous strike of the same key keeps ringing its
// release tail underneath. Three states, because "key held" and
// "voice in use" are different facts:
//   V_HELD      key is down, envelope gated on
//   V_RELEASING key is up, release tail still audible
//   V_IDLE      tail done (or never started) — free for allocation
// RELEASING promotes to IDLE lazily during allocation scans, once
// esp_timer says the tail has run out — no timer task.
typedef enum { V_IDLE = 0, V_HELD, V_RELEASING } voice_state_t;

typedef struct {
    voice_state_t state;
    uint8_t  note;
    uint8_t  vel;           // stored so a live CC edit can re-render (#70)
    uint8_t  channel;       // stored for later multi-timbrality; omni today
    uint32_t web_owner;     // 0 for MIDI; token for a browser session
    uint32_t stamp;         // allocation order, for oldest-steal
    int64_t release_until; // conservative threshold deadline, also steal rank
    int64_t release_checked;
    double release_log, release_rate; // accumulated RC decay, per µs
    bool release_started;
    uint32_t release_revision;
} voice_t;

static voice_t s_voices[NUM_VOICES];
#ifdef S3_LEGACY_HOST_TEST
static uint32_t s_stamp;
#endif
static uint8_t  s_wheel;   // CC1 mod wheel, 0..127, omni for now
static int16_t  s_bend;    // pitch bend as Q8.10 offset, ±2 semitones
// #89 round 2: velocity scales the ENVELOPE AMOUNT, so there is no static
// per-voice velocity term on the cutoff bus any more -- s_vel_cut retired.
// The g(vel) helpers live further down, below ENV_SPAN.
static int32_t  s_cut_off; // CC 74/106 cutoff brightness offset (Q8.10)

// Live CC edits COALESCE (issue #70 crash fix): a CC just marks what
// changed; apply_dirty() does the heavy work at a bounded rate.
// Rendering per-CC flooded engine_link and starved the CPU (task
// watchdog) when a controller swept — render_active_voices() alone
// is ~320 element writes.
#define D_RENDER 1u   // re-render sounding voices (element words)
#define D_ENV    2u   // push amp env to producers
#define D_CUT    4u   // refresh cutoff buses
#define D_LFO    8u   // push LFO 1
#define D_GAIN  16u   // refresh gain-bus bases (master volume)
#define D_ENV2  32u   // push MOD env producers (#42)
#define D_LFO2  64u   // push LFO 2 (#73)
static uint32_t s_dirty;
static int64_t s_last_apply, s_last_expression;
static uint32_t s_apply_active;
static unsigned s_render_cursor;
#define EXPRESSION_MIN_US 1000
#define RECONFIG_MIN_US 4000
#define VA_BATCH_US 250
static TaskHandle_t s_va_task;
static uint32_t s_requested_engine=CONFIG_FPGA_FM_ONLY?FM_ENGINE_FM:UINT32_MAX;
#ifndef S3_LEGACY_HOST_TEST
static bool s_fm_only_boot_select_pending=CONFIG_FPGA_FM_ONLY;
#endif
#ifdef S3_LEGACY_HOST_TEST
static uint16_t s_dds_banks[16];
#endif
static uint32_t s_dds_switch_id,s_dds_switch_context;
#ifndef S3_LEGACY_HOST_TEST
static uint32_t s_dds_stop_ticket,s_dds_stop_reset_count;
#endif
static int64_t s_dds_switch_deadline;
static patch_t s_dds_candidate;
static bool fm_active(engine_fm_status_t *state)
{
    engine_link_fm_status(state);
    return engine_fm_control_active(state);
}

// ── Bus plan (B3/B5, firmware convention — bus_architecture.md) ─────
// bus 2:      global pitch offset — the pitch wheel. Every element's
//             pitch pointer references it.
// bus 3:      global resonance offset — TEMPORARY CC 71 assignment
//             (Thor, 2026-09-03) until the MIDI schema is nailed
//             down. Every element's Q pointer references it; the
//             knob writes (cc << 7) − RESO so the effective code is
//             exactly cc << 7 (0 = Butterworth .. 127 ≈ self-osc).
// bus 16+v:   voice v's gain bus — OWNED BY THE AMP ENVELOPE (B5;
//             volume semantics since issue #40): base = −ENV_SPAN
//             (the quiet floor), the ADSR source adds level ×
//             (+ENV_SPAN) — the level simply ADDS volume: floor at
//             level 0, the note's full volume at level 1. No
//             negative-depth trick. Velocity is note-static and
//             bakes into the GAIN (volume) word instead.
// bus 4:      CHANNEL cutoff bus (#44, the scope ladder made real):
//             wheel + bend + CC74/106 — ONE firmware write, fanned
//             out to the 32 per-voice cutoff buses by type-3 bus
//             sources in the walker (channel → voice → element).
// bus 48+v:   voice v's cutoff offset — per-voice base carries ONLY
//             the velocity term now; the walker adds the MOD env and
//             the channel-bus fan-out on top (#84 chaining).
// bus 80+v:   voice v's GATE bus — the ADSR watches it (level-
//             sensitive, > 0 = held). Note-on/off is ONE live write.
// Bend rides BOTH pitch and cutoff buses so filter key tracking
// follows bends (Thor).
#define BUS_DUTY_GLOBAL  1   // global PWM bus (#73) — was null; every
                             // duty pointer references it (bus 0 stays
                             // the strict null bus)
#define BUS_PITCH_GLOBAL 2
#define BUS_RESO_GLOBAL  3
#define BUS_CH_CUT       4   // channel cutoff bus (#44)
#define BUS_GAIN(v)   (16 + (v))
#define BUS_CUT(v)    (48 + (v))
#define BUS_VGATE(v)  (80 + (v))

// Producer plan: entries 0..31 = LFOs (0 is the boot vibrato),
// entries 32..63 = per-voice amp ADSRs, 64..127 = per-voice PAIRS of
// (MOD env, channel-cut fan-out) — the pair MUST be adjacent: both
// write BUS_CUT(v), and #84 chain-summing requires same-bus writers
// in consecutive walker slots.
#define PROD_ADSR(v)   (32 + (v))
#define PROD_MODENV(v) (64 + 2 * (v))  // MOD env (#42): 2nd ADSR per
                                       // voice, watches the gate bus,
                                       // drives the voice's CUTOFF bus
#define PROD_FANOUT(v) (65 + 2 * (v))  // type-3 bus source (#44):
                                       // BUS_CH_CUT → BUS_CUT(v), unity
#define PROD_LFO2     1            // global LFO 2 (#73)
// Envelope span: 0x2800 Q8.10 = 10 octaves = 60 dB (Thor, iterating
// by ear — -96 dB buried attacks below audibility, 48 dB proved too
// shallow, 72 dB tried briefly; sustain LSB = span/256 = 0.234 dB).
// The linear level ramp into the log-encoded gain IS an
// exponential-amplitude curve (Thor) — slow-then-fast. Whether the
// attack should additionally be LINEARIZED in amplitude (RC-style
// fast-then-slow) is an OPEN QUESTION for discussion/testing — the
// agent's suggestion, not a decision. With volume semantics the bus
// base is MINUS this span and the source depth is PLUS it.
#define ENV_SPAN      0x2800 // 60 dB (Thor's by-ear pick)

// ---- #89 round 2: velocity scales the envelope AMOUNT -------------------
//
// c(vel), Q16. Linear today, which is the base shape Thor picked; it is a
// TABLE rather than an expression so the curve is an ear-tuning surface that
// can change without touching the amount arithmetic.
static uint32_t vel_curve_q16(uint8_t vel)
{
    return ((uint32_t)vel * 65536u) / 127u;
}

// g(vel) = 1 - (amt/127) * (1 - c(vel)), Q16.
//   amt = 0    -> g = 1 for every note: velocity OFF, full patch amount
//                 (the true-zero/isolation rule, design.md)
//   vel = 127  -> g = 1 whatever the amount: full velocity, full amount
// One-sided by construction -- no neutral point, the OB-8/DX7 convention.
static uint32_t vel_gain_q16(uint8_t vel, uint8_t amt)
{
    uint32_t c = vel_curve_q16(vel);
    return 65536u - ((uint32_t)amt * (65536u - c)) / 127u;
}

// The amp envelope's DEPTH for this note: the full 60 dB span scaled by
// g(vel). Base is -ENV_SPAN, so a soft note rises from the same silence to a
// LOWER peak rather than starting higher -- smaller excursion, same ramp rate,
// hence the shorter perceived attack.
static uint32_t amp_depth_for(uint8_t vel)
{
    return (uint32_t)(((int64_t)ENV_SPAN * vel_gain_q16(vel, g_patch.vel_amp_amt)) >> 16);
}

// The MOD envelope's DEPTH for this note. Signed: CC 107 is bipolar, so the
// scaling must preserve the sign and the 18-bit mask is applied after.
static uint32_t mod_depth_for(uint8_t vel)
{
    int32_t d = (int32_t)g_patch.env1_depth;
    int64_t scaled = ((int64_t)d * vel_gain_q16(vel, g_patch.vel_mod_amt)) >> 16;
    return (uint32_t)(int32_t)scaled & 0x3FFFF;
}
// RATES word in the universal A, D, S, R order: bytes 0/1/3 are
// 8-bit log2 RATES — increment = (16+low4) << high4 in 1/16-LSB
// units (the envelope level carries 4 fractional bits: that IS the
// four-octave down-bias, needed because decay only traverses
// peak→sustain; full-range times span ~44 s .. ~0.7 ms). All 256
// codes are distinct equal-ratio steps of a log2 ladder, so a MIDI
// CC maps perceptually linearly as (cc << 1). Byte 2 is the SUSTAIN
// LEVEL, one LSB = span/256 below peak (0.1875 dB at the 48 dB
// span).
// The amp envelope's A,D,S,R now lives in g_patch.env[0]
// (patch.h, #69); patch_adsr_rate1/rate2() convert it into the linear
// coefficients the CSP multiplies by (#145).
// patch_default() carries the ear-tuned values (0x98/0x20/0xF0/0x28).

// The per-voice cutoff bus BASE is now ZERO (#89 round 2). It used to carry
// the velocity term; velocity moved onto the MOD envelope's DEPTH word, where
// it scales the excursion instead of offsetting the starting point. The
// channel-wide terms live on BUS_CH_CUT, fanned out by the walker's type-3
// sources. The write is kept so a re-used voice cannot inherit a stale base.
#ifdef S3_LEGACY_HOST_TEST
static uint32_t cut_bus_value(int v)
{
    (void)v;
    return 0u;
}
#endif

// The CHANNEL cutoff value: wheel opens up to ~+5 octaves (Thor
// 2026-09-08), bend tracks ±2 semitones, CC 74/106 spans ±8 oct (#88).
static uint32_t ch_cut_value(void)
{
    return (uint32_t)((int32_t)s_wheel * 40 + s_bend + s_cut_off);
}
static QueueHandle_t s_queue;
static int s_sub_id = -1;

// RC release recurrence in adsr.sv: y[n+1] = y[n]*(1-k/2^24),
// once per complete CSP pass at 96 kHz. A 1/1024 normalized residue adds
// <0.06 dB to the 60 dB envelope floor; only then use exact element mute.
// Integer negative-product rounding/creep decays at least this fast, so the
// continuous upper bound is conservative. Do not reuse the former linear
// ramp / 48 kHz estimate (it cut tails with 10–13.5% level remaining).
#define RELEASE_LOG_THRESHOLD 6.931471805599453 // log(1024)
#define RELEASE_PASS_GUARD_US 22
static double release_rate_for(uint32_t k)
{
    return k ? -log1p(-(double)k/(1u<<ADSR_K_SHIFT))*0.096 : 0.0;
}
static int64_t release_tail_us(void)
{
    uint32_t k=(patch_adsr_rate2(&g_patch.env[0])>>4)&0x3FFFFu;
    return (int64_t)ceil(RELEASE_LOG_THRESHOLD/release_rate_for(k)) + RELEASE_PASS_GUARD_US;
}
static void begin_release(int v)
{
    voice_t *voice=&s_voices[v];
    voice->state=V_RELEASING;
    voice->release_started=false;
    voice->release_log=0;
    voice->release_until=esp_timer_get_time()+release_tail_us();
    engine_link_bus_write(BUS_VGATE(v),0);
}

// MIDI note → UQ4.10 log₂ pitch (octave [13:10], fraction [9:0]).
// A4 = 69 → 0x1700, verified 440 Hz by ear.
static uint16_t midi_to_pitch(uint8_t note)
{
    return (uint16_t)((note / 12) << 10) | (uint16_t)((note % 12) * 1024 / 12);
}

static engine_note_t *s_note_capture;

static void send(uint8_t elem, uint8_t word, uint32_t value)
{
    engine_cmd_t c = {.elem = elem, .word = word, .value = value};
    if (s_note_capture) {
        if (s_note_capture->count < ENGINE_NOTE_WORDS)
            s_note_capture->words[s_note_capture->count++] = c;
        return;
    }
    if (!engine_link_send(&c))
        ESP_LOGW(TAG, "engine queue full, elem %d word %d lost", elem, word);
}

// Base cutoff: 1/2 octave above the note (UQ4.10 log2). The mod
// wheel's contribution no longer touches the FILTER words at all —
// it rides cutoff bus 1, which every element's cutoff pointer
// references (see engine_link_init).
static uint16_t voice_fc(uint8_t note)
{
    // Key tracking (#91/#94, CC 31): 0..200% with CENTER 64 = 100% =
    // the historical hardwired tracking (Thor 2026-09-10: capping at
    // 100% felt undersensitive — convention overtracks to 200%).
    // 0 pins the cutoff at the C4 reference regardless of note.
    int32_t p   = (int32_t)midi_to_pitch(note);
    int32_t ref = (int32_t)midi_to_pitch(60);
    int32_t kt  = g_patch.filter.key_track;
    int32_t fc  = ref + ((p - ref) * kt) / 64 + 0x200;
    if (fc < 0) fc = 0;
    return (fc > 0x3FFF) ? 0x3FFF : (uint16_t)fc;
}

// How each of the 8 elements maps onto the two oscillators for the
// active voice structure (issue #72). osc = which oscillator (0/1),
// detune = unison spread in pitch LSBs, l/r = channel enables (both =
// centre), active = sounding (else GATE-muted).
typedef struct {
    uint8_t osc;
    int16_t detune;
    bool    l, r;
    bool    active;
} evoice_t;

// Fill the voicing plan for the current voice_struct. Returns the
// number of active (sounding) elements, for loudness make-up.
static int build_voicing(evoice_t p[ELEMS_PER_VOICE])
{
    for (int u = 0; u < ELEMS_PER_VOICE; u++) p[u] = (evoice_t){0};
    int  ud = g_patch.unison_detune;         // LSB per spread step
    int  n  = 0;

    // l/r mark which side a unison element LEANS (alternating). How
    // FAR it leans is continuous now — voice_program scales the far
    // side by g_patch.unison_stereo (#98: CC 28 is a spread AMOUNT
    // per the schema, not the on/off the old bool made of it).
    switch (g_patch.voice_struct) {
    case VOICE_2_PLAIN:                       // osc1 + osc2, centred
        p[0] = (evoice_t){0, 0, true, true, true};
        p[1] = (evoice_t){1, 0, true, true, true};
        n = 2;
        break;
    case VOICE_7_PLUS_1:                       // supersaw x7 + osc2
        for (int i = 0; i < 7; i++)
            p[i] = (evoice_t){0, (int16_t)(SPREAD7[i] * ud),
                              !(i & 1), (i & 1) != 0, true};
        p[7] = (evoice_t){1, 0, true, true, true};
        n = 8;
        break;
    case VOICE_4_PLUS_4:                       // both oscillators x4
        for (int i = 0; i < 4; i++) {
            p[i]     = (evoice_t){0, (int16_t)(SPREAD4[i] * ud),
                                  !(i & 1), (i & 1) != 0, true};
            p[i + 4] = (evoice_t){1, (int16_t)(SPREAD4[i] * ud),
                                  !(i & 1), (i & 1) != 0, true};
        }
        n = 8;
        break;
    }
    return n;
}

// Hard-mute a voice's elements via the per-element GATE word (exact
// mute in the gateware). Issue #68: the amp envelope's floor lives on
// the gain BUS, which bottoms at the "quietest audible" code, never
// true silence — inaudible per element but 256 elements sum ~48 dB
// and hum at high volume. So a fully-released voice gets GATE off,
// the ONE path that reaches exact zero; note_on re-gates via
// voice_program. (Enlarging ENV_SPAN to floor the bus into silence
// would wreck the ear-tuned envelope feel — hence firmware muting.)
static void hard_mute_voice(int v)
{
    for (int u = 0; u < ELEMS_PER_VOICE; u++)
        send((uint8_t)(v * ELEMS_PER_VOICE + u), 4, 0);   // GATE off
}

// Retire release tails that have run out: RELEASING → IDLE, and mute
// the voice exactly (once, at the transition). Called both from the
// note_on allocation scan and the task's periodic sweep, so an idle
// voice reaches true silence even with no further notes played.
static void promote_idle(int64_t now)
{
    // All amp envelopes normally share kR, so cache its expensive log decode.
    static uint32_t cached_k=UINT32_MAX;
    static double cached_rate;
    for (int v=0;v<NUM_VOICES;v++) {
        voice_t *voice=&s_voices[v];
        if (voice->state!=V_RELEASING) continue;
        if (!voice->release_started && !engine_link_bus_committed(BUS_VGATE(v),0))
            continue; // transport failure must never consume the tail budget
        uint32_t revision;
        uint32_t k=engine_link_release_coefficient(PROD_ADSR(v),&revision);
        if (k!=cached_k) { cached_k=k; cached_rate=release_rate_for(k); }
        if (!voice->release_started) {
            voice->release_started=true;
            voice->release_checked=now+RELEASE_PASS_GUARD_US;
            voice->release_rate=cached_rate;
            voice->release_revision=revision;
        }
        if (now>voice->release_checked) {
            // The commit could have happened anywhere since the last check.
            // Use the slower rate for that interval, then the new active rate.
            // If two rate changes landed between checks (including a change
            // away and back), the intervening rate is unknown: assume no
            // decay in that interval rather than cutting the voice too early.
            double conservative=(uint32_t)(revision-voice->release_revision)>=2
                ? 0.0 : fmin(voice->release_rate,cached_rate);
            voice->release_log+=(now-voice->release_checked)*conservative;
            voice->release_checked=now;
        }
        voice->release_rate=cached_rate;
        voice->release_revision=revision;
        double remaining=RELEASE_LOG_THRESHOLD-voice->release_log;
        if (remaining<=0) {
            voice->state=V_IDLE;
            hard_mute_voice(v);
        } else if (cached_rate>0) {
            voice->release_until=voice->release_checked+(int64_t)ceil(remaining/cached_rate);
        } else {
            voice->release_until=INT64_MAX; // zero/uncommitted k cannot release
        }
    }
}

// Render a voice from the two-oscillator plan (issue #72). Each active
// element takes its oscillator's wave / duty / pitch (note + coarse +
// fine + unison detune); pan and velocity bake into the GAIN word, the
// amp envelope articulates on the gain bus above it. Inactive elements
// (2-plain uses only 2 of 8) are GATE-muted. note_on re-gates here; a
// released voice was GATE-muted by promote_idle (#68).
static void voice_program(int v, uint8_t note, uint8_t vel)
{
    uint16_t fc = voice_fc(note);
    // Per-note ceiling only: fixed VOL_REF minus velocity (softer hits
    // are LOWER values). MASTER volume is NOT here — it rides the
    // gain-bus base (refresh_gain_buses), so a CC 7 sweep is bus
    // writes, not a re-render of every element.
    // #89 round 2: velocity is NOT here any more. The ceiling is the same for
    // every note; velocity scales the amp envelope's DEPTH so a soft note
    // reaches a lower peak from the same floor. vel stays in the signature
    // because note_on and the live re-render both pass it.
    (void)vel;
    int32_t vol = (int32_t)VOL_REF;

    // GAIN word carries the mode byte (filter type/dual) from the patch
    // so CC 29/30 render on re-program (#70).
    uint32_t mode = ((uint32_t)(g_patch.filter.dual & 1) << 16)
                  | ((uint32_t)(g_patch.filter.type & 3) << 17);

    evoice_t plan[ELEMS_PER_VOICE];
    int active = build_voicing(plan);
    // Loudness make-up: fewer summed elements are quieter. Gentle
    // UQ4.4 step boost (≈0.375 dB/step) — a conservative first pass so
    // 2-plain is not jarringly quiet vs the 8-element modes; Thor tunes
    // by ear (or we fold it into per-patch volume).
    int32_t makeup = (active <= 2) ? 8 : (active <= 4) ? 4 : 0;
    int32_t base   = (int32_t)midi_to_pitch(note);
    int      mix   = g_patch.osc_mix;      // ±: + favours osc2, − osc1

    for (int u = 0; u < ELEMS_PER_VOICE; u++) {
        uint8_t elem = (uint8_t)(v * ELEMS_PER_VOICE + u);
        if (!plan[u].active) {
            send(elem, 4, 0);              // GATE off = exact element mute
            continue;
        }
        const osc_t *o = &g_patch.osc[plan[u].osc];

        int32_t pitch = base + SEMI_LSB(o->coarse) + o->fine + plan[u].detune;
        if (pitch < 0)      pitch = 0;
        if (pitch > 0x3FFF) pitch = 0x3FFF;

        int32_t ovol = vol + makeup;
        if (plan[u].osc == 0 && mix > 0) ovol -= mix;   // favour osc2
        if (plan[u].osc == 1 && mix < 0) ovol += mix;   // favour osc1
        // Balance extremes are a MUTE (#91): the mix term above tops
        // out at ~23.6 dB of attenuation (63 UQ4.4 steps), so CC 24 at
        // the rails must silence the disfavored oscillator outright.
        bool bal_mute = (plan[u].osc == 0 && mix >= 63)
                     || (plan[u].osc == 1 && mix <= -63);
        // Pan (#91, CC 10): log-domain per-side attenuation, full
        // deflection mutes the far side (same shape as balance).
        int32_t pan  = g_patch.pan;
        int32_t lvol = ovol - (pan > 0 ?  pan : 0);
        int32_t rvol = ovol - (pan < 0 ? -pan : 0);
        if (lvol < 0x01) lvol = 0x01;
        if (lvol > 0xFF) lvol = 0xFF;
        if (rvol < 0x01) rvol = 0x01;
        if (rvol > 0xFF) rvol = 0xFF;
        // Continuous stereo spread (#98, CC 28): a leaning unison
        // element keeps its near side at full and attenuates the FAR
        // side by spread>>1 log-gain steps (0.375 dB each) — spread 0
        // = centered, 126 = −23.6 dB, 127 = exact far-side mute (the
        // historical hard pan, bit-for-bit at the default).
        int32_t spread = g_patch.unison_stereo & 0x7F;
        bool    lean   = plan[u].l != plan[u].r;
        bool l_en = plan[u].l || (lean && spread < 127);
        bool r_en = plan[u].r || (lean && spread < 127);
        if (lean && spread < 127) {
            if (plan[u].l) rvol -= spread >> 1;
            else           lvol -= spread >> 1;
            if (lvol < 0x01) lvol = 0x01;
            if (rvol < 0x01) rvol = 0x01;
        }
        uint32_t l = (l_en && !bal_mute && pan <  63)
                       ? (uint32_t)lvol : VOL_MUTE;
        uint32_t r = (r_en && !bal_mute && pan > -63)
                       ? (uint32_t)rvol : VOL_MUTE;

        send(elem, 0, (uint32_t)pitch | ((uint32_t)o->wave << 14));   // OSC
        send(elem, 1, (uint32_t)o->duty & 0xFFFFFF);                  // DUTY
        send(elem, 2, ((uint32_t)g_patch.filter.resonance << 14) | fc);
        send(elem, 3, (r << 8) | l | mode);                          // GAIN
        send(elem, 4, 1);                  // GATE on
    }
}

// Re-render HELD voices' element words from the current patch — the
// render half of live CC editing (#70). Only keys still down: a
// releasing tail is fading out, so re-programming its element words on
// every CC is inaudible and just multiplies the SPI load that pinned
// engine_link (#70 watchdog) — bus params (pitch/cutoff/reso/gain)
// still track tails, only the element-word rewrite is skipped. Needs
// the per-voice note+vel, which note_on stores.
// Push amp-envelope rates and this voice's own velocity amount.
// Tail bookkeeping reads the confirmed active coefficient after the swap.
static void update_amp_env_voice(int v)
{
    engine_link_prod_write(PROD_ADSR(v), 1, patch_adsr_rate1(&g_patch.env[0]));
    engine_link_prod_write(PROD_ADSR(v), 3, patch_adsr_rate2(&g_patch.env[0]));
    engine_link_prod_write(PROD_ADSR(v), 2, amp_depth_for(s_voices[v].vel));
}

// CC → LFO phase increment. The gateware increment is LINEAR in
// frequency (freq = inc * Fs / 2^24, Fs = 96 kHz), so an even-sounding
// control needs the log2/exponential mapping HERE (Thor: the old
// linear val<<7 crammed the useful slow range into the bottom and made
// everything past ~40 uselessly fast). Exponential 0.03 Hz .. 30 Hz
// across the CC — one equal frequency RATIO per step.
//   inc = freq * 2^24 / 96000  ≈ freq * 174.76
// CC → pulse-width duty offset, Q0.24 (#94 F2): duty = 0.5·10^(−v),
// v = val/127 — equal duty RATIO per step, 50% down to 5%, never the
// degenerate 0%/100%. Offset from square = 0.5 − duty.
static int32_t duty_from_cc(uint8_t val)
{
    float duty = 0.5f * powf(10.0f, -(float)val / 127.0f);
    return (int32_t)((0.5f - duty) * 16777216.0f);
}

#define LFO_FS_HZ   48000.0f   // half-rate walker (#100): an LFO's
                               // phase advances every OTHER sample
static uint16_t lfo_rate_from_cc(uint8_t val)
{
    float freq = 0.03f * powf(1000.0f, (float)val / 127.0f);   // 0.03..30 Hz
    float inc  = freq * (16777216.0f / LFO_FS_HZ);
    if (inc < 1.0f)      inc = 1.0f;
    if (inc > 65535.0f)  inc = 65535.0f;
    return (uint16_t)(inc + 0.5f);
}

// LFO 1 = source 0 (the vibrato). CC 76/77 rate/depth, CC 113 shape.
static void update_lfo1(void)
{
    engine_link_prod_write(0, 0,
        CSP_OPC_LFO | ((uint32_t)(g_patch.lfo[0].shape & 3) << 4)
           | ((uint32_t)BUS_PITCH_GLOBAL << 6)
           | ((uint32_t)g_patch.lfo[0].rate << 16));
    engine_link_prod_write(0, 2, (uint32_t)(uint16_t)g_patch.lfo[0].depth);
}

// LFO 2 = source 1 (#73), global. CC 109/110/111/112. Destinations:
// duty (PWM), resonance, PITCH (rides bus summing #84 — dual vibrato
// with LFO 1), or CUTOFF (#92 first route: the channel cut bus, whose
// 32 per-voice sends relay the LFO's contribution — possible since
// sends read the bus OUTPUT SUM). When the destination moves, the old
// bus's effective value would go stale (nothing writes it any more),
// so restore its firmware base.
static int32_t s_reso_off;   // CC 71's last bus offset (for restore)
static void refresh_cut_buses(void);   // defined below (dest restore)
static void update_lfo2(void)
{
    static uint16_t prev_bus = BUS_DUTY_GLOBAL;
    uint16_t bus = (g_patch.lfo[1].dest == 1) ? BUS_RESO_GLOBAL
                 : (g_patch.lfo[1].dest == 2) ? BUS_PITCH_GLOBAL
                 : (g_patch.lfo[1].dest == 3) ? BUS_CH_CUT
                                              : BUS_DUTY_GLOBAL;
    if (bus != prev_bus) {
        if (prev_bus == BUS_DUTY_GLOBAL)
            engine_link_bus_write(BUS_DUTY_GLOBAL, 0);
        else if (prev_bus == BUS_RESO_GLOBAL)
            engine_link_bus_write(BUS_RESO_GLOBAL, (uint32_t)s_reso_off);
        else if (prev_bus == BUS_CH_CUT)
            refresh_cut_buses();   // rewrite the base → sum un-freezes
        // prev == pitch needs no restore: LFO 1 (slot 0) rewrites the
        // pitch bus every sample, so it never goes stale.
        prev_bus = bus;
    }
    engine_link_prod_write(PROD_LFO2, 0,
        CSP_OPC_LFO | ((uint32_t)(g_patch.lfo[1].shape & 3) << 4)
           | ((uint32_t)bus << 6)
           | ((uint32_t)g_patch.lfo[1].rate << 16));
    engine_link_prod_write(PROD_LFO2, 2,
        (uint32_t)(uint16_t)g_patch.lfo[1].depth);
}

// MOD envelope (#42) = the even slots of the 64..127 pairs (#44
// layout: each voice's MOD env sits adjacent to its fan-out bus
// source — both write BUS_CUT(v), and #84 summing needs consecutive
// slots), one per voice: watches
// the voice's gate bus (same gate the amp env watches), drives the
// voice's CUTOFF bus with a SIGNED depth (the walker DEPTH word is
// signed 18-bit) — classic filter envelope, bipolar. Rates from
// patch env[1] (CCs 102–105), depth CC 107, dest CC 108 (stored;
// cutoff is the implemented destination).
static void update_mod_env_voice(int v)
{
    engine_link_prod_write(PROD_MODENV(v), 0,
        CSP_OPC_ADSR | ((uint32_t)BUS_CUT(v) << 6)
           | ((uint32_t)BUS_VGATE(v) << 16));
    engine_link_prod_write(PROD_MODENV(v), 1, patch_adsr_rate1(&g_patch.env[1]));
    engine_link_prod_write(PROD_MODENV(v), 2, mod_depth_for(s_voices[v].vel));
    engine_link_prod_write(PROD_MODENV(v), 3, patch_adsr_rate2(&g_patch.env[1]));
}
static void update_mod_env(void)
{
    for (int v=0; v<NUM_VOICES; v++) update_mod_env_voice(v);
}

// Every note-on gets a FRESH voice — allocation never matches on the
// note. Preference: least-recently-used IDLE voice; else the
// most-decayed RELEASING voice (earliest tail end — the least
// audible casualty); else steal the oldest HELD voice. Only the
// steal cases start their attack from a non-silent level (the gate
// is level-sensitive), and they only happen when all 32 voices are
// genuinely in use.
#ifdef S3_LEGACY_HOST_TEST
static bool note_on_owned(uint8_t channel, uint8_t note, uint8_t vel, uint32_t owner)
{
    if(s_dds_switch_id)return false; // old timbre is closing; never reopen its gates

    engine_fm_status_t fm;
    if(fm_active(&fm))return fm_voice_on(0,channel,owner,note,vel);
    engine_ks_state_t engine;engine_link_ks_state(&engine);
    if(engine.switching)return false;
    if(engine.requested_mode) {ks_voice_on(0,channel,owner,note,vel);return true;}
    int64_t now = esp_timer_get_time();
    int pick = -1;

    promote_idle(now);   // retire + hard-mute any finished tails

    uint32_t best = UINT32_MAX;
    for (int v = 0; v < NUM_VOICES; v++)
        if (s_voices[v].state == V_IDLE && s_voices[v].stamp < best)
            { best = s_voices[v].stamp; pick = v; }
    if (pick < 0) {
        int64_t soonest = INT64_MAX;
        for (int v = 0; v < NUM_VOICES; v++)
            if (s_voices[v].state == V_RELEASING &&
                s_voices[v].release_until < soonest)
                { soonest = s_voices[v].release_until; pick = v; }
    }
    if (pick < 0) {
        uint32_t oldest = UINT32_MAX;
        for (int v = 0; v < NUM_VOICES; v++)
            if (s_voices[v].stamp < oldest)
                { oldest = s_voices[v].stamp; pick = v; }
    }

    s_voices[pick] = (voice_t){.state = V_HELD, .note = note, .vel = vel,
                               .channel = channel, .web_owner = owner, .stamp = ++s_stamp};

    engine_note_t transaction = {
        .voice = pick, .amp_entry = PROD_ADSR(pick), .mod_entry = PROD_MODENV(pick),
        .amp_depth = amp_depth_for(vel), .mod_depth = mod_depth_for(vel),
        .cutoff = cut_bus_value(pick),
    };
    s_note_capture = &transaction;
    voice_program(pick, note, vel);
    s_note_capture = NULL;
    // A preset can be adopted inside the 4 ms config throttle. Put this
    // voice's current ADSR words in the desired bank before requesting On;
    // the existing engine transaction confirms that bank before opening it.
    update_amp_env_voice(pick);
    update_mod_env_voice(pick);
    if (!engine_link_note_on(&transaction)) {
        // Invalid/unavailable engine: retain no HELD bookkeeping that could
        // mask the next Off. Valid transactions use a lossless desired image.
        s_voices[pick].state = V_IDLE;
        hard_mute_voice(pick);
        ESP_LOGE(TAG, "note transaction unavailable, voice %d muted", pick);
        return false;
    }
    return true;
}
#endif

// Refresh the cutoff buses of active voices — the wheel and bend
// terms are shared, so both events land here. Since #44 this is ONE
// channel-bus write — the walker's type-3 fan-out entries distribute
// it to every voice (channel → voice → element).
static void refresh_cut_buses(void)
{
    engine_link_bus_write(BUS_CH_CUT, ch_cut_value());
}

// Boot wiring for the fan-out (#44): 32 stateless SEND sources,
// entry PROD_FANOUT(v) = BUS_CH_CUT × unity → BUS_CUT(v), each in the
// slot adjacent to its voice's MOD env (same target bus — #84 chain
// summing requires consecutive slots). Word 1 (RATES) is meaningless
// for a SEND.
static void init_fanout_sources(void)
{
    for (int v = 0; v < NUM_VOICES; v++) {
        engine_link_prod_write(PROD_FANOUT(v), 0,
            CSP_OPC_SEND | ((uint32_t)BUS_CUT(v) << 6) | ((uint32_t)BUS_CH_CUT << 16));
        engine_link_prod_write(PROD_FANOUT(v), 2, 0x10000u);   // unity
    }
}

// Master volume → every voice's gain-bus base (all 32, since the base
// persists and a not-yet-played voice must already carry it). Base =
// −ENV_SPAN + (g_patch.volume − VOL_REF)·64: at VOL_REF the offset is
// 0 and the base is the plain envelope floor. No swap, no rewrite —
// the amp-ADSR producer keeps adding the envelope on top.
static void refresh_gain_buses(void)
{
    int32_t off  = ((int32_t)g_patch.volume - VOL_REF) * 64;
    int32_t base = -(int32_t)ENV_SPAN + off;
    for (int v = 0; v < NUM_VOICES; v++)
        engine_link_bus_write(BUS_GAIN(v), (uint32_t)base & 0x3FFFF);
}

// CC 71 → global resonance bus (TEMPORARY assignment, see bus plan).
// One live bus write moves every element: effective resonance code
// = RESO + (cc<<7 − RESO) = cc << 7 — 0 = Butterworth, 127 ≈ 15.9
// octaves of Q = self-oscillation. Conventional knob: up = more.
static void reso_update(uint8_t val)
{
    // Tempered scale (#43, Thor 2026-09-17): the cc<<7 mapping described
    // above ran Q up to ~15.9 octaves (Q~42000) at the top -- a
    // hair-trigger into the undamped/static zone. Rescale so CC 127 tops
    // out at r = 5.5 octaves = Q~32 (a sharp but stable peak), still
    // linear in log2(Q) so each step is an equal Q ratio (~3%/step).
    int32_t code   = ((int32_t)val * 5632) / 127;   // 5632 = r 5.5 oct -> Q 32
    int32_t offset = code - (int32_t)g_patch.filter.resonance;
    s_reso_off = offset;   // remembered so LFO 2 dest changes can restore
    engine_link_bus_write(BUS_RESO_GLOBAL, (uint32_t)offset);
}

// Mod wheel → cutoff term (0 to ~+5 octaves, wheel*40 Q8.10 LSB).
static void wheel_update(uint8_t val)
{
    if (val == s_wheel)
        return;
    s_wheel = val;
    s_dirty |= D_CUT;   // coalesced (was refresh_cut_buses per CC)
}

// Pitch wheel: ±bend_range semitones (RPN 0, #74; default ±2). Q8.10
// has 1024/12 ≈ 85.3 LSB per semitone: off = delta × range × 85.33 /
// 8192 = delta × range / 96. ONE write to the global pitch bus moves
// every element; the cutoff buses get the same term so filter key
// tracking follows the bend (Thor).
#ifdef S3_LEGACY_HOST_TEST
static void bend_update(uint16_t bend14)
{
    int16_t off = (int16_t)(((int32_t)bend14 - 8192)
                            * g_patch.bend_range / 96);
    if (off == s_bend)
        return;
    s_bend = off;
    engine_link_bus_write(BUS_PITCH_GLOBAL, (uint32_t)(int32_t)off);  // 1 write
    s_dirty |= D_CUT;   // cut-bus refresh coalesced
}

// Note-off releases the OLDEST HELD voice carrying that note — FIFO
// pairing with note-ons, since MIDI guarantees one off per on. One
// gate-bus write: the ADSR sees the level drop and releases. Element
// GATE words stay on; parameters stay live. A voice whose key was
// stolen carries a different note by now and is correctly skipped.
static void note_off_owned(uint8_t note, uint32_t owner)
{
    engine_fm_status_t fm;
    if(fm_active(&fm)) {fm_voice_off(0,0,owner,note);return;}
    engine_ks_state_t engine;engine_link_ks_state(&engine);
    if(engine.requested_mode) {ks_voice_off(0,0,owner,note);return;}
    int pick = -1;
    uint32_t oldest = UINT32_MAX;
    for (int v = 0; v < NUM_VOICES; v++)
        if (s_voices[v].state == V_HELD && s_voices[v].note == note && s_voices[v].web_owner == owner &&
            s_voices[v].stamp < oldest)
            { oldest = s_voices[v].stamp; pick = v; }
    if (pick < 0)
        return;   // off without a matching held on (steal ate it)
    begin_release(pick);
}

static void note_on(uint8_t channel, uint8_t note, uint8_t vel)
{ note_on_owned(channel, note, vel, 0); }
static void note_off(uint8_t note)
{ note_off_owned(note, 0); }
#endif

// Program the active patch over MIDI CCs (#70, docs/midi_schema.md).
// The whole basic-patch surface without SysEx. Each CC mutates
// g_patch and renders: bus params update a bus live, producer params
// update a source, element-word params re-render sounding voices.
// s_cut_off carries the CC74/106 cutoff brightness; env inversions
// follow the schema ((127-cc)<<1, panel convention).
//
// STORED-but-not-yet-rendered (their own issues): MOD env (#42),
// osc 2 / unison (#72), 2nd LFO + LFO shape/dest (#71), key
// tracking, arp (#76), pan. A controller may set them; they render
// when those rungs land.
static uint8_t  s_cut_coarse = 64, s_cut_fine;   // CC74 / CC106
static void apply_cutoff(void)
{
    // 14-bit brightness centred at coarse 64: (val-8192) scaled so
    // coarse spans a few octaves, fine interpolates.
    int32_t v14 = ((int32_t)s_cut_coarse << 7) | s_cut_fine;   // 0..16383
    s_cut_off = v14 - 8192;          // ±8 octaves — authority rule (#88):
                                     // full deflection must reach the
                                     // rails; Thor 2026-09-10 could not
                                     // dial cutoff to 0 at the old ±4
                                     // (was >>2/±2, then >>1/±4)
    s_dirty |= D_CUT;   // coalesced
}

// RPN state (#74): CC 101/100 select an RPN, CC 6 (data entry MSB)
// writes it. RPN 0/0 = pitch-bend range, the standard mechanism.
// 127/127 is RPN null; an NRPN select (99/98) also deselects.
static uint8_t s_rpn_msb = 127, s_rpn_lsb = 127;

static bool s_test_tone_requested;
static void all_sound_off(void);
#ifndef S3_LEGACY_HOST_TEST
static bool s_feedback_dirty;
static uint8_t s_feedback_except_source = MIDI_SOURCE_DEFAULT;

static void feedback_mark(uint8_t source)
{
    s_feedback_dirty = true;
    s_feedback_except_source = source;
}
#endif
static void handle_cc(uint8_t num, uint8_t val)
{
    engine_ks_state_t engine;engine_link_ks_state(&engine);
    if(engine.requested_mode) {
        if(num==7) {g_patch.volume=(uint8_t)(val<127 ? val<<1 : 0xFE);s_dirty|=D_GAIN;
#ifdef S3_LEGACY_HOST_TEST
            ks_voice_expression();
#endif
        }
        else if(num==10) {g_patch.pan=(int8_t)((int)val-64);
#ifdef S3_LEGACY_HOST_TEST
            ks_voice_expression();
#endif
        }
#ifdef S3_LEGACY_HOST_TEST
        else if(num==123)ks_voice_all_notes_off();
#endif
        else if(num==120)all_sound_off();
        else if(num==119 && !val) {s_test_tone_requested=false;engine_link_bus_write(1023,0);}
        return;
    }
#ifndef S3_LEGACY_HOST_TEST
    parameter_cc_state_t controls = parameter_cc_state_from_patch(
        &g_patch, s_cut_off, s_reso_off);
    uint32_t effect = parameter_cc_apply(&g_patch, &controls, num, val);
    if (effect) {
        s_cut_coarse = controls.cutoff_coarse;
        s_cut_fine = controls.cutoff_fine;
        s_cut_off = controls.cutoff_offset;
        s_reso_off = controls.resonance_offset;
        s_dirty |= effect & (D_RENDER | D_ENV | D_CUT | D_LFO |
                             D_GAIN | D_ENV2 | D_LFO2);
        if (effect & PARAM_CC_RESONANCE_WRITE)
            engine_link_bus_write(BUS_RESO_GLOBAL, (uint32_t)s_reso_off);
        return;
    }
#endif
    switch (num) {
    // ---- live: buses ----
    case 1:  wheel_update(val); break;               // mod wheel → cutoff
    case 71: reso_update(val); break;                // resonance (temp bus 3)
    case 74: s_cut_coarse = val; apply_cutoff(); break;
    case 106:s_cut_fine   = val; apply_cutoff(); break;

    // ---- RPN 0: pitch-bend range (#74) ----
    case 101: s_rpn_msb = val; break;
    case 100: s_rpn_lsb = val; break;
    case 99: case 98: s_rpn_msb = s_rpn_lsb = 127; break;  // NRPN deselects
    case 6:
        if (s_rpn_msb == 0 && s_rpn_lsb == 0) {
            uint8_t semis = val < 1 ? 1 : (val > 12 ? 12 : val);
            g_patch.bend_range = semis;   // takes effect on the next bend
        }
        break;
    case 38: break;                       // data entry LSB: cents, ignored

    // ---- live: amp envelope (producers) ----
    // A/D/R are log2 RATES (higher byte = faster), so invert →
    // knob up = longer. Sustain is a LEVEL (higher byte = louder),
    // so it is NOT inverted → knob up = louder (Thor: 79 was upside
    // down when uniformly inverted).
    case 73: g_patch.env[0].attack  = (uint8_t)((127 - val) << 1); s_dirty |= D_ENV; break;
    case 75: g_patch.env[0].decay   = (uint8_t)((127 - val) << 1); s_dirty |= D_ENV; break;
    case 79: g_patch.env[0].sustain = (uint8_t)(val << 1);         s_dirty |= D_ENV; break;
    case 72: g_patch.env[0].release = (uint8_t)((127 - val) << 1); s_dirty |= D_ENV; break;

    // ---- live: LFO 1 (source 0) ----
    case 76: g_patch.lfo[0].rate  = lfo_rate_from_cc(val); s_dirty |= D_LFO; break;
    case 77: g_patch.lfo[0].depth = (int16_t)(val << 2);  s_dirty |= D_LFO; break;
    case 113: g_patch.lfo[0].shape = (uint8_t)(val >> 5); s_dirty |= D_LFO; break;

    // ---- live: LFO 2 (source 1, #73) ----
    case 109: g_patch.lfo[1].rate = lfo_rate_from_cc(val); s_dirty |= D_LFO2; break;
    // Depth scale is per-destination: duty bus decodes <<<13 (1024 LSB
    // = full ±1.0 duty), resonance as-is (1024 = 1 octave of Q),
    // cutoff like resonance (val<<5 ≈ up to ±4 octaves of wobble).
    case 110: g_patch.lfo[1].depth =
                  (int16_t)(g_patch.lfo[1].dest == 1 ? val << 5
                          : g_patch.lfo[1].dest == 2 ? val << 2   // pitch: like CC 77
                          : g_patch.lfo[1].dest == 3 ? val << 5   // cutoff
                                                     : val << 4);
              s_dirty |= D_LFO2; break;
    case 111: g_patch.lfo[1].shape = (uint8_t)(val >> 5); s_dirty |= D_LFO2; break;
    // Four destinations (#92 first route — the send fan-out makes
    // cutoff reachable): 0..31 duty/PWM, 32..63 resonance,
    // 64..95 pitch (sums with LFO 1, #84), 96..127 CUTOFF (channel
    // cut bus → the 32 per-voice sends → every voice's filter).
    case 112: g_patch.lfo[1].dest  = (uint8_t)((val * 4) >> 7); s_dirty |= D_LFO2; break;

    // ---- live: master volume → gain-bus base (not a re-render) ----
    case 7:  g_patch.volume = (uint8_t)(val < 127 ? val << 1 : 0xFE);
             s_dirty |= D_GAIN; break;
    case 10: g_patch.pan = (int8_t)((int)val - 64);     // pan (#91)
             s_dirty |= D_RENDER; break;
    case 31: g_patch.filter.key_track = (int16_t)val;   // key track (#91)
             s_dirty |= D_RENDER; break;

    // ---- live: element-word params (re-render sounding voices) ----
    case 20: g_patch.osc[0].wave = (waveform_t)(val >> 5);   // 0..3
             s_dirty |= D_RENDER; break;
    case 21: g_patch.osc[1].wave = (waveform_t)(val >> 5);   // osc2 wave
             s_dirty |= D_RENDER; break;
    // Coarse: ±12 semitones in WHOLE semitone steps, ~5 CC steps per
    // semitone, center = 64 (val-64 was ±63 — far too sensitive for
    // hand-tuning an interval; Thor 2026-09-07). Same mapping both
    // oscillators (osc1 pitch/fine added Thor 2026-09-10: CC 14/15).
    case 14: g_patch.osc[0].coarse = (int16_t)((val * 25) / 128 - 12);
             s_dirty |= D_RENDER; break;
    case 22: g_patch.osc[1].coarse = (int16_t)((val * 25) / 128 - 12);
             s_dirty |= D_RENDER; break;
    // Fine: full travel = ±0.5 semitone (Thor 2026-09-10; was ±1.5).
    // 0.5 semi = 1024/24 ≈ 42.7 LSB → (val−64)*2/3 spans ±42.
    case 15: g_patch.osc[0].fine = (int16_t)((((int)val - 64) * 2) / 3);
             s_dirty |= D_RENDER; break;
    case 23: g_patch.osc[1].fine = (int16_t)((((int)val - 64) * 2) / 3);
             s_dirty |= D_RENDER; break;
    case 24: g_patch.osc_mix = (int8_t)((int)val - 64);     // osc balance
             s_dirty |= D_RENDER; break;
    // Pulse width (#94 F2, Thor 2026-09-10): UNIPOLAR — the two halves
    // of the old bipolar range sound identical (duty d and 1−d are the
    // same spectrum, inverted), and the rails reached silent 0%/100%.
    // Now 0 = square (50%), 127 = 5% pulse, EQUAL-RATIO duty steps
    // (log taper: duty = 0.5·10^(−val/127)) — fine resolution at the
    // thin end where the effect is dramatic, never degenerate.
    case 25: g_patch.osc[0].duty = duty_from_cc(val);
             s_dirty |= D_RENDER; break;
    case 85: g_patch.osc[1].duty = duty_from_cc(val);   // osc2 PW (#91)
             s_dirty |= D_RENDER; break;
    // Velocity sensitivity (#89): read at note_on — new notes pick the
    // change up; held notes keep their velocity terms until re-struck.
    // #89 round 2: amounts, not sensitivities. A live edit has to re-push
    // both envelopes' DEPTH words or the change is inaudible until the next
    // note-on -- which is exactly how a knob feels broken.
    case 86: g_patch.vel_amp_amt = val; s_dirty |= D_ENV;  break;
    case 87: g_patch.vel_mod_amt = val; s_dirty |= D_ENV2; break;
    case 26: { uint8_t m = (uint8_t)((val * 3) >> 7);       // 3 voice modes
               g_patch.voice_struct = (voice_struct_t)(m > 2 ? 2 : m);
               s_dirty |= D_RENDER; } break;
    case 27: g_patch.unison_detune = (int16_t)(val >> 2);   // 0..31 LSB/step
             s_dirty |= D_RENDER; break;
    case 28: g_patch.unison_stereo = (int16_t)val;          // stereo spread
             s_dirty |= D_RENDER; break;
    // Three filter types only (RTL: 0=LP, 1=BP, 2=HP; anything else
    // falls into the LP default). Map the CC across exactly those
    // three so the top of travel is HP, not a second LP.
    case 29: g_patch.filter.type = (uint8_t)((val * 3) >> 7);  // 0..2
             s_dirty |= D_RENDER; break;
    case 30: g_patch.filter.dual = val >= 64;
             s_dirty |= D_RENDER; break;

    // ---- test tone (#81): ≥64 replaces BOTH outputs with the
    // gateware's full-scale 187.5 Hz sine (bus-1023 control latch) —
    // the audio-chain purity reference, remotely switchable so the
    // test suite needs no console. ----
    case 119:
        s_test_tone_requested = val >= 64;
        engine_link_bus_write(1023, val >= 64 ? 1u : 0u);
        break;

    // ---- panic (found via the BLE fuzzer's stuck notes: its final
    // CC 123 was a no-op, so note-offs dropped under flood backpressure
    // left voices ringing forever) ----
    case 123:                          // all notes off: release held voices
        for (int v = 0; v < NUM_VOICES; v++)
            if (s_voices[v].state == V_HELD) {
                begin_release(v);
            }
        break;
    case 120:                          // all sound off: immediate silence
        // Clear the diagnostic latch and queued web keys as well as voices,
        // so the next owner batch cannot revive a partially advanced chord.
        all_sound_off();
        break;

    // ---- MOD env (#42): live on the cutoff buses ----
    case 102: g_patch.env[1].attack  = (uint8_t)((127 - val) << 1); s_dirty |= D_ENV2; break;
    case 103: g_patch.env[1].decay   = (uint8_t)((127 - val) << 1); s_dirty |= D_ENV2; break;
    case 104: g_patch.env[1].sustain = (uint8_t)(val << 1);         s_dirty |= D_ENV2; break;
    case 105: g_patch.env[1].release = (uint8_t)((127 - val) << 1); s_dirty |= D_ENV2; break;
    // Depth: BIPOLAR, centre 64 = off. Square-law taper (Thor
    // 2026-09-10: linear over ±16 oct was 3 semitones per click —
    // overly sensitive): d·|d|·4 gives ~±1 oct at a quarter turn,
    // ±4 oct at half, ±16 oct at the rails — fine near centre, full
    // authority (#88) at the ends.
    case 107: {
        int d = (int)val - 64;
        g_patch.env1_depth = (int16_t)((d * (d < 0 ? -d : d)) << 2);
        s_dirty |= D_ENV2;
    } break;
    case 108: g_patch.env1_dest = (uint8_t)(val >> 5);  // stored; cutoff live
              break;

    default: break;   // unmapped / deferred CCs ignored
    }
}

static void handle_midi_source(const midi_message_t *m, uint8_t source)
{
#ifndef S3_LEGACY_HOST_TEST
    // Only local panel/slider parameters remain on EVT_MIDI. USB/Web/BLE
    // performance uses FPGA raw MIDI; DIN and DDS Program Change return
    // traffic are explicitly outside this product mode's supported inputs.
    engine_flute_state_t flute;engine_link_flute_state(&flute);
    if(flute.requested_mode || flute.applied_mode || flute.switching)return;
    if((source==MIDI_SOURCE_DEFAULT || source==MIDI_SOURCE_PANEL) &&
       m && (m->status&0xf0)==0xb0 && m->len>=2 && m->data[1]<=127 &&
       web_control_cc_allowed(m->data[0])) {
        handle_cc(m->data[0],m->data[1]);
        feedback_mark(source);
    }
#else
    uint8_t type = m->status & 0xF0;
    uint8_t ch   = m->status & 0x0F;
    if(type==0xB0 && (m->data[0]==0 || m->data[0]==32)) {
        dds_bank_select(s_dds_banks,ch,m->data[0],m->data[1]);return;
    }
    engine_fm_status_t fm;
    if(fm_active(&fm)) {
        if(type==0x90 && m->data[1])fm_voice_on(source,ch,0,m->data[0],m->data[1]);
        else if(type==0x80 || (type==0x90 && !m->data[1]))fm_voice_off(source,ch,0,m->data[0]);
        else if(type==0xB0 && m->data[0]==120)all_sound_off();
        else if(type==0xB0)fm_voice_cc(source,ch,m->data[0],m->data[1]);
        else if(type==0xE0)fm_voice_bend(source,ch,((uint16_t)m->data[1]<<7)|m->data[0]);
        else if(type==0xC0 && fm.available && fm.valid && !fm.needs_select && fm_preset(fm.profile_id,m->data[0])) {
            all_sound_off();engine_link_fm_preset(m->data[0]);
        }
        return;
    }
    engine_ks_state_t engine;engine_link_ks_state(&engine);
    if(engine.requested_mode) {
        if(type==0x90 && !engine.switching)ks_voice_on(source,ch,0,m->data[0],m->data[1]);
        else if(type==0x80)ks_voice_off(source,ch,0,m->data[0]);
        else if(type==0xB0 && m->data[0]==64)ks_voice_sustain(source,ch,m->data[1]>=64);
        else if(type==0xB0)handle_cc(m->data[0],m->data[1]);
        return;
    }
    if(engine.switching && type==0x90)return;

    switch (type) {
    case 0x90:                        // parser normalises vel 0 → note off
        note_on(ch, m->data[0], m->data[1]);
        break;
    case 0x80:
        note_off(m->data[0]);
        break;
    case 0xB0:
        handle_cc(m->data[0], m->data[1]);
        break;
    case 0xC0: { // one Omni DDS patch; bank state itself is per channel
        char error[DDS_ERROR_MAX];uint32_t id;
        if(!dds_presets_request(DDS_PROGRAM,"",NULL,0,(unsigned)s_dds_banks[ch]*128+m->data[0],&id,error))
            ESP_LOGW(TAG,"DDS Program Change: %s",error);
    } break;
    case 0xE0:                        // pitch wheel, 14-bit
        bend_update((uint16_t)(((uint16_t)m->data[1] << 7) | m->data[0]));
        break;
    default:
        break;
    }
#endif
}

// Browser bookkeeping stays in the same owner loop as MIDI and patch edits.
static struct {
    uint32_t token, sequence, parameter_sequence, stop_sequence, keys, desired_keys;
    uint32_t stop_ticket, stop_wait_sequence;
    uint8_t velocity, midi_channel, key_channel[WEB_KEYS];
    int64_t expires;
    bool armed;
} s_web[WEB_CLIENTS];
static uint32_t s_web_revision, s_panic_revision;
static int64_t s_web_last_publish;
static uint32_t s_control_epoch = 1;
static uint32_t s_v2_result_sequence[WEB_CLIENTS], s_v2_result_code[WEB_CLIENTS];
static uint32_t s_v2_result_context[WEB_CLIENTS];
static void web_release(unsigned client)
{
    uint32_t owner = s_web[client].token;
#ifdef S3_LEGACY_HOST_TEST
    ks_voice_release_owner(owner);
    fm_voice_release_owner(owner);
    for (unsigned v = 0; v < NUM_VOICES; v++)
        if (s_voices[v].state == V_HELD && s_voices[v].web_owner == owner && owner)
            begin_release(v);
#else
    raw_midi_state_t raw;raw_midi_state(&raw);
    if(owner && raw.source[MIDI_RAW_SOURCE_WEB].session==owner)
        raw_midi_flush(MIDI_RAW_SOURCE_WEB);
#endif
    s_web[client].keys = s_web[client].desired_keys = 0;
}
#ifndef S3_LEGACY_HOST_TEST
static midi_template_image_t s_template_image;
static midi_patch_snapshot_t s_template_snapshot;
static bool s_template_full_sub;
static unsigned s_template_decay;
static bool s_template_valid,s_template_compiled;
static uint32_t s_template_reset_count;
static int64_t s_template_last_publish;
static uint32_t s_template_fault,s_template_fault_generation;

static bool owner_route_blocked(void);
static bool owner_channel_routes(void)
{
    engine_cpu_mode_state_t mode;engine_link_cpu_mode_state(&mode);
    return CONFIG_FPGA_AE350_CONTROL && mode.supported && mode.valid && !mode.fault && mode.channel_routes;
}
static bool owner_parallel_sources(void)
{
    engine_audio_routes_state_t routes;engine_link_audio_routes_state(&routes);
    return CONFIG_FPGA_AE350_CONTROL && routes.available && routes.valid;
}
#ifndef S3_LEGACY_HOST_TEST
static bool owner_local_sync(void);
static bool owner_local_frozen(void);
#endif
#include "owner_ks_timbre.inc.h"
static bool owner_raw_ready_snapshot(const raw_midi_state_t *raw)
{
    if(CONFIG_FPGA_AE350_CONTROL) {
        midi_template_link_state_t image;midi_template_link_state(&image);
        return raw->cpu_control && raw->available && raw->initialized && raw->input_capabilities_known &&
            raw->enabled && raw->ready && !raw->stop_pending && !s_dds_switch_id && !owner_route_blocked() &&
            (image.ready || image.applied_generation) && !image.fault &&
            raw->fault!=RAW_MIDI_IO && raw->fault!=RAW_MIDI_RESET && raw->fault!=RAW_MIDI_UNAVAILABLE;
    }
#ifndef S3_LEGACY_HOST_TEST
    engine_local_state_t local;engine_link_local_state(&local);
    if(local.supported && (local.local_active || local.blocked || local.remote_pending))
        return local.local_active && local.adopted && !local.blocked &&
            raw->available && raw->initialized && raw->input_capabilities_known &&
            raw->enabled && raw->ready && !raw->stop_pending;
#endif
    if(owner_kst_blocked())return false;
    midi_template_link_state_t image;midi_template_link_state(&image);
    engine_ks_state_t ks;engine_link_ks_state(&ks);
    engine_fm_status_t fm;engine_link_fm_status(&fm);
    // A confirmed previous template remains usable during an ordinary hot
    // update. Initial upload has no applied generation; faults revoke both.
    return !owner_route_blocked() &&
        (image.ready || image.applied_generation) && !image.fault &&
        raw->available && raw->initialized && raw->input_capabilities_known &&
        raw->enabled && raw->ready && !raw->stop_pending &&
        raw->fault!=RAW_MIDI_IO && raw->fault!=RAW_MIDI_RESET && raw->fault!=RAW_MIDI_UNAVAILABLE && !s_dds_switch_id &&
        !ks.switching && !ks.recovering && ks.requested_mode==ks.applied_mode &&
        (!ks.requested_mode || ks.valid) &&
        !fm.switching && !fm.recovering && !fm.needs_select &&
        (!engine_fm_control_active(&fm) || engine_fm_playable(&fm)) &&
        (!CONFIG_FPGA_FM_ONLY || engine_fm_playable(&fm));
}
static bool owner_raw_ready(void)
{
    raw_midi_state_t raw;raw_midi_state(&raw);
    return owner_raw_ready_snapshot(&raw);
}

static void owner_templates_update(void)
{
#ifndef S3_LEGACY_HOST_TEST
    owner_local_sync();
    owner_kst_update();
    if(owner_local_frozen() || owner_kst_unknown())return;
#endif
    if(owner_route_blocked()){engine_link_performance_ready(false);return;}
    if(s_fm_only_boot_select_pending) {
        engine_fm_status_t fm;engine_link_fm_status(&fm);
        // The validation image boots into FM after real capability discovery.
        // A fault/reset still requires the existing explicit SELECT recovery.
        if(fm.needs_select || fm.recovering)s_fm_only_boot_select_pending=false;
        else if(engine_fm_selectable(&fm) && engine_link_fm_mode(fm.backend_id))
            s_fm_only_boot_select_pending=false;
    }
    // Preset adoption owns this gate-close interval. Do not republish the old
    // patch or grant USB/BLE permission while its replacement is still loading.
    if(s_dds_switch_id){engine_link_performance_ready(false);return;}
    raw_midi_state_t raw;raw_midi_state(&raw);
    midi_template_link_state_t link;midi_template_link_state(&link);
    unsigned decay=ks_voice_decay_ms();
    bool recovery=raw.reset_count!=s_template_reset_count ||
        (link.fault && (link.fault!=s_template_fault || link.generation!=s_template_fault_generation));
    s_template_fault=link.fault;s_template_fault_generation=link.generation;
    if(recovery)all_sound_off();
    if(CONFIG_FPGA_AE350_CONTROL && !link.sub_patch_valid) {engine_link_performance_ready(false);return;}
    const midi_patch_snapshot_t snapshot={.patch=g_patch,.cutoff_offset=s_cut_off,.resonance_offset=s_reso_off};
    bool full_sub=CONFIG_FPGA_AE350_CONTROL && link.sub_patch_valid && link.sub_patch_supported;
    if(s_template_valid && !recovery && s_template_kst_generation==s_kst_model_generation && decay==s_template_decay &&
       s_template_full_sub==full_sub && snapshot.cutoff_offset==s_template_snapshot.cutoff_offset &&
       snapshot.resonance_offset==s_template_snapshot.resonance_offset &&
       !memcmp(&snapshot.patch,&s_template_snapshot.patch,sizeof(snapshot.patch))) {
        // A route fence can close permission without changing this cache.
        // Compiler rejection is cached too; it must never grant permission.
        if(s_template_compiled)engine_link_performance_ready(true);
        return;
    }
    int64_t now=esp_timer_get_time();
    if(s_template_valid && !recovery && now-s_template_last_publish<20000) {
        if(s_template_compiled)engine_link_performance_ready(true);
        return;
    }
    // Cache even rejected input: an unchanged invalid patch cannot cause a
    // full 128-note compile on every owner tick. A new input/recovery retries.
    s_template_snapshot=snapshot;s_template_full_sub=full_sub;s_template_decay=decay;s_template_kst_generation=s_kst_model_generation;
    s_template_valid=true;s_template_reset_count=raw.reset_count;
    s_template_last_publish=now;
    s_template_compiled=midi_templates_compile_snapshot(&snapshot,decay,full_sub,&s_template_image);
    if(s_template_compiled)s_template_compiled=owner_kst_compile(&s_template_image,decay);
    if(!s_template_compiled) {
        engine_link_performance_ready(false);return;
    }
    midi_template_link_publish(&s_template_image);
    owner_kst_published();
    // Permission only: the SPI owner still requires capability, static
    // control completion and template ACK before actually enabling MIDI.
    engine_link_performance_ready(true);
}

static void web_cancel(unsigned client)
{
    web_release(client);s_web[client].armed=false;
}

static uint32_t web_snapshot_raw(unsigned client,uint32_t target,uint8_t velocity,
                                 int64_t expires,int64_t now)
{
    if(now>=expires) {web_cancel(client);return FM_RESULT_CANCELLED;}
    raw_midi_state_t raw;raw_midi_state(&raw);
    uint64_t claim=raw.source[MIDI_RAW_SOURCE_WEB].session;
    if((s_web[client].keys || claim==s_web[client].token) &&
       s_web[client].expires && now>=s_web[client].expires) {
        web_cancel(client);
        if(target)return FM_RESULT_CANCELLED;
        claim=0;
    }
    s_web[client].expires=expires;
    if(target && raw.stop_pending){web_cancel(client);return FM_RESULT_BUSY;}
    if(!target)s_web[client].armed=true;
    if(!s_web[client].armed)return FM_RESULT_CANCELLED;
    // An idle empty heartbeat never claims the one Web source. An existing
    // same-token raw stream remains bound, preserving bend/Running Status.
    if(!target && !s_web[client].keys && claim!=s_web[client].token) {
        s_web[client].desired_keys=0;return FM_RESULT_OK;
    }
    if(!owner_raw_ready_snapshot(&raw) || !raw_midi_claim(MIDI_RAW_SOURCE_WEB,s_web[client].token)) {
        web_cancel(client);return FM_RESULT_BUSY;
    }
    const uint8_t transmit_channel=owner_channel_routes()?s_web[client].midi_channel:0;
    uint8_t bytes[WEB_KEYS*3];size_t length=0;
    uint32_t changed=s_web[client].keys^target;
    // Offs precede Ons within this event; no later raw event can overtake it.
    for(unsigned pass=0;pass<2;pass++)for(unsigned key=0;key<WEB_KEYS;key++) {
        uint32_t bit=1u<<key;
        bool on=(target&bit)!=0;
        if(!(changed&bit) || on!=(pass!=0))continue;
        bytes[length++]=(uint8_t)((on?0x90:0x80) |
            (on?transmit_channel:s_web[client].key_channel[key]));
        bytes[length++]=(uint8_t)(WEB_FIRST_NOTE+key);
        bytes[length++]=on?velocity:0;
    }
    bool ok=length?raw_midi_submit(MIDI_RAW_SOURCE_WEB,s_web[client].token,bytes,length,expires):
        raw_midi_renew(MIDI_RAW_SOURCE_WEB,s_web[client].token,expires);
    if(!ok){web_cancel(client);return FM_RESULT_BUSY;}
    for(unsigned key=0;key<WEB_KEYS;key++)
        if((changed&target)&(1u<<key))s_web[client].key_channel[key]=transmit_channel;
    s_web[client].keys=s_web[client].desired_keys=target;
    s_web[client].velocity=velocity;
    return FM_RESULT_OK;
}

static void handle_web_raw(const web_v2_event_t *event,int64_t now)
{
    web_raw_job_t job;
    if(!web_raw_queue_take(event->a,&job))return;
    unsigned client=event->client;
    if(client>=WEB_CLIENTS || job.client!=client ||
       job.request.token!=event->token || job.request.sequence!=event->sequence ||
       job.request.context_epoch!=event->context_epoch || job.expires!=event->expires ||
       s_web[client].token!=event->token)return;
    if(event->sequence<=s_web[client].sequence)return;
    uint32_t result=FM_RESULT_OK;
    if(event->context_epoch!=s_control_epoch)result=FM_RESULT_STALE_EPOCH;
    else if(now>=event->expires || !s_web[client].armed)result=FM_RESULT_CANCELLED;
    else {
        raw_midi_state_t raw;raw_midi_state(&raw);
        if(s_web[client].expires && now>=s_web[client].expires &&
           (s_web[client].keys || raw.source[MIDI_RAW_SOURCE_WEB].session==event->token))
            result=FM_RESULT_CANCELLED;
    }
    if(result==FM_RESULT_OK && (!owner_raw_ready() || !raw_midi_claim(MIDI_RAW_SOURCE_WEB,event->token)))result=FM_RESULT_BUSY;
    if(result==FM_RESULT_OK && !raw_midi_submit(MIDI_RAW_SOURCE_WEB,event->token,job.request.bytes,
                                             job.request.length,event->expires))result=FM_RESULT_BUSY;
    if(result!=FM_RESULT_OK)web_cancel(client);
    else s_web[client].expires=event->expires;
    // Raw HTTP acknowledges queueing only. Share the ordering fence without
    // overwriting the existing v2 snapshot's adopted/result acknowledgement.
    s_web[client].sequence=event->sequence;
}
#endif
#ifndef S3_LEGACY_HOST_TEST
static bool owner_route_blocked(void)
{
    if(CONFIG_FPGA_AE350_CONTROL) {
        engine_cpu_mode_state_t mode;engine_link_cpu_mode_state(&mode);
        bool parallel=(owner_parallel_sources() || mode.channel_routes) && mode.applied<=2;
        return mode.supported && (!mode.valid || mode.fault ||
            (!parallel && (!mode.ready || mode.busy || mode.pending)));
    }
    if(CONFIG_FPGA_FM_ONLY)return false;
    engine_local_state_t local;engine_link_local_state(&local);
    if(local.supported && (local.local_active || local.blocked || local.remote_pending))return local.blocked;
    engine_ks_state_t ks;engine_fm_status_t fm;
    engine_link_ks_state(&ks);engine_link_fm_status(&fm);
    engine_flute_state_t flute;engine_link_flute_state(&flute);
    if(flute.switching || ((flute.requested_mode || flute.applied_mode) &&
       (flute.fault || !flute.valid || !flute.ready || flute.requested_mode!=flute.applied_mode)))return true;
    uint32_t actual=flute.applied_mode?ENGINE_MODE_FLUTE:fm.applied_backend?FM_ENGINE_FM:ks.applied_mode?FM_ENGINE_KS:FM_ENGINE_DDS;
    return s_requested_engine!=UINT32_MAX && s_requested_engine!=actual;
}
#endif
#include "owner_reverb.inc.h"
#include "owner_fdc.inc.h"
static void all_sound_off(void)
{
    // Clear ESP32 control state for recovery/reconfiguration. On independent
    // input hardware this never requests USB FLUSH; explicit global web_stop
    // adds that separate, acknowledged scope below.
#ifndef S3_LEGACY_HOST_TEST
    owner_reverb_cancel();
    owner_fdc_cancel();
    owner_kst_cancel();
#endif
    if (++s_control_epoch == 0) s_control_epoch = 1;
    dds_presets_owner_context(s_control_epoch);
    engine_fm_status_t fm;
    if(fm_active(&fm) && !fm.needs_select && !fm.recovering) {
        engine_ks_state_t ks;engine_link_ks_state(&ks);
        s_requested_engine=CONFIG_FPGA_FM_ONLY?FM_ENGINE_FM:
            fm.applied_backend?FM_ENGINE_FM:ks.applied_mode?FM_ENGINE_KS:FM_ENGINE_DDS;
    }
#ifdef S3_LEGACY_HOST_TEST
    ks_voice_panic();fm_voice_panic();
#else
    engine_link_performance_ready(false);
    raw_midi_flush(MIDI_RAW_SOURCE_ALL);
    web_raw_queue_reset();s_template_valid=false;
#endif
    // A panic must also disable the full-scale diagnostic output latch.
    handle_cc(119, 0);
    // Hard-mute DDS even while KS is selected. Its RC tails must not
    // reappear when the FPGA switches back to the DDS PCM source.
#ifdef S3_LEGACY_HOST_TEST
    for(unsigned v=0;v<NUM_VOICES;v++) {
        s_voices[v].state=V_IDLE;engine_link_bus_write(BUS_VGATE(v),0);hard_mute_voice(v);
    }
#endif
    for (unsigned i = 0; i < WEB_CLIENTS; i++) {
        s_web[i].keys = s_web[i].desired_keys = 0;
        s_web[i].armed = false; // require a fresh empty-key packet before On
    }
    s_panic_revision++;
    engine_link_owner_panic_ack();
    engine_link_fm_owner_ack();
}
#include "owner_local.inc.h"
static void web_stop(unsigned client, uint32_t sequence)
{
#ifndef S3_LEGACY_HOST_TEST
    owner_remote_cancel();
    owner_fdc_stop();
#endif
    all_sound_off();
#ifdef S3_LEGACY_HOST_TEST
    s_web[client].stop_sequence=sequence;
#else
    // Explicit user STOP is the only web operation that clears USB as well
    // as ESP32 sources. Automatic recovery continues to use all_sound_off().
    s_web[client].stop_ticket=raw_midi_request_stop_all();
    s_web[client].stop_wait_sequence=sequence;
#endif
}
#ifndef S3_LEGACY_HOST_TEST
static void web_confirm_stops(const raw_midi_state_t *raw)
{
    for(unsigned i=0;i<WEB_CLIENTS;i++) {
        uint32_t ticket=s_web[i].stop_ticket;
        if(ticket && raw->stop_completed && (int32_t)(raw->stop_completed-ticket)>=0) {
            s_web[i].stop_sequence=s_web[i].stop_wait_sequence;
            s_web[i].stop_ticket=s_web[i].stop_wait_sequence=0;
        }
    }
}
#endif
static void handle_web_core(const web_event_t *e, int64_t now,bool v2)
{
#ifndef S3_LEGACY_HOST_TEST
    owner_local_sync();
#endif
    if (e->client >= WEB_CLIENTS) return;
    unsigned i = e->client;
    if (e->op == WEB_START) {
        web_release(i);
        s_web[i].token = e->token;
        s_web[i].sequence = s_web[i].parameter_sequence = s_web[i].stop_sequence = 0;
        s_web[i].stop_ticket=s_web[i].stop_wait_sequence=0;
        s_web[i].expires=0;s_web[i].velocity=0;
        s_web[i].midi_channel=1; /* Web defaults to KS, human MIDI channel 2. */
        memset(s_web[i].key_channel,0,sizeof(s_web[i].key_channel));
        s_v2_result_sequence[i] = s_v2_result_code[i] = s_v2_result_context[i] = 0;
        s_web[i].armed = true;
    } else {
        if (s_web[i].token != e->token || e->sequence <= s_web[i].sequence) return;
#ifndef S3_LEGACY_HOST_TEST
        if(!v2 && e->op==WEB_ENGINE && e->a<=1 && !e->b) {
            web_v2_event_t select={.client=e->client,.token=e->token,.sequence=e->sequence,
                .context_epoch=s_control_epoch,.op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=e->a,.expires=now+WEB_LEASE_US};
            if(owner_remote_begin(&select,now))return;
        }
        engine_flute_state_t flute;engine_link_flute_state(&flute);
        if(!v2 && (flute.requested_mode || flute.applied_mode || flute.switching) &&
           e->op!=WEB_STOP && !(e->op==WEB_KEYS_UPDATE && !e->a))return;
        if(!v2 && owner_local_frozen() && e->op!=WEB_STOP && e->op!=WEB_KEYS_UPDATE)return;
#endif
        engine_fm_status_t fm;
        if(fm_active(&fm) && !v2 && e->op!=WEB_STOP && !(e->op==WEB_KEYS_UPDATE && !e->a))return;
        s_web[i].sequence = e->sequence;
        if (e->op == WEB_CC && web_control_cc_allowed(e->a) && e->b <= 127) {
#if CONFIG_FPGA_AE350_CONTROL && !defined(S3_LEGACY_HOST_TEST)
            midi_template_link_state_t patch_link;midi_template_link_state(&patch_link);
            if(sub_patch1_required_cc(e->a) &&
               (!patch_link.sub_patch_supported || !patch_link.sub_patch_valid))return;
#endif
            handle_cc(e->a, e->b);
#ifndef S3_LEGACY_HOST_TEST
            feedback_mark(MIDI_SOURCE_DEFAULT);
#endif
            s_web[i].parameter_sequence = e->sequence;
        } else if (e->op == WEB_STOP) {
            // Frozen client STOP is explicit global Panic. Ordinary own-page
            // release is KEYS_UPDATE mask=0 and remains source-local above.
            web_stop(i,e->sequence);
        } else if(e->op==WEB_ENGINE && e->a<=1 && !e->b) {
            if(CONFIG_FPGA_FM_ONLY)return;
            engine_ks_state_t engine;engine_link_ks_state(&engine);
            if(engine.recovering || (e->a && !engine.available))return;
            all_sound_off();
            if(!engine_link_ks_mode(e->a!=0))return;
            s_requested_engine=e->a;
            s_web[i].parameter_sequence=e->sequence;
        } else if(e->op==WEB_KS_DECAY && e->a>=200 && e->a<=8000 && !e->b) {
            ks_voice_decay(e->a);s_web[i].parameter_sequence=e->sequence;
        } else if (e->op == WEB_KEYS_UPDATE && e->a <= WEB_KEYS_MASK && e->b >= 1 && e->b <= 127) {
#ifndef S3_LEGACY_HOST_TEST
            s_v2_result_code[i]=web_snapshot_raw(i,e->a,e->b,e->expires,now);
#else
            // Never revive a key from a late request or a lost connection.
            if (now >= e->expires) { web_release(i); s_web[i].armed = false; return; }
            // Check the old lease before accepting a renewal, even if this
            // task resumes with a heartbeat already waiting in its ring.
            if ((s_web[i].keys | s_web[i].desired_keys) && now >= s_web[i].expires) {
                web_release(i); s_web[i].armed = false;
            }
            s_web[i].expires = e->expires;
            if (!e->a) s_web[i].armed = true;
            if (!s_web[i].armed) return;
            s_web[i].desired_keys = e->a;
            s_web[i].velocity = e->b;
#endif
        }
    }
}
static void handle_web(const web_event_t *e,int64_t now)
{handle_web_core(e,now,false);}
static void handle_web_v2(const web_v2_event_t *e, int64_t now)
{
#ifndef S3_LEGACY_HOST_TEST
    owner_local_sync();
#endif
    if (e->client >= WEB_CLIENTS || s_web[e->client].token != e->token ||
        e->sequence <= s_web[e->client].sequence) return;
    unsigned i = e->client;
    uint32_t result = FM_RESULT_OK;
    if (!web_control_v2_valid(e)) result = FM_RESULT_BAD_VALUE;
    else if (e->context_epoch != s_control_epoch) result = FM_RESULT_STALE_EPOCH;
#ifndef S3_LEGACY_HOST_TEST
    else if(e->op==FM_HTTP_OPCODE_ENGINE_SELECT && owner_remote_begin(e,now))return;
#endif
    else {
        engine_ks_state_t engine; engine_link_ks_state(&engine);
        engine_fm_status_t fm;engine_link_fm_status(&fm);
        bool active=fm.requested_backend || fm.selected || fm.switching || fm.recovering;
#ifndef S3_LEGACY_HOST_TEST
        if(CONFIG_FPGA_AE350_CONTROL && e->op==FM_HTTP_OPCODE_CC && sub_patch1_required_cc(e->a)) {
            midi_template_link_state_t image;midi_template_link_state(&image);
            if(!image.sub_patch_valid || !image.sub_patch_supported)result=FM_RESULT_UNSUPPORTED;
        }
        engine_flute_state_t flute;engine_link_flute_state(&flute);
        if(e->op==FM_HTTP_OPCODE_ENGINE_SELECT && e->a==ENGINE_MODE_FLUTE && !flute.available)result=FM_RESULT_UNSUPPORTED;
        if((flute.requested_mode || flute.applied_mode) && e->op==FM_HTTP_OPCODE_KEYS_UPDATE &&
           (e->a&((1u<<(60-WEB_FIRST_NOTE))-1u)))result=FM_RESULT_UNSUPPORTED;
        if((flute.requested_mode || flute.applied_mode) &&
           e->op!=FM_HTTP_OPCODE_STOP && e->op!=FM_HTTP_OPCODE_ENGINE_SELECT &&
           e->op!=FM_HTTP_OPCODE_KEYS_UPDATE)result=FM_RESULT_UNSUPPORTED;
        if(owner_local_frozen() && e->op!=FM_HTTP_OPCODE_STOP &&
           e->op!=FM_HTTP_OPCODE_ENGINE_SELECT && e->op!=FM_HTTP_OPCODE_KEYS_UPDATE)result=FM_RESULT_BUSY;
        if(e->op!=FM_HTTP_OPCODE_STOP && e->op!=FM_HTTP_OPCODE_ENGINE_SELECT &&
           !(e->op==FM_HTTP_OPCODE_KEYS_UPDATE && !e->a) && owner_route_blocked())result=FM_RESULT_BUSY;
#endif
        if((e->op==FM_HTTP_OPCODE_FM_PARAMETER || e->op==FM_HTTP_OPCODE_FM_PRESET ||
           (e->op==FM_HTTP_OPCODE_ENGINE_SELECT && e->a==FM_ENGINE_FM)) && !fm.available)result=FM_RESULT_UNSUPPORTED;
        if(e->op==FM_HTTP_OPCODE_ENGINE_SELECT && e->a==FM_ENGINE_FM &&
           (e->b!=fm.backend_id || !engine_fm_selectable(&fm)))result=FM_RESULT_UNSUPPORTED;
        if (e->op == FM_HTTP_OPCODE_ENGINE_SELECT && e->a == FM_ENGINE_KS && !engine.available)
            result = FM_RESULT_UNSUPPORTED;
        if(CONFIG_FPGA_FM_ONLY && e->op==FM_HTTP_OPCODE_ENGINE_SELECT && e->a!=FM_ENGINE_FM)
            result=FM_RESULT_UNSUPPORTED;
        if (e->op == FM_HTTP_OPCODE_ENGINE_SELECT && engine.recovering)
            result = FM_RESULT_BUSY;
        if (e->op == FM_HTTP_OPCODE_KEYS_UPDATE && e->a &&
            (fm.switching || fm.recovering || fm.needs_select || (active && !engine_fm_playable(&fm)) ||
             engine.switching || engine.recovering || engine.requested_mode != engine.applied_mode ||
             (engine.requested_mode && !engine.valid))) result = FM_RESULT_BUSY;
    }
    if (result == FM_RESULT_OK) {
        if(e->op==FM_HTTP_OPCODE_ENGINE_SELECT) {
#ifndef S3_LEGACY_HOST_TEST
            result=owner_mode_apply(e);
            if(result==FM_RESULT_OK)s_web[i].parameter_sequence=e->sequence;
            s_web[i].sequence=e->sequence;goto v2_done;
#else
            engine_fm_status_t fm;engine_link_fm_status(&fm);
            all_sound_off();
            if((e->a==FM_ENGINE_FM || fm.selected || fm.requested_backend || fm.needs_select || fm.switching) &&
               !engine_link_fm_mode(e->a==FM_ENGINE_FM?e->b:FM_BACKEND_UNBOUND))result=FM_RESULT_BUSY;
            if(result==FM_RESULT_OK && !engine_link_ks_mode(e->a==FM_ENGINE_KS))result=FM_RESULT_BUSY;
            if(result==FM_RESULT_OK) {
                s_requested_engine=e->a;
                s_web[i].parameter_sequence=e->sequence;
            }
            s_web[i].sequence=e->sequence;
            goto v2_done;
#endif
        } else if(e->op==FM_HTTP_OPCODE_FM_PARAMETER) {
            result=engine_link_fm_parameter(e->a,e->b,e->c);
            s_web[i].sequence=e->sequence;if(!result)s_web[i].parameter_sequence=e->sequence;
            goto v2_done;
        } else if(e->op==FM_HTTP_OPCODE_FM_PRESET) {
            engine_fm_status_t fm;engine_link_fm_status(&fm);
            if(!fm_preset(fm.profile_id,e->a))result=FM_RESULT_BAD_VALUE;
            else {all_sound_off();result=engine_link_fm_preset(e->a);}
            s_web[i].sequence=e->sequence;if(!result)s_web[i].parameter_sequence=e->sequence;
            goto v2_done;
        } else if(e->op==FM_HTTP_OPCODE_CC) {
            engine_fm_status_t fm;
            if(fm_active(&fm)) {
                if(e->a==7 || e->a==10){handle_cc(e->a,e->b);
#ifdef S3_LEGACY_HOST_TEST
                    fm_voice_web_expression();
#endif
                    s_web[i].parameter_sequence=e->sequence;}
                else result=FM_RESULT_UNSUPPORTED;
                s_web[i].sequence=e->sequence;goto v2_done;
            }
        }
        web_event_t legacy = {.expires=e->expires, .token=e->token, .sequence=e->sequence,
            .a=e->a, .client=e->client, .op=e->op, .b=(uint8_t)e->b};
        handle_web_core(&legacy, now,true);
#ifndef S3_LEGACY_HOST_TEST
        if(e->op==FM_HTTP_OPCODE_KEYS_UPDATE)result=s_v2_result_code[i];
        if(CONFIG_FPGA_AE350_CONTROL && e->op==FM_HTTP_OPCODE_CC && sub_patch1_required_cc(e->a) &&
           s_web[i].parameter_sequence!=e->sequence)result=FM_RESULT_UNSUPPORTED;
#else
        if (e->op == FM_HTTP_OPCODE_KEYS_UPDATE && e->a && s_web[i].desired_keys != e->a)
            result = FM_RESULT_CANCELLED;
#endif
    } else {
        s_web[i].sequence = e->sequence;
#ifndef S3_LEGACY_HOST_TEST
        if(e->op==FM_HTTP_OPCODE_KEYS_UPDATE && e->a)web_cancel(i);
#endif
    }
v2_done:
    s_v2_result_sequence[i] = e->sequence;
    s_v2_result_code[i] = result;
    s_v2_result_context[i] = e->context_epoch;
}
static bool web_expire(int64_t now)
{
    bool changed = false;
    for (unsigned i = 0; i < WEB_CLIENTS; i++)
        if (
#ifdef S3_LEGACY_HOST_TEST
            (s_web[i].keys | s_web[i].desired_keys) &&
#else
            s_web[i].expires && s_web[i].armed &&
#endif
            now >= s_web[i].expires) {
            web_release(i); s_web[i].armed = false; changed = true;
        }
    return changed;
}
// A 25-key chord advances fairly in the remaining musical batch budget;
// each step is no larger than one ordinary MIDI note transaction.
static bool web_advance(int64_t deadline)
{
#ifndef S3_LEGACY_HOST_TEST
    (void)deadline;return false; // snapshots are encoded at their event position
#else
    static unsigned cursor;
    bool changed = false;
    for (unsigned step = 0; step < WEB_CLIENTS * WEB_KEYS && esp_timer_get_time() < deadline; step++) {
        unsigned position = cursor++ % (WEB_CLIENTS * WEB_KEYS);
        unsigned i = position / WEB_KEYS, k = position % WEB_KEYS;
        uint32_t bit = 1u << k;
        if (!(bit & (s_web[i].keys ^ s_web[i].desired_keys))) continue;
        if (bit & s_web[i].desired_keys) {
            if(note_on_owned(0, WEB_FIRST_NOTE + k, s_web[i].velocity, s_web[i].token))s_web[i].keys |= bit;
            else {
                // Backend readiness may change after ingress validation.
                // Cancel this source's whole old intent; never retry its On.
                web_release(i);s_web[i].armed=false;s_v2_result_code[i]=FM_RESULT_BUSY;
            }
        } else {
            note_off_owned(WEB_FIRST_NOTE + k, s_web[i].token);
            s_web[i].keys &= ~bit;
        }
        changed = true;
    }
    return changed;
#endif
}
static void publish_web_state(void)
{
    // Full FM cache lives in BSS, never on the 3 KiB voice task stack.
    static web_control_state_t state;
    memset(&state,0,sizeof(state));state.patch=g_patch;
    state.cutoff_offset=s_cut_off;state.resonance_offset=s_reso_off;
    state.revision=++s_web_revision;state.panic_revision=s_panic_revision;
    state.ready=true;state.rendering_pending=(s_dirty|s_apply_active)!=0;
    state.test_tone_requested=s_test_tone_requested;state.context_epoch=s_control_epoch;
    engine_link_ks_state(&state.engine);state.ks_decay_ms=ks_voice_decay_ms();
    engine_link_fm_state(&state.fm);
#ifndef S3_LEGACY_HOST_TEST
    engine_link_flute_state(&state.flute);
#endif
    state.requested_engine=state.fm.status.requested_backend?FM_ENGINE_FM:
        s_requested_engine==UINT32_MAX?(state.engine.requested_mode?FM_ENGINE_KS:FM_ENGINE_DDS):s_requested_engine;
    state.applied_engine=state.flute.applied_mode?ENGINE_MODE_FLUTE:state.fm.status.applied_backend?FM_ENGINE_FM:state.engine.applied_mode?FM_ENGINE_KS:FM_ENGINE_DDS;
    state.mode_switching=state.flute.switching || state.fm.status.switching || state.engine.switching || state.requested_engine!=state.applied_engine;
    state.mode_recovering=((state.flute.requested_mode || state.flute.applied_mode) && state.flute.fault) || state.fm.status.recovering || state.engine.recovering;
#ifndef S3_LEGACY_HOST_TEST
    owner_reverb_snapshot(&state.reverb);
    midi_template_link_state_t patch_link;midi_template_link_state(&patch_link);
    state.sub_patch_required=CONFIG_FPGA_AE350_CONTROL;
    state.sub_patch_supported=patch_link.sub_patch_supported;state.sub_patch_valid=patch_link.sub_patch_valid;
    engine_link_cpu_mode_state(&state.cpu_mode);
    engine_link_local_state(&state.local);
    raw_midi_state_t raw;raw_midi_state(&raw);
    web_confirm_stops(&raw);
    state.inputs=(web_input_state_t){
        .version=1,.available=raw.available,.initialized=raw.initialized,
        .enabled=raw.enabled,.ready=owner_raw_ready_snapshot(&raw),
        .capabilities_known=raw.input_capabilities_known,.independent_usb=raw.independent_usb,
        .usb_configured=raw.available && (raw.usb_status&1u)!=0,
        .usb_error=raw.available && (raw.usb_status&2u)!=0,
        .fault=raw.fault,.stop_requested=raw.stop_requested,.stop_completed=raw.stop_completed,
        .stop_fault=raw.stop_fault,.stop_pending=raw.stop_pending,
    };
    if(state.local.supported && (state.local.local_active || state.local.blocked || state.local.remote_pending)) {
        state.requested_engine=state.local.requested;state.applied_engine=state.local.applied;
        state.mode_switching=state.local.blocked || state.local.busy;
        state.mode_recovering=!state.local.valid || state.local.fault;
        state.engine.switching=state.mode_switching;
        state.engine.recovering=state.mode_recovering;
    }
    if(s_remote_mode.active)state.mode_switching=true;
    if(state.cpu_mode.supported) {
        state.requested_engine=s_remote_mode.active && s_remote_mode.cpu?s_remote_mode.event.a:state.cpu_mode.desired;
        state.applied_engine=state.cpu_mode.applied<=2?state.cpu_mode.applied:FM_ENGINE_DDS;
        state.mode_switching=state.cpu_mode.busy || state.cpu_mode.pending || s_remote_mode.active ||
            state.requested_engine!=state.applied_engine;
        state.mode_recovering=!state.cpu_mode.valid || state.cpu_mode.fault || state.cpu_mode.applied>2;
    }
#endif
    for (unsigned i = 0; i < WEB_CLIENTS; i++) {
        state.token[i] = s_web[i].token;
        state.sequence[i] = s_web[i].sequence;
        state.parameter_sequence[i] = s_web[i].parameter_sequence;
        state.stop_sequence[i] = s_web[i].stop_sequence;
        state.keys[i] = s_web[i].keys;
        state.midi_channel[i] = s_web[i].midi_channel;
        state.v2_result_sequence[i] = s_v2_result_sequence[i];
        state.v2_result_code[i] = s_v2_result_code[i];
        state.v2_result_context[i] = s_v2_result_context[i];
        if (s_web[i].keys != s_web[i].desired_keys) state.rendering_pending = true;
    }
    web_control_store(&state);
    status_led_voice_heartbeat();
}

#ifndef S3_LEGACY_HOST_TEST
static void feedback_service(void)
{
    uint64_t sync_epoch = 0;
    bool sync_pending = ble_midi_feedback_sync_pending(&sync_epoch);
    if (!s_feedback_dirty && !sync_pending)
        return;

    static web_control_state_t state;
    web_control_read(&state);
    if (sync_pending)
        midi_feedback_publish(&state, MIDI_SOURCE_DEFAULT, sync_epoch);
    if (s_feedback_dirty &&
        midi_feedback_publish(&state, s_feedback_except_source, 0))
        s_feedback_dirty = false;
}
#endif

// Expression buses have the next 1 ms deadline. Reconfiguration is
// coalesced independently and element rendering advances a fair voice cursor
// across bounded batches. Changes arriving during a render remain dirty for
// its next generation rather than restarting/starving the final voices.
static void apply_dirty(int64_t now)
{
#ifndef S3_LEGACY_HOST_TEST
    if(owner_local_frozen())return;
#endif
    const uint32_t fast = D_CUT | D_GAIN;
    if ((s_dirty & fast) && now-s_last_expression >= EXPRESSION_MIN_US) {
        uint32_t bits=s_dirty & fast;
        s_dirty &= ~bits;
        if (bits & D_CUT) refresh_cut_buses();
        if (bits & D_GAIN) refresh_gain_buses();
        s_last_expression=now;
    }
    if (!s_apply_active && (s_dirty & ~fast) && now-s_last_apply >= RECONFIG_MIN_US) {
        s_apply_active=s_dirty & ~fast; s_dirty &= fast;
        s_render_cursor=0; s_last_apply=now;
    }
    int64_t deadline=esp_timer_get_time()+VA_BATCH_US;
#define APPLY_CONFIG(bit, call) do { \
    if ((s_apply_active & (bit)) && esp_timer_get_time()<deadline) { \
        call; s_apply_active &= ~(bit); \
    } \
} while (0)
    APPLY_CONFIG(D_LFO, update_lfo1());
    APPLY_CONFIG(D_LFO2, update_lfo2());
#undef APPLY_CONFIG
    const uint32_t voices = D_ENV | D_ENV2 | D_RENDER;
    if (s_apply_active & voices) {
        while (s_render_cursor<NUM_VOICES && esp_timer_get_time()<deadline) {
            unsigned v=s_render_cursor++;
            if (s_apply_active & D_ENV) update_amp_env_voice(v);
            if (s_apply_active & D_ENV2) update_mod_env_voice(v);
            if ((s_apply_active & D_RENDER) && s_voices[v].state==V_HELD)
                voice_program(v,s_voices[v].note,s_voices[v].vel);
        }
        if (s_render_cursor==NUM_VOICES) s_apply_active &= ~voices;
    }
}
// Preset IO remains outside this task. Adoption is fenced by owner context
// and existing gate/bank confirmations, not a wall-clock delay.
static bool dds_selected(void)
{
    if(CONFIG_FPGA_FM_ONLY)return false;
#ifndef S3_LEGACY_HOST_TEST
    if(owner_route_blocked())return false;
    if(CONFIG_FPGA_AE350_CONTROL) {
        engine_cpu_mode_state_t mode;engine_link_cpu_mode_state(&mode);
        if(mode.supported && (mode.desired!=FM_ENGINE_DDS || mode.applied!=FM_ENGINE_DDS))return false;
    }
#endif
    engine_ks_state_t ks;engine_fm_status_t fm;
    engine_link_ks_state(&ks);engine_link_fm_status(&fm);
#ifndef S3_LEGACY_HOST_TEST
    engine_flute_state_t flute;engine_link_flute_state(&flute);
    if(flute.requested_mode || flute.applied_mode || flute.switching)return false;
#endif
    return !ks.requested_mode && !ks.applied_mode && !ks.switching && !ks.recovering &&
        !engine_fm_control_active(&fm);
}
static void dds_owner_request(uint32_t id)
{
    patch_t snapshot=g_patch;
    snapshot.filter.cutoff=(uint16_t)(int16_t)s_cut_off;
    int32_t resonance=(int32_t)g_patch.filter.resonance+s_reso_off;
    snapshot.filter.resonance=(uint16_t)(resonance<0?0:resonance>0x3FFF?0x3FFF:resonance);
    snapshot.lfo[0].dest=2;
    dds_presets_owner_context(s_control_epoch);
    dds_presets_owner_begin(id,&snapshot,s_control_epoch,dds_selected() && !s_dds_switch_id);
}
static bool dds_owner_poll(int64_t now)
{
    uint32_t waiting=dds_presets_waiting();if(waiting)dds_owner_request(waiting);
    uint32_t id,context;
    if(!s_dds_switch_id && dds_presets_ready(&id,&s_dds_candidate,&context)) {
        if(context!=s_control_epoch || !dds_selected()) {
            dds_presets_owner_complete(id,s_control_epoch,"load cancelled: owner context/engine changed");return false;
        }
        all_sound_off(); // cancels pending GateOn and stale web keys first
        s_dds_switch_id=id;s_dds_switch_context=s_control_epoch;
        s_dds_switch_deadline=now+500000;dds_presets_owner_closing(id);
#ifndef S3_LEGACY_HOST_TEST
        if(CONFIG_FPGA_AE350_CONTROL) {
            raw_midi_state_t raw;raw_midi_state(&raw);
            s_dds_stop_reset_count=raw.reset_count;
            s_dds_stop_ticket=raw_midi_request_stop_all();
        }
#endif
    }
    if(!s_dds_switch_id)return false;
    if(s_dds_switch_context!=s_control_epoch || !dds_selected() || now>s_dds_switch_deadline) {
        dds_presets_owner_complete(s_dds_switch_id,s_control_epoch,"load cancelled during gate close/recovery; old patch retained");
        s_dds_switch_id=0;return true;
    }
#ifndef S3_LEGACY_HOST_TEST
    if(CONFIG_FPGA_AE350_CONTROL) {
        // CPU1 owns the gates/banks. Its acknowledged global STOP is the
        // replacement fence, including native USB, before changing timbre.
        raw_midi_state_t raw;raw_midi_state(&raw);
        if(!raw.cpu_control || !raw.available || !raw.initialized || raw.stop_fault ||
           raw.reset_count!=s_dds_stop_reset_count) {
            dds_presets_owner_complete(s_dds_switch_id,s_control_epoch,"load cancelled: CPU STOP unavailable or reset; old patch retained");
            s_dds_switch_id=0;return true;
        }
        if(raw.stop_pending || !s_dds_stop_ticket || !raw.stop_completed ||
           (int32_t)(raw.stop_completed-s_dds_stop_ticket)<0)return false;
    } else
#endif
    {
        engine_ks_state_t state;engine_link_ks_state(&state);
        if(!state.control_status_valid || state.bank_pending || state.flushing || state.gate_off_pending || state.shadow_gate_pending)return false;
        for(unsigned v=0;v<NUM_VOICES;v++)if(!engine_link_bus_committed(BUS_VGATE(v),0))return false;
    }
    g_patch=s_dds_candidate;
    s_cut_off=(int16_t)g_patch.filter.cutoff;
    g_patch.filter.cutoff=0; // unused legacy field; offset lives in owner bus
    g_patch.lfo[0].dest=0; // unused legacy field; renderer fixes LFO1 to pitch
    int32_t cut=s_cut_off+8192;
    s_cut_coarse=(uint8_t)(cut<0?0:cut>16383?127:cut>>7);s_cut_fine=(uint8_t)(cut&127);
    s_reso_off=0;engine_link_bus_write(BUS_RESO_GLOBAL,0);
    // Bend/wheel are performance expression, not stored timbre. Preserve them.
    update_lfo1();update_lfo2();refresh_cut_buses();refresh_gain_buses();
    s_dirty|=D_RENDER|D_ENV|D_ENV2|D_LFO|D_LFO2|D_CUT|D_GAIN;
    dds_presets_owner_complete(s_dds_switch_id,s_control_epoch,NULL);
#ifndef S3_LEGACY_HOST_TEST
    feedback_mark(MIDI_SOURCE_DEFAULT);
#endif
    s_dds_switch_id=0;return true;
}

static void voice_tick_cb(void *arg)
{
    (void)arg; xTaskNotifyGive(s_va_task);
}
static void voice_alloc_task(void *arg)
{
    (void)arg;
    evt_t evt;
    // A boot SPI fault may have published Panic before this subscriber was
    // installed. Initial ownership is empty; explicitly complete that fence.
    engine_ks_state_t engine;engine_link_ks_state(&engine);
    if(engine.recovering)all_sound_off();
    publish_web_state();
    while (1) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        int64_t deadline=esp_timer_get_time()+VA_BATCH_US;
        // The ring is authoritative and is inspected every 1 ms. Neither the
        // 100 Hz RTOS queue timeout nor a 10 ms delay paces musical events.
        bool state_changed = false;
#ifndef S3_LEGACY_HOST_TEST
        state_changed=owner_local_sync();
#endif
        bool was_pending = (s_dirty | s_apply_active) != 0;
        while (esp_timer_get_time()<deadline &&
               event_bus_receive(s_sub_id,&evt,0)==pdTRUE) {
            state_changed = true;
#ifndef S3_LEGACY_HOST_TEST
            owner_local_sync();
#endif
            if (evt.kind==EVT_PANIC) all_sound_off();
            else if (evt.kind==EVT_MIDI) handle_midi_source(&evt.midi,evt.midi_source);
            else if (evt.kind==EVT_WEB) handle_web(&evt.web, esp_timer_get_time());
            else if (evt.kind==EVT_WEB_V2) handle_web_v2(&evt.web_v2, esp_timer_get_time());
#ifndef S3_LEGACY_HOST_TEST
            else if (evt.kind==EVT_WEB_RAW) handle_web_raw(&evt.web_v2,esp_timer_get_time());
            else if (evt.kind==EVT_WEB_REVERB) handle_web_reverb(&evt.web_v2,esp_timer_get_time());
            else if (evt.kind==EVT_WEB_FDC) handle_web_fdc(&evt.web_v2,esp_timer_get_time());
            else if (evt.kind==EVT_WEB_KS_TIMBRE) handle_web_ks_timbre(&evt.web_v2,esp_timer_get_time());
#endif
            else if (evt.kind==EVT_DDS_PRESET) {
#ifndef S3_LEGACY_HOST_TEST
                if(owner_local_frozen())dds_presets_owner_complete(evt.dds_id,s_control_epoch,"FPGA local control active");
                else
#endif
                dds_owner_request(evt.dds_id);
            }
        }
        int64_t now=esp_timer_get_time();
        if (
#ifndef S3_LEGACY_HOST_TEST
            !owner_local_frozen() &&
#endif
            dds_owner_poll(now)) state_changed = true;
        #ifndef S3_LEGACY_HOST_TEST
        if(owner_reverb_poll(now))state_changed=true;
        if(owner_fdc_poll(now))state_changed=true;
        if(owner_kst_poll(now))state_changed=true;
        if(owner_remote_poll(now))state_changed=true;
#endif
        if (web_expire(now)) state_changed = true;
        if (!s_dds_switch_id && web_advance(deadline)) state_changed = true;
        promote_idle(now);
#ifdef S3_LEGACY_HOST_TEST
        ks_voice_poll();
#else
        owner_templates_update();
#endif
        apply_dirty(now);
        // Readback (including initialization) advances without ingress events.
        if (state_changed || was_pending != ((s_dirty | s_apply_active) != 0) || now-s_web_last_publish>=20000) {
            publish_web_state();s_web_last_publish=now;
        }
#ifndef S3_LEGACY_HOST_TEST
        feedback_service();
#endif
        ulTaskNotifyTake(pdTRUE,0); // wait for a future wake after each batch
    }
}

// Push the bus plan into the pointer words: every element's pitch →
// the global pitch bus, cutoff → its voice's cutoff bus, gains → its
// voice's gain bus. Static wiring, written once, rides the swap.
static void wire_pointers(void)
{
    for (int e = 0; e < NUM_VOICES * ELEMS_PER_VOICE; e++) {
        int v = e / ELEMS_PER_VOICE;
        send((uint8_t)e, 5,
             (uint32_t)BUS_PITCH_GLOBAL
             | ((uint32_t)BUS_DUTY_GLOBAL << 10)   // PWM bus (#73)
             | ((uint32_t)BUS_CUT(v) << 20));
        send((uint8_t)e, 6,
             (uint32_t)BUS_RESO_GLOBAL
             | ((uint32_t)BUS_GAIN(v) << 10) | ((uint32_t)BUS_GAIN(v) << 20));
    }
}

void voice_alloc_init(void)
{
#ifndef S3_LEGACY_HOST_TEST
    // Start closed until static configuration and the first template exist.
    engine_link_performance_ready(false);
#endif
    s_queue = xQueueCreate(VA_QUEUE_LEN, sizeof(evt_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "failed to create event queue");
        return;
    }
#ifndef S3_LEGACY_HOST_TEST
    midi_feedback_init();
#endif
    patch_default(&g_patch);   // active sound (#69) — the former
                               // hardcoded timbre, now in one struct
    dds_presets_owner_context(s_control_epoch);
    if(dds_presets_boot_patch(&g_patch)) {
        s_cut_off=(int16_t)g_patch.filter.cutoff;s_reso_off=0;
        g_patch.filter.cutoff=0;g_patch.lfo[0].dest=0;
        int32_t cut=s_cut_off+8192;
        s_cut_coarse=(uint8_t)(cut<0?0:cut>16383?127:cut>>7);s_cut_fine=(uint8_t)(cut&127);
    }
    wire_pointers();
    // Null buses. The duty pointer (PTRS0[19:10]) and every other
    // pointer left at its default target bus 0 — the intended "zero"
    // bus. NO RUNTIME PATH ever writes bus 0/1 (that is what makes
    // them safe "zero" targets), and unwritten bus BSRAM is NOT
    // guaranteed zero on the GW2AR (the very reason pitch/reso below
    // are written explicitly) — hence these two one-time nulling
    // writes at init, the only writes those buses ever get. Without
    // them, eff_duty = word + (garbage << 13) saturates and CC 25
    // pulse-width does nothing.
    engine_link_bus_write(0, 0);
    engine_link_bus_write(1, 0);
    engine_link_bus_write(BUS_PITCH_GLOBAL, 0);
    engine_link_bus_write(BUS_RESO_GLOBAL, 0);   // baseline = RESO

    // B4: source 0 — the boot vibrato (LFO 1), from patch.lfo[0].
    // CC 76/77/113 retune it live via update_lfo1().
    update_lfo1();
    update_lfo2();     // source 1 (#73): PWM/reso wobble, depth 0 at boot
    update_mod_env();  // MOD envs, even slots of the 64..127 pairs (#42/#44)
    init_fanout_sources();  // fan-out bus sources, odd slots (#44)
    refresh_cut_buses();    // channel cutoff bus base (wheel/bend/CC74 = 0)

    // B5: per-voice amp envelopes — sources 32..63. Each watches its
    // voice's gate bus and drives its voice's gain bus: base is the
    // quiet floor (−ENV_SPAN) shifted by master volume, the envelope
    // level ADDS up to the note's GAIN word ceiling (volume semantics,
    // issue #40 — the subtracts-silence trick is retired). Bases are
    // live bus writes; config rides the swap.
    for (int v = 0; v < NUM_VOICES; v++) {
        engine_link_prod_write(PROD_ADSR(v), 0,
            CSP_OPC_ADSR | ((uint32_t)BUS_GAIN(v) << 6)
               | ((uint32_t)BUS_VGATE(v) << 16));
        engine_link_prod_write(PROD_ADSR(v), 1, patch_adsr_rate1(&g_patch.env[0]));
        engine_link_prod_write(PROD_ADSR(v), 2, ENV_SPAN);
        engine_link_prod_write(PROD_ADSR(v), 3, patch_adsr_rate2(&g_patch.env[0]));
    }
    refresh_gain_buses();   // gain-bus bases from g_patch.volume
    s_sub_id = event_bus_subscribe_reliable(s_queue);
    if (s_sub_id < 0) {
        ESP_LOGE(TAG, "no free subscriber slot");
        return;
    }
    if (xTaskCreate(voice_alloc_task, "voice_alloc", VA_TASK_STACK, NULL,
                    VA_TASK_PRIO, &s_va_task) != pdPASS) {
        ESP_LOGE(TAG, "failed to create task");
        return;
    }
    const esp_timer_create_args_t tick_args = {
        .callback = voice_tick_cb, .name = "voice_tick",
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, EXPRESSION_MIN_US));
#ifndef S3_LEGACY_HOST_TEST
    owner_templates_update();
#endif
    // Valid desired-image writes are lossless; reject counts still expose
    // invalid commands attempting to bypass the gate transaction.
    if (engine_link_drops() > 0)
        ESP_LOGE(TAG, "INIT DROPPED %u engine writes — config incomplete!",
                 (unsigned)engine_link_drops());
    ESP_LOGI(TAG, "%d voices x %d elements ready (sub id %d)",
             NUM_VOICES, ELEMS_PER_VOICE, s_sub_id);
}
