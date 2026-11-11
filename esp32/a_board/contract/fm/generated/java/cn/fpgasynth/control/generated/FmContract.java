// CONTRACT FROZEN / HARDWARE NOT INTEGRATED; 请勿手改 / DO NOT EDIT.
// Generated from schema + backend profiles; transport/backend lifecycle is not implemented by this codec.
// Schema version: 1.0.1
// Schema SHA256: 68d7d85ab97942edb128efcb3dffe01bdc50fb98baf9476ab4663d4f22c2cc73

package cn.fpgasynth.control.generated;

public final class FmContract {
  private FmContract() {}
  public static final String FM_SCHEMA_VERSION = "1.0.1";
  public static final String FM_SCHEMA_SHA256 = "68d7d85ab97942edb128efcb3dffe01bdc50fb98baf9476ab4663d4f22c2cc73";

  public static final long FM_ABI_MAJOR = 0x00000001L;
  public static final long FM_ABI_MINOR = 0x00000001L;
  public static final long FM_CAP_MAGIC = 0x464d4331L;
  public static final long FM_SPI_WORD_BASE = 0x00000600L;
  public static final long FM_SPI_WINDOW_WORDS = 0x00000100L;
  public static final long FM_SPI_WORD_BITS = 0x00000020L;
  public static final long FM_TARGET_NONE = 0x0000ffffL;
  public static final long FM_MAX_WIRE_INDEX = 0x0000fffeL;
  public static final long FM_MAX_DESCRIPTOR_VOICES = 0x00000100L;
  public static final long FM_MAX_OPERATORS_PER_VOICE = 0x00000020L;
  public static final long FM_MAX_PARAMETERS = 0x00000100L;
  public static final long FM_MAX_PRESETS = 0x00000080L;
  public static final long FM_MAX_BACKEND_PROFILES = 0x00000010L;
  public static final long FM_HTTP_API_MAJOR = 0x00000002L;
  public static final long FM_HTTP_API_MINOR = 0x00000000L;
  public static final long FM_HTTP_COMMAND_MAX_BYTES = 0x0000005fL;
  public static final long FM_PERFORMANCE_LEASE_MS = 0x000004b0L;
  public static final long FM_FLAG_READY = 0x00000001L;
  public static final long FM_FLAG_SELECTED = 0x00000002L;
  public static final long FM_FLAG_MUTED = 0x00000004L;
  public static final long FM_FLAG_BUSY = 0x00000008L;
  public static final long FM_FLAG_FAULT = 0x00000010L;
  public static final long FM_FEATURE_NOTE_ON = 0x00000001L;
  public static final long FM_FEATURE_NOTE_OFF = 0x00000002L;
  public static final long FM_FEATURE_SET_PITCH = 0x00000004L;
  public static final long FM_FEATURE_SET_GAIN_PAN = 0x00000008L;
  public static final long FM_FEATURE_SET_PARAMETER = 0x00000010L;
  public static final long FM_FEATURE_APPLY_PRESET = 0x00000020L;
  public static final long FM_FEATURE_RELEASE_ALL = 0x00000040L;
  public static final long FM_FEATURE_PRIORITY_PANIC = 0x00000080L;
  public static final long FM_FEATURE_NEXT_NOTE_PARAMETERS = 0x00000100L;
  public static final long FM_FEATURE_NATIVE_STEREO_PAN = 0x00000200L;
  public static final long FM_ABI_VERSION_WORD = 0x00010001L;
  public static final long FM_HTTP_API_VERSION_WORD = 0x00020000L;
  public static final long FM_ENGINE_DDS = 0x00000000L;
  public static final long FM_ENGINE_KS = 0x00000001L;
  public static final long FM_ENGINE_FM = 0x00000002L;
  public static final long FM_SCOPE_ENGINE = 0x00000000L;
  public static final long FM_SCOPE_VOICE = 0x00000001L;
  public static final long FM_SCOPE_VOICE_OPERATOR = 0x00000002L;
  public static final long FM_SCOPE_PATCH = 0x00000003L;
  public static final long FM_SCOPE_PATCH_OPERATOR = 0x00000004L;
  public static final long FM_SPI_OPCODE_SELECT_BACKEND = 0x00000000L;
  public static final long FM_SPI_OPCODE_PANIC = 0x00000001L;
  public static final long FM_SPI_OPCODE_NOTE_ON = 0x00000002L;
  public static final long FM_SPI_OPCODE_NOTE_OFF = 0x00000003L;
  public static final long FM_SPI_OPCODE_SET_PITCH = 0x00000004L;
  public static final long FM_SPI_OPCODE_SET_GAIN_PAN = 0x00000005L;
  public static final long FM_SPI_OPCODE_SET_PARAMETER = 0x00000006L;
  public static final long FM_SPI_OPCODE_APPLY_PRESET = 0x00000007L;
  public static final long FM_SPI_OPCODE_RELEASE_ALL = 0x00000008L;
  public static final long FM_HTTP_OPCODE_CC = 0x00000002L;
  public static final long FM_HTTP_OPCODE_KEYS_UPDATE = 0x00000003L;
  public static final long FM_HTTP_OPCODE_STOP = 0x00000004L;
  public static final long FM_HTTP_OPCODE_ENGINE_SELECT = 0x00000005L;
  public static final long FM_HTTP_OPCODE_KS_DECAY = 0x00000006L;
  public static final long FM_HTTP_OPCODE_FM_PARAMETER = 0x00000007L;
  public static final long FM_HTTP_OPCODE_FM_PRESET = 0x00000008L;
  public static final long FM_RESULT_OK = 0x00000000L;
  public static final long FM_RESULT_BUSY = 0x00000001L;
  public static final long FM_RESULT_UNSUPPORTED = 0x00000002L;
  public static final long FM_RESULT_BAD_TARGET = 0x00000003L;
  public static final long FM_RESULT_BAD_VALUE = 0x00000004L;
  public static final long FM_RESULT_STALE_EPOCH = 0x00000005L;
  public static final long FM_RESULT_ABI_MISMATCH = 0x00000006L;
  public static final long FM_RESULT_BACKEND_FAULT = 0x00000007L;
  public static final long FM_RESULT_CANCELLED = 0x00000008L;
  public static final long FM_RESULT_SEQUENCE_CONFLICT = 0x00000009L;
  public static final long FM_RESULT_STALE_SEQUENCE = 0x0000000aL;
  public static final long FM_RESULT_STALE_SESSION = 0x0000000bL;
  public static final long FM_RESULT_STALE_TOPOLOGY = 0x0000000cL;
  public static final long FM_RESULT_STALE_VOICE = 0x0000000dL;
  public static final long FM_PHASE_UNAVAILABLE = 0x00000000L;
  public static final long FM_PHASE_INITIALIZING = 0x00000001L;
  public static final long FM_PHASE_READY = 0x00000002L;
  public static final long FM_PHASE_SWITCHING = 0x00000003L;
  public static final long FM_PHASE_RECOVERING = 0x00000004L;
  public static final long FM_PHASE_BLOCKED = 0x00000005L;
  public static final long FM_BACKEND_UNBOUND = 0x00000000L;
  public static final long FM_BACKEND_JT51_OPM = 0x00000001L;
  public static final long FM_BACKEND_MSFA_TWO_OP_REFERENCE = 0x00000002L;
  public static final long FM_REG_CAP_MAGIC = 0x00000600L;
  public static final long FM_REG_ABI_VERSION = 0x00000601L;
  public static final long FM_REG_BACKEND_ID = 0x00000602L;
  public static final long FM_REG_VOICE_COUNT = 0x00000603L;
  public static final long FM_REG_MAX_OPERATOR_COUNT = 0x00000604L;
  public static final long FM_REG_FEATURES = 0x00000605L;
  public static final long FM_REG_EPOCH = 0x00000606L;
  public static final long FM_REG_PHASE = 0x00000607L;
  public static final long FM_REG_ACCEPTED_SEQUENCE = 0x00000608L;
  public static final long FM_REG_COMPLETED_SEQUENCE = 0x00000609L;
  public static final long FM_REG_RESULT_SEQUENCE = 0x0000060aL;
  public static final long FM_REG_RESULT = 0x0000060bL;
  public static final long FM_REG_TOPOLOGY_GENERATION = 0x0000060cL;
  public static final long FM_REG_STATUS_GENERATION = 0x0000060dL;
  public static final long FM_REG_FLAGS = 0x0000060eL;
  public static final long FM_REG_SESSION = 0x0000060fL;
  public static final long FM_REG_CMD_OPCODE_ID = 0x00000610L;
  public static final long FM_REG_CMD_SCOPE_ID = 0x00000611L;
  public static final long FM_REG_CMD_TARGET_WORD = 0x00000612L;
  public static final long FM_REG_CMD_PARAMETER_ID = 0x00000613L;
  public static final long FM_REG_CMD_VALUE_U32 = 0x00000614L;
  public static final long FM_REG_CMD_ARG0_U32 = 0x00000615L;
  public static final long FM_REG_CMD_ARG1_U32 = 0x00000616L;
  public static final long FM_REG_CMD_SESSION_ID = 0x00000617L;
  public static final long FM_REG_CMD_ENGINE_EPOCH = 0x00000618L;
  public static final long FM_REG_CMD_TOPOLOGY_GENERATION = 0x00000619L;
  public static final long FM_REG_CMD_VOICE_GENERATION = 0x0000061aL;
  public static final long FM_REG_CMD_SEQUENCE_ID = 0x0000061bL;
  public static final long FM_REG_CMD_COMMIT = 0x0000061cL;
  public static final long FM_REG_PANIC_SEQUENCE = 0x00000630L;
  public static final long FM_REG_PANIC_ACCEPTED_SEQUENCE = 0x00000631L;
  public static final long FM_REG_PANIC_COMPLETED_SEQUENCE = 0x00000632L;
  public static final long FM_REG_SOURCE_RATE_NUMERATOR = 0x00000640L;
  public static final long FM_REG_SOURCE_RATE_DENOMINATOR = 0x00000641L;
  public static final long FM_REG_OUTPUT_RATE_NUMERATOR = 0x00000642L;
  public static final long FM_REG_OUTPUT_RATE_DENOMINATOR = 0x00000643L;
  public static final long FM_REG_PROFILE_VERSION = 0x00000644L;
  public static final long FM_REG_PARAMETER_COUNT = 0x00000645L;
  public static final long FM_REG_PRESET_COUNT = 0x00000646L;
  public static final long FM_REG_VOICE_QUERY_INDEX = 0x00000647L;
  public static final long FM_REG_VOICE_QUERY_GENERATION = 0x00000648L;
  public static final long FM_REG_VOICE_QUERY_OPERATOR_COUNT = 0x00000649L;
  public static final long FM_REG_PROFILE_SHA256_0 = 0x00000660L;
  public static final long FM_REG_PROFILE_SHA256_1 = 0x00000661L;
  public static final long FM_REG_PROFILE_SHA256_2 = 0x00000662L;
  public static final long FM_REG_PROFILE_SHA256_3 = 0x00000663L;
  public static final long FM_REG_PROFILE_SHA256_4 = 0x00000664L;
  public static final long FM_REG_PROFILE_SHA256_5 = 0x00000665L;
  public static final long FM_REG_PROFILE_SHA256_6 = 0x00000666L;
  public static final long FM_REG_PROFILE_SHA256_7 = 0x00000667L;
  public static final long FM_REG_RESULT_SESSION = 0x00000620L;
  public static final long FM_REG_RESULT_EPOCH = 0x00000621L;
  public static final long FM_REG_ACCEPTED_SESSION = 0x00000622L;
  public static final long FM_REG_ACCEPTED_EPOCH = 0x00000623L;
  public static final long FM_REG_COMPLETED_SESSION = 0x00000624L;
  public static final long FM_REG_COMPLETED_EPOCH = 0x00000625L;
  public static final long FM_REG_PANIC_SESSION = 0x00000626L;
  public static final long FM_REG_PANIC_EPOCH = 0x00000627L;
  public static final long FM_REG_PANIC_RESULT_SEQUENCE = 0x00000633L;
  public static final long FM_REG_PANIC_RESULT = 0x00000634L;
  public static final long FM_REG_PANIC_RESULT_SESSION = 0x00000635L;
  public static final long FM_REG_PANIC_RESULT_EPOCH = 0x00000636L;
  public static final long FM_COMMAND_WORD_COUNT = 0x0000000cL;
  public static final long FM_COMMAND_BYTE_COUNT = 0x00000030L;
  public static final long FM_CMD_WORD_OPCODE_ID = 0x00000000L;
  public static final long FM_CMD_WORD_SCOPE_ID = 0x00000001L;
  public static final long FM_CMD_WORD_TARGET_WORD = 0x00000002L;
  public static final long FM_CMD_WORD_PARAMETER_ID = 0x00000003L;
  public static final long FM_CMD_WORD_VALUE_U32 = 0x00000004L;
  public static final long FM_CMD_WORD_ARG0_U32 = 0x00000005L;
  public static final long FM_CMD_WORD_ARG1_U32 = 0x00000006L;
  public static final long FM_CMD_WORD_SESSION_ID = 0x00000007L;
  public static final long FM_CMD_WORD_ENGINE_EPOCH = 0x00000008L;
  public static final long FM_CMD_WORD_TOPOLOGY_GENERATION = 0x00000009L;
  public static final long FM_CMD_WORD_VOICE_GENERATION = 0x0000000aL;
  public static final long FM_CMD_WORD_SEQUENCE_ID = 0x0000000bL;
  public static final long FM_UNIT_CHIP_CODE = 0x00000000L;
  public static final long FM_APPLY_TIME_NEXT_NOTE = 0x00000000L;
  public static final long FM_VALUE_TYPE_U32 = 0x00000000L;

  private static boolean isU32(long value) {
    return value >= 0 && value <= 0xffffffffL;
  }

  public static long packTarget(long voice, long operator) {
    if (!isU32(voice) || voice > 0xffffL ||
        !isU32(operator) || operator > 0xffffL) {
      throw new IllegalArgumentException("target fields must be unsigned 16-bit integers");
    }
    return (voice << 16) | (operator << 0);
  }

  private static void requireWord(long word) {
    if (!isU32(word)) {
      throw new IllegalArgumentException("target word must be a uint32 integer");
    }
  }

  public static long targetVoice(long word) {
    requireWord(word);
    return (word >>> 16) & 0xffffL;
  }

  public static long targetOperator(long word) {
    requireWord(word);
    return (word >>> 0) & 0xffffL;
  }

  public static boolean validateTarget(long scope, long word, long voice_count,
                                       long selected_operator_count, long patch_operator_count) {
    if (!isU32(scope) || !isU32(word) || !isU32(voice_count) ||
        !isU32(selected_operator_count) || !isU32(patch_operator_count)) return false;
    if (voice_count < 1 || voice_count > FM_MAX_DESCRIPTOR_VOICES) return false;
    if (selected_operator_count < 0 || selected_operator_count > FM_MAX_OPERATORS_PER_VOICE) return false;
    if (patch_operator_count < 0 || patch_operator_count > FM_MAX_OPERATORS_PER_VOICE) return false;
    long voice = targetVoice(word);
    long operator = targetOperator(word);
    if (scope == FM_SCOPE_ENGINE) return (voice == FM_TARGET_NONE) && (operator == FM_TARGET_NONE);
    if (scope == FM_SCOPE_VOICE) return (voice < voice_count) && (operator == FM_TARGET_NONE);
    if (scope == FM_SCOPE_VOICE_OPERATOR) return (voice < voice_count) && (operator < selected_operator_count);
    if (scope == FM_SCOPE_PATCH) return (voice == FM_TARGET_NONE) && (operator == FM_TARGET_NONE);
    if (scope == FM_SCOPE_PATCH_OPERATOR) return (voice == FM_TARGET_NONE) && (operator < patch_operator_count);
    return false;
  }
  public static final long FM_PROFILE_JT51_OPM_VOICE_COUNT = 32L;
  public static final long FM_PROFILE_JT51_OPM_PATCH_OPERATOR_COUNT = 4L;
  public static final long FM_PROFILE_JT51_OPM_MAX_OPERATOR_COUNT = 4L;
  public static final long FM_PROFILE_MSFA_TWO_OP_REFERENCE_VOICE_COUNT = 8L;
  public static final long FM_PROFILE_MSFA_TWO_OP_REFERENCE_PATCH_OPERATOR_COUNT = 2L;
  public static final long FM_PROFILE_MSFA_TWO_OP_REFERENCE_MAX_OPERATOR_COUNT = 2L;
  public static final String FM_PROFILES_SHA256 = "e0eb566504f7f06c41ded10eb4a91abdc9bc04cab6638d2ffea48d177ab68fbd";
  public static final String FM_CONTRACT_SHA256 = "100bc058603e6184741bc2aba62745622e1b881290a642f33b4c46c96340d123";
  private static final long[] PROFILES_SHA256_WORDS = {0xe0eb5665L, 0x04f7f06cL, 0x41ded10eL, 0xb4a91abdL, 0xc9bc04caL, 0xb6638d2fL, 0xfea48d17L, 0x7ab68fbdL};
  public static long profilesSha256Word(long index) { return index >= 0 && index < 8 ? PROFILES_SHA256_WORDS[(int)index] : -1; }
  public static boolean abiCompatible(long word) { return isU32(word) && word == FM_ABI_VERSION_WORD; }
  public static final class Command {
    public final long opcode_id;
    public final long scope_id;
    public final long target_word;
    public final long parameter_id;
    public final long value_u32;
    public final long arg0_u32;
    public final long arg1_u32;
    public final long session_id;
    public final long engine_epoch;
    public final long topology_generation;
    public final long voice_generation;
    public final long sequence_id;
    public Command(long opcode_id, long scope_id, long target_word, long parameter_id, long value_u32, long arg0_u32, long arg1_u32, long session_id, long engine_epoch, long topology_generation, long voice_generation, long sequence_id) {
      if (!(isU32(opcode_id) && isU32(scope_id) && isU32(target_word) && isU32(parameter_id) && isU32(value_u32) && isU32(arg0_u32) && isU32(arg1_u32) && isU32(session_id) && isU32(engine_epoch) && isU32(topology_generation) && isU32(voice_generation) && isU32(sequence_id)))
        throw new IllegalArgumentException("command fields must be uint32");
      this.opcode_id = opcode_id;
      this.scope_id = scope_id;
      this.target_word = target_word;
      this.parameter_id = parameter_id;
      this.value_u32 = value_u32;
      this.arg0_u32 = arg0_u32;
      this.arg1_u32 = arg1_u32;
      this.session_id = session_id;
      this.engine_epoch = engine_epoch;
      this.topology_generation = topology_generation;
      this.voice_generation = voice_generation;
      this.sequence_id = sequence_id;
    }
  }
  public static long[] serializeCommand(Command command) {
    if (command == null) throw new IllegalArgumentException("command is null");
    return new long[] {command.opcode_id, command.scope_id, command.target_word, command.parameter_id, command.value_u32, command.arg0_u32, command.arg1_u32, command.session_id, command.engine_epoch, command.topology_generation, command.voice_generation, command.sequence_id};
  }
  public static Command deserializeCommand(long[] words) {
    if (words == null || words.length != FM_COMMAND_WORD_COUNT) throw new IllegalArgumentException("command length");
    return new Command(words[(int)FM_CMD_WORD_OPCODE_ID], words[(int)FM_CMD_WORD_SCOPE_ID], words[(int)FM_CMD_WORD_TARGET_WORD], words[(int)FM_CMD_WORD_PARAMETER_ID], words[(int)FM_CMD_WORD_VALUE_U32], words[(int)FM_CMD_WORD_ARG0_U32], words[(int)FM_CMD_WORD_ARG1_U32], words[(int)FM_CMD_WORD_SESSION_ID], words[(int)FM_CMD_WORD_ENGINE_EPOCH], words[(int)FM_CMD_WORD_TOPOLOGY_GENERATION], words[(int)FM_CMD_WORD_VOICE_GENERATION], words[(int)FM_CMD_WORD_SEQUENCE_ID]);
  }
  public static final class Parameter {
    public final long id, minimum, maximum, scope_id, unit_id, apply_time_id, value_type_id;
    public final String name;
    private Parameter(long id, String name, long minimum, long maximum, long scope_id, long unit_id, long apply_time_id, long value_type_id) {
      this.id=id; this.name=name; this.minimum=minimum; this.maximum=maximum; this.scope_id=scope_id;
      this.unit_id=unit_id; this.apply_time_id=apply_time_id; this.value_type_id=value_type_id;
    }
  }
  public static final class PresetValue {
    public final long parameter_id, operator_index, value_u32;
    private PresetValue(long p, long o, long v) { parameter_id=p; operator_index=o; value_u32=v; }
  }
  public static final class Preset {
    public final long id, key_on_logical_mask, values_count; public final String name;
    private final PresetValue[] values;
    private Preset(long id, String name, long mask, PresetValue[] values) {
      this.id=id; this.name=name; key_on_logical_mask=mask; this.values=values; values_count=values.length;
    }
    public PresetValue value(long index) { return index >= 0 && index < values.length ? values[(int)index] : null; }
  }
  public static final class Profile {
    public final long id, backend_id, profile_version, voice_count, patch_operator_count, parameter_count, preset_count;
    public final boolean runtime_available; public final String key, profile_sha256;
    private final long[] operators_per_voice; private final String[] operator_labels;
    private final Parameter[] parameters; private final Preset[] presets;
    private Profile(long id, long backend_id, long version, String key, String digest, long patchCount, long[] counts, String[] labels, Parameter[] parameters, Preset[] presets) {
      this.id=id; this.backend_id=backend_id; profile_version=version; this.key=key; profile_sha256=digest;
      patch_operator_count=patchCount; operators_per_voice=counts; operator_labels=labels; voice_count=counts.length;
      this.parameters=parameters; this.presets=presets; parameter_count=parameters.length; preset_count=presets.length; runtime_available=false;
    }
    public String operatorLabel(long index) { return index >= 0 && index < operator_labels.length ? operator_labels[(int)index] : null; }
  }
  private static final Profile[] PROFILES = {
    new Profile(1L, 1L, 65536L, "jt51_opm", "e0eb566504f7f06c41ded10eb4a91abdc9bc04cab6638d2ffea48d177ab68fbd", 4L,
      new long[] {4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L, 4L},
      new String[] {"M1", "C1", "M2", "C2"}, new Parameter[] {
        new Parameter(1L, "algorithm", 0L, 7L, 3L, 0L, 0L, 0L),
        new Parameter(2L, "feedback", 0L, 7L, 3L, 0L, 0L, 0L),
        new Parameter(16L, "multiplier", 0L, 15L, 4L, 0L, 0L, 0L),
        new Parameter(17L, "total_level", 0L, 127L, 4L, 0L, 0L, 0L),
        new Parameter(18L, "attack_rate", 0L, 31L, 4L, 0L, 0L, 0L),
        new Parameter(19L, "decay1_rate", 0L, 31L, 4L, 0L, 0L, 0L),
        new Parameter(20L, "decay2_rate", 0L, 31L, 4L, 0L, 0L, 0L),
        new Parameter(21L, "sustain_level", 0L, 15L, 4L, 0L, 0L, 0L),
        new Parameter(22L, "release_rate", 0L, 15L, 4L, 0L, 0L, 0L),
        new Parameter(23L, "detune1", 0L, 7L, 4L, 0L, 0L, 0L),
        new Parameter(24L, "detune2", 0L, 3L, 4L, 0L, 0L, 0L),
        new Parameter(25L, "rate_scaling", 0L, 3L, 4L, 0L, 0L, 0L),
        new Parameter(26L, "am_enable", 0L, 1L, 4L, 0L, 0L, 0L),
      }, new Preset[] {
        new Preset(0L, "diagnostic_sine", 1L, new PresetValue[] {
          new PresetValue(1L, 65535L, 7L),
          new PresetValue(2L, 65535L, 0L),
          new PresetValue(16L, 0L, 1L),
          new PresetValue(17L, 0L, 8L),
          new PresetValue(18L, 0L, 31L),
          new PresetValue(19L, 0L, 0L),
          new PresetValue(20L, 0L, 0L),
          new PresetValue(21L, 0L, 0L),
          new PresetValue(22L, 0L, 15L),
          new PresetValue(23L, 0L, 0L),
          new PresetValue(24L, 0L, 0L),
          new PresetValue(25L, 0L, 0L),
          new PresetValue(26L, 0L, 0L),
          new PresetValue(16L, 1L, 1L),
          new PresetValue(17L, 1L, 127L),
          new PresetValue(18L, 1L, 31L),
          new PresetValue(19L, 1L, 0L),
          new PresetValue(20L, 1L, 0L),
          new PresetValue(21L, 1L, 0L),
          new PresetValue(22L, 1L, 15L),
          new PresetValue(23L, 1L, 0L),
          new PresetValue(24L, 1L, 0L),
          new PresetValue(25L, 1L, 0L),
          new PresetValue(26L, 1L, 0L),
          new PresetValue(16L, 2L, 1L),
          new PresetValue(17L, 2L, 127L),
          new PresetValue(18L, 2L, 31L),
          new PresetValue(19L, 2L, 0L),
          new PresetValue(20L, 2L, 0L),
          new PresetValue(21L, 2L, 0L),
          new PresetValue(22L, 2L, 15L),
          new PresetValue(23L, 2L, 0L),
          new PresetValue(24L, 2L, 0L),
          new PresetValue(25L, 2L, 0L),
          new PresetValue(26L, 2L, 0L),
          new PresetValue(16L, 3L, 1L),
          new PresetValue(17L, 3L, 127L),
          new PresetValue(18L, 3L, 31L),
          new PresetValue(19L, 3L, 0L),
          new PresetValue(20L, 3L, 0L),
          new PresetValue(21L, 3L, 0L),
          new PresetValue(22L, 3L, 15L),
          new PresetValue(23L, 3L, 0L),
          new PresetValue(24L, 3L, 0L),
          new PresetValue(25L, 3L, 0L),
          new PresetValue(26L, 3L, 0L),
        }),
        new Preset(1L, "two_op_keys", 3L, new PresetValue[] {
          new PresetValue(1L, 65535L, 4L),
          new PresetValue(2L, 65535L, 0L),
          new PresetValue(16L, 0L, 1L),
          new PresetValue(17L, 0L, 28L),
          new PresetValue(18L, 0L, 28L),
          new PresetValue(19L, 0L, 10L),
          new PresetValue(20L, 0L, 2L),
          new PresetValue(21L, 0L, 5L),
          new PresetValue(22L, 0L, 14L),
          new PresetValue(23L, 0L, 0L),
          new PresetValue(24L, 0L, 0L),
          new PresetValue(25L, 0L, 0L),
          new PresetValue(26L, 0L, 0L),
          new PresetValue(16L, 1L, 1L),
          new PresetValue(17L, 1L, 8L),
          new PresetValue(18L, 1L, 25L),
          new PresetValue(19L, 1L, 8L),
          new PresetValue(20L, 1L, 2L),
          new PresetValue(21L, 1L, 4L),
          new PresetValue(22L, 1L, 14L),
          new PresetValue(23L, 1L, 0L),
          new PresetValue(24L, 1L, 0L),
          new PresetValue(25L, 1L, 0L),
          new PresetValue(26L, 1L, 0L),
          new PresetValue(16L, 2L, 1L),
          new PresetValue(17L, 2L, 127L),
          new PresetValue(18L, 2L, 31L),
          new PresetValue(19L, 2L, 0L),
          new PresetValue(20L, 2L, 0L),
          new PresetValue(21L, 2L, 0L),
          new PresetValue(22L, 2L, 14L),
          new PresetValue(23L, 2L, 0L),
          new PresetValue(24L, 2L, 0L),
          new PresetValue(25L, 2L, 0L),
          new PresetValue(26L, 2L, 0L),
          new PresetValue(16L, 3L, 1L),
          new PresetValue(17L, 3L, 127L),
          new PresetValue(18L, 3L, 31L),
          new PresetValue(19L, 3L, 0L),
          new PresetValue(20L, 3L, 0L),
          new PresetValue(21L, 3L, 0L),
          new PresetValue(22L, 3L, 14L),
          new PresetValue(23L, 3L, 0L),
          new PresetValue(24L, 3L, 0L),
          new PresetValue(25L, 3L, 0L),
          new PresetValue(26L, 3L, 0L),
        }),
        new Preset(2L, "four_op_bell", 15L, new PresetValue[] {
          new PresetValue(1L, 65535L, 0L),
          new PresetValue(2L, 65535L, 2L),
          new PresetValue(16L, 0L, 1L),
          new PresetValue(17L, 0L, 26L),
          new PresetValue(18L, 0L, 31L),
          new PresetValue(19L, 0L, 12L),
          new PresetValue(20L, 0L, 5L),
          new PresetValue(21L, 0L, 8L),
          new PresetValue(22L, 0L, 15L),
          new PresetValue(23L, 0L, 0L),
          new PresetValue(24L, 0L, 0L),
          new PresetValue(25L, 0L, 0L),
          new PresetValue(26L, 0L, 0L),
          new PresetValue(16L, 1L, 3L),
          new PresetValue(17L, 1L, 30L),
          new PresetValue(18L, 1L, 31L),
          new PresetValue(19L, 1L, 8L),
          new PresetValue(20L, 1L, 2L),
          new PresetValue(21L, 1L, 4L),
          new PresetValue(22L, 1L, 15L),
          new PresetValue(23L, 1L, 0L),
          new PresetValue(24L, 1L, 0L),
          new PresetValue(25L, 1L, 0L),
          new PresetValue(26L, 1L, 0L),
          new PresetValue(16L, 2L, 2L),
          new PresetValue(17L, 2L, 42L),
          new PresetValue(18L, 2L, 31L),
          new PresetValue(19L, 2L, 9L),
          new PresetValue(20L, 2L, 3L),
          new PresetValue(21L, 2L, 6L),
          new PresetValue(22L, 2L, 15L),
          new PresetValue(23L, 2L, 0L),
          new PresetValue(24L, 2L, 0L),
          new PresetValue(25L, 2L, 0L),
          new PresetValue(26L, 2L, 0L),
          new PresetValue(16L, 3L, 1L),
          new PresetValue(17L, 3L, 8L),
          new PresetValue(18L, 3L, 31L),
          new PresetValue(19L, 3L, 8L),
          new PresetValue(20L, 3L, 3L),
          new PresetValue(21L, 3L, 5L),
          new PresetValue(22L, 3L, 15L),
          new PresetValue(23L, 3L, 0L),
          new PresetValue(24L, 3L, 0L),
          new PresetValue(25L, 3L, 0L),
          new PresetValue(26L, 3L, 0L),
        }),
        new Preset(3L, "electric_piano_tine", 15L, new PresetValue[] {
          new PresetValue(1L, 65535L, 4L),
          new PresetValue(2L, 65535L, 0L),
          new PresetValue(16L, 0L, 1L),
          new PresetValue(17L, 0L, 32L),
          new PresetValue(18L, 0L, 31L),
          new PresetValue(19L, 0L, 14L),
          new PresetValue(20L, 0L, 5L),
          new PresetValue(21L, 0L, 10L),
          new PresetValue(22L, 0L, 10L),
          new PresetValue(23L, 0L, 0L),
          new PresetValue(24L, 0L, 0L),
          new PresetValue(25L, 0L, 0L),
          new PresetValue(26L, 0L, 0L),
          new PresetValue(16L, 1L, 1L),
          new PresetValue(17L, 1L, 10L),
          new PresetValue(18L, 1L, 31L),
          new PresetValue(19L, 1L, 8L),
          new PresetValue(20L, 1L, 3L),
          new PresetValue(21L, 1L, 8L),
          new PresetValue(22L, 1L, 10L),
          new PresetValue(23L, 1L, 0L),
          new PresetValue(24L, 1L, 0L),
          new PresetValue(25L, 1L, 0L),
          new PresetValue(26L, 1L, 0L),
          new PresetValue(16L, 2L, 14L),
          new PresetValue(17L, 2L, 40L),
          new PresetValue(18L, 2L, 31L),
          new PresetValue(19L, 2L, 18L),
          new PresetValue(20L, 2L, 7L),
          new PresetValue(21L, 2L, 12L),
          new PresetValue(22L, 2L, 10L),
          new PresetValue(23L, 2L, 0L),
          new PresetValue(24L, 2L, 0L),
          new PresetValue(25L, 2L, 0L),
          new PresetValue(26L, 2L, 0L),
          new PresetValue(16L, 3L, 1L),
          new PresetValue(17L, 3L, 18L),
          new PresetValue(18L, 3L, 31L),
          new PresetValue(19L, 3L, 12L),
          new PresetValue(20L, 3L, 5L),
          new PresetValue(21L, 3L, 10L),
          new PresetValue(22L, 3L, 10L),
          new PresetValue(23L, 3L, 0L),
          new PresetValue(24L, 3L, 0L),
          new PresetValue(25L, 3L, 0L),
          new PresetValue(26L, 3L, 0L),
        }),
        new Preset(4L, "metal_bell", 15L, new PresetValue[] {
          new PresetValue(1L, 65535L, 4L),
          new PresetValue(2L, 65535L, 0L),
          new PresetValue(16L, 0L, 3L),
          new PresetValue(17L, 0L, 24L),
          new PresetValue(18L, 0L, 31L),
          new PresetValue(19L, 0L, 10L),
          new PresetValue(20L, 0L, 5L),
          new PresetValue(21L, 0L, 12L),
          new PresetValue(22L, 0L, 6L),
          new PresetValue(23L, 0L, 0L),
          new PresetValue(24L, 0L, 1L),
          new PresetValue(25L, 0L, 0L),
          new PresetValue(26L, 0L, 0L),
          new PresetValue(16L, 1L, 1L),
          new PresetValue(17L, 1L, 10L),
          new PresetValue(18L, 1L, 31L),
          new PresetValue(19L, 1L, 6L),
          new PresetValue(20L, 1L, 3L),
          new PresetValue(21L, 1L, 11L),
          new PresetValue(22L, 1L, 6L),
          new PresetValue(23L, 1L, 0L),
          new PresetValue(24L, 1L, 0L),
          new PresetValue(25L, 1L, 0L),
          new PresetValue(26L, 1L, 0L),
          new PresetValue(16L, 2L, 7L),
          new PresetValue(17L, 2L, 34L),
          new PresetValue(18L, 2L, 31L),
          new PresetValue(19L, 2L, 13L),
          new PresetValue(20L, 2L, 6L),
          new PresetValue(21L, 2L, 12L),
          new PresetValue(22L, 2L, 6L),
          new PresetValue(23L, 2L, 0L),
          new PresetValue(24L, 2L, 0L),
          new PresetValue(25L, 2L, 0L),
          new PresetValue(26L, 2L, 0L),
          new PresetValue(16L, 3L, 2L),
          new PresetValue(17L, 3L, 20L),
          new PresetValue(18L, 3L, 31L),
          new PresetValue(19L, 3L, 8L),
          new PresetValue(20L, 3L, 4L),
          new PresetValue(21L, 3L, 12L),
          new PresetValue(22L, 3L, 6L),
          new PresetValue(23L, 3L, 0L),
          new PresetValue(24L, 3L, 0L),
          new PresetValue(25L, 3L, 0L),
          new PresetValue(26L, 3L, 0L),
        }),
      }),
    new Profile(2L, 2L, 65536L, "msfa_two_op_reference", "e0eb566504f7f06c41ded10eb4a91abdc9bc04cab6638d2ffea48d177ab68fbd", 2L,
      new long[] {2L, 2L, 2L, 2L, 2L, 2L, 2L, 2L},
      new String[] {"modulator", "carrier"}, new Parameter[] {
      }, new Preset[] {
      }),
  };
  public static Profile getProfile(long id) {
    if (!isU32(id)) return null; for (Profile p : PROFILES) if (p.id == id) return p; return null;
  }
  public static Parameter getParameter(long id, long parameterId) {
    Profile p=getProfile(id); if (p == null || !isU32(parameterId)) return null;
    for (Parameter value : p.parameters) if (value.id == parameterId) return value; return null;
  }
  public static Preset getPreset(long id, long presetId) {
    Profile p=getProfile(id); if (p == null || !isU32(presetId)) return null;
    for (Preset value : p.presets) if (value.id == presetId) return value; return null;
  }
  public static long profileOperatorCount(long id, long voice) {
    Profile p=getProfile(id); return p != null && isU32(voice) && voice < p.voice_count ? p.operators_per_voice[(int)voice] : -1;
  }
  public static boolean validateParameter(long id, long parameterId, long scope, long word, long value) {
    if (!isU32(id) || !isU32(parameterId) || !isU32(scope) || !isU32(word) || !isU32(value)) return false;
    Profile p=getProfile(id); Parameter parameter=getParameter(id, parameterId);
    if (p == null || parameter == null || scope != parameter.scope_id || value < parameter.minimum || value > parameter.maximum) return false;
    long selected=profileOperatorCount(id, targetVoice(word));
    return validateTarget(scope, word, p.voice_count, selected < 0 ? 0 : selected, p.patch_operator_count);
  }

  public static long profileNativeRegisterSlot(long id, long logical_operator) {
    if (!isU32(id) || !isU32(logical_operator)) return 0xffffffffL;
    if (id == 1L && logical_operator == 0L) return 0L;
    if (id == 1L && logical_operator == 1L) return 2L;
    if (id == 1L && logical_operator == 2L) return 1L;
    if (id == 1L && logical_operator == 3L) return 3L;
    return 0xffffffffL;
  }
  public static long profileNativeKeyOnBit(long id, long logical_operator) {
    if (!isU32(id) || !isU32(logical_operator)) return 0xffffffffL;
    if (id == 1L && logical_operator == 0L) return 3L;
    if (id == 1L && logical_operator == 1L) return 4L;
    if (id == 1L && logical_operator == 2L) return 5L;
    if (id == 1L && logical_operator == 3L) return 6L;
    return 0xffffffffL;
  }
}
