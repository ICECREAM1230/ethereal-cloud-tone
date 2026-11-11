// Semantic web commands; voice_alloc owns all sound state.
#pragma once
#include "event_bus.h"
#include "patch.h"
#include "engine_link.h"
#include "fm_engine.h"
#define WEB_CLIENTS 4
#define WEB_KEYS 25
#define WEB_FIRST_NOTE 48
#define WEB_LEASE_US 1200000LL
#define WEB_KEYS_MASK ((1u << WEB_KEYS) - 1u)
typedef struct {
    patch_t patch;
    int32_t cutoff_offset, resonance_offset;
    uint32_t revision, panic_revision;
    uint32_t token[WEB_CLIENTS], sequence[WEB_CLIENTS], keys[WEB_CLIENTS];
    uint32_t parameter_sequence[WEB_CLIENTS], stop_sequence[WEB_CLIENTS];
    bool ready, rendering_pending, test_tone_requested;
    engine_ks_state_t engine;
    unsigned ks_decay_ms;
    uint32_t context_epoch;
    uint32_t v2_result_sequence[WEB_CLIENTS], v2_result_code[WEB_CLIENTS];
    uint32_t v2_result_context[WEB_CLIENTS];
    engine_fm_state_t fm;
    uint32_t requested_engine, applied_engine;
    bool mode_switching, mode_recovering;
} web_control_state_t;
// Only the voice owner publishes. Readers receive one coherent RAM snapshot.
void web_control_store(const web_control_state_t *state);
void web_control_read(web_control_state_t *state);
bool web_control_cc_allowed(unsigned cc);
// Strict ASCII decimal grammar: token seq op a b. No ignored trailing data.
bool web_control_parse(const char *body, web_event_t *event);
