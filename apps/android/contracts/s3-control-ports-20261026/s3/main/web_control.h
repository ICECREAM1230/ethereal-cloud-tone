// Semantic web commands; voice_alloc owns all sound state.
#pragma once
#include "event_bus.h"
#include "patch.h"
#include "engine_link.h"
#include "fm_engine.h"
#include "reverb_control.h"
#define WEB_CLIENTS 4
#define WEB_KEYS 25
#define WEB_FIRST_NOTE 48
#define WEB_LEASE_US 1200000LL
#define WEB_KEYS_MASK ((1u << WEB_KEYS) - 1u)
// Independent observations: HTTP owner readiness never proves MIDI or audio.
// Only the voice owner copies these from the SPI owner's cache.
typedef struct {
    uint32_t version;
    bool available, initialized, enabled, ready;
    bool capabilities_known, independent_usb, usb_configured, usb_error;
    uint32_t fault, stop_requested, stop_completed, stop_fault;
    bool stop_pending;
} web_input_state_t;
typedef struct {
    uint32_t token, sequence, context_epoch, request_id, code, applied_sequence;
} web_effect_result_t;
typedef struct {
    engine_reverb_state_t link;
    bool pending;
    web_effect_result_t request, result, applied;
} web_reverb_state_t;
typedef struct {
    patch_t patch;
    int32_t cutoff_offset, resonance_offset;
    uint32_t revision, panic_revision;
    uint32_t token[WEB_CLIENTS], sequence[WEB_CLIENTS], keys[WEB_CLIENTS];
    uint32_t parameter_sequence[WEB_CLIENTS], stop_sequence[WEB_CLIENTS];
    bool ready, rendering_pending, test_tone_requested;
    web_input_state_t inputs;
    engine_local_state_t local;
    engine_ks_state_t engine;
    unsigned ks_decay_ms;
    uint32_t context_epoch;
    uint32_t v2_result_sequence[WEB_CLIENTS], v2_result_code[WEB_CLIENTS];
    uint32_t v2_result_context[WEB_CLIENTS];
    engine_fm_state_t fm;
    uint32_t requested_engine, applied_engine;
    bool mode_switching, mode_recovering;
    web_reverb_state_t reverb;
} web_control_state_t;
// BLE parameter feedback needs the applied patch and its ownership fence,
// without retaining the HTTP/FM/KS response image in each reader task.
typedef struct {
    patch_t patch;
    int32_t cutoff_offset, resonance_offset;
    uint32_t context_epoch, revision;
    bool eligible;
} web_control_feedback_state_t;
// Only the voice owner publishes. Readers receive one coherent RAM snapshot.
void web_control_store(const web_control_state_t *state);
void web_control_read(web_control_state_t *state);
void web_control_read_feedback(web_control_feedback_state_t *state);
bool web_control_cc_allowed(unsigned cc);
// Strict ASCII decimal grammar: token seq op a b. No ignored trailing data.
bool web_control_parse(const char *body, web_event_t *event);
/* One immutable reservation spans HTTP receipt, owner dispatch and real ACK.
 * IDs are never recycled during a boot; expired slots can be replaced. */
bool web_control_reverb_reserve(web_v2_event_t *event, int64_t now);
bool web_control_reverb_matches(const web_v2_event_t *event);
void web_control_reverb_release(uint32_t id);
