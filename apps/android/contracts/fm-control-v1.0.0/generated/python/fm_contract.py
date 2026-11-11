# CONTRACT FROZEN / HARDWARE NOT INTEGRATED; 请勿手改 / DO NOT EDIT.
# Generated from schema + backend profiles; transport/backend lifecycle is not implemented by this codec.
# Schema version: 1.0.0
# Schema SHA256: 5bf9e83b4f156f998d7fc7cada35815d3ca802c303f31c875e1888adf7378d36

FM_SCHEMA_VERSION = "1.0.0"
FM_SCHEMA_SHA256 = "5bf9e83b4f156f998d7fc7cada35815d3ca802c303f31c875e1888adf7378d36"

FM_ABI_MAJOR = 0x00000001
FM_ABI_MINOR = 0x00000001
FM_CAP_MAGIC = 0x464d4331
FM_SPI_WORD_BASE = 0x00000600
FM_SPI_WINDOW_WORDS = 0x00000100
FM_SPI_WORD_BITS = 0x00000020
FM_TARGET_NONE = 0x0000ffff
FM_MAX_WIRE_INDEX = 0x0000fffe
FM_MAX_DESCRIPTOR_VOICES = 0x00000100
FM_MAX_OPERATORS_PER_VOICE = 0x00000020
FM_MAX_PARAMETERS = 0x00000100
FM_MAX_PRESETS = 0x00000080
FM_MAX_BACKEND_PROFILES = 0x00000010
FM_HTTP_API_MAJOR = 0x00000002
FM_HTTP_API_MINOR = 0x00000000
FM_HTTP_COMMAND_MAX_BYTES = 0x0000005f
FM_PERFORMANCE_LEASE_MS = 0x000004b0
FM_ABI_VERSION_WORD = 0x00010001
FM_HTTP_API_VERSION_WORD = 0x00020000
FM_ENGINE_DDS = 0x00000000
FM_ENGINE_KS = 0x00000001
FM_ENGINE_FM = 0x00000002
FM_SCOPE_ENGINE = 0x00000000
FM_SCOPE_VOICE = 0x00000001
FM_SCOPE_VOICE_OPERATOR = 0x00000002
FM_SCOPE_PATCH = 0x00000003
FM_SCOPE_PATCH_OPERATOR = 0x00000004
FM_SPI_OPCODE_SELECT_BACKEND = 0x00000000
FM_SPI_OPCODE_PANIC = 0x00000001
FM_SPI_OPCODE_NOTE_ON = 0x00000002
FM_SPI_OPCODE_NOTE_OFF = 0x00000003
FM_SPI_OPCODE_SET_PITCH = 0x00000004
FM_SPI_OPCODE_SET_GAIN_PAN = 0x00000005
FM_SPI_OPCODE_SET_PARAMETER = 0x00000006
FM_SPI_OPCODE_APPLY_PRESET = 0x00000007
FM_SPI_OPCODE_RELEASE_ALL = 0x00000008
FM_HTTP_OPCODE_CC = 0x00000002
FM_HTTP_OPCODE_KEYS_UPDATE = 0x00000003
FM_HTTP_OPCODE_STOP = 0x00000004
FM_HTTP_OPCODE_ENGINE_SELECT = 0x00000005
FM_HTTP_OPCODE_KS_DECAY = 0x00000006
FM_HTTP_OPCODE_FM_PARAMETER = 0x00000007
FM_HTTP_OPCODE_FM_PRESET = 0x00000008
FM_RESULT_OK = 0x00000000
FM_RESULT_BUSY = 0x00000001
FM_RESULT_UNSUPPORTED = 0x00000002
FM_RESULT_BAD_TARGET = 0x00000003
FM_RESULT_BAD_VALUE = 0x00000004
FM_RESULT_STALE_EPOCH = 0x00000005
FM_RESULT_ABI_MISMATCH = 0x00000006
FM_RESULT_BACKEND_FAULT = 0x00000007
FM_RESULT_CANCELLED = 0x00000008
FM_RESULT_SEQUENCE_CONFLICT = 0x00000009
FM_RESULT_STALE_SEQUENCE = 0x0000000a
FM_RESULT_STALE_SESSION = 0x0000000b
FM_RESULT_STALE_TOPOLOGY = 0x0000000c
FM_RESULT_STALE_VOICE = 0x0000000d
FM_PHASE_UNAVAILABLE = 0x00000000
FM_PHASE_INITIALIZING = 0x00000001
FM_PHASE_READY = 0x00000002
FM_PHASE_SWITCHING = 0x00000003
FM_PHASE_RECOVERING = 0x00000004
FM_PHASE_BLOCKED = 0x00000005
FM_BACKEND_UNBOUND = 0x00000000
FM_BACKEND_JT51_OPM = 0x00000001
FM_BACKEND_MSFA_TWO_OP_REFERENCE = 0x00000002
FM_REG_CAP_MAGIC = 0x00000600
FM_REG_ABI_VERSION = 0x00000601
FM_REG_BACKEND_ID = 0x00000602
FM_REG_VOICE_COUNT = 0x00000603
FM_REG_MAX_OPERATOR_COUNT = 0x00000604
FM_REG_FEATURES = 0x00000605
FM_REG_EPOCH = 0x00000606
FM_REG_PHASE = 0x00000607
FM_REG_ACCEPTED_SEQUENCE = 0x00000608
FM_REG_COMPLETED_SEQUENCE = 0x00000609
FM_REG_RESULT_SEQUENCE = 0x0000060a
FM_REG_RESULT = 0x0000060b
FM_REG_TOPOLOGY_GENERATION = 0x0000060c
FM_REG_STATUS_GENERATION = 0x0000060d
FM_REG_FLAGS = 0x0000060e
FM_REG_SESSION = 0x0000060f
FM_REG_CMD_OPCODE_ID = 0x00000610
FM_REG_CMD_SCOPE_ID = 0x00000611
FM_REG_CMD_TARGET_WORD = 0x00000612
FM_REG_CMD_PARAMETER_ID = 0x00000613
FM_REG_CMD_VALUE_U32 = 0x00000614
FM_REG_CMD_ARG0_U32 = 0x00000615
FM_REG_CMD_ARG1_U32 = 0x00000616
FM_REG_CMD_SESSION_ID = 0x00000617
FM_REG_CMD_ENGINE_EPOCH = 0x00000618
FM_REG_CMD_TOPOLOGY_GENERATION = 0x00000619
FM_REG_CMD_VOICE_GENERATION = 0x0000061a
FM_REG_CMD_SEQUENCE_ID = 0x0000061b
FM_REG_CMD_COMMIT = 0x0000061c
FM_REG_PANIC_SEQUENCE = 0x00000630
FM_REG_PANIC_ACCEPTED_SEQUENCE = 0x00000631
FM_REG_PANIC_COMPLETED_SEQUENCE = 0x00000632
FM_REG_SOURCE_RATE_NUMERATOR = 0x00000640
FM_REG_SOURCE_RATE_DENOMINATOR = 0x00000641
FM_REG_OUTPUT_RATE_NUMERATOR = 0x00000642
FM_REG_OUTPUT_RATE_DENOMINATOR = 0x00000643
FM_REG_PROFILE_VERSION = 0x00000644
FM_REG_PARAMETER_COUNT = 0x00000645
FM_REG_PRESET_COUNT = 0x00000646
FM_REG_VOICE_QUERY_INDEX = 0x00000647
FM_REG_VOICE_QUERY_GENERATION = 0x00000648
FM_REG_VOICE_QUERY_OPERATOR_COUNT = 0x00000649
FM_REG_PROFILE_SHA256_0 = 0x00000660
FM_REG_PROFILE_SHA256_1 = 0x00000661
FM_REG_PROFILE_SHA256_2 = 0x00000662
FM_REG_PROFILE_SHA256_3 = 0x00000663
FM_REG_PROFILE_SHA256_4 = 0x00000664
FM_REG_PROFILE_SHA256_5 = 0x00000665
FM_REG_PROFILE_SHA256_6 = 0x00000666
FM_REG_PROFILE_SHA256_7 = 0x00000667
FM_REG_RESULT_SESSION = 0x00000620
FM_REG_RESULT_EPOCH = 0x00000621
FM_REG_ACCEPTED_SESSION = 0x00000622
FM_REG_ACCEPTED_EPOCH = 0x00000623
FM_REG_COMPLETED_SESSION = 0x00000624
FM_REG_COMPLETED_EPOCH = 0x00000625
FM_REG_PANIC_SESSION = 0x00000626
FM_REG_PANIC_EPOCH = 0x00000627
FM_REG_PANIC_RESULT_SEQUENCE = 0x00000633
FM_REG_PANIC_RESULT = 0x00000634
FM_REG_PANIC_RESULT_SESSION = 0x00000635
FM_REG_PANIC_RESULT_EPOCH = 0x00000636
FM_COMMAND_WORD_COUNT = 0x0000000c
FM_COMMAND_BYTE_COUNT = 0x00000030
FM_CMD_WORD_OPCODE_ID = 0x00000000
FM_CMD_WORD_SCOPE_ID = 0x00000001
FM_CMD_WORD_TARGET_WORD = 0x00000002
FM_CMD_WORD_PARAMETER_ID = 0x00000003
FM_CMD_WORD_VALUE_U32 = 0x00000004
FM_CMD_WORD_ARG0_U32 = 0x00000005
FM_CMD_WORD_ARG1_U32 = 0x00000006
FM_CMD_WORD_SESSION_ID = 0x00000007
FM_CMD_WORD_ENGINE_EPOCH = 0x00000008
FM_CMD_WORD_TOPOLOGY_GENERATION = 0x00000009
FM_CMD_WORD_VOICE_GENERATION = 0x0000000a
FM_CMD_WORD_SEQUENCE_ID = 0x0000000b
FM_UNIT_CHIP_CODE = 0x00000000
FM_APPLY_TIME_NEXT_NOTE = 0x00000000
FM_VALUE_TYPE_U32 = 0x00000000


def _is_u32(value):
    return type(value) is int and 0 <= value <= 0xffffffff


def pack_target(voice, operator):
    """Pack two unsigned 16-bit indices; NONE is accepted."""
    if not (_is_u32(voice) and voice <= 0xffff and
            _is_u32(operator) and operator <= 0xffff):
        raise ValueError("target fields must be unsigned 16-bit integers, not bool")
    return (voice << 16) | (operator << 0)


def unpack_target(word):
    """Return (voice, operator), each unsigned."""
    if not _is_u32(word):
        raise ValueError("target word must be a uint32 integer, not bool")
    return ((word >> 16) & 0xffff,
            (word >> 0) & 0xffff)


def validate_target(scope, word, voice_count, selected_operator_count, patch_operator_count):
    """Validate indices using the selected voice and explicit patch topology counts."""
    if not all(_is_u32(value) for value in
               (scope, word, voice_count, selected_operator_count, patch_operator_count)):
        return False
    if not 1 <= voice_count <= FM_MAX_DESCRIPTOR_VOICES:
        return False
    if not 0 <= selected_operator_count <= FM_MAX_OPERATORS_PER_VOICE:
        return False
    if not 0 <= patch_operator_count <= FM_MAX_OPERATORS_PER_VOICE:
        return False
    voice, operator = unpack_target(word)
    if scope == FM_SCOPE_ENGINE:
        return (voice == FM_TARGET_NONE) and (operator == FM_TARGET_NONE)
    if scope == FM_SCOPE_VOICE:
        return (voice < voice_count) and (operator == FM_TARGET_NONE)
    if scope == FM_SCOPE_VOICE_OPERATOR:
        return (voice < voice_count) and (operator < selected_operator_count)
    if scope == FM_SCOPE_PATCH:
        return (voice == FM_TARGET_NONE) and (operator == FM_TARGET_NONE)
    if scope == FM_SCOPE_PATCH_OPERATOR:
        return (voice == FM_TARGET_NONE) and (operator < patch_operator_count)
    return False

FM_PROFILES_SHA256 = "51a6af55646fe2adb54bf4f9f3d06e2d262bd830ec5f30f80c0e5bfa2eadf49f"
FM_CONTRACT_SHA256 = "c3b8c4d73ac466b5b726a1cf1a7a346ce742ff61cec8ee1a9258b0c4d3651930"
FM_COMMAND_FIELDS = ('opcode_id', 'scope_id', 'target_word', 'parameter_id', 'value_u32', 'arg0_u32', 'arg1_u32', 'session_id', 'engine_epoch', 'topology_generation', 'voice_generation', 'sequence_id')
FM_PROFILE_IDS = (1, 2)
FM_PROFILES_SHA256_WORDS = (1369878357, 1685054125, 3041654009, 4090523181, 640407600, 3965661432, 202267642, 783152287)

# Returned metadata is copied; callers cannot alter the internal contract tables.
import copy as _copy
_PROFILES = {1: {'id': 1, 'key': 'jt51_opm', 'backend_id': 1, 'profile_version': 65536, 'role': 'implementation_target', 'runtime_available': False, 'voice_count': 8, 'operators_per_voice': [4, 4, 4, 4, 4, 4, 4, 4], 'patch_operator_count': 4, 'operator_labels': ['M1', 'C1', 'M2', 'C2'], 'adapter_mapping': {'register_slots': [0, 2, 1, 3], 'key_on_bits': [3, 4, 5, 6]}, 'parameters': [{'id': 1, 'name': 'algorithm', 'minimum': 0, 'maximum': 7, 'scope': 'PATCH', 'unit': 'chip_code', 'apply_time': 'next_note', 'value_type': 'u32', 'scope_id': 3, 'unit_id': 0, 'apply_time_id': 0, 'value_type_id': 0}, {'id': 2, 'name': 'feedback', 'minimum': 0, 'maximum': 7, 'scope': 'PATCH', 'unit': 'chip_code', 'apply_time': 'next_note', 'value_type': 'u32', 'scope_id': 3, 'unit_id': 0, 'apply_time_id': 0, 'value_type_id': 0}, {'id': 16, 'name': 'multiplier', 'minimum': 0, 'maximum': 15, 'scope': 'PATCH_OPERATOR', 'unit': 'chip_code', 'apply_time': 'next_note', 'value_type': 'u32', 'scope_id': 4, 'unit_id': 0, 'apply_time_id': 0, 'value_type_id': 0}, {'id': 17, 'name': 'total_level', 'minimum': 0, 'maximum': 127, 'scope': 'PATCH_OPERATOR', 'unit': 'chip_code', 'apply_time': 'next_note', 'value_type': 'u32', 'scope_id': 4, 'unit_id': 0, 'apply_time_id': 0, 'value_type_id': 0}, {'id': 18, 'name': 'attack_rate', 'minimum': 0, 'maximum': 31, 'scope': 'PATCH_OPERATOR', 'unit': 'chip_code', 'apply_time': 'next_note', 'value_type': 'u32', 'scope_id': 4, 'unit_id': 0, 'apply_time_id': 0, 'value_type_id': 0}, {'id': 19, 'name': 'decay1_rate', 'minimum': 0, 'maximum': 31, 'scope': 'PATCH_OPERATOR', 'unit': 'chip_code', 'apply_time': 'next_note', 'value_type': 'u32', 'scope_id': 4, 'unit_id': 0, 'apply_time_id': 0, 'value_type_id': 0}, {'id': 20, 'name': 'decay2_rate', 'minimum': 0, 'maximum': 31, 'scope': 'PATCH_OPERATOR', 'unit': 'chip_code', 'apply_time': 'next_note', 'value_type': 'u32', 'scope_id': 4, 'unit_id': 0, 'apply_time_id': 0, 'value_type_id': 0}, {'id': 21, 'name': 'sustain_level', 'minimum': 0, 'maximum': 15, 'scope': 'PATCH_OPERATOR', 'unit': 'chip_code', 'apply_time': 'next_note', 'value_type': 'u32', 'scope_id': 4, 'unit_id': 0, 'apply_time_id': 0, 'value_type_id': 0}, {'id': 22, 'name': 'release_rate', 'minimum': 0, 'maximum': 15, 'scope': 'PATCH_OPERATOR', 'unit': 'chip_code', 'apply_time': 'next_note', 'value_type': 'u32', 'scope_id': 4, 'unit_id': 0, 'apply_time_id': 0, 'value_type_id': 0}, {'id': 23, 'name': 'detune1', 'minimum': 0, 'maximum': 7, 'scope': 'PATCH_OPERATOR', 'unit': 'chip_code', 'apply_time': 'next_note', 'value_type': 'u32', 'scope_id': 4, 'unit_id': 0, 'apply_time_id': 0, 'value_type_id': 0}, {'id': 24, 'name': 'detune2', 'minimum': 0, 'maximum': 3, 'scope': 'PATCH_OPERATOR', 'unit': 'chip_code', 'apply_time': 'next_note', 'value_type': 'u32', 'scope_id': 4, 'unit_id': 0, 'apply_time_id': 0, 'value_type_id': 0}, {'id': 25, 'name': 'rate_scaling', 'minimum': 0, 'maximum': 3, 'scope': 'PATCH_OPERATOR', 'unit': 'chip_code', 'apply_time': 'next_note', 'value_type': 'u32', 'scope_id': 4, 'unit_id': 0, 'apply_time_id': 0, 'value_type_id': 0}, {'id': 26, 'name': 'am_enable', 'minimum': 0, 'maximum': 1, 'scope': 'PATCH_OPERATOR', 'unit': 'chip_code', 'apply_time': 'next_note', 'value_type': 'u32', 'scope_id': 4, 'unit_id': 0, 'apply_time_id': 0, 'value_type_id': 0}], 'presets': [{'id': 0, 'name': 'diagnostic_sine', 'description': 'algorithm 7, one audible M1 carrier; diagnostic patch', 'key_on_logical_mask': 1, 'values': [{'parameter_id': 1, 'operator_index': 65535, 'value_u32': 7}, {'parameter_id': 2, 'operator_index': 65535, 'value_u32': 0}, {'parameter_id': 16, 'operator_index': 0, 'value_u32': 1}, {'parameter_id': 17, 'operator_index': 0, 'value_u32': 32}, {'parameter_id': 18, 'operator_index': 0, 'value_u32': 31}, {'parameter_id': 19, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 20, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 21, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 22, 'operator_index': 0, 'value_u32': 15}, {'parameter_id': 23, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 24, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 25, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 26, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 16, 'operator_index': 1, 'value_u32': 1}, {'parameter_id': 17, 'operator_index': 1, 'value_u32': 127}, {'parameter_id': 18, 'operator_index': 1, 'value_u32': 31}, {'parameter_id': 19, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 20, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 21, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 22, 'operator_index': 1, 'value_u32': 15}, {'parameter_id': 23, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 24, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 25, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 26, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 16, 'operator_index': 2, 'value_u32': 1}, {'parameter_id': 17, 'operator_index': 2, 'value_u32': 127}, {'parameter_id': 18, 'operator_index': 2, 'value_u32': 31}, {'parameter_id': 19, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 20, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 21, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 22, 'operator_index': 2, 'value_u32': 15}, {'parameter_id': 23, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 24, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 25, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 26, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 16, 'operator_index': 3, 'value_u32': 1}, {'parameter_id': 17, 'operator_index': 3, 'value_u32': 127}, {'parameter_id': 18, 'operator_index': 3, 'value_u32': 31}, {'parameter_id': 19, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 20, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 21, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 22, 'operator_index': 3, 'value_u32': 15}, {'parameter_id': 23, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 24, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 25, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 26, 'operator_index': 3, 'value_u32': 0}]}, {'id': 1, 'name': 'two_op_keys', 'description': 'designed two-operator keys patch; not a GM/Yamaha preset', 'key_on_logical_mask': 3, 'values': [{'parameter_id': 1, 'operator_index': 65535, 'value_u32': 4}, {'parameter_id': 2, 'operator_index': 65535, 'value_u32': 0}, {'parameter_id': 16, 'operator_index': 0, 'value_u32': 1}, {'parameter_id': 17, 'operator_index': 0, 'value_u32': 28}, {'parameter_id': 18, 'operator_index': 0, 'value_u32': 28}, {'parameter_id': 19, 'operator_index': 0, 'value_u32': 10}, {'parameter_id': 20, 'operator_index': 0, 'value_u32': 2}, {'parameter_id': 21, 'operator_index': 0, 'value_u32': 5}, {'parameter_id': 22, 'operator_index': 0, 'value_u32': 14}, {'parameter_id': 23, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 24, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 25, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 26, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 16, 'operator_index': 1, 'value_u32': 1}, {'parameter_id': 17, 'operator_index': 1, 'value_u32': 36}, {'parameter_id': 18, 'operator_index': 1, 'value_u32': 25}, {'parameter_id': 19, 'operator_index': 1, 'value_u32': 8}, {'parameter_id': 20, 'operator_index': 1, 'value_u32': 2}, {'parameter_id': 21, 'operator_index': 1, 'value_u32': 4}, {'parameter_id': 22, 'operator_index': 1, 'value_u32': 14}, {'parameter_id': 23, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 24, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 25, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 26, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 16, 'operator_index': 2, 'value_u32': 1}, {'parameter_id': 17, 'operator_index': 2, 'value_u32': 127}, {'parameter_id': 18, 'operator_index': 2, 'value_u32': 31}, {'parameter_id': 19, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 20, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 21, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 22, 'operator_index': 2, 'value_u32': 14}, {'parameter_id': 23, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 24, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 25, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 26, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 16, 'operator_index': 3, 'value_u32': 1}, {'parameter_id': 17, 'operator_index': 3, 'value_u32': 127}, {'parameter_id': 18, 'operator_index': 3, 'value_u32': 31}, {'parameter_id': 19, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 20, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 21, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 22, 'operator_index': 3, 'value_u32': 14}, {'parameter_id': 23, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 24, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 25, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 26, 'operator_index': 3, 'value_u32': 0}]}, {'id': 2, 'name': 'four_op_bell', 'description': 'designed four-operator bell-like patch; not a DX7/GM preset', 'key_on_logical_mask': 15, 'values': [{'parameter_id': 1, 'operator_index': 65535, 'value_u32': 0}, {'parameter_id': 2, 'operator_index': 65535, 'value_u32': 2}, {'parameter_id': 16, 'operator_index': 0, 'value_u32': 1}, {'parameter_id': 17, 'operator_index': 0, 'value_u32': 26}, {'parameter_id': 18, 'operator_index': 0, 'value_u32': 31}, {'parameter_id': 19, 'operator_index': 0, 'value_u32': 12}, {'parameter_id': 20, 'operator_index': 0, 'value_u32': 5}, {'parameter_id': 21, 'operator_index': 0, 'value_u32': 8}, {'parameter_id': 22, 'operator_index': 0, 'value_u32': 15}, {'parameter_id': 23, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 24, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 25, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 26, 'operator_index': 0, 'value_u32': 0}, {'parameter_id': 16, 'operator_index': 1, 'value_u32': 3}, {'parameter_id': 17, 'operator_index': 1, 'value_u32': 30}, {'parameter_id': 18, 'operator_index': 1, 'value_u32': 31}, {'parameter_id': 19, 'operator_index': 1, 'value_u32': 8}, {'parameter_id': 20, 'operator_index': 1, 'value_u32': 2}, {'parameter_id': 21, 'operator_index': 1, 'value_u32': 4}, {'parameter_id': 22, 'operator_index': 1, 'value_u32': 15}, {'parameter_id': 23, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 24, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 25, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 26, 'operator_index': 1, 'value_u32': 0}, {'parameter_id': 16, 'operator_index': 2, 'value_u32': 2}, {'parameter_id': 17, 'operator_index': 2, 'value_u32': 42}, {'parameter_id': 18, 'operator_index': 2, 'value_u32': 31}, {'parameter_id': 19, 'operator_index': 2, 'value_u32': 9}, {'parameter_id': 20, 'operator_index': 2, 'value_u32': 3}, {'parameter_id': 21, 'operator_index': 2, 'value_u32': 6}, {'parameter_id': 22, 'operator_index': 2, 'value_u32': 15}, {'parameter_id': 23, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 24, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 25, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 26, 'operator_index': 2, 'value_u32': 0}, {'parameter_id': 16, 'operator_index': 3, 'value_u32': 1}, {'parameter_id': 17, 'operator_index': 3, 'value_u32': 32}, {'parameter_id': 18, 'operator_index': 3, 'value_u32': 31}, {'parameter_id': 19, 'operator_index': 3, 'value_u32': 8}, {'parameter_id': 20, 'operator_index': 3, 'value_u32': 3}, {'parameter_id': 21, 'operator_index': 3, 'value_u32': 5}, {'parameter_id': 22, 'operator_index': 3, 'value_u32': 15}, {'parameter_id': 23, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 24, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 25, 'operator_index': 3, 'value_u32': 0}, {'parameter_id': 26, 'operator_index': 3, 'value_u32': 0}]}], 'source': {'upstream': 'https://github.com/jotego/jt51', 'commit': '985a573dcfc1ff135553a39f7eae21d18ba57cbe', 'preset_source': 'development/tang_mega_138k_synth/experiments/audio/fm_experiments/jt51/run_native.py', 'native_arrays': 'M1/M2/C1/C2; stored values remapped to logical M1/C1/M2/C2', 'pitch_range': 'adapter validation required; no full MIDI acoustic range claim'}, 'profile_sha256': '51a6af55646fe2adb54bf4f9f3d06e2d262bd830ec5f30f80c0e5bfa2eadf49f', 'parameter_count': 13, 'preset_count': 3}, 2: {'id': 2, 'key': 'msfa_two_op_reference', 'backend_id': 2, 'profile_version': 65536, 'role': 'offline_reference', 'runtime_available': False, 'voice_count': 8, 'operators_per_voice': [2, 2, 2, 2, 2, 2, 2, 2], 'patch_operator_count': 2, 'operator_labels': ['modulator', 'carrier'], 'parameters': [], 'presets': [], 'source': {'upstream': 'https://github.com/google/music-synthesizer-for-android', 'commit': 'f67d41d313b7dc85f6fb99e79e515cc9d208cfff', 'note': 'offline reference only; not current runtime FM backend'}, 'profile_sha256': '51a6af55646fe2adb54bf4f9f3d06e2d262bd830ec5f30f80c0e5bfa2eadf49f', 'parameter_count': 0, 'preset_count': 0}}

def abi_compatible(word):
    return _is_u32(word) and word == FM_ABI_VERSION_WORD

def serialize_command(command):
    if type(command) is not dict or set(command) != set(FM_COMMAND_FIELDS):
        raise ValueError("command must contain exactly the schema fields")
    words = tuple(command[name] for name in FM_COMMAND_FIELDS)
    if not all(_is_u32(word) for word in words):
        raise ValueError("command fields must be uint32 integers, not bool")
    return words

def deserialize_command(words):
    if type(words) not in (list, tuple) or len(words) != FM_COMMAND_WORD_COUNT or not all(_is_u32(word) for word in words):
        raise ValueError("command words require exact length and uint32 integers")
    return dict(zip(FM_COMMAND_FIELDS, words))

def get_profile(profile_id):
    profile = _PROFILES.get(profile_id) if _is_u32(profile_id) else None
    return _copy.deepcopy(profile) if profile is not None else None

def get_parameter(profile_id, parameter_id):
    profile = _PROFILES.get(profile_id) if _is_u32(profile_id) else None
    if profile is None or not _is_u32(parameter_id): return None
    return next((_copy.deepcopy(p) for p in profile["parameters"] if p["id"] == parameter_id), None)

def get_preset(profile_id, preset_id):
    profile = _PROFILES.get(profile_id) if _is_u32(profile_id) else None
    if profile is None or not _is_u32(preset_id): return None
    return next((_copy.deepcopy(p) for p in profile["presets"] if p["id"] == preset_id), None)

def profile_operator_count(profile_id, voice):
    profile = _PROFILES.get(profile_id) if _is_u32(profile_id) else None
    if profile is None or not _is_u32(voice) or voice >= profile["voice_count"]: return None
    return profile["operators_per_voice"][voice]

def _profile_native_mapping(profile_id, logical_operator, kind):
    if not _is_u32(profile_id) or not _is_u32(logical_operator): return 0xffffffff
    profile = _PROFILES.get(profile_id)
    mapping = profile.get("adapter_mapping", {}).get(kind, ()) if profile is not None else ()
    return mapping[logical_operator] if logical_operator < len(mapping) else 0xffffffff

def profile_native_register_slot(profile_id, logical_operator):
    return _profile_native_mapping(profile_id, logical_operator, "register_slots")

def profile_native_key_on_bit(profile_id, logical_operator):
    return _profile_native_mapping(profile_id, logical_operator, "key_on_bits")

def validate_parameter(profile_id, parameter_id, scope, word, value):
    if not all(_is_u32(x) for x in (profile_id, parameter_id, scope, word, value)): return False
    profile = _PROFILES.get(profile_id)
    parameter = get_parameter(profile_id, parameter_id)
    if profile is None or parameter is None or scope != parameter["scope_id"]: return False
    voice, _ = unpack_target(word)
    selected = profile_operator_count(profile_id, voice)
    return (parameter["minimum"] <= value <= parameter["maximum"] and
            validate_target(scope, word, profile["voice_count"], 0 if selected is None else selected, profile["patch_operator_count"]))
