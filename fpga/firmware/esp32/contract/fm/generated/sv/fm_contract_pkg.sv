// CONTRACT FROZEN / HARDWARE NOT INTEGRATED; 请勿手改 / DO NOT EDIT.
// Generated from schema + backend profiles; transport/backend lifecycle is not implemented by this codec.
// Schema version: 1.0.1
// Schema SHA256: 68d7d85ab97942edb128efcb3dffe01bdc50fb98baf9476ab4663d4f22c2cc73

package fm_contract_pkg;
  localparam logic [39:0] FM_SCHEMA_VERSION = "1.0.1";
  localparam logic [255:0] FM_SCHEMA_SHA256 = 256'h68d7d85ab97942edb128efcb3dffe01bdc50fb98baf9476ab4663d4f22c2cc73;

  localparam logic [31:0] FM_ABI_MAJOR = 32'h00000001;
  localparam logic [31:0] FM_ABI_MINOR = 32'h00000001;
  localparam logic [31:0] FM_CAP_MAGIC = 32'h464d4331;
  localparam logic [31:0] FM_SPI_WORD_BASE = 32'h00000600;
  localparam logic [31:0] FM_SPI_WINDOW_WORDS = 32'h00000100;
  localparam logic [31:0] FM_SPI_WORD_BITS = 32'h00000020;
  localparam logic [31:0] FM_TARGET_NONE = 32'h0000ffff;
  localparam logic [31:0] FM_MAX_WIRE_INDEX = 32'h0000fffe;
  localparam logic [31:0] FM_MAX_DESCRIPTOR_VOICES = 32'h00000100;
  localparam logic [31:0] FM_MAX_OPERATORS_PER_VOICE = 32'h00000020;
  localparam logic [31:0] FM_MAX_PARAMETERS = 32'h00000100;
  localparam logic [31:0] FM_MAX_PRESETS = 32'h00000080;
  localparam logic [31:0] FM_MAX_BACKEND_PROFILES = 32'h00000010;
  localparam logic [31:0] FM_HTTP_API_MAJOR = 32'h00000002;
  localparam logic [31:0] FM_HTTP_API_MINOR = 32'h00000000;
  localparam logic [31:0] FM_HTTP_COMMAND_MAX_BYTES = 32'h0000005f;
  localparam logic [31:0] FM_PERFORMANCE_LEASE_MS = 32'h000004b0;
  localparam logic [31:0] FM_FLAG_READY = 32'h00000001;
  localparam logic [31:0] FM_FLAG_SELECTED = 32'h00000002;
  localparam logic [31:0] FM_FLAG_MUTED = 32'h00000004;
  localparam logic [31:0] FM_FLAG_BUSY = 32'h00000008;
  localparam logic [31:0] FM_FLAG_FAULT = 32'h00000010;
  localparam logic [31:0] FM_FEATURE_NOTE_ON = 32'h00000001;
  localparam logic [31:0] FM_FEATURE_NOTE_OFF = 32'h00000002;
  localparam logic [31:0] FM_FEATURE_SET_PITCH = 32'h00000004;
  localparam logic [31:0] FM_FEATURE_SET_GAIN_PAN = 32'h00000008;
  localparam logic [31:0] FM_FEATURE_SET_PARAMETER = 32'h00000010;
  localparam logic [31:0] FM_FEATURE_APPLY_PRESET = 32'h00000020;
  localparam logic [31:0] FM_FEATURE_RELEASE_ALL = 32'h00000040;
  localparam logic [31:0] FM_FEATURE_PRIORITY_PANIC = 32'h00000080;
  localparam logic [31:0] FM_FEATURE_NEXT_NOTE_PARAMETERS = 32'h00000100;
  localparam logic [31:0] FM_FEATURE_NATIVE_STEREO_PAN = 32'h00000200;
  localparam logic [31:0] FM_ABI_VERSION_WORD = 32'h00010001;
  localparam logic [31:0] FM_HTTP_API_VERSION_WORD = 32'h00020000;
  localparam logic [31:0] FM_ENGINE_DDS = 32'h00000000;
  localparam logic [31:0] FM_ENGINE_KS = 32'h00000001;
  localparam logic [31:0] FM_ENGINE_FM = 32'h00000002;
  localparam logic [31:0] FM_SCOPE_ENGINE = 32'h00000000;
  localparam logic [31:0] FM_SCOPE_VOICE = 32'h00000001;
  localparam logic [31:0] FM_SCOPE_VOICE_OPERATOR = 32'h00000002;
  localparam logic [31:0] FM_SCOPE_PATCH = 32'h00000003;
  localparam logic [31:0] FM_SCOPE_PATCH_OPERATOR = 32'h00000004;
  localparam logic [31:0] FM_SPI_OPCODE_SELECT_BACKEND = 32'h00000000;
  localparam logic [31:0] FM_SPI_OPCODE_PANIC = 32'h00000001;
  localparam logic [31:0] FM_SPI_OPCODE_NOTE_ON = 32'h00000002;
  localparam logic [31:0] FM_SPI_OPCODE_NOTE_OFF = 32'h00000003;
  localparam logic [31:0] FM_SPI_OPCODE_SET_PITCH = 32'h00000004;
  localparam logic [31:0] FM_SPI_OPCODE_SET_GAIN_PAN = 32'h00000005;
  localparam logic [31:0] FM_SPI_OPCODE_SET_PARAMETER = 32'h00000006;
  localparam logic [31:0] FM_SPI_OPCODE_APPLY_PRESET = 32'h00000007;
  localparam logic [31:0] FM_SPI_OPCODE_RELEASE_ALL = 32'h00000008;
  localparam logic [31:0] FM_HTTP_OPCODE_CC = 32'h00000002;
  localparam logic [31:0] FM_HTTP_OPCODE_KEYS_UPDATE = 32'h00000003;
  localparam logic [31:0] FM_HTTP_OPCODE_STOP = 32'h00000004;
  localparam logic [31:0] FM_HTTP_OPCODE_ENGINE_SELECT = 32'h00000005;
  localparam logic [31:0] FM_HTTP_OPCODE_KS_DECAY = 32'h00000006;
  localparam logic [31:0] FM_HTTP_OPCODE_FM_PARAMETER = 32'h00000007;
  localparam logic [31:0] FM_HTTP_OPCODE_FM_PRESET = 32'h00000008;
  localparam logic [31:0] FM_RESULT_OK = 32'h00000000;
  localparam logic [31:0] FM_RESULT_BUSY = 32'h00000001;
  localparam logic [31:0] FM_RESULT_UNSUPPORTED = 32'h00000002;
  localparam logic [31:0] FM_RESULT_BAD_TARGET = 32'h00000003;
  localparam logic [31:0] FM_RESULT_BAD_VALUE = 32'h00000004;
  localparam logic [31:0] FM_RESULT_STALE_EPOCH = 32'h00000005;
  localparam logic [31:0] FM_RESULT_ABI_MISMATCH = 32'h00000006;
  localparam logic [31:0] FM_RESULT_BACKEND_FAULT = 32'h00000007;
  localparam logic [31:0] FM_RESULT_CANCELLED = 32'h00000008;
  localparam logic [31:0] FM_RESULT_SEQUENCE_CONFLICT = 32'h00000009;
  localparam logic [31:0] FM_RESULT_STALE_SEQUENCE = 32'h0000000a;
  localparam logic [31:0] FM_RESULT_STALE_SESSION = 32'h0000000b;
  localparam logic [31:0] FM_RESULT_STALE_TOPOLOGY = 32'h0000000c;
  localparam logic [31:0] FM_RESULT_STALE_VOICE = 32'h0000000d;
  localparam logic [31:0] FM_PHASE_UNAVAILABLE = 32'h00000000;
  localparam logic [31:0] FM_PHASE_INITIALIZING = 32'h00000001;
  localparam logic [31:0] FM_PHASE_READY = 32'h00000002;
  localparam logic [31:0] FM_PHASE_SWITCHING = 32'h00000003;
  localparam logic [31:0] FM_PHASE_RECOVERING = 32'h00000004;
  localparam logic [31:0] FM_PHASE_BLOCKED = 32'h00000005;
  localparam logic [31:0] FM_BACKEND_UNBOUND = 32'h00000000;
  localparam logic [31:0] FM_BACKEND_JT51_OPM = 32'h00000001;
  localparam logic [31:0] FM_BACKEND_MSFA_TWO_OP_REFERENCE = 32'h00000002;
  localparam logic [31:0] FM_REG_CAP_MAGIC = 32'h00000600;
  localparam logic [31:0] FM_REG_ABI_VERSION = 32'h00000601;
  localparam logic [31:0] FM_REG_BACKEND_ID = 32'h00000602;
  localparam logic [31:0] FM_REG_VOICE_COUNT = 32'h00000603;
  localparam logic [31:0] FM_REG_MAX_OPERATOR_COUNT = 32'h00000604;
  localparam logic [31:0] FM_REG_FEATURES = 32'h00000605;
  localparam logic [31:0] FM_REG_EPOCH = 32'h00000606;
  localparam logic [31:0] FM_REG_PHASE = 32'h00000607;
  localparam logic [31:0] FM_REG_ACCEPTED_SEQUENCE = 32'h00000608;
  localparam logic [31:0] FM_REG_COMPLETED_SEQUENCE = 32'h00000609;
  localparam logic [31:0] FM_REG_RESULT_SEQUENCE = 32'h0000060a;
  localparam logic [31:0] FM_REG_RESULT = 32'h0000060b;
  localparam logic [31:0] FM_REG_TOPOLOGY_GENERATION = 32'h0000060c;
  localparam logic [31:0] FM_REG_STATUS_GENERATION = 32'h0000060d;
  localparam logic [31:0] FM_REG_FLAGS = 32'h0000060e;
  localparam logic [31:0] FM_REG_SESSION = 32'h0000060f;
  localparam logic [31:0] FM_REG_CMD_OPCODE_ID = 32'h00000610;
  localparam logic [31:0] FM_REG_CMD_SCOPE_ID = 32'h00000611;
  localparam logic [31:0] FM_REG_CMD_TARGET_WORD = 32'h00000612;
  localparam logic [31:0] FM_REG_CMD_PARAMETER_ID = 32'h00000613;
  localparam logic [31:0] FM_REG_CMD_VALUE_U32 = 32'h00000614;
  localparam logic [31:0] FM_REG_CMD_ARG0_U32 = 32'h00000615;
  localparam logic [31:0] FM_REG_CMD_ARG1_U32 = 32'h00000616;
  localparam logic [31:0] FM_REG_CMD_SESSION_ID = 32'h00000617;
  localparam logic [31:0] FM_REG_CMD_ENGINE_EPOCH = 32'h00000618;
  localparam logic [31:0] FM_REG_CMD_TOPOLOGY_GENERATION = 32'h00000619;
  localparam logic [31:0] FM_REG_CMD_VOICE_GENERATION = 32'h0000061a;
  localparam logic [31:0] FM_REG_CMD_SEQUENCE_ID = 32'h0000061b;
  localparam logic [31:0] FM_REG_CMD_COMMIT = 32'h0000061c;
  localparam logic [31:0] FM_REG_PANIC_SEQUENCE = 32'h00000630;
  localparam logic [31:0] FM_REG_PANIC_ACCEPTED_SEQUENCE = 32'h00000631;
  localparam logic [31:0] FM_REG_PANIC_COMPLETED_SEQUENCE = 32'h00000632;
  localparam logic [31:0] FM_REG_SOURCE_RATE_NUMERATOR = 32'h00000640;
  localparam logic [31:0] FM_REG_SOURCE_RATE_DENOMINATOR = 32'h00000641;
  localparam logic [31:0] FM_REG_OUTPUT_RATE_NUMERATOR = 32'h00000642;
  localparam logic [31:0] FM_REG_OUTPUT_RATE_DENOMINATOR = 32'h00000643;
  localparam logic [31:0] FM_REG_PROFILE_VERSION = 32'h00000644;
  localparam logic [31:0] FM_REG_PARAMETER_COUNT = 32'h00000645;
  localparam logic [31:0] FM_REG_PRESET_COUNT = 32'h00000646;
  localparam logic [31:0] FM_REG_VOICE_QUERY_INDEX = 32'h00000647;
  localparam logic [31:0] FM_REG_VOICE_QUERY_GENERATION = 32'h00000648;
  localparam logic [31:0] FM_REG_VOICE_QUERY_OPERATOR_COUNT = 32'h00000649;
  localparam logic [31:0] FM_REG_PROFILE_SHA256_0 = 32'h00000660;
  localparam logic [31:0] FM_REG_PROFILE_SHA256_1 = 32'h00000661;
  localparam logic [31:0] FM_REG_PROFILE_SHA256_2 = 32'h00000662;
  localparam logic [31:0] FM_REG_PROFILE_SHA256_3 = 32'h00000663;
  localparam logic [31:0] FM_REG_PROFILE_SHA256_4 = 32'h00000664;
  localparam logic [31:0] FM_REG_PROFILE_SHA256_5 = 32'h00000665;
  localparam logic [31:0] FM_REG_PROFILE_SHA256_6 = 32'h00000666;
  localparam logic [31:0] FM_REG_PROFILE_SHA256_7 = 32'h00000667;
  localparam logic [31:0] FM_REG_RESULT_SESSION = 32'h00000620;
  localparam logic [31:0] FM_REG_RESULT_EPOCH = 32'h00000621;
  localparam logic [31:0] FM_REG_ACCEPTED_SESSION = 32'h00000622;
  localparam logic [31:0] FM_REG_ACCEPTED_EPOCH = 32'h00000623;
  localparam logic [31:0] FM_REG_COMPLETED_SESSION = 32'h00000624;
  localparam logic [31:0] FM_REG_COMPLETED_EPOCH = 32'h00000625;
  localparam logic [31:0] FM_REG_PANIC_SESSION = 32'h00000626;
  localparam logic [31:0] FM_REG_PANIC_EPOCH = 32'h00000627;
  localparam logic [31:0] FM_REG_PANIC_RESULT_SEQUENCE = 32'h00000633;
  localparam logic [31:0] FM_REG_PANIC_RESULT = 32'h00000634;
  localparam logic [31:0] FM_REG_PANIC_RESULT_SESSION = 32'h00000635;
  localparam logic [31:0] FM_REG_PANIC_RESULT_EPOCH = 32'h00000636;
  localparam logic [31:0] FM_COMMAND_WORD_COUNT = 32'h0000000c;
  localparam logic [31:0] FM_COMMAND_BYTE_COUNT = 32'h00000030;
  localparam logic [31:0] FM_CMD_WORD_OPCODE_ID = 32'h00000000;
  localparam logic [31:0] FM_CMD_WORD_SCOPE_ID = 32'h00000001;
  localparam logic [31:0] FM_CMD_WORD_TARGET_WORD = 32'h00000002;
  localparam logic [31:0] FM_CMD_WORD_PARAMETER_ID = 32'h00000003;
  localparam logic [31:0] FM_CMD_WORD_VALUE_U32 = 32'h00000004;
  localparam logic [31:0] FM_CMD_WORD_ARG0_U32 = 32'h00000005;
  localparam logic [31:0] FM_CMD_WORD_ARG1_U32 = 32'h00000006;
  localparam logic [31:0] FM_CMD_WORD_SESSION_ID = 32'h00000007;
  localparam logic [31:0] FM_CMD_WORD_ENGINE_EPOCH = 32'h00000008;
  localparam logic [31:0] FM_CMD_WORD_TOPOLOGY_GENERATION = 32'h00000009;
  localparam logic [31:0] FM_CMD_WORD_VOICE_GENERATION = 32'h0000000a;
  localparam logic [31:0] FM_CMD_WORD_SEQUENCE_ID = 32'h0000000b;
  localparam logic [31:0] FM_UNIT_CHIP_CODE = 32'h00000000;
  localparam logic [31:0] FM_APPLY_TIME_NEXT_NOTE = 32'h00000000;
  localparam logic [31:0] FM_VALUE_TYPE_U32 = 32'h00000000;

  // Check wide fields before converting them to fm_pack_target's 16-bit inputs.
  function automatic logic fm_fields_valid(input logic [31:0] voice, input logic [31:0] operator_index);
/*synthesis translate_off*/
`ifndef SYNTHESIS
    if ($isunknown({voice, operator_index})) return 1'b0;
`endif
/*synthesis translate_on*/
    return voice <= 32'h0000ffff && operator_index <= 32'h0000ffff;
  endfunction

  function automatic logic [31:0] fm_pack_target(input logic [15:0] voice, input logic [15:0] operator_index);
    return ({16'b0, voice} << 16) | ({16'b0, operator_index} << 0);
  endfunction

  function automatic logic [15:0] fm_target_voice(input logic [31:0] word);
    return word[31:16];
  endfunction

  function automatic logic [15:0] fm_target_operator(input logic [31:0] word);
    return word[15:0];
  endfunction

  function automatic logic fm_validate_target(
      input logic [31:0] scope, input logic [31:0] word, input logic [31:0] voice_count,
      input logic [31:0] selected_operator_count, input logic [31:0] patch_operator_count);
    logic [31:0] voice;
    logic [31:0] operator_index;
/*synthesis translate_off*/
`ifndef SYNTHESIS
    if ($isunknown({scope, word, voice_count, selected_operator_count, patch_operator_count})) return 1'b0;
`endif
/*synthesis translate_on*/
    if (voice_count < 32'd1 || voice_count > FM_MAX_DESCRIPTOR_VOICES) return 1'b0;
    if (selected_operator_count > FM_MAX_OPERATORS_PER_VOICE) return 1'b0;
    if (patch_operator_count > FM_MAX_OPERATORS_PER_VOICE) return 1'b0;
    voice = {16'b0, fm_target_voice(word)};
    operator_index = {16'b0, fm_target_operator(word)};
    if (scope == FM_SCOPE_ENGINE) return (voice == FM_TARGET_NONE) && (operator_index == FM_TARGET_NONE);
    if (scope == FM_SCOPE_VOICE) return (voice < voice_count) && (operator_index == FM_TARGET_NONE);
    if (scope == FM_SCOPE_VOICE_OPERATOR) return (voice < voice_count) && (operator_index < selected_operator_count);
    if (scope == FM_SCOPE_PATCH) return (voice == FM_TARGET_NONE) && (operator_index == FM_TARGET_NONE);
    if (scope == FM_SCOPE_PATCH_OPERATOR) return (voice == FM_TARGET_NONE) && (operator_index < patch_operator_count);
    return 1'b0;
  endfunction
  localparam logic [31:0] FM_PROFILE_JT51_OPM_VOICE_COUNT = 32'd32;
  localparam logic [31:0] FM_PROFILE_JT51_OPM_PATCH_OPERATOR_COUNT = 32'd4;
  localparam logic [31:0] FM_PROFILE_JT51_OPM_MAX_OPERATOR_COUNT = 32'd4;
  localparam logic [31:0] FM_PROFILE_MSFA_TWO_OP_REFERENCE_VOICE_COUNT = 32'd8;
  localparam logic [31:0] FM_PROFILE_MSFA_TWO_OP_REFERENCE_PATCH_OPERATOR_COUNT = 32'd2;
  localparam logic [31:0] FM_PROFILE_MSFA_TWO_OP_REFERENCE_MAX_OPERATOR_COUNT = 32'd2;
  // X/Z guards apply in simulation; hardware needs independently verified CDC and legality.
  localparam logic [255:0] FM_PROFILES_SHA256 = 256'he0eb566504f7f06c41ded10eb4a91abdc9bc04cab6638d2ffea48d177ab68fbd;
  localparam logic [255:0] FM_CONTRACT_SHA256 = 256'h100bc058603e6184741bc2aba62745622e1b881290a642f33b4c46c96340d123;
  function automatic logic [31:0] fm_profiles_sha256_word(input logic [31:0] word_index);
    case (word_index)
      32'd0: return 32'he0eb5665;
      32'd1: return 32'h04f7f06c;
      32'd2: return 32'h41ded10e;
      32'd3: return 32'hb4a91abd;
      32'd4: return 32'hc9bc04ca;
      32'd5: return 32'hb6638d2f;
      32'd6: return 32'hfea48d17;
      32'd7: return 32'h7ab68fbd;
      default: return '0;
    endcase
  endfunction
  typedef struct packed {
    logic [31:0] opcode_id;
    logic [31:0] scope_id;
    logic [31:0] target_word;
    logic [31:0] parameter_id;
    logic [31:0] value_u32;
    logic [31:0] arg0_u32;
    logic [31:0] arg1_u32;
    logic [31:0] session_id;
    logic [31:0] engine_epoch;
    logic [31:0] topology_generation;
    logic [31:0] voice_generation;
    logic [31:0] sequence_id;
  } fm_command_t;
  typedef logic [FM_COMMAND_WORD_COUNT-1:0][31:0] fm_command_words_t;
  function automatic logic fm_abi_compatible(input logic [31:0] word);
/*synthesis translate_off*/
`ifndef SYNTHESIS
    if ($isunknown(word)) return 1'b0;
`endif
/*synthesis translate_on*/
    return word == FM_ABI_VERSION_WORD;
  endfunction
  function automatic logic fm_command_valid(input fm_command_t command);
/*synthesis translate_off*/
`ifndef SYNTHESIS
    if ($isunknown(command)) return 1'b0;
`endif
/*synthesis translate_on*/
    return 1'b1; // Typed width only; hardware needs CDC/legality protection.
  endfunction
  function automatic logic fm_command_words_valid(input fm_command_words_t words);
/*synthesis translate_off*/
`ifndef SYNTHESIS
    if ($isunknown(words)) return 1'b0;
`endif
/*synthesis translate_on*/
    return 1'b1; // Typed width only; hardware needs CDC/legality protection.
  endfunction
  function automatic fm_command_words_t fm_serialize_command(input fm_command_t command);
    fm_command_words_t words;
    words[FM_CMD_WORD_OPCODE_ID] = command.opcode_id;
    words[FM_CMD_WORD_SCOPE_ID] = command.scope_id;
    words[FM_CMD_WORD_TARGET_WORD] = command.target_word;
    words[FM_CMD_WORD_PARAMETER_ID] = command.parameter_id;
    words[FM_CMD_WORD_VALUE_U32] = command.value_u32;
    words[FM_CMD_WORD_ARG0_U32] = command.arg0_u32;
    words[FM_CMD_WORD_ARG1_U32] = command.arg1_u32;
    words[FM_CMD_WORD_SESSION_ID] = command.session_id;
    words[FM_CMD_WORD_ENGINE_EPOCH] = command.engine_epoch;
    words[FM_CMD_WORD_TOPOLOGY_GENERATION] = command.topology_generation;
    words[FM_CMD_WORD_VOICE_GENERATION] = command.voice_generation;
    words[FM_CMD_WORD_SEQUENCE_ID] = command.sequence_id;
    return words;
  endfunction
  function automatic fm_command_t fm_deserialize_command(input fm_command_words_t words);
    fm_command_t command;
    command.opcode_id = words[FM_CMD_WORD_OPCODE_ID];
    command.scope_id = words[FM_CMD_WORD_SCOPE_ID];
    command.target_word = words[FM_CMD_WORD_TARGET_WORD];
    command.parameter_id = words[FM_CMD_WORD_PARAMETER_ID];
    command.value_u32 = words[FM_CMD_WORD_VALUE_U32];
    command.arg0_u32 = words[FM_CMD_WORD_ARG0_U32];
    command.arg1_u32 = words[FM_CMD_WORD_ARG1_U32];
    command.session_id = words[FM_CMD_WORD_SESSION_ID];
    command.engine_epoch = words[FM_CMD_WORD_ENGINE_EPOCH];
    command.topology_generation = words[FM_CMD_WORD_TOPOLOGY_GENERATION];
    command.voice_generation = words[FM_CMD_WORD_VOICE_GENERATION];
    command.sequence_id = words[FM_CMD_WORD_SEQUENCE_ID];
    return command;
  endfunction
  typedef struct packed { logic valid; logic [31:0] id, backend_id, profile_version, voice_count, patch_operator_count;
    logic runtime_available; logic [31:0] parameter_count, preset_count; } fm_profile_info_t;
  typedef struct packed { logic valid; logic [31:0] id, minimum, maximum, scope_id, unit_id, apply_time_id, value_type_id; } fm_parameter_info_t;
  typedef struct packed { logic valid; logic [31:0] id, key_on_logical_mask, values_count; } fm_preset_info_t;
  typedef struct packed { logic valid; logic [31:0] parameter_id, operator_index, value_u32; } fm_preset_value_info_t;
  function automatic fm_profile_info_t fm_profile_info(input logic [31:0] id);
    fm_profile_info_t info; info = '0;
/*synthesis translate_off*/
`ifndef SYNTHESIS
    if ($isunknown(id)) return info;
`endif
/*synthesis translate_on*/
    case (id)
      32'd1: begin info.valid=1'b1; info.id=32'd1; info.backend_id=32'd1;
        info.profile_version=32'd65536; info.voice_count=32'd32; info.patch_operator_count=32'd4;
        info.runtime_available=1'b0; info.parameter_count=32'd13; info.preset_count=32'd5; end
      32'd2: begin info.valid=1'b1; info.id=32'd2; info.backend_id=32'd2;
        info.profile_version=32'd65536; info.voice_count=32'd8; info.patch_operator_count=32'd2;
        info.runtime_available=1'b0; info.parameter_count=32'd0; info.preset_count=32'd0; end
      default: ;
    endcase
    return info;
  endfunction
  function automatic logic [255:0] fm_profile_sha256(input logic [31:0] id);
    case (id)
      32'd1: return 256'he0eb566504f7f06c41ded10eb4a91abdc9bc04cab6638d2ffea48d177ab68fbd;
      32'd2: return 256'he0eb566504f7f06c41ded10eb4a91abdc9bc04cab6638d2ffea48d177ab68fbd;
      default: return '0;
    endcase
  endfunction
  function automatic logic [31:0] fm_profile_operator_count(input logic [31:0] id, input logic [31:0] voice);
/*synthesis translate_off*/
`ifndef SYNTHESIS
    if ($isunknown({id,voice})) return 32'hffffffff;
`endif
/*synthesis translate_on*/
    case (id)
      32'd1: begin case (voice)
        32'd0: return 32'd4;
        32'd1: return 32'd4;
        32'd2: return 32'd4;
        32'd3: return 32'd4;
        32'd4: return 32'd4;
        32'd5: return 32'd4;
        32'd6: return 32'd4;
        32'd7: return 32'd4;
        32'd8: return 32'd4;
        32'd9: return 32'd4;
        32'd10: return 32'd4;
        32'd11: return 32'd4;
        32'd12: return 32'd4;
        32'd13: return 32'd4;
        32'd14: return 32'd4;
        32'd15: return 32'd4;
        32'd16: return 32'd4;
        32'd17: return 32'd4;
        32'd18: return 32'd4;
        32'd19: return 32'd4;
        32'd20: return 32'd4;
        32'd21: return 32'd4;
        32'd22: return 32'd4;
        32'd23: return 32'd4;
        32'd24: return 32'd4;
        32'd25: return 32'd4;
        32'd26: return 32'd4;
        32'd27: return 32'd4;
        32'd28: return 32'd4;
        32'd29: return 32'd4;
        32'd30: return 32'd4;
        32'd31: return 32'd4;
        default: return 32'hffffffff; endcase end
      32'd2: begin case (voice)
        32'd0: return 32'd2;
        32'd1: return 32'd2;
        32'd2: return 32'd2;
        32'd3: return 32'd2;
        32'd4: return 32'd2;
        32'd5: return 32'd2;
        32'd6: return 32'd2;
        32'd7: return 32'd2;
        default: return 32'hffffffff; endcase end
      default: return 32'hffffffff;
    endcase
  endfunction
  function automatic fm_parameter_info_t fm_parameter_info(input logic [31:0] id, input logic [31:0] parameter_id);
    fm_parameter_info_t info; info='0;
/*synthesis translate_off*/
`ifndef SYNTHESIS
    if ($isunknown({id,parameter_id})) return info;
`endif
/*synthesis translate_on*/
    case (id)
      32'd1: begin case (parameter_id)
        32'd1: begin info.valid=1'b1; info.id=32'd1; info.minimum=32'd0; info.maximum=32'd7; info.scope_id=32'd3; info.unit_id=32'd0; info.apply_time_id=32'd0; info.value_type_id=32'd0; end
        32'd2: begin info.valid=1'b1; info.id=32'd2; info.minimum=32'd0; info.maximum=32'd7; info.scope_id=32'd3; info.unit_id=32'd0; info.apply_time_id=32'd0; info.value_type_id=32'd0; end
        32'd16: begin info.valid=1'b1; info.id=32'd16; info.minimum=32'd0; info.maximum=32'd15; info.scope_id=32'd4; info.unit_id=32'd0; info.apply_time_id=32'd0; info.value_type_id=32'd0; end
        32'd17: begin info.valid=1'b1; info.id=32'd17; info.minimum=32'd0; info.maximum=32'd127; info.scope_id=32'd4; info.unit_id=32'd0; info.apply_time_id=32'd0; info.value_type_id=32'd0; end
        32'd18: begin info.valid=1'b1; info.id=32'd18; info.minimum=32'd0; info.maximum=32'd31; info.scope_id=32'd4; info.unit_id=32'd0; info.apply_time_id=32'd0; info.value_type_id=32'd0; end
        32'd19: begin info.valid=1'b1; info.id=32'd19; info.minimum=32'd0; info.maximum=32'd31; info.scope_id=32'd4; info.unit_id=32'd0; info.apply_time_id=32'd0; info.value_type_id=32'd0; end
        32'd20: begin info.valid=1'b1; info.id=32'd20; info.minimum=32'd0; info.maximum=32'd31; info.scope_id=32'd4; info.unit_id=32'd0; info.apply_time_id=32'd0; info.value_type_id=32'd0; end
        32'd21: begin info.valid=1'b1; info.id=32'd21; info.minimum=32'd0; info.maximum=32'd15; info.scope_id=32'd4; info.unit_id=32'd0; info.apply_time_id=32'd0; info.value_type_id=32'd0; end
        32'd22: begin info.valid=1'b1; info.id=32'd22; info.minimum=32'd0; info.maximum=32'd15; info.scope_id=32'd4; info.unit_id=32'd0; info.apply_time_id=32'd0; info.value_type_id=32'd0; end
        32'd23: begin info.valid=1'b1; info.id=32'd23; info.minimum=32'd0; info.maximum=32'd7; info.scope_id=32'd4; info.unit_id=32'd0; info.apply_time_id=32'd0; info.value_type_id=32'd0; end
        32'd24: begin info.valid=1'b1; info.id=32'd24; info.minimum=32'd0; info.maximum=32'd3; info.scope_id=32'd4; info.unit_id=32'd0; info.apply_time_id=32'd0; info.value_type_id=32'd0; end
        32'd25: begin info.valid=1'b1; info.id=32'd25; info.minimum=32'd0; info.maximum=32'd3; info.scope_id=32'd4; info.unit_id=32'd0; info.apply_time_id=32'd0; info.value_type_id=32'd0; end
        32'd26: begin info.valid=1'b1; info.id=32'd26; info.minimum=32'd0; info.maximum=32'd1; info.scope_id=32'd4; info.unit_id=32'd0; info.apply_time_id=32'd0; info.value_type_id=32'd0; end
        default: ; endcase end
      default: ;
    endcase
    return info;
  endfunction
  function automatic fm_preset_info_t fm_preset_info(input logic [31:0] id, input logic [31:0] preset_id);
    fm_preset_info_t info; info='0;
/*synthesis translate_off*/
`ifndef SYNTHESIS
    if ($isunknown({id,preset_id})) return info;
`endif
/*synthesis translate_on*/
    case (id)
      32'd1: begin case (preset_id)
        32'd0: begin info.valid=1'b1; info.id=32'd0; info.key_on_logical_mask=32'd1; info.values_count=32'd46; end
        32'd1: begin info.valid=1'b1; info.id=32'd1; info.key_on_logical_mask=32'd3; info.values_count=32'd46; end
        32'd2: begin info.valid=1'b1; info.id=32'd2; info.key_on_logical_mask=32'd15; info.values_count=32'd46; end
        32'd3: begin info.valid=1'b1; info.id=32'd3; info.key_on_logical_mask=32'd15; info.values_count=32'd46; end
        32'd4: begin info.valid=1'b1; info.id=32'd4; info.key_on_logical_mask=32'd15; info.values_count=32'd46; end
        default: ; endcase end
      default: ;
    endcase
    return info;
  endfunction
  function automatic fm_preset_value_info_t fm_preset_value_info(input logic [31:0] id, input logic [31:0] preset_id, input logic [31:0] value_index);
    fm_preset_value_info_t info; info='0;
/*synthesis translate_off*/
`ifndef SYNTHESIS
    if ($isunknown({id,preset_id,value_index})) return info;
`endif
/*synthesis translate_on*/
    case (id)
      32'd1: begin case (preset_id)
        32'd0: begin case (value_index)
          32'd0: begin info.valid=1'b1; info.parameter_id=32'd1; info.operator_index=32'd65535; info.value_u32=32'd7; end
          32'd1: begin info.valid=1'b1; info.parameter_id=32'd2; info.operator_index=32'd65535; info.value_u32=32'd0; end
          32'd2: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd0; info.value_u32=32'd1; end
          32'd3: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd0; info.value_u32=32'd8; end
          32'd4: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd0; info.value_u32=32'd31; end
          32'd5: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd6: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd7: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd8: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd0; info.value_u32=32'd15; end
          32'd9: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd10: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd11: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd12: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd13: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd1; info.value_u32=32'd1; end
          32'd14: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd1; info.value_u32=32'd127; end
          32'd15: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd1; info.value_u32=32'd31; end
          32'd16: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd17: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd18: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd19: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd1; info.value_u32=32'd15; end
          32'd20: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd21: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd22: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd23: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd24: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd2; info.value_u32=32'd1; end
          32'd25: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd2; info.value_u32=32'd127; end
          32'd26: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd2; info.value_u32=32'd31; end
          32'd27: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd28: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd29: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd30: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd2; info.value_u32=32'd15; end
          32'd31: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd32: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd33: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd34: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd35: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd3; info.value_u32=32'd1; end
          32'd36: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd3; info.value_u32=32'd127; end
          32'd37: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd3; info.value_u32=32'd31; end
          32'd38: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd39: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd40: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd41: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd3; info.value_u32=32'd15; end
          32'd42: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd43: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd44: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd45: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd3; info.value_u32=32'd0; end
          default: ; endcase end
        32'd1: begin case (value_index)
          32'd0: begin info.valid=1'b1; info.parameter_id=32'd1; info.operator_index=32'd65535; info.value_u32=32'd4; end
          32'd1: begin info.valid=1'b1; info.parameter_id=32'd2; info.operator_index=32'd65535; info.value_u32=32'd0; end
          32'd2: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd0; info.value_u32=32'd1; end
          32'd3: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd0; info.value_u32=32'd28; end
          32'd4: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd0; info.value_u32=32'd28; end
          32'd5: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd0; info.value_u32=32'd10; end
          32'd6: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd0; info.value_u32=32'd2; end
          32'd7: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd0; info.value_u32=32'd5; end
          32'd8: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd0; info.value_u32=32'd14; end
          32'd9: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd10: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd11: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd12: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd13: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd1; info.value_u32=32'd1; end
          32'd14: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd1; info.value_u32=32'd8; end
          32'd15: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd1; info.value_u32=32'd25; end
          32'd16: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd1; info.value_u32=32'd8; end
          32'd17: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd1; info.value_u32=32'd2; end
          32'd18: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd1; info.value_u32=32'd4; end
          32'd19: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd1; info.value_u32=32'd14; end
          32'd20: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd21: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd22: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd23: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd24: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd2; info.value_u32=32'd1; end
          32'd25: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd2; info.value_u32=32'd127; end
          32'd26: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd2; info.value_u32=32'd31; end
          32'd27: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd28: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd29: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd30: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd2; info.value_u32=32'd14; end
          32'd31: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd32: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd33: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd34: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd35: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd3; info.value_u32=32'd1; end
          32'd36: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd3; info.value_u32=32'd127; end
          32'd37: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd3; info.value_u32=32'd31; end
          32'd38: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd39: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd40: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd41: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd3; info.value_u32=32'd14; end
          32'd42: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd43: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd44: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd45: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd3; info.value_u32=32'd0; end
          default: ; endcase end
        32'd2: begin case (value_index)
          32'd0: begin info.valid=1'b1; info.parameter_id=32'd1; info.operator_index=32'd65535; info.value_u32=32'd0; end
          32'd1: begin info.valid=1'b1; info.parameter_id=32'd2; info.operator_index=32'd65535; info.value_u32=32'd2; end
          32'd2: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd0; info.value_u32=32'd1; end
          32'd3: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd0; info.value_u32=32'd26; end
          32'd4: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd0; info.value_u32=32'd31; end
          32'd5: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd0; info.value_u32=32'd12; end
          32'd6: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd0; info.value_u32=32'd5; end
          32'd7: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd0; info.value_u32=32'd8; end
          32'd8: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd0; info.value_u32=32'd15; end
          32'd9: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd10: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd11: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd12: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd13: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd1; info.value_u32=32'd3; end
          32'd14: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd1; info.value_u32=32'd30; end
          32'd15: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd1; info.value_u32=32'd31; end
          32'd16: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd1; info.value_u32=32'd8; end
          32'd17: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd1; info.value_u32=32'd2; end
          32'd18: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd1; info.value_u32=32'd4; end
          32'd19: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd1; info.value_u32=32'd15; end
          32'd20: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd21: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd22: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd23: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd24: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd2; info.value_u32=32'd2; end
          32'd25: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd2; info.value_u32=32'd42; end
          32'd26: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd2; info.value_u32=32'd31; end
          32'd27: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd2; info.value_u32=32'd9; end
          32'd28: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd2; info.value_u32=32'd3; end
          32'd29: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd2; info.value_u32=32'd6; end
          32'd30: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd2; info.value_u32=32'd15; end
          32'd31: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd32: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd33: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd34: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd35: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd3; info.value_u32=32'd1; end
          32'd36: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd3; info.value_u32=32'd8; end
          32'd37: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd3; info.value_u32=32'd31; end
          32'd38: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd3; info.value_u32=32'd8; end
          32'd39: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd3; info.value_u32=32'd3; end
          32'd40: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd3; info.value_u32=32'd5; end
          32'd41: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd3; info.value_u32=32'd15; end
          32'd42: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd43: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd44: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd45: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd3; info.value_u32=32'd0; end
          default: ; endcase end
        32'd3: begin case (value_index)
          32'd0: begin info.valid=1'b1; info.parameter_id=32'd1; info.operator_index=32'd65535; info.value_u32=32'd4; end
          32'd1: begin info.valid=1'b1; info.parameter_id=32'd2; info.operator_index=32'd65535; info.value_u32=32'd0; end
          32'd2: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd0; info.value_u32=32'd1; end
          32'd3: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd0; info.value_u32=32'd32; end
          32'd4: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd0; info.value_u32=32'd31; end
          32'd5: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd0; info.value_u32=32'd14; end
          32'd6: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd0; info.value_u32=32'd5; end
          32'd7: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd0; info.value_u32=32'd10; end
          32'd8: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd0; info.value_u32=32'd10; end
          32'd9: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd10: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd11: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd12: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd13: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd1; info.value_u32=32'd1; end
          32'd14: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd1; info.value_u32=32'd10; end
          32'd15: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd1; info.value_u32=32'd31; end
          32'd16: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd1; info.value_u32=32'd8; end
          32'd17: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd1; info.value_u32=32'd3; end
          32'd18: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd1; info.value_u32=32'd8; end
          32'd19: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd1; info.value_u32=32'd10; end
          32'd20: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd21: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd22: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd23: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd24: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd2; info.value_u32=32'd14; end
          32'd25: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd2; info.value_u32=32'd40; end
          32'd26: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd2; info.value_u32=32'd31; end
          32'd27: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd2; info.value_u32=32'd18; end
          32'd28: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd2; info.value_u32=32'd7; end
          32'd29: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd2; info.value_u32=32'd12; end
          32'd30: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd2; info.value_u32=32'd10; end
          32'd31: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd32: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd33: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd34: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd35: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd3; info.value_u32=32'd1; end
          32'd36: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd3; info.value_u32=32'd18; end
          32'd37: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd3; info.value_u32=32'd31; end
          32'd38: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd3; info.value_u32=32'd12; end
          32'd39: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd3; info.value_u32=32'd5; end
          32'd40: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd3; info.value_u32=32'd10; end
          32'd41: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd3; info.value_u32=32'd10; end
          32'd42: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd43: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd44: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd45: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd3; info.value_u32=32'd0; end
          default: ; endcase end
        32'd4: begin case (value_index)
          32'd0: begin info.valid=1'b1; info.parameter_id=32'd1; info.operator_index=32'd65535; info.value_u32=32'd4; end
          32'd1: begin info.valid=1'b1; info.parameter_id=32'd2; info.operator_index=32'd65535; info.value_u32=32'd0; end
          32'd2: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd0; info.value_u32=32'd3; end
          32'd3: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd0; info.value_u32=32'd24; end
          32'd4: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd0; info.value_u32=32'd31; end
          32'd5: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd0; info.value_u32=32'd10; end
          32'd6: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd0; info.value_u32=32'd5; end
          32'd7: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd0; info.value_u32=32'd12; end
          32'd8: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd0; info.value_u32=32'd6; end
          32'd9: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd10: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd0; info.value_u32=32'd1; end
          32'd11: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd12: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd0; info.value_u32=32'd0; end
          32'd13: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd1; info.value_u32=32'd1; end
          32'd14: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd1; info.value_u32=32'd10; end
          32'd15: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd1; info.value_u32=32'd31; end
          32'd16: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd1; info.value_u32=32'd6; end
          32'd17: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd1; info.value_u32=32'd3; end
          32'd18: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd1; info.value_u32=32'd11; end
          32'd19: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd1; info.value_u32=32'd6; end
          32'd20: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd21: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd22: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd23: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd1; info.value_u32=32'd0; end
          32'd24: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd2; info.value_u32=32'd7; end
          32'd25: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd2; info.value_u32=32'd34; end
          32'd26: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd2; info.value_u32=32'd31; end
          32'd27: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd2; info.value_u32=32'd13; end
          32'd28: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd2; info.value_u32=32'd6; end
          32'd29: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd2; info.value_u32=32'd12; end
          32'd30: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd2; info.value_u32=32'd6; end
          32'd31: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd32: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd33: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd34: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd2; info.value_u32=32'd0; end
          32'd35: begin info.valid=1'b1; info.parameter_id=32'd16; info.operator_index=32'd3; info.value_u32=32'd2; end
          32'd36: begin info.valid=1'b1; info.parameter_id=32'd17; info.operator_index=32'd3; info.value_u32=32'd20; end
          32'd37: begin info.valid=1'b1; info.parameter_id=32'd18; info.operator_index=32'd3; info.value_u32=32'd31; end
          32'd38: begin info.valid=1'b1; info.parameter_id=32'd19; info.operator_index=32'd3; info.value_u32=32'd8; end
          32'd39: begin info.valid=1'b1; info.parameter_id=32'd20; info.operator_index=32'd3; info.value_u32=32'd4; end
          32'd40: begin info.valid=1'b1; info.parameter_id=32'd21; info.operator_index=32'd3; info.value_u32=32'd12; end
          32'd41: begin info.valid=1'b1; info.parameter_id=32'd22; info.operator_index=32'd3; info.value_u32=32'd6; end
          32'd42: begin info.valid=1'b1; info.parameter_id=32'd23; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd43: begin info.valid=1'b1; info.parameter_id=32'd24; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd44: begin info.valid=1'b1; info.parameter_id=32'd25; info.operator_index=32'd3; info.value_u32=32'd0; end
          32'd45: begin info.valid=1'b1; info.parameter_id=32'd26; info.operator_index=32'd3; info.value_u32=32'd0; end
          default: ; endcase end
        default: ; endcase end
      default: ;
    endcase
    return info;
  endfunction
  function automatic logic fm_validate_parameter(input logic [31:0] id, input logic [31:0] parameter_id, input logic [31:0] scope, input logic [31:0] word, input logic [31:0] value_u32);
    fm_profile_info_t profile_info; fm_parameter_info_t parameter_info; logic [31:0] selected;
/*synthesis translate_off*/
`ifndef SYNTHESIS
    if ($isunknown({id,parameter_id,scope,word,value_u32})) return 1'b0;
`endif
/*synthesis translate_on*/
    profile_info=fm_profile_info(id); parameter_info=fm_parameter_info(id,parameter_id);
    if (!profile_info.valid || !parameter_info.valid || scope != parameter_info.scope_id ||
        value_u32 < parameter_info.minimum || value_u32 > parameter_info.maximum) return 1'b0;
    selected=fm_profile_operator_count(id, {16'b0,fm_target_voice(word)});
    if (selected == 32'hffffffff) selected=32'd0;
    return fm_validate_target(scope,word,profile_info.voice_count,selected,profile_info.patch_operator_count);
  endfunction

  function automatic logic [31:0] fm_profile_native_register_slot(input logic [31:0] id, input logic [31:0] logical_operator);
/*synthesis translate_off*/
`ifndef SYNTHESIS
    if ($isunknown({id,logical_operator})) return 32'hffffffff;
`endif
/*synthesis translate_on*/
    case (id)
      32'd1: begin case (logical_operator)
        32'd0: return 32'd0;
        32'd1: return 32'd2;
        32'd2: return 32'd1;
        32'd3: return 32'd3;
        default: return 32'hffffffff; endcase end
      default: return 32'hffffffff;
    endcase
  endfunction
  function automatic logic [31:0] fm_profile_native_key_on_bit(input logic [31:0] id, input logic [31:0] logical_operator);
/*synthesis translate_off*/
`ifndef SYNTHESIS
    if ($isunknown({id,logical_operator})) return 32'hffffffff;
`endif
/*synthesis translate_on*/
    case (id)
      32'd1: begin case (logical_operator)
        32'd0: return 32'd3;
        32'd1: return 32'd4;
        32'd2: return 32'd5;
        32'd3: return 32'd6;
        default: return 32'hffffffff; endcase end
      default: return 32'hffffffff;
    endcase
  endfunction
endpackage
