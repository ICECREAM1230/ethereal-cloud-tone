// CONTRACT FROZEN / HARDWARE NOT INTEGRATED; 请勿手改 / DO NOT EDIT.
// Generated from schema + backend profiles; transport/backend lifecycle is not implemented by this codec.
// Schema version: 1.0.1
// Schema SHA256: 68d7d85ab97942edb128efcb3dffe01bdc50fb98baf9476ab4663d4f22c2cc73

#ifndef FM_CONTRACT_H
#define FM_CONTRACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FM_SCHEMA_VERSION "1.0.1"
#define FM_SCHEMA_SHA256 "68d7d85ab97942edb128efcb3dffe01bdc50fb98baf9476ab4663d4f22c2cc73"

#define FM_ABI_MAJOR UINT32_C(0x00000001)
#define FM_ABI_MINOR UINT32_C(0x00000001)
#define FM_CAP_MAGIC UINT32_C(0x464d4331)
#define FM_SPI_WORD_BASE UINT32_C(0x00000600)
#define FM_SPI_WINDOW_WORDS UINT32_C(0x00000100)
#define FM_SPI_WORD_BITS UINT32_C(0x00000020)
#define FM_TARGET_NONE UINT32_C(0x0000ffff)
#define FM_MAX_WIRE_INDEX UINT32_C(0x0000fffe)
#define FM_MAX_DESCRIPTOR_VOICES UINT32_C(0x00000100)
#define FM_MAX_OPERATORS_PER_VOICE UINT32_C(0x00000020)
#define FM_MAX_PARAMETERS UINT32_C(0x00000100)
#define FM_MAX_PRESETS UINT32_C(0x00000080)
#define FM_MAX_BACKEND_PROFILES UINT32_C(0x00000010)
#define FM_HTTP_API_MAJOR UINT32_C(0x00000002)
#define FM_HTTP_API_MINOR UINT32_C(0x00000000)
#define FM_HTTP_COMMAND_MAX_BYTES UINT32_C(0x0000005f)
#define FM_PERFORMANCE_LEASE_MS UINT32_C(0x000004b0)
#define FM_FLAG_READY UINT32_C(0x00000001)
#define FM_FLAG_SELECTED UINT32_C(0x00000002)
#define FM_FLAG_MUTED UINT32_C(0x00000004)
#define FM_FLAG_BUSY UINT32_C(0x00000008)
#define FM_FLAG_FAULT UINT32_C(0x00000010)
#define FM_FEATURE_NOTE_ON UINT32_C(0x00000001)
#define FM_FEATURE_NOTE_OFF UINT32_C(0x00000002)
#define FM_FEATURE_SET_PITCH UINT32_C(0x00000004)
#define FM_FEATURE_SET_GAIN_PAN UINT32_C(0x00000008)
#define FM_FEATURE_SET_PARAMETER UINT32_C(0x00000010)
#define FM_FEATURE_APPLY_PRESET UINT32_C(0x00000020)
#define FM_FEATURE_RELEASE_ALL UINT32_C(0x00000040)
#define FM_FEATURE_PRIORITY_PANIC UINT32_C(0x00000080)
#define FM_FEATURE_NEXT_NOTE_PARAMETERS UINT32_C(0x00000100)
#define FM_FEATURE_NATIVE_STEREO_PAN UINT32_C(0x00000200)
#define FM_ABI_VERSION_WORD UINT32_C(0x00010001)
#define FM_HTTP_API_VERSION_WORD UINT32_C(0x00020000)
#define FM_ENGINE_DDS UINT32_C(0x00000000)
#define FM_ENGINE_KS UINT32_C(0x00000001)
#define FM_ENGINE_FM UINT32_C(0x00000002)
#define FM_SCOPE_ENGINE UINT32_C(0x00000000)
#define FM_SCOPE_VOICE UINT32_C(0x00000001)
#define FM_SCOPE_VOICE_OPERATOR UINT32_C(0x00000002)
#define FM_SCOPE_PATCH UINT32_C(0x00000003)
#define FM_SCOPE_PATCH_OPERATOR UINT32_C(0x00000004)
#define FM_SPI_OPCODE_SELECT_BACKEND UINT32_C(0x00000000)
#define FM_SPI_OPCODE_PANIC UINT32_C(0x00000001)
#define FM_SPI_OPCODE_NOTE_ON UINT32_C(0x00000002)
#define FM_SPI_OPCODE_NOTE_OFF UINT32_C(0x00000003)
#define FM_SPI_OPCODE_SET_PITCH UINT32_C(0x00000004)
#define FM_SPI_OPCODE_SET_GAIN_PAN UINT32_C(0x00000005)
#define FM_SPI_OPCODE_SET_PARAMETER UINT32_C(0x00000006)
#define FM_SPI_OPCODE_APPLY_PRESET UINT32_C(0x00000007)
#define FM_SPI_OPCODE_RELEASE_ALL UINT32_C(0x00000008)
#define FM_HTTP_OPCODE_CC UINT32_C(0x00000002)
#define FM_HTTP_OPCODE_KEYS_UPDATE UINT32_C(0x00000003)
#define FM_HTTP_OPCODE_STOP UINT32_C(0x00000004)
#define FM_HTTP_OPCODE_ENGINE_SELECT UINT32_C(0x00000005)
#define FM_HTTP_OPCODE_KS_DECAY UINT32_C(0x00000006)
#define FM_HTTP_OPCODE_FM_PARAMETER UINT32_C(0x00000007)
#define FM_HTTP_OPCODE_FM_PRESET UINT32_C(0x00000008)
#define FM_RESULT_OK UINT32_C(0x00000000)
#define FM_RESULT_BUSY UINT32_C(0x00000001)
#define FM_RESULT_UNSUPPORTED UINT32_C(0x00000002)
#define FM_RESULT_BAD_TARGET UINT32_C(0x00000003)
#define FM_RESULT_BAD_VALUE UINT32_C(0x00000004)
#define FM_RESULT_STALE_EPOCH UINT32_C(0x00000005)
#define FM_RESULT_ABI_MISMATCH UINT32_C(0x00000006)
#define FM_RESULT_BACKEND_FAULT UINT32_C(0x00000007)
#define FM_RESULT_CANCELLED UINT32_C(0x00000008)
#define FM_RESULT_SEQUENCE_CONFLICT UINT32_C(0x00000009)
#define FM_RESULT_STALE_SEQUENCE UINT32_C(0x0000000a)
#define FM_RESULT_STALE_SESSION UINT32_C(0x0000000b)
#define FM_RESULT_STALE_TOPOLOGY UINT32_C(0x0000000c)
#define FM_RESULT_STALE_VOICE UINT32_C(0x0000000d)
#define FM_PHASE_UNAVAILABLE UINT32_C(0x00000000)
#define FM_PHASE_INITIALIZING UINT32_C(0x00000001)
#define FM_PHASE_READY UINT32_C(0x00000002)
#define FM_PHASE_SWITCHING UINT32_C(0x00000003)
#define FM_PHASE_RECOVERING UINT32_C(0x00000004)
#define FM_PHASE_BLOCKED UINT32_C(0x00000005)
#define FM_BACKEND_UNBOUND UINT32_C(0x00000000)
#define FM_BACKEND_JT51_OPM UINT32_C(0x00000001)
#define FM_BACKEND_MSFA_TWO_OP_REFERENCE UINT32_C(0x00000002)
#define FM_REG_CAP_MAGIC UINT32_C(0x00000600)
#define FM_REG_ABI_VERSION UINT32_C(0x00000601)
#define FM_REG_BACKEND_ID UINT32_C(0x00000602)
#define FM_REG_VOICE_COUNT UINT32_C(0x00000603)
#define FM_REG_MAX_OPERATOR_COUNT UINT32_C(0x00000604)
#define FM_REG_FEATURES UINT32_C(0x00000605)
#define FM_REG_EPOCH UINT32_C(0x00000606)
#define FM_REG_PHASE UINT32_C(0x00000607)
#define FM_REG_ACCEPTED_SEQUENCE UINT32_C(0x00000608)
#define FM_REG_COMPLETED_SEQUENCE UINT32_C(0x00000609)
#define FM_REG_RESULT_SEQUENCE UINT32_C(0x0000060a)
#define FM_REG_RESULT UINT32_C(0x0000060b)
#define FM_REG_TOPOLOGY_GENERATION UINT32_C(0x0000060c)
#define FM_REG_STATUS_GENERATION UINT32_C(0x0000060d)
#define FM_REG_FLAGS UINT32_C(0x0000060e)
#define FM_REG_SESSION UINT32_C(0x0000060f)
#define FM_REG_CMD_OPCODE_ID UINT32_C(0x00000610)
#define FM_REG_CMD_SCOPE_ID UINT32_C(0x00000611)
#define FM_REG_CMD_TARGET_WORD UINT32_C(0x00000612)
#define FM_REG_CMD_PARAMETER_ID UINT32_C(0x00000613)
#define FM_REG_CMD_VALUE_U32 UINT32_C(0x00000614)
#define FM_REG_CMD_ARG0_U32 UINT32_C(0x00000615)
#define FM_REG_CMD_ARG1_U32 UINT32_C(0x00000616)
#define FM_REG_CMD_SESSION_ID UINT32_C(0x00000617)
#define FM_REG_CMD_ENGINE_EPOCH UINT32_C(0x00000618)
#define FM_REG_CMD_TOPOLOGY_GENERATION UINT32_C(0x00000619)
#define FM_REG_CMD_VOICE_GENERATION UINT32_C(0x0000061a)
#define FM_REG_CMD_SEQUENCE_ID UINT32_C(0x0000061b)
#define FM_REG_CMD_COMMIT UINT32_C(0x0000061c)
#define FM_REG_PANIC_SEQUENCE UINT32_C(0x00000630)
#define FM_REG_PANIC_ACCEPTED_SEQUENCE UINT32_C(0x00000631)
#define FM_REG_PANIC_COMPLETED_SEQUENCE UINT32_C(0x00000632)
#define FM_REG_SOURCE_RATE_NUMERATOR UINT32_C(0x00000640)
#define FM_REG_SOURCE_RATE_DENOMINATOR UINT32_C(0x00000641)
#define FM_REG_OUTPUT_RATE_NUMERATOR UINT32_C(0x00000642)
#define FM_REG_OUTPUT_RATE_DENOMINATOR UINT32_C(0x00000643)
#define FM_REG_PROFILE_VERSION UINT32_C(0x00000644)
#define FM_REG_PARAMETER_COUNT UINT32_C(0x00000645)
#define FM_REG_PRESET_COUNT UINT32_C(0x00000646)
#define FM_REG_VOICE_QUERY_INDEX UINT32_C(0x00000647)
#define FM_REG_VOICE_QUERY_GENERATION UINT32_C(0x00000648)
#define FM_REG_VOICE_QUERY_OPERATOR_COUNT UINT32_C(0x00000649)
#define FM_REG_PROFILE_SHA256_0 UINT32_C(0x00000660)
#define FM_REG_PROFILE_SHA256_1 UINT32_C(0x00000661)
#define FM_REG_PROFILE_SHA256_2 UINT32_C(0x00000662)
#define FM_REG_PROFILE_SHA256_3 UINT32_C(0x00000663)
#define FM_REG_PROFILE_SHA256_4 UINT32_C(0x00000664)
#define FM_REG_PROFILE_SHA256_5 UINT32_C(0x00000665)
#define FM_REG_PROFILE_SHA256_6 UINT32_C(0x00000666)
#define FM_REG_PROFILE_SHA256_7 UINT32_C(0x00000667)
#define FM_REG_RESULT_SESSION UINT32_C(0x00000620)
#define FM_REG_RESULT_EPOCH UINT32_C(0x00000621)
#define FM_REG_ACCEPTED_SESSION UINT32_C(0x00000622)
#define FM_REG_ACCEPTED_EPOCH UINT32_C(0x00000623)
#define FM_REG_COMPLETED_SESSION UINT32_C(0x00000624)
#define FM_REG_COMPLETED_EPOCH UINT32_C(0x00000625)
#define FM_REG_PANIC_SESSION UINT32_C(0x00000626)
#define FM_REG_PANIC_EPOCH UINT32_C(0x00000627)
#define FM_REG_PANIC_RESULT_SEQUENCE UINT32_C(0x00000633)
#define FM_REG_PANIC_RESULT UINT32_C(0x00000634)
#define FM_REG_PANIC_RESULT_SESSION UINT32_C(0x00000635)
#define FM_REG_PANIC_RESULT_EPOCH UINT32_C(0x00000636)
#define FM_COMMAND_WORD_COUNT UINT32_C(0x0000000c)
#define FM_COMMAND_BYTE_COUNT UINT32_C(0x00000030)
#define FM_CMD_WORD_OPCODE_ID UINT32_C(0x00000000)
#define FM_CMD_WORD_SCOPE_ID UINT32_C(0x00000001)
#define FM_CMD_WORD_TARGET_WORD UINT32_C(0x00000002)
#define FM_CMD_WORD_PARAMETER_ID UINT32_C(0x00000003)
#define FM_CMD_WORD_VALUE_U32 UINT32_C(0x00000004)
#define FM_CMD_WORD_ARG0_U32 UINT32_C(0x00000005)
#define FM_CMD_WORD_ARG1_U32 UINT32_C(0x00000006)
#define FM_CMD_WORD_SESSION_ID UINT32_C(0x00000007)
#define FM_CMD_WORD_ENGINE_EPOCH UINT32_C(0x00000008)
#define FM_CMD_WORD_TOPOLOGY_GENERATION UINT32_C(0x00000009)
#define FM_CMD_WORD_VOICE_GENERATION UINT32_C(0x0000000a)
#define FM_CMD_WORD_SEQUENCE_ID UINT32_C(0x0000000b)
#define FM_UNIT_CHIP_CODE UINT32_C(0x00000000)
#define FM_APPLY_TIME_NEXT_NOTE UINT32_C(0x00000000)
#define FM_VALUE_TYPE_U32 UINT32_C(0x00000000)

/* Validates before narrowing; failure never writes *out. */
static inline bool fm_pack_target(uint32_t voice, uint32_t operator_index, uint32_t *out)
{
    if (out == NULL || voice > UINT32_C(0xffff) ||
        operator_index > UINT32_C(0xffff)) return false;
    *out = (voice << 16) | (operator_index << 0);
    return true;
}

static inline uint32_t fm_target_voice(uint32_t word)
{
    return (word >> 16) & UINT32_C(0xffff);
}

static inline uint32_t fm_target_operator(uint32_t word)
{
    return (word >> 0) & UINT32_C(0xffff);
}

static inline bool fm_validate_target(uint32_t scope, uint32_t word, uint32_t voice_count,
                                      uint32_t selected_operator_count, uint32_t patch_operator_count)
{
    if (voice_count < UINT32_C(1) || voice_count > FM_MAX_DESCRIPTOR_VOICES) return false;
    if (selected_operator_count > FM_MAX_OPERATORS_PER_VOICE) return false;
    if (patch_operator_count > FM_MAX_OPERATORS_PER_VOICE) return false;
    const uint32_t voice = fm_target_voice(word);
    const uint32_t operator_index = fm_target_operator(word);
    if (scope == FM_SCOPE_ENGINE) return (voice == FM_TARGET_NONE) && (operator_index == FM_TARGET_NONE);
    if (scope == FM_SCOPE_VOICE) return (voice < voice_count) && (operator_index == FM_TARGET_NONE);
    if (scope == FM_SCOPE_VOICE_OPERATOR) return (voice < voice_count) && (operator_index < selected_operator_count);
    if (scope == FM_SCOPE_PATCH) return (voice == FM_TARGET_NONE) && (operator_index == FM_TARGET_NONE);
    if (scope == FM_SCOPE_PATCH_OPERATOR) return (voice == FM_TARGET_NONE) && (operator_index < patch_operator_count);
    return false;
}

#define FM_PROFILE_JT51_OPM_VOICE_COUNT UINT32_C(8)
#define FM_PROFILE_JT51_OPM_PATCH_OPERATOR_COUNT UINT32_C(4)
#define FM_PROFILE_JT51_OPM_MAX_OPERATOR_COUNT UINT32_C(4)
#define FM_PROFILE_MSFA_TWO_OP_REFERENCE_VOICE_COUNT UINT32_C(8)
#define FM_PROFILE_MSFA_TWO_OP_REFERENCE_PATCH_OPERATOR_COUNT UINT32_C(2)
#define FM_PROFILE_MSFA_TWO_OP_REFERENCE_MAX_OPERATOR_COUNT UINT32_C(2)
#define FM_PROFILES_SHA256 "408ec706674c15671c752ac9e9f741a8d5247ec23d7df4b25f69600347c010ea"
#define FM_CONTRACT_SHA256 "9df9e901e63c6a18ca6bcee6f78b5a035cb1e8aec00ceae43d24ea1fca75bea0"
static const uint32_t fm_profiles_sha256_words[8] = {UINT32_C(0x408ec706), UINT32_C(0x674c1567), UINT32_C(0x1c752ac9), UINT32_C(0xe9f741a8), UINT32_C(0xd5247ec2), UINT32_C(0x3d7df4b2), UINT32_C(0x5f696003), UINT32_C(0x47c010ea)};

typedef struct {
    uint32_t opcode_id;
    uint32_t scope_id;
    uint32_t target_word;
    uint32_t parameter_id;
    uint32_t value_u32;
    uint32_t arg0_u32;
    uint32_t arg1_u32;
    uint32_t session_id;
    uint32_t engine_epoch;
    uint32_t topology_generation;
    uint32_t voice_generation;
    uint32_t sequence_id;
} fm_command_t;

static inline bool fm_abi_compatible(uint32_t word)
{ return word == FM_ABI_VERSION_WORD; }

static inline bool fm_serialize_command(const fm_command_t *command, uint32_t *words, size_t count)
{
    if (command == NULL || words == NULL || count != FM_COMMAND_WORD_COUNT) return false;
    words[FM_CMD_WORD_OPCODE_ID] = command->opcode_id;
    words[FM_CMD_WORD_SCOPE_ID] = command->scope_id;
    words[FM_CMD_WORD_TARGET_WORD] = command->target_word;
    words[FM_CMD_WORD_PARAMETER_ID] = command->parameter_id;
    words[FM_CMD_WORD_VALUE_U32] = command->value_u32;
    words[FM_CMD_WORD_ARG0_U32] = command->arg0_u32;
    words[FM_CMD_WORD_ARG1_U32] = command->arg1_u32;
    words[FM_CMD_WORD_SESSION_ID] = command->session_id;
    words[FM_CMD_WORD_ENGINE_EPOCH] = command->engine_epoch;
    words[FM_CMD_WORD_TOPOLOGY_GENERATION] = command->topology_generation;
    words[FM_CMD_WORD_VOICE_GENERATION] = command->voice_generation;
    words[FM_CMD_WORD_SEQUENCE_ID] = command->sequence_id;
    return true;
}

static inline bool fm_deserialize_command(const uint32_t *words, size_t count, fm_command_t *command)
{
    if (command == NULL || words == NULL || count != FM_COMMAND_WORD_COUNT) return false;
    command->opcode_id = words[FM_CMD_WORD_OPCODE_ID];
    command->scope_id = words[FM_CMD_WORD_SCOPE_ID];
    command->target_word = words[FM_CMD_WORD_TARGET_WORD];
    command->parameter_id = words[FM_CMD_WORD_PARAMETER_ID];
    command->value_u32 = words[FM_CMD_WORD_VALUE_U32];
    command->arg0_u32 = words[FM_CMD_WORD_ARG0_U32];
    command->arg1_u32 = words[FM_CMD_WORD_ARG1_U32];
    command->session_id = words[FM_CMD_WORD_SESSION_ID];
    command->engine_epoch = words[FM_CMD_WORD_ENGINE_EPOCH];
    command->topology_generation = words[FM_CMD_WORD_TOPOLOGY_GENERATION];
    command->voice_generation = words[FM_CMD_WORD_VOICE_GENERATION];
    command->sequence_id = words[FM_CMD_WORD_SEQUENCE_ID];
    return true;
}

typedef struct { uint32_t id; const char *name; uint32_t minimum, maximum, scope_id, unit_id, apply_time_id, value_type_id; } fm_parameter_t;
typedef struct { uint32_t parameter_id, operator_index, value_u32; } fm_preset_value_t;
typedef struct { uint32_t id; const char *name; uint32_t key_on_logical_mask, values_count; const fm_preset_value_t *values; } fm_preset_t;
typedef struct { uint32_t id, backend_id, profile_version, voice_count, patch_operator_count; bool runtime_available;
    uint32_t parameter_count, preset_count; const char *key, *profile_sha256; const uint32_t *operators_per_voice;
    const char *const *operator_labels; const fm_parameter_t *parameters; const fm_preset_t *presets; } fm_profile_t;

static const uint32_t fm_profile_1_operators[] = {4, 4, 4, 4, 4, 4, 4, 4};
static const char *const fm_profile_1_labels[] = {"M1", "C1", "M2", "C2"};
static const fm_parameter_t fm_profile_1_parameters[] = {
    {1u, "algorithm", 0u, 7u, 3u, 0u, 0u, 0u},
    {2u, "feedback", 0u, 7u, 3u, 0u, 0u, 0u},
    {16u, "multiplier", 0u, 15u, 4u, 0u, 0u, 0u},
    {17u, "total_level", 0u, 127u, 4u, 0u, 0u, 0u},
    {18u, "attack_rate", 0u, 31u, 4u, 0u, 0u, 0u},
    {19u, "decay1_rate", 0u, 31u, 4u, 0u, 0u, 0u},
    {20u, "decay2_rate", 0u, 31u, 4u, 0u, 0u, 0u},
    {21u, "sustain_level", 0u, 15u, 4u, 0u, 0u, 0u},
    {22u, "release_rate", 0u, 15u, 4u, 0u, 0u, 0u},
    {23u, "detune1", 0u, 7u, 4u, 0u, 0u, 0u},
    {24u, "detune2", 0u, 3u, 4u, 0u, 0u, 0u},
    {25u, "rate_scaling", 0u, 3u, 4u, 0u, 0u, 0u},
    {26u, "am_enable", 0u, 1u, 4u, 0u, 0u, 0u},
};
static const fm_preset_value_t fm_profile_1_preset_0_values[] = {
    {1u, 65535u, 7u},
    {2u, 65535u, 0u},
    {16u, 0u, 1u},
    {17u, 0u, 8u},
    {18u, 0u, 31u},
    {19u, 0u, 0u},
    {20u, 0u, 0u},
    {21u, 0u, 0u},
    {22u, 0u, 15u},
    {23u, 0u, 0u},
    {24u, 0u, 0u},
    {25u, 0u, 0u},
    {26u, 0u, 0u},
    {16u, 1u, 1u},
    {17u, 1u, 127u},
    {18u, 1u, 31u},
    {19u, 1u, 0u},
    {20u, 1u, 0u},
    {21u, 1u, 0u},
    {22u, 1u, 15u},
    {23u, 1u, 0u},
    {24u, 1u, 0u},
    {25u, 1u, 0u},
    {26u, 1u, 0u},
    {16u, 2u, 1u},
    {17u, 2u, 127u},
    {18u, 2u, 31u},
    {19u, 2u, 0u},
    {20u, 2u, 0u},
    {21u, 2u, 0u},
    {22u, 2u, 15u},
    {23u, 2u, 0u},
    {24u, 2u, 0u},
    {25u, 2u, 0u},
    {26u, 2u, 0u},
    {16u, 3u, 1u},
    {17u, 3u, 127u},
    {18u, 3u, 31u},
    {19u, 3u, 0u},
    {20u, 3u, 0u},
    {21u, 3u, 0u},
    {22u, 3u, 15u},
    {23u, 3u, 0u},
    {24u, 3u, 0u},
    {25u, 3u, 0u},
    {26u, 3u, 0u},
};
static const fm_preset_value_t fm_profile_1_preset_1_values[] = {
    {1u, 65535u, 4u},
    {2u, 65535u, 0u},
    {16u, 0u, 1u},
    {17u, 0u, 28u},
    {18u, 0u, 28u},
    {19u, 0u, 10u},
    {20u, 0u, 2u},
    {21u, 0u, 5u},
    {22u, 0u, 14u},
    {23u, 0u, 0u},
    {24u, 0u, 0u},
    {25u, 0u, 0u},
    {26u, 0u, 0u},
    {16u, 1u, 1u},
    {17u, 1u, 8u},
    {18u, 1u, 25u},
    {19u, 1u, 8u},
    {20u, 1u, 2u},
    {21u, 1u, 4u},
    {22u, 1u, 14u},
    {23u, 1u, 0u},
    {24u, 1u, 0u},
    {25u, 1u, 0u},
    {26u, 1u, 0u},
    {16u, 2u, 1u},
    {17u, 2u, 127u},
    {18u, 2u, 31u},
    {19u, 2u, 0u},
    {20u, 2u, 0u},
    {21u, 2u, 0u},
    {22u, 2u, 14u},
    {23u, 2u, 0u},
    {24u, 2u, 0u},
    {25u, 2u, 0u},
    {26u, 2u, 0u},
    {16u, 3u, 1u},
    {17u, 3u, 127u},
    {18u, 3u, 31u},
    {19u, 3u, 0u},
    {20u, 3u, 0u},
    {21u, 3u, 0u},
    {22u, 3u, 14u},
    {23u, 3u, 0u},
    {24u, 3u, 0u},
    {25u, 3u, 0u},
    {26u, 3u, 0u},
};
static const fm_preset_value_t fm_profile_1_preset_2_values[] = {
    {1u, 65535u, 0u},
    {2u, 65535u, 2u},
    {16u, 0u, 1u},
    {17u, 0u, 26u},
    {18u, 0u, 31u},
    {19u, 0u, 12u},
    {20u, 0u, 5u},
    {21u, 0u, 8u},
    {22u, 0u, 15u},
    {23u, 0u, 0u},
    {24u, 0u, 0u},
    {25u, 0u, 0u},
    {26u, 0u, 0u},
    {16u, 1u, 3u},
    {17u, 1u, 30u},
    {18u, 1u, 31u},
    {19u, 1u, 8u},
    {20u, 1u, 2u},
    {21u, 1u, 4u},
    {22u, 1u, 15u},
    {23u, 1u, 0u},
    {24u, 1u, 0u},
    {25u, 1u, 0u},
    {26u, 1u, 0u},
    {16u, 2u, 2u},
    {17u, 2u, 42u},
    {18u, 2u, 31u},
    {19u, 2u, 9u},
    {20u, 2u, 3u},
    {21u, 2u, 6u},
    {22u, 2u, 15u},
    {23u, 2u, 0u},
    {24u, 2u, 0u},
    {25u, 2u, 0u},
    {26u, 2u, 0u},
    {16u, 3u, 1u},
    {17u, 3u, 8u},
    {18u, 3u, 31u},
    {19u, 3u, 8u},
    {20u, 3u, 3u},
    {21u, 3u, 5u},
    {22u, 3u, 15u},
    {23u, 3u, 0u},
    {24u, 3u, 0u},
    {25u, 3u, 0u},
    {26u, 3u, 0u},
};
static const fm_preset_value_t fm_profile_1_preset_3_values[] = {
    {1u, 65535u, 4u},
    {2u, 65535u, 0u},
    {16u, 0u, 1u},
    {17u, 0u, 32u},
    {18u, 0u, 31u},
    {19u, 0u, 14u},
    {20u, 0u, 5u},
    {21u, 0u, 10u},
    {22u, 0u, 10u},
    {23u, 0u, 0u},
    {24u, 0u, 0u},
    {25u, 0u, 0u},
    {26u, 0u, 0u},
    {16u, 1u, 1u},
    {17u, 1u, 10u},
    {18u, 1u, 31u},
    {19u, 1u, 8u},
    {20u, 1u, 3u},
    {21u, 1u, 8u},
    {22u, 1u, 10u},
    {23u, 1u, 0u},
    {24u, 1u, 0u},
    {25u, 1u, 0u},
    {26u, 1u, 0u},
    {16u, 2u, 14u},
    {17u, 2u, 40u},
    {18u, 2u, 31u},
    {19u, 2u, 18u},
    {20u, 2u, 7u},
    {21u, 2u, 12u},
    {22u, 2u, 10u},
    {23u, 2u, 0u},
    {24u, 2u, 0u},
    {25u, 2u, 0u},
    {26u, 2u, 0u},
    {16u, 3u, 1u},
    {17u, 3u, 18u},
    {18u, 3u, 31u},
    {19u, 3u, 12u},
    {20u, 3u, 5u},
    {21u, 3u, 10u},
    {22u, 3u, 10u},
    {23u, 3u, 0u},
    {24u, 3u, 0u},
    {25u, 3u, 0u},
    {26u, 3u, 0u},
};
static const fm_preset_value_t fm_profile_1_preset_4_values[] = {
    {1u, 65535u, 4u},
    {2u, 65535u, 0u},
    {16u, 0u, 3u},
    {17u, 0u, 24u},
    {18u, 0u, 31u},
    {19u, 0u, 10u},
    {20u, 0u, 5u},
    {21u, 0u, 12u},
    {22u, 0u, 6u},
    {23u, 0u, 0u},
    {24u, 0u, 1u},
    {25u, 0u, 0u},
    {26u, 0u, 0u},
    {16u, 1u, 1u},
    {17u, 1u, 10u},
    {18u, 1u, 31u},
    {19u, 1u, 6u},
    {20u, 1u, 3u},
    {21u, 1u, 11u},
    {22u, 1u, 6u},
    {23u, 1u, 0u},
    {24u, 1u, 0u},
    {25u, 1u, 0u},
    {26u, 1u, 0u},
    {16u, 2u, 7u},
    {17u, 2u, 34u},
    {18u, 2u, 31u},
    {19u, 2u, 13u},
    {20u, 2u, 6u},
    {21u, 2u, 12u},
    {22u, 2u, 6u},
    {23u, 2u, 0u},
    {24u, 2u, 0u},
    {25u, 2u, 0u},
    {26u, 2u, 0u},
    {16u, 3u, 2u},
    {17u, 3u, 20u},
    {18u, 3u, 31u},
    {19u, 3u, 8u},
    {20u, 3u, 4u},
    {21u, 3u, 12u},
    {22u, 3u, 6u},
    {23u, 3u, 0u},
    {24u, 3u, 0u},
    {25u, 3u, 0u},
    {26u, 3u, 0u},
};
static const fm_preset_t fm_profile_1_presets[] = {
    {0u, "diagnostic_sine", 1u, 46u, fm_profile_1_preset_0_values},
    {1u, "two_op_keys", 3u, 46u, fm_profile_1_preset_1_values},
    {2u, "four_op_bell", 15u, 46u, fm_profile_1_preset_2_values},
    {3u, "electric_piano_tine", 15u, 46u, fm_profile_1_preset_3_values},
    {4u, "metal_bell", 15u, 46u, fm_profile_1_preset_4_values},
};
static const uint32_t fm_profile_2_operators[] = {2, 2, 2, 2, 2, 2, 2, 2};
static const char *const fm_profile_2_labels[] = {"modulator", "carrier"};
static const fm_profile_t fm_profiles[] = {
    {1u, 1u, 65536u, 8u, 4u, false, 13u, 5u, "jt51_opm", "408ec706674c15671c752ac9e9f741a8d5247ec23d7df4b25f69600347c010ea", fm_profile_1_operators, fm_profile_1_labels, fm_profile_1_parameters, fm_profile_1_presets},
    {2u, 2u, 65536u, 8u, 2u, false, 0u, 0u, "msfa_two_op_reference", "408ec706674c15671c752ac9e9f741a8d5247ec23d7df4b25f69600347c010ea", fm_profile_2_operators, fm_profile_2_labels, NULL, NULL},
};

static inline const fm_profile_t *fm_profile(uint32_t id)
{
    for (size_t i = 0; i < sizeof(fm_profiles) / sizeof(fm_profiles[0]); ++i)
        if (fm_profiles[i].id == id) return &fm_profiles[i];
    return NULL;
}

static inline const fm_parameter_t *fm_parameter(uint32_t id, uint32_t parameter_id)
{
    const fm_profile_t *profile = fm_profile(id);
    if (profile == NULL) return NULL;
    for (uint32_t i = 0; i < profile->parameter_count; ++i)
        if (profile->parameters[i].id == parameter_id) return &profile->parameters[i];
    return NULL;
}

static inline const fm_preset_t *fm_preset(uint32_t id, uint32_t preset_id)
{
    const fm_profile_t *profile = fm_profile(id);
    if (profile == NULL) return NULL;
    for (uint32_t i = 0; i < profile->preset_count; ++i)
        if (profile->presets[i].id == preset_id) return &profile->presets[i];
    return NULL;
}

static inline bool fm_profile_operator_count(uint32_t id, uint32_t voice, uint32_t *out)
{
    const fm_profile_t *profile = fm_profile(id);
    if (profile == NULL || out == NULL || voice >= profile->voice_count) return false;
    *out = profile->operators_per_voice[voice];
    return true;
}

static inline bool fm_validate_parameter(uint32_t id, uint32_t parameter_id, uint32_t scope, uint32_t word, uint32_t value)
{
    const fm_profile_t *profile = fm_profile(id);
    const fm_parameter_t *parameter_info = fm_parameter(id, parameter_id);
    if (profile == NULL || parameter_info == NULL || scope != parameter_info->scope_id ||
        value < parameter_info->minimum || value > parameter_info->maximum) return false;
    uint32_t selected = 0;
    (void)fm_profile_operator_count(id, fm_target_voice(word), &selected);
    return fm_validate_target(scope, word, profile->voice_count, selected, profile->patch_operator_count);
}

static inline uint32_t fm_profile_native_register_slot(uint32_t id, uint32_t logical_operator)
{
    switch (id) {
    case 1u: switch (logical_operator) {
        case 0u: return 0u;
        case 1u: return 2u;
        case 2u: return 1u;
        case 3u: return 3u;
        default: return UINT32_C(0xffffffff); }
    default: return UINT32_C(0xffffffff);
    }
}
static inline uint32_t fm_profile_native_key_on_bit(uint32_t id, uint32_t logical_operator)
{
    switch (id) {
    case 1u: switch (logical_operator) {
        case 0u: return 3u;
        case 1u: return 4u;
        case 2u: return 5u;
        case 3u: return 6u;
        default: return UINT32_C(0xffffffff); }
    default: return UINT32_C(0xffffffff);
    }
}
#endif /* FM_CONTRACT_H */
