// CONTRACT FROZEN / HARDWARE NOT INTEGRATED; 请勿手改 / DO NOT EDIT.
// Generated from schema + backend profiles; transport/backend lifecycle is not implemented by this codec.
// Schema version: 1.0.1
// Schema SHA256: 68d7d85ab97942edb128efcb3dffe01bdc50fb98baf9476ab4663d4f22c2cc73

export const FM_SCHEMA_VERSION = "1.0.1";
export const FM_SCHEMA_SHA256 = "68d7d85ab97942edb128efcb3dffe01bdc50fb98baf9476ab4663d4f22c2cc73";

export const FM_ABI_MAJOR = 0x00000001;
export const FM_ABI_MINOR = 0x00000001;
export const FM_CAP_MAGIC = 0x464d4331;
export const FM_SPI_WORD_BASE = 0x00000600;
export const FM_SPI_WINDOW_WORDS = 0x00000100;
export const FM_SPI_WORD_BITS = 0x00000020;
export const FM_TARGET_NONE = 0x0000ffff;
export const FM_MAX_WIRE_INDEX = 0x0000fffe;
export const FM_MAX_DESCRIPTOR_VOICES = 0x00000100;
export const FM_MAX_OPERATORS_PER_VOICE = 0x00000020;
export const FM_MAX_PARAMETERS = 0x00000100;
export const FM_MAX_PRESETS = 0x00000080;
export const FM_MAX_BACKEND_PROFILES = 0x00000010;
export const FM_HTTP_API_MAJOR = 0x00000002;
export const FM_HTTP_API_MINOR = 0x00000000;
export const FM_HTTP_COMMAND_MAX_BYTES = 0x0000005f;
export const FM_PERFORMANCE_LEASE_MS = 0x000004b0;
export const FM_FLAG_READY = 0x00000001;
export const FM_FLAG_SELECTED = 0x00000002;
export const FM_FLAG_MUTED = 0x00000004;
export const FM_FLAG_BUSY = 0x00000008;
export const FM_FLAG_FAULT = 0x00000010;
export const FM_FEATURE_NOTE_ON = 0x00000001;
export const FM_FEATURE_NOTE_OFF = 0x00000002;
export const FM_FEATURE_SET_PITCH = 0x00000004;
export const FM_FEATURE_SET_GAIN_PAN = 0x00000008;
export const FM_FEATURE_SET_PARAMETER = 0x00000010;
export const FM_FEATURE_APPLY_PRESET = 0x00000020;
export const FM_FEATURE_RELEASE_ALL = 0x00000040;
export const FM_FEATURE_PRIORITY_PANIC = 0x00000080;
export const FM_FEATURE_NEXT_NOTE_PARAMETERS = 0x00000100;
export const FM_FEATURE_NATIVE_STEREO_PAN = 0x00000200;
export const FM_ABI_VERSION_WORD = 0x00010001;
export const FM_HTTP_API_VERSION_WORD = 0x00020000;
export const FM_ENGINE_DDS = 0x00000000;
export const FM_ENGINE_KS = 0x00000001;
export const FM_ENGINE_FM = 0x00000002;
export const FM_SCOPE_ENGINE = 0x00000000;
export const FM_SCOPE_VOICE = 0x00000001;
export const FM_SCOPE_VOICE_OPERATOR = 0x00000002;
export const FM_SCOPE_PATCH = 0x00000003;
export const FM_SCOPE_PATCH_OPERATOR = 0x00000004;
export const FM_SPI_OPCODE_SELECT_BACKEND = 0x00000000;
export const FM_SPI_OPCODE_PANIC = 0x00000001;
export const FM_SPI_OPCODE_NOTE_ON = 0x00000002;
export const FM_SPI_OPCODE_NOTE_OFF = 0x00000003;
export const FM_SPI_OPCODE_SET_PITCH = 0x00000004;
export const FM_SPI_OPCODE_SET_GAIN_PAN = 0x00000005;
export const FM_SPI_OPCODE_SET_PARAMETER = 0x00000006;
export const FM_SPI_OPCODE_APPLY_PRESET = 0x00000007;
export const FM_SPI_OPCODE_RELEASE_ALL = 0x00000008;
export const FM_HTTP_OPCODE_CC = 0x00000002;
export const FM_HTTP_OPCODE_KEYS_UPDATE = 0x00000003;
export const FM_HTTP_OPCODE_STOP = 0x00000004;
export const FM_HTTP_OPCODE_ENGINE_SELECT = 0x00000005;
export const FM_HTTP_OPCODE_KS_DECAY = 0x00000006;
export const FM_HTTP_OPCODE_FM_PARAMETER = 0x00000007;
export const FM_HTTP_OPCODE_FM_PRESET = 0x00000008;
export const FM_RESULT_OK = 0x00000000;
export const FM_RESULT_BUSY = 0x00000001;
export const FM_RESULT_UNSUPPORTED = 0x00000002;
export const FM_RESULT_BAD_TARGET = 0x00000003;
export const FM_RESULT_BAD_VALUE = 0x00000004;
export const FM_RESULT_STALE_EPOCH = 0x00000005;
export const FM_RESULT_ABI_MISMATCH = 0x00000006;
export const FM_RESULT_BACKEND_FAULT = 0x00000007;
export const FM_RESULT_CANCELLED = 0x00000008;
export const FM_RESULT_SEQUENCE_CONFLICT = 0x00000009;
export const FM_RESULT_STALE_SEQUENCE = 0x0000000a;
export const FM_RESULT_STALE_SESSION = 0x0000000b;
export const FM_RESULT_STALE_TOPOLOGY = 0x0000000c;
export const FM_RESULT_STALE_VOICE = 0x0000000d;
export const FM_PHASE_UNAVAILABLE = 0x00000000;
export const FM_PHASE_INITIALIZING = 0x00000001;
export const FM_PHASE_READY = 0x00000002;
export const FM_PHASE_SWITCHING = 0x00000003;
export const FM_PHASE_RECOVERING = 0x00000004;
export const FM_PHASE_BLOCKED = 0x00000005;
export const FM_BACKEND_UNBOUND = 0x00000000;
export const FM_BACKEND_JT51_OPM = 0x00000001;
export const FM_BACKEND_MSFA_TWO_OP_REFERENCE = 0x00000002;
export const FM_REG_CAP_MAGIC = 0x00000600;
export const FM_REG_ABI_VERSION = 0x00000601;
export const FM_REG_BACKEND_ID = 0x00000602;
export const FM_REG_VOICE_COUNT = 0x00000603;
export const FM_REG_MAX_OPERATOR_COUNT = 0x00000604;
export const FM_REG_FEATURES = 0x00000605;
export const FM_REG_EPOCH = 0x00000606;
export const FM_REG_PHASE = 0x00000607;
export const FM_REG_ACCEPTED_SEQUENCE = 0x00000608;
export const FM_REG_COMPLETED_SEQUENCE = 0x00000609;
export const FM_REG_RESULT_SEQUENCE = 0x0000060a;
export const FM_REG_RESULT = 0x0000060b;
export const FM_REG_TOPOLOGY_GENERATION = 0x0000060c;
export const FM_REG_STATUS_GENERATION = 0x0000060d;
export const FM_REG_FLAGS = 0x0000060e;
export const FM_REG_SESSION = 0x0000060f;
export const FM_REG_CMD_OPCODE_ID = 0x00000610;
export const FM_REG_CMD_SCOPE_ID = 0x00000611;
export const FM_REG_CMD_TARGET_WORD = 0x00000612;
export const FM_REG_CMD_PARAMETER_ID = 0x00000613;
export const FM_REG_CMD_VALUE_U32 = 0x00000614;
export const FM_REG_CMD_ARG0_U32 = 0x00000615;
export const FM_REG_CMD_ARG1_U32 = 0x00000616;
export const FM_REG_CMD_SESSION_ID = 0x00000617;
export const FM_REG_CMD_ENGINE_EPOCH = 0x00000618;
export const FM_REG_CMD_TOPOLOGY_GENERATION = 0x00000619;
export const FM_REG_CMD_VOICE_GENERATION = 0x0000061a;
export const FM_REG_CMD_SEQUENCE_ID = 0x0000061b;
export const FM_REG_CMD_COMMIT = 0x0000061c;
export const FM_REG_PANIC_SEQUENCE = 0x00000630;
export const FM_REG_PANIC_ACCEPTED_SEQUENCE = 0x00000631;
export const FM_REG_PANIC_COMPLETED_SEQUENCE = 0x00000632;
export const FM_REG_SOURCE_RATE_NUMERATOR = 0x00000640;
export const FM_REG_SOURCE_RATE_DENOMINATOR = 0x00000641;
export const FM_REG_OUTPUT_RATE_NUMERATOR = 0x00000642;
export const FM_REG_OUTPUT_RATE_DENOMINATOR = 0x00000643;
export const FM_REG_PROFILE_VERSION = 0x00000644;
export const FM_REG_PARAMETER_COUNT = 0x00000645;
export const FM_REG_PRESET_COUNT = 0x00000646;
export const FM_REG_VOICE_QUERY_INDEX = 0x00000647;
export const FM_REG_VOICE_QUERY_GENERATION = 0x00000648;
export const FM_REG_VOICE_QUERY_OPERATOR_COUNT = 0x00000649;
export const FM_REG_PROFILE_SHA256_0 = 0x00000660;
export const FM_REG_PROFILE_SHA256_1 = 0x00000661;
export const FM_REG_PROFILE_SHA256_2 = 0x00000662;
export const FM_REG_PROFILE_SHA256_3 = 0x00000663;
export const FM_REG_PROFILE_SHA256_4 = 0x00000664;
export const FM_REG_PROFILE_SHA256_5 = 0x00000665;
export const FM_REG_PROFILE_SHA256_6 = 0x00000666;
export const FM_REG_PROFILE_SHA256_7 = 0x00000667;
export const FM_REG_RESULT_SESSION = 0x00000620;
export const FM_REG_RESULT_EPOCH = 0x00000621;
export const FM_REG_ACCEPTED_SESSION = 0x00000622;
export const FM_REG_ACCEPTED_EPOCH = 0x00000623;
export const FM_REG_COMPLETED_SESSION = 0x00000624;
export const FM_REG_COMPLETED_EPOCH = 0x00000625;
export const FM_REG_PANIC_SESSION = 0x00000626;
export const FM_REG_PANIC_EPOCH = 0x00000627;
export const FM_REG_PANIC_RESULT_SEQUENCE = 0x00000633;
export const FM_REG_PANIC_RESULT = 0x00000634;
export const FM_REG_PANIC_RESULT_SESSION = 0x00000635;
export const FM_REG_PANIC_RESULT_EPOCH = 0x00000636;
export const FM_COMMAND_WORD_COUNT = 0x0000000c;
export const FM_COMMAND_BYTE_COUNT = 0x00000030;
export const FM_CMD_WORD_OPCODE_ID = 0x00000000;
export const FM_CMD_WORD_SCOPE_ID = 0x00000001;
export const FM_CMD_WORD_TARGET_WORD = 0x00000002;
export const FM_CMD_WORD_PARAMETER_ID = 0x00000003;
export const FM_CMD_WORD_VALUE_U32 = 0x00000004;
export const FM_CMD_WORD_ARG0_U32 = 0x00000005;
export const FM_CMD_WORD_ARG1_U32 = 0x00000006;
export const FM_CMD_WORD_SESSION_ID = 0x00000007;
export const FM_CMD_WORD_ENGINE_EPOCH = 0x00000008;
export const FM_CMD_WORD_TOPOLOGY_GENERATION = 0x00000009;
export const FM_CMD_WORD_VOICE_GENERATION = 0x0000000a;
export const FM_CMD_WORD_SEQUENCE_ID = 0x0000000b;
export const FM_UNIT_CHIP_CODE = 0x00000000;
export const FM_APPLY_TIME_NEXT_NOTE = 0x00000000;
export const FM_VALUE_TYPE_U32 = 0x00000000;

function isU32(value) {
  return Number.isInteger(value) && value >= 0 && value <= 0xffffffff;
}

export function packTarget(voice, operator) {
  if (!isU32(voice) || voice > 0xffff ||
      !isU32(operator) || operator > 0xffff) {
    throw new RangeError("target fields must be unsigned 16-bit integers");
  }
  return (voice * (2 ** 16) + operator * (2 ** 0)) >>> 0;
}

export function unpackTarget(word) {
  if (!isU32(word)) {
    throw new RangeError("target word must be a uint32 integer");
  }
  return { voice: (word >>> 16) & 0xffff,
           operator: (word >>> 0) & 0xffff };
}

export function validateTarget(scope, word, voice_count, selected_operator_count, patch_operator_count) {
  if (![scope, word, voice_count, selected_operator_count, patch_operator_count].every(isU32)) {
    return false;
  }
  if (voice_count < 1 || voice_count > FM_MAX_DESCRIPTOR_VOICES) return false;
  if (selected_operator_count < 0 || selected_operator_count > FM_MAX_OPERATORS_PER_VOICE) return false;
  if (patch_operator_count < 0 || patch_operator_count > FM_MAX_OPERATORS_PER_VOICE) return false;
  const { voice, operator } = unpackTarget(word);
  if (scope === FM_SCOPE_ENGINE) return (voice === FM_TARGET_NONE) && (operator === FM_TARGET_NONE);
  if (scope === FM_SCOPE_VOICE) return (voice < voice_count) && (operator === FM_TARGET_NONE);
  if (scope === FM_SCOPE_VOICE_OPERATOR) return (voice < voice_count) && (operator < selected_operator_count);
  if (scope === FM_SCOPE_PATCH) return (voice === FM_TARGET_NONE) && (operator === FM_TARGET_NONE);
  if (scope === FM_SCOPE_PATCH_OPERATOR) return (voice === FM_TARGET_NONE) && (operator < patch_operator_count);
  return false;
}

export const FM_PROFILE_JT51_OPM_VOICE_COUNT = 8;
export const FM_PROFILE_JT51_OPM_PATCH_OPERATOR_COUNT = 4;
export const FM_PROFILE_JT51_OPM_MAX_OPERATOR_COUNT = 4;
export const FM_PROFILE_MSFA_TWO_OP_REFERENCE_VOICE_COUNT = 8;
export const FM_PROFILE_MSFA_TWO_OP_REFERENCE_PATCH_OPERATOR_COUNT = 2;
export const FM_PROFILE_MSFA_TWO_OP_REFERENCE_MAX_OPERATOR_COUNT = 2;
export const FM_PROFILES_SHA256 = "51a6af55646fe2adb54bf4f9f3d06e2d262bd830ec5f30f80c0e5bfa2eadf49f";
export const FM_CONTRACT_SHA256 = "86aa104f60f3e8fc586a9f64052032e45b24d7148f43610b3ef0e86c9e5fed38";
export const FM_COMMAND_FIELDS = Object.freeze(["opcode_id", "scope_id", "target_word", "parameter_id", "value_u32", "arg0_u32", "arg1_u32", "session_id", "engine_epoch", "topology_generation", "voice_generation", "sequence_id"]);
export const FM_PROFILE_IDS = Object.freeze([1, 2]);
export const FM_PROFILES_SHA256_WORDS = Object.freeze([1369878357, 1685054125, 3041654009, 4090523181, 640407600, 3965661432, 202267642, 783152287]);
function freezeDeep(value) {
  if (value && typeof value === "object") {
    for (const member of Object.values(value)) freezeDeep(member);
    Object.freeze(value);
  }
  return value;
}
const PROFILES = freezeDeep({"1":{"id":1,"key":"jt51_opm","backend_id":1,"profile_version":65536,"role":"implementation_target","runtime_available":false,"voice_count":8,"operators_per_voice":[4,4,4,4,4,4,4,4],"patch_operator_count":4,"operator_labels":["M1","C1","M2","C2"],"adapter_mapping":{"register_slots":[0,2,1,3],"key_on_bits":[3,4,5,6]},"parameters":[{"id":1,"name":"algorithm","minimum":0,"maximum":7,"scope":"PATCH","unit":"chip_code","apply_time":"next_note","value_type":"u32","scope_id":3,"unit_id":0,"apply_time_id":0,"value_type_id":0},{"id":2,"name":"feedback","minimum":0,"maximum":7,"scope":"PATCH","unit":"chip_code","apply_time":"next_note","value_type":"u32","scope_id":3,"unit_id":0,"apply_time_id":0,"value_type_id":0},{"id":16,"name":"multiplier","minimum":0,"maximum":15,"scope":"PATCH_OPERATOR","unit":"chip_code","apply_time":"next_note","value_type":"u32","scope_id":4,"unit_id":0,"apply_time_id":0,"value_type_id":0},{"id":17,"name":"total_level","minimum":0,"maximum":127,"scope":"PATCH_OPERATOR","unit":"chip_code","apply_time":"next_note","value_type":"u32","scope_id":4,"unit_id":0,"apply_time_id":0,"value_type_id":0},{"id":18,"name":"attack_rate","minimum":0,"maximum":31,"scope":"PATCH_OPERATOR","unit":"chip_code","apply_time":"next_note","value_type":"u32","scope_id":4,"unit_id":0,"apply_time_id":0,"value_type_id":0},{"id":19,"name":"decay1_rate","minimum":0,"maximum":31,"scope":"PATCH_OPERATOR","unit":"chip_code","apply_time":"next_note","value_type":"u32","scope_id":4,"unit_id":0,"apply_time_id":0,"value_type_id":0},{"id":20,"name":"decay2_rate","minimum":0,"maximum":31,"scope":"PATCH_OPERATOR","unit":"chip_code","apply_time":"next_note","value_type":"u32","scope_id":4,"unit_id":0,"apply_time_id":0,"value_type_id":0},{"id":21,"name":"sustain_level","minimum":0,"maximum":15,"scope":"PATCH_OPERATOR","unit":"chip_code","apply_time":"next_note","value_type":"u32","scope_id":4,"unit_id":0,"apply_time_id":0,"value_type_id":0},{"id":22,"name":"release_rate","minimum":0,"maximum":15,"scope":"PATCH_OPERATOR","unit":"chip_code","apply_time":"next_note","value_type":"u32","scope_id":4,"unit_id":0,"apply_time_id":0,"value_type_id":0},{"id":23,"name":"detune1","minimum":0,"maximum":7,"scope":"PATCH_OPERATOR","unit":"chip_code","apply_time":"next_note","value_type":"u32","scope_id":4,"unit_id":0,"apply_time_id":0,"value_type_id":0},{"id":24,"name":"detune2","minimum":0,"maximum":3,"scope":"PATCH_OPERATOR","unit":"chip_code","apply_time":"next_note","value_type":"u32","scope_id":4,"unit_id":0,"apply_time_id":0,"value_type_id":0},{"id":25,"name":"rate_scaling","minimum":0,"maximum":3,"scope":"PATCH_OPERATOR","unit":"chip_code","apply_time":"next_note","value_type":"u32","scope_id":4,"unit_id":0,"apply_time_id":0,"value_type_id":0},{"id":26,"name":"am_enable","minimum":0,"maximum":1,"scope":"PATCH_OPERATOR","unit":"chip_code","apply_time":"next_note","value_type":"u32","scope_id":4,"unit_id":0,"apply_time_id":0,"value_type_id":0}],"presets":[{"id":0,"name":"diagnostic_sine","description":"algorithm 7, one audible M1 carrier; diagnostic patch","key_on_logical_mask":1,"values":[{"parameter_id":1,"operator_index":65535,"value_u32":7},{"parameter_id":2,"operator_index":65535,"value_u32":0},{"parameter_id":16,"operator_index":0,"value_u32":1},{"parameter_id":17,"operator_index":0,"value_u32":32},{"parameter_id":18,"operator_index":0,"value_u32":31},{"parameter_id":19,"operator_index":0,"value_u32":0},{"parameter_id":20,"operator_index":0,"value_u32":0},{"parameter_id":21,"operator_index":0,"value_u32":0},{"parameter_id":22,"operator_index":0,"value_u32":15},{"parameter_id":23,"operator_index":0,"value_u32":0},{"parameter_id":24,"operator_index":0,"value_u32":0},{"parameter_id":25,"operator_index":0,"value_u32":0},{"parameter_id":26,"operator_index":0,"value_u32":0},{"parameter_id":16,"operator_index":1,"value_u32":1},{"parameter_id":17,"operator_index":1,"value_u32":127},{"parameter_id":18,"operator_index":1,"value_u32":31},{"parameter_id":19,"operator_index":1,"value_u32":0},{"parameter_id":20,"operator_index":1,"value_u32":0},{"parameter_id":21,"operator_index":1,"value_u32":0},{"parameter_id":22,"operator_index":1,"value_u32":15},{"parameter_id":23,"operator_index":1,"value_u32":0},{"parameter_id":24,"operator_index":1,"value_u32":0},{"parameter_id":25,"operator_index":1,"value_u32":0},{"parameter_id":26,"operator_index":1,"value_u32":0},{"parameter_id":16,"operator_index":2,"value_u32":1},{"parameter_id":17,"operator_index":2,"value_u32":127},{"parameter_id":18,"operator_index":2,"value_u32":31},{"parameter_id":19,"operator_index":2,"value_u32":0},{"parameter_id":20,"operator_index":2,"value_u32":0},{"parameter_id":21,"operator_index":2,"value_u32":0},{"parameter_id":22,"operator_index":2,"value_u32":15},{"parameter_id":23,"operator_index":2,"value_u32":0},{"parameter_id":24,"operator_index":2,"value_u32":0},{"parameter_id":25,"operator_index":2,"value_u32":0},{"parameter_id":26,"operator_index":2,"value_u32":0},{"parameter_id":16,"operator_index":3,"value_u32":1},{"parameter_id":17,"operator_index":3,"value_u32":127},{"parameter_id":18,"operator_index":3,"value_u32":31},{"parameter_id":19,"operator_index":3,"value_u32":0},{"parameter_id":20,"operator_index":3,"value_u32":0},{"parameter_id":21,"operator_index":3,"value_u32":0},{"parameter_id":22,"operator_index":3,"value_u32":15},{"parameter_id":23,"operator_index":3,"value_u32":0},{"parameter_id":24,"operator_index":3,"value_u32":0},{"parameter_id":25,"operator_index":3,"value_u32":0},{"parameter_id":26,"operator_index":3,"value_u32":0}]},{"id":1,"name":"two_op_keys","description":"designed two-operator keys patch; not a GM/Yamaha preset","key_on_logical_mask":3,"values":[{"parameter_id":1,"operator_index":65535,"value_u32":4},{"parameter_id":2,"operator_index":65535,"value_u32":0},{"parameter_id":16,"operator_index":0,"value_u32":1},{"parameter_id":17,"operator_index":0,"value_u32":28},{"parameter_id":18,"operator_index":0,"value_u32":28},{"parameter_id":19,"operator_index":0,"value_u32":10},{"parameter_id":20,"operator_index":0,"value_u32":2},{"parameter_id":21,"operator_index":0,"value_u32":5},{"parameter_id":22,"operator_index":0,"value_u32":14},{"parameter_id":23,"operator_index":0,"value_u32":0},{"parameter_id":24,"operator_index":0,"value_u32":0},{"parameter_id":25,"operator_index":0,"value_u32":0},{"parameter_id":26,"operator_index":0,"value_u32":0},{"parameter_id":16,"operator_index":1,"value_u32":1},{"parameter_id":17,"operator_index":1,"value_u32":36},{"parameter_id":18,"operator_index":1,"value_u32":25},{"parameter_id":19,"operator_index":1,"value_u32":8},{"parameter_id":20,"operator_index":1,"value_u32":2},{"parameter_id":21,"operator_index":1,"value_u32":4},{"parameter_id":22,"operator_index":1,"value_u32":14},{"parameter_id":23,"operator_index":1,"value_u32":0},{"parameter_id":24,"operator_index":1,"value_u32":0},{"parameter_id":25,"operator_index":1,"value_u32":0},{"parameter_id":26,"operator_index":1,"value_u32":0},{"parameter_id":16,"operator_index":2,"value_u32":1},{"parameter_id":17,"operator_index":2,"value_u32":127},{"parameter_id":18,"operator_index":2,"value_u32":31},{"parameter_id":19,"operator_index":2,"value_u32":0},{"parameter_id":20,"operator_index":2,"value_u32":0},{"parameter_id":21,"operator_index":2,"value_u32":0},{"parameter_id":22,"operator_index":2,"value_u32":14},{"parameter_id":23,"operator_index":2,"value_u32":0},{"parameter_id":24,"operator_index":2,"value_u32":0},{"parameter_id":25,"operator_index":2,"value_u32":0},{"parameter_id":26,"operator_index":2,"value_u32":0},{"parameter_id":16,"operator_index":3,"value_u32":1},{"parameter_id":17,"operator_index":3,"value_u32":127},{"parameter_id":18,"operator_index":3,"value_u32":31},{"parameter_id":19,"operator_index":3,"value_u32":0},{"parameter_id":20,"operator_index":3,"value_u32":0},{"parameter_id":21,"operator_index":3,"value_u32":0},{"parameter_id":22,"operator_index":3,"value_u32":14},{"parameter_id":23,"operator_index":3,"value_u32":0},{"parameter_id":24,"operator_index":3,"value_u32":0},{"parameter_id":25,"operator_index":3,"value_u32":0},{"parameter_id":26,"operator_index":3,"value_u32":0}]},{"id":2,"name":"four_op_bell","description":"designed four-operator bell-like patch; not a DX7/GM preset","key_on_logical_mask":15,"values":[{"parameter_id":1,"operator_index":65535,"value_u32":0},{"parameter_id":2,"operator_index":65535,"value_u32":2},{"parameter_id":16,"operator_index":0,"value_u32":1},{"parameter_id":17,"operator_index":0,"value_u32":26},{"parameter_id":18,"operator_index":0,"value_u32":31},{"parameter_id":19,"operator_index":0,"value_u32":12},{"parameter_id":20,"operator_index":0,"value_u32":5},{"parameter_id":21,"operator_index":0,"value_u32":8},{"parameter_id":22,"operator_index":0,"value_u32":15},{"parameter_id":23,"operator_index":0,"value_u32":0},{"parameter_id":24,"operator_index":0,"value_u32":0},{"parameter_id":25,"operator_index":0,"value_u32":0},{"parameter_id":26,"operator_index":0,"value_u32":0},{"parameter_id":16,"operator_index":1,"value_u32":3},{"parameter_id":17,"operator_index":1,"value_u32":30},{"parameter_id":18,"operator_index":1,"value_u32":31},{"parameter_id":19,"operator_index":1,"value_u32":8},{"parameter_id":20,"operator_index":1,"value_u32":2},{"parameter_id":21,"operator_index":1,"value_u32":4},{"parameter_id":22,"operator_index":1,"value_u32":15},{"parameter_id":23,"operator_index":1,"value_u32":0},{"parameter_id":24,"operator_index":1,"value_u32":0},{"parameter_id":25,"operator_index":1,"value_u32":0},{"parameter_id":26,"operator_index":1,"value_u32":0},{"parameter_id":16,"operator_index":2,"value_u32":2},{"parameter_id":17,"operator_index":2,"value_u32":42},{"parameter_id":18,"operator_index":2,"value_u32":31},{"parameter_id":19,"operator_index":2,"value_u32":9},{"parameter_id":20,"operator_index":2,"value_u32":3},{"parameter_id":21,"operator_index":2,"value_u32":6},{"parameter_id":22,"operator_index":2,"value_u32":15},{"parameter_id":23,"operator_index":2,"value_u32":0},{"parameter_id":24,"operator_index":2,"value_u32":0},{"parameter_id":25,"operator_index":2,"value_u32":0},{"parameter_id":26,"operator_index":2,"value_u32":0},{"parameter_id":16,"operator_index":3,"value_u32":1},{"parameter_id":17,"operator_index":3,"value_u32":32},{"parameter_id":18,"operator_index":3,"value_u32":31},{"parameter_id":19,"operator_index":3,"value_u32":8},{"parameter_id":20,"operator_index":3,"value_u32":3},{"parameter_id":21,"operator_index":3,"value_u32":5},{"parameter_id":22,"operator_index":3,"value_u32":15},{"parameter_id":23,"operator_index":3,"value_u32":0},{"parameter_id":24,"operator_index":3,"value_u32":0},{"parameter_id":25,"operator_index":3,"value_u32":0},{"parameter_id":26,"operator_index":3,"value_u32":0}]}],"source":{"upstream":"https://github.com/jotego/jt51","commit":"985a573dcfc1ff135553a39f7eae21d18ba57cbe","preset_source":"development/tang_mega_138k_synth/experiments/audio/fm_experiments/jt51/run_native.py","native_arrays":"M1/M2/C1/C2; stored values remapped to logical M1/C1/M2/C2","pitch_range":"adapter validation required; no full MIDI acoustic range claim"},"profile_sha256":"51a6af55646fe2adb54bf4f9f3d06e2d262bd830ec5f30f80c0e5bfa2eadf49f","parameter_count":13,"preset_count":3},"2":{"id":2,"key":"msfa_two_op_reference","backend_id":2,"profile_version":65536,"role":"offline_reference","runtime_available":false,"voice_count":8,"operators_per_voice":[2,2,2,2,2,2,2,2],"patch_operator_count":2,"operator_labels":["modulator","carrier"],"parameters":[],"presets":[],"source":{"upstream":"https://github.com/google/music-synthesizer-for-android","commit":"f67d41d313b7dc85f6fb99e79e515cc9d208cfff","note":"offline reference only; not current runtime FM backend"},"profile_sha256":"51a6af55646fe2adb54bf4f9f3d06e2d262bd830ec5f30f80c0e5bfa2eadf49f","parameter_count":0,"preset_count":0}});
export function abiCompatible(word) { return isU32(word) && word === FM_ABI_VERSION_WORD; }
export function serializeCommand(command) {
  if (command === null || typeof command !== "object" || Array.isArray(command) ||
      Object.keys(command).length !== FM_COMMAND_WORD_COUNT ||
      !FM_COMMAND_FIELDS.every(name => Object.hasOwn(command, name) && isU32(command[name]))) {
    throw new RangeError("command must contain exactly the uint32 schema fields");
  }
  return FM_COMMAND_FIELDS.map(name => command[name]);
}
export function deserializeCommand(words) {
  if (!Array.isArray(words) || words.length !== FM_COMMAND_WORD_COUNT ||
      !Array.from(words).every(isU32)) throw new RangeError("command words require exact uint32 length");
  return Object.fromEntries(FM_COMMAND_FIELDS.map((name, index) => [name, words[index]]));
}
export function getProfile(id) { return isU32(id) ? (PROFILES[id] ?? null) : null; }
export function getParameter(id, parameterId) {
  return isU32(parameterId) ? (getProfile(id)?.parameters.find(p => p.id === parameterId) ?? null) : null;
}
export function getPreset(id, presetId) {
  return isU32(presetId) ? (getProfile(id)?.presets.find(p => p.id === presetId) ?? null) : null;
}
export function profileOperatorCount(id, voice) {
  const profile = getProfile(id);
  return profile && isU32(voice) && voice < profile.voice_count ? profile.operators_per_voice[voice] : null;
}
function profileNativeMapping(id, logicalOperator, kind) {
  if (!isU32(id) || !isU32(logicalOperator)) return 0xffffffff;
  const mapping = getProfile(id)?.adapter_mapping?.[kind];
  return mapping && logicalOperator < mapping.length ? mapping[logicalOperator] : 0xffffffff;
}
export function profileNativeRegisterSlot(id, logicalOperator) { return profileNativeMapping(id, logicalOperator, "register_slots"); }
export function profileNativeKeyOnBit(id, logicalOperator) { return profileNativeMapping(id, logicalOperator, "key_on_bits"); }
export function validateParameter(id, parameterId, scope, word, value) {
  if (![id, parameterId, scope, word, value].every(isU32)) return false;
  const profile = getProfile(id), parameter = getParameter(id, parameterId);
  if (!profile || !parameter || parameter.scope_id !== scope) return false;
  const selected = profileOperatorCount(id, unpackTarget(word).voice) ?? 0;
  return value >= parameter.minimum && value <= parameter.maximum &&
         validateTarget(scope, word, profile.voice_count, selected, profile.patch_operator_count);
}
