// FM desired state serviced exclusively by engine_link's SPI task.
#pragma once
#include "fm_contract.h"

#define ENGINE_FM_VOICES FM_PROFILE_JT51_OPM_VOICE_COUNT
#define ENGINE_FM_PARAMETER_SLOTS 64
typedef struct { uint32_t session_id, engine_epoch, sequence_id; } engine_fm_identity_t;
typedef struct { engine_fm_identity_t identity; uint32_t code; } engine_fm_result_t;
typedef struct {
    bool available, valid, selected, switching, recovering, needs_select, ready;
    uint32_t reason, backend_id, profile_id, profile_version, voice_count;
    uint32_t session_id, engine_epoch, topology_generation, phase, flags, features;
    uint32_t requested_backend, applied_backend, pending_mask;
    engine_fm_identity_t accepted, completed;
    engine_fm_result_t result;
    engine_fm_identity_t panic_accepted, panic_completed;
    engine_fm_result_t panic_result;
} engine_fm_status_t;
typedef struct {
    uint32_t parameter_id, target_word, value_u32, sequence_id;
} engine_fm_parameter_t;
typedef struct {
    engine_fm_status_t status;
    uint32_t source_numerator, source_denominator, output_numerator, output_denominator;
    uint32_t operators_per_voice[ENGINE_FM_VOICES], voice_generations[ENGINE_FM_VOICES];
    unsigned parameter_count;
    engine_fm_parameter_t parameters[ENGINE_FM_PARAMETER_SLOTS];
} engine_fm_state_t;
enum { FM_LINK_NO_CAPABILITY=1, FM_LINK_ABI, FM_LINK_PROFILE, FM_LINK_READBACK,
       FM_LINK_TIMEOUT, FM_LINK_CONTEXT, FM_LINK_BACKEND };
void engine_link_fm_status(engine_fm_status_t *state);
void engine_link_fm_state(engine_fm_state_t *state);
bool engine_link_fm_mode(uint32_t backend_id);
bool engine_link_fm_note_on(unsigned voice, uint32_t pitch, uint8_t velocity, uint8_t volume, uint8_t pan);
void engine_link_fm_note_off(unsigned voice);
void engine_link_fm_pitch(unsigned voice, uint32_t pitch);
void engine_link_fm_gain(unsigned voice, uint8_t volume, uint8_t pan);
uint32_t engine_link_fm_parameter(uint32_t id, uint32_t target, uint32_t value);
uint32_t engine_link_fm_preset(uint32_t id);
void engine_link_fm_panic(void);
void engine_link_fm_owner_ack(void);
