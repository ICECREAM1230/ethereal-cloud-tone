"""Schema-driven ABI 1.1 command and catalog generation (standard library only)."""

import hashlib
import json

COMMAND_FIELDS = ("opcode_id", "scope_id", "target_word", "parameter_id", "value_u32",
                  "arg0_u32", "arg1_u32", "session_id", "engine_epoch",
                  "topology_generation", "voice_generation", "sequence_id")


def validate_extension(schema, require, integer, identifier):
    command = schema.get("command")
    require(isinstance(command, dict), "command must be an object")
    require(command.get("word_bits") == 32 and type(command.get("word_bits")) is int,
            "command.word_bits must be integer 32")
    require(integer(command.get("word_count"), "command.word_count", 1, 256) == 12,
            "ABI 1.1 has exactly 12 command words")
    require(command.get("word_byte_order") == "msb_first", "command word byte order must be msb_first")
    fields = command.get("fields")
    require(isinstance(fields, list) and len(fields) == command["word_count"], "command field count mismatch")
    names = []
    register_names = []
    for index, field in enumerate(fields):
        require(isinstance(field, dict), "command field must be an object")
        name = identifier(field.get("name"), "command field name")
        names.append(name)
        require(integer(field.get("word_index"), f"command.{name}.word_index", 0, 11) == index,
                "command word indices must match list order")
        require(integer(field.get("width_bits"), f"command.{name}.width_bits", 1, 32) == 32,
                "command field width must be 32")
        require(field.get("value_type") == "u32", "command field type must be u32")
        register = identifier(field.get("register"), "command register")
        require(register in schema["spi"]["registers"], "command register is missing")
        register_names.append(register)
    require(tuple(names) == COMMAND_FIELDS, "ABI 1.1 command fields/order are frozen")
    require(len(set(register_names)) == len(register_names), "command registers collide")
    start = schema["spi"]["registers"][register_names[0]]
    require(all(schema["spi"]["registers"][register] == start + index
                for index, register in enumerate(register_names)), "command register words must be consecutive")
    require(command.get("commit_register") in schema["spi"]["registers"], "commit register is missing")
    require(schema["spi"]["registers"][command["commit_register"]] == start + len(fields),
            "commit must follow the command words")
    require(command.get("commit_value_field") == "sequence_id", "commit must use sequence_id")
    catalog = schema.get("catalog")
    require(isinstance(catalog, dict), "catalog must be an object")
    require(catalog.get("file") == "backend-profiles.json", "catalog file must be backend-profiles.json")
    for group, needed in (("units", "chip_code"), ("apply_times", "next_note"), ("value_types", "u32")):
        members = catalog.get(group)
        require(isinstance(members, dict) and needed in members, f"catalog.{group} lacks {needed}")
        values = set()
        for name, value in members.items():
            identifier(name, f"catalog.{group} name")
            integer(value, f"catalog.{group}.{name}")
            require(value not in values, f"catalog.{group} duplicate enum value")
            values.add(value)


def validate_profiles(catalog, schema, require, integer, identifier):
    require(isinstance(catalog, dict), "backend profiles must be an object")
    require(catalog.get("catalog_version") == "1.0.0", "unsupported catalog_version")
    require(catalog.get("status") == "contract_frozen_not_integrated", "catalog must remain not integrated")
    profiles = catalog.get("profiles")
    limits = schema["constants"]
    require(isinstance(profiles, list) and 1 <= len(profiles) <= limits["MAX_BACKEND_PROFILES"],
            "profile count exceeds parser budget")
    ids, keys, capability_identities = set(), set(), set()
    for profile in profiles:
        require(isinstance(profile, dict), "profile must be an object")
        profile_id = integer(profile.get("id"), "profile.id", 1)
        key = identifier(profile.get("key"), "profile.key")
        require(profile_id not in ids and key not in keys, "profile id/key collision")
        ids.add(profile_id); keys.add(key)
        backend_id = integer(profile.get("backend_id"), "profile.backend_id", 1)
        profile_version = integer(profile.get("profile_version"), "profile.profile_version", 1)
        require((backend_id, profile_version) not in capability_identities, "backend/version identity collision")
        capability_identities.add((backend_id, profile_version))
        require(profile.get("role") in ("implementation_target", "offline_reference"), "unknown profile role")
        require(profile.get("runtime_available") is False, "offline catalog cannot assert runtime availability")
        voices = integer(profile.get("voice_count"), "profile.voice_count", 1, limits["MAX_DESCRIPTOR_VOICES"])
        counts = profile.get("operators_per_voice")
        require(isinstance(counts, list) and len(counts) == voices, "operator count list must cover every voice")
        for count in counts:
            integer(count, "operators_per_voice element", 0, limits["MAX_OPERATORS_PER_VOICE"])
        patch_count = integer(profile.get("patch_operator_count"), "patch_operator_count", 0,
                              limits["MAX_OPERATORS_PER_VOICE"])
        labels = profile.get("operator_labels")
        require(isinstance(labels, list) and len(labels) >= max([patch_count] + counts) and
                len(labels) <= limits["MAX_OPERATORS_PER_VOICE"], "operator labels do not cover logical indices")
        require(all(isinstance(label, str) and label for label in labels) and len(set(labels)) == len(labels),
                "operator labels must be unique nonempty strings")
        mapping = profile.get("adapter_mapping")
        if mapping is not None:
            require(isinstance(mapping, dict), "adapter_mapping must be an object")
            for name, values in mapping.items():
                require(name in ("register_slots", "key_on_bits"), "unknown adapter mapping")
                require(isinstance(values, list) and len(values) == len(labels), "adapter mapping length mismatch")
                for value in values:
                    integer(value, "adapter mapping value", 0, 31)
                require(len(set(values)) == len(values), "adapter mapping collides")
        parameters = profile.get("parameters")
        presets = profile.get("presets")
        require(isinstance(parameters, list) and len(parameters) <= limits["MAX_PARAMETERS"], "parameter budget")
        require(isinstance(presets, list) and len(presets) <= limits["MAX_PRESETS"], "preset budget")
        parameter_ids, parameter_names = {}, set()
        for parameter in parameters:
            require(isinstance(parameter, dict), "parameter must be an object")
            pid = integer(parameter.get("id"), "parameter.id")
            name = identifier(parameter.get("name"), "parameter.name")
            require(pid not in parameter_ids and name not in parameter_names, "parameter id/name collision")
            parameter_ids[pid] = parameter; parameter_names.add(name)
            minimum = integer(parameter.get("minimum"), "parameter.minimum")
            maximum = integer(parameter.get("maximum"), "parameter.maximum")
            require(minimum <= maximum, "parameter range is reversed")
            require(isinstance(parameter.get("scope"), str) and parameter["scope"] in schema["enums"]["scope"], "unknown parameter scope")
            for singular, plural in (("unit", "units"), ("apply_time", "apply_times"), ("value_type", "value_types")):
                require(isinstance(parameter.get(singular), str) and parameter[singular] in schema["catalog"][plural], f"unknown parameter {singular}")
            require(parameter["value_type"] == "u32", "only u32 parameter values are supported")
        preset_ids, preset_names = set(), set()
        for preset in presets:
            require(isinstance(preset, dict), "preset must be an object")
            pid = integer(preset.get("id"), "preset.id")
            name = identifier(preset.get("name"), "preset.name")
            require(pid not in preset_ids and name not in preset_names, "preset id/name collision")
            preset_ids.add(pid); preset_names.add(name)
            integer(preset.get("key_on_logical_mask", 0), "preset.key_on_logical_mask", 0,
                    min((1 << patch_count) - 1, 0xffffffff))
            values = preset.get("values")
            require(isinstance(values, list) and len(values) <= limits["MAX_PARAMETERS"] *
                    (limits["MAX_OPERATORS_PER_VOICE"] + 1), "preset value budget")
            seen = set()
            for value in values:
                require(isinstance(value, dict), "preset value must be an object")
                parameter_id = integer(value.get("parameter_id"), "preset parameter_id")
                operator_index = integer(value.get("operator_index"), "preset operator_index", 0, 65535)
                word = integer(value.get("value_u32"), "preset value_u32")
                require(parameter_id in parameter_ids, "preset refers to unknown parameter")
                parameter = parameter_ids[parameter_id]
                require(parameter["minimum"] <= word <= parameter["maximum"], "preset value outside parameter range")
                require(parameter["scope"] in ("PATCH", "PATCH_OPERATOR"), "preset requires patch parameter scope")
                require((parameter["scope"] == "PATCH" and operator_index == 65535) or
                        (parameter["scope"] == "PATCH_OPERATOR" and operator_index < patch_count),
                        "preset operator is invalid for parameter scope")
                require((parameter_id, operator_index) not in seen, "duplicate preset parameter target")
                seen.add((parameter_id, operator_index))
    return catalog


def identities(raw_schema, raw_profiles):
    schema_digest = hashlib.sha256(raw_schema).digest()
    profiles_digest = hashlib.sha256(raw_profiles).digest()
    return {"schema": schema_digest.hex(), "profiles": profiles_digest.hex(),
            "contract": hashlib.sha256(b"FM_CONTROL_IDENTITY_V1\0" + schema_digest + profiles_digest).hexdigest()}


def normalized_profiles(schema, catalog, catalog_digest):
    result = []
    for profile in catalog["profiles"]:
        info = dict(profile)
        # Capability PROFILE_SHA256 identifies the complete catalog, paired with backend/version.
        info["profile_sha256"] = catalog_digest
        info["parameter_count"] = len(profile["parameters"])
        info["preset_count"] = len(profile["presets"])
        info["parameters"] = []
        for parameter in profile["parameters"]:
            item = dict(parameter)
            item.update(scope_id=schema["enums"]["scope"][parameter["scope"]],
                        unit_id=schema["catalog"]["units"][parameter["unit"]],
                        apply_time_id=schema["catalog"]["apply_times"][parameter["apply_time"]],
                        value_type_id=schema["catalog"]["value_types"][parameter["value_type"]])
            info["parameters"].append(item)
        result.append(info)
    return result


def render_extension(schema, catalog, identity, language):
    profiles = normalized_profiles(schema, catalog, identity["profiles"])
    dimensions = []
    for profile in profiles:
        prefix = "FM_PROFILE_" + profile["key"].upper()
        for suffix, value in (("VOICE_COUNT", profile["voice_count"]),
                              ("PATCH_OPERATOR_COUNT", profile["patch_operator_count"]),
                              ("MAX_OPERATOR_COUNT", max(profile["operators_per_voice"]))):
            name = prefix + "_" + suffix
            # Static dimensions are needed by HDL elaboration and C storage.
            # Runtime targets still use the selected voice's actual count.
            if language == "python": declaration = f"{name} = {value}"
            elif language == "js": declaration = f"export const {name} = {value};"
            elif language == "c": declaration = f"#define {name} UINT32_C({value})"
            elif language == "java": declaration = f"  public static final long {name} = {value}L;"
            else: declaration = f"  localparam logic [31:0] {name} = 32'd{value};"
            dimensions.append(declaration)
    return "\n".join(dimensions) + "\n" + {"python": render_python, "js": render_js, "c": render_c,
            "java": render_java, "sv": render_sv}[language](schema, profiles, identity)


def render_python(schema, profiles, identity):
    fields = [f["name"] for f in schema["command"]["fields"]]
    lines = [f'FM_PROFILES_SHA256 = "{identity["profiles"]}"', f'FM_CONTRACT_SHA256 = "{identity["contract"]}"',
             f"FM_COMMAND_FIELDS = {tuple(fields)!r}", f"FM_PROFILE_IDS = {tuple(p['id'] for p in profiles)!r}",
             f"FM_PROFILES_SHA256_WORDS = {tuple(int(identity['profiles'][i:i+8],16) for i in range(0,64,8))!r}",
             "", "# Returned metadata is copied; callers cannot alter the internal contract tables.",
             "import copy as _copy", f"_PROFILES = {repr({p['id']: p for p in profiles})}", "",
             "def abi_compatible(word):", "    return _is_u32(word) and word == FM_ABI_VERSION_WORD", "",
             "def serialize_command(command):",
             "    if type(command) is not dict or set(command) != set(FM_COMMAND_FIELDS):",
             '        raise ValueError("command must contain exactly the schema fields")',
             "    words = tuple(command[name] for name in FM_COMMAND_FIELDS)",
             "    if not all(_is_u32(word) for word in words):",
             '        raise ValueError("command fields must be uint32 integers, not bool")',
             "    return words", "", "def deserialize_command(words):",
             "    if type(words) not in (list, tuple) or len(words) != FM_COMMAND_WORD_COUNT or not all(_is_u32(word) for word in words):",
             '        raise ValueError("command words require exact length and uint32 integers")',
             "    return dict(zip(FM_COMMAND_FIELDS, words))", "",
             "def get_profile(profile_id):", "    profile = _PROFILES.get(profile_id) if _is_u32(profile_id) else None",
             "    return _copy.deepcopy(profile) if profile is not None else None", "",
             "def get_parameter(profile_id, parameter_id):", "    profile = _PROFILES.get(profile_id) if _is_u32(profile_id) else None",
             "    if profile is None or not _is_u32(parameter_id): return None",
             '    return next((_copy.deepcopy(p) for p in profile["parameters"] if p["id"] == parameter_id), None)', "",
             "def get_preset(profile_id, preset_id):", "    profile = _PROFILES.get(profile_id) if _is_u32(profile_id) else None",
             "    if profile is None or not _is_u32(preset_id): return None",
             '    return next((_copy.deepcopy(p) for p in profile["presets"] if p["id"] == preset_id), None)', "",
             "def profile_operator_count(profile_id, voice):", "    profile = _PROFILES.get(profile_id) if _is_u32(profile_id) else None",
             '    if profile is None or not _is_u32(voice) or voice >= profile["voice_count"]: return None',
             '    return profile["operators_per_voice"][voice]', "",
             "def _profile_native_mapping(profile_id, logical_operator, kind):",
             "    if not _is_u32(profile_id) or not _is_u32(logical_operator): return 0xffffffff",
             "    profile = _PROFILES.get(profile_id)",
             '    mapping = profile.get("adapter_mapping", {}).get(kind, ()) if profile is not None else ()',
             "    return mapping[logical_operator] if logical_operator < len(mapping) else 0xffffffff", "",
             "def profile_native_register_slot(profile_id, logical_operator):",
             '    return _profile_native_mapping(profile_id, logical_operator, "register_slots")', "",
             "def profile_native_key_on_bit(profile_id, logical_operator):",
             '    return _profile_native_mapping(profile_id, logical_operator, "key_on_bits")', "",
             "def validate_parameter(profile_id, parameter_id, scope, word, value):",
             "    if not all(_is_u32(x) for x in (profile_id, parameter_id, scope, word, value)): return False",
             "    profile = _PROFILES.get(profile_id)", "    parameter = get_parameter(profile_id, parameter_id)",
             '    if profile is None or parameter is None or scope != parameter["scope_id"]: return False',
             "    voice, _ = unpack_target(word)", "    selected = profile_operator_count(profile_id, voice)",
             '    return (parameter["minimum"] <= value <= parameter["maximum"] and',
             '            validate_target(scope, word, profile["voice_count"], 0 if selected is None else selected, profile["patch_operator_count"]))', ""]
    return "\n".join(lines)


def render_js(schema, profiles, identity):
    fields = [f["name"] for f in schema["command"]["fields"]]
    data = json.dumps({str(p["id"]): p for p in profiles}, ensure_ascii=False, separators=(",", ":"))
    return f'''export const FM_PROFILES_SHA256 = "{identity['profiles']}";
export const FM_CONTRACT_SHA256 = "{identity['contract']}";
export const FM_COMMAND_FIELDS = Object.freeze({json.dumps(fields)});
export const FM_PROFILE_IDS = Object.freeze({json.dumps([p['id'] for p in profiles])});
export const FM_PROFILES_SHA256_WORDS = Object.freeze({json.dumps([int(identity['profiles'][i:i+8],16) for i in range(0,64,8)])});
function freezeDeep(value) {{
  if (value && typeof value === "object") {{
    for (const member of Object.values(value)) freezeDeep(member);
    Object.freeze(value);
  }}
  return value;
}}
const PROFILES = freezeDeep({data});
export function abiCompatible(word) {{ return isU32(word) && word === FM_ABI_VERSION_WORD; }}
export function serializeCommand(command) {{
  if (command === null || typeof command !== "object" || Array.isArray(command) ||
      Object.keys(command).length !== FM_COMMAND_WORD_COUNT ||
      !FM_COMMAND_FIELDS.every(name => Object.hasOwn(command, name) && isU32(command[name]))) {{
    throw new RangeError("command must contain exactly the uint32 schema fields");
  }}
  return FM_COMMAND_FIELDS.map(name => command[name]);
}}
export function deserializeCommand(words) {{
  if (!Array.isArray(words) || words.length !== FM_COMMAND_WORD_COUNT ||
      !Array.from(words).every(isU32)) throw new RangeError("command words require exact uint32 length");
  return Object.fromEntries(FM_COMMAND_FIELDS.map((name, index) => [name, words[index]]));
}}
export function getProfile(id) {{ return isU32(id) ? (PROFILES[id] ?? null) : null; }}
export function getParameter(id, parameterId) {{
  return isU32(parameterId) ? (getProfile(id)?.parameters.find(p => p.id === parameterId) ?? null) : null;
}}
export function getPreset(id, presetId) {{
  return isU32(presetId) ? (getProfile(id)?.presets.find(p => p.id === presetId) ?? null) : null;
}}
export function profileOperatorCount(id, voice) {{
  const profile = getProfile(id);
  return profile && isU32(voice) && voice < profile.voice_count ? profile.operators_per_voice[voice] : null;
}}
function profileNativeMapping(id, logicalOperator, kind) {{
  if (!isU32(id) || !isU32(logicalOperator)) return 0xffffffff;
  const mapping = getProfile(id)?.adapter_mapping?.[kind];
  return mapping && logicalOperator < mapping.length ? mapping[logicalOperator] : 0xffffffff;
}}
export function profileNativeRegisterSlot(id, logicalOperator) {{ return profileNativeMapping(id, logicalOperator, "register_slots"); }}
export function profileNativeKeyOnBit(id, logicalOperator) {{ return profileNativeMapping(id, logicalOperator, "key_on_bits"); }}
export function validateParameter(id, parameterId, scope, word, value) {{
  if (![id, parameterId, scope, word, value].every(isU32)) return false;
  const profile = getProfile(id), parameter = getParameter(id, parameterId);
  if (!profile || !parameter || parameter.scope_id !== scope) return false;
  const selected = profileOperatorCount(id, unpackTarget(word).voice) ?? 0;
  return value >= parameter.minimum && value <= parameter.maximum &&
         validateTarget(scope, word, profile.voice_count, selected, profile.patch_operator_count);
}}
'''


def c_string(value):
    return json.dumps(value, ensure_ascii=False)


def mapping_functions(profiles, language):
    """Emit backend metadata lookups only; generic codecs never assemble native addresses."""
    lines = []
    for suffix, key in (("register_slot", "register_slots"), ("key_on_bit", "key_on_bits")):
        if language == "c":
            lines += [f"static inline uint32_t fm_profile_native_{suffix}(uint32_t id, uint32_t logical_operator)",
                      "{", "    switch (id) {"]
        elif language == "java":
            camel = "RegisterSlot" if suffix == "register_slot" else "KeyOnBit"
            lines += [f"  public static long profileNative{camel}(long id, long logical_operator) {{",
                      "    if (!isU32(id) || !isU32(logical_operator)) return 0xffffffffL;"]
        else:
            lines += [f"  function automatic logic [31:0] fm_profile_native_{suffix}(input logic [31:0] id, input logic [31:0] logical_operator);",
                      "    if ($isunknown({id,logical_operator})) return 32'hffffffff;", "    case (id)"]
        for profile in profiles:
            values = profile.get("adapter_mapping", {}).get(key, [])
            if not values: continue
            if language == "c":
                lines += [f"    case {profile['id']}u: switch (logical_operator) {{"]
                lines += [f"        case {index}u: return {value}u;" for index,value in enumerate(values)]
                lines += ["        default: return UINT32_C(0xffffffff); }"]
            elif language == "java":
                lines += [f"    if (id == {profile['id']}L && logical_operator == {index}L) return {value}L;" for index,value in enumerate(values)]
            else:
                lines += [f"      32'd{profile['id']}: begin case (logical_operator)"]
                lines += [f"        32'd{index}: return 32'd{value};" for index,value in enumerate(values)]
                lines += ["        default: return 32'hffffffff; endcase end"]
        if language == "c": lines += ["    default: return UINT32_C(0xffffffff);", "    }", "}"]
        elif language == "java": lines += ["    return 0xffffffffL;", "  }"]
        else: lines += ["      default: return 32'hffffffff;", "    endcase", "  endfunction"]
    return lines


def render_c(schema, profiles, identity):
    fields = [f["name"] for f in schema["command"]["fields"]]
    lines = [f'#define FM_PROFILES_SHA256 "{identity["profiles"]}"', f'#define FM_CONTRACT_SHA256 "{identity["contract"]}"',
             "static const uint32_t fm_profiles_sha256_words[8] = {" + ", ".join(f"UINT32_C(0x{identity['profiles'][i:i+8]})" for i in range(0,64,8)) + "};",
             "", "typedef struct {"]
    lines += [f"    uint32_t {name};" for name in fields]
    lines += ["} fm_command_t;", "", "static inline bool fm_abi_compatible(uint32_t word)",
              "{ return word == FM_ABI_VERSION_WORD; }", "",
              "static inline bool fm_serialize_command(const fm_command_t *command, uint32_t *words, size_t count)",
              "{", "    if (command == NULL || words == NULL || count != FM_COMMAND_WORD_COUNT) return false;"]
    lines += [f"    words[FM_CMD_WORD_{name.upper()}] = command->{name};" for name in fields]
    lines += ["    return true;", "}", "",
              "static inline bool fm_deserialize_command(const uint32_t *words, size_t count, fm_command_t *command)",
              "{", "    if (command == NULL || words == NULL || count != FM_COMMAND_WORD_COUNT) return false;"]
    lines += [f"    command->{name} = words[FM_CMD_WORD_{name.upper()}];" for name in fields]
    lines += ["    return true;", "}", "",
              "typedef struct { uint32_t id; const char *name; uint32_t minimum, maximum, scope_id, unit_id, apply_time_id, value_type_id; } fm_parameter_t;",
              "typedef struct { uint32_t parameter_id, operator_index, value_u32; } fm_preset_value_t;",
              "typedef struct { uint32_t id; const char *name; uint32_t key_on_logical_mask, values_count; const fm_preset_value_t *values; } fm_preset_t;",
              "typedef struct { uint32_t id, backend_id, profile_version, voice_count, patch_operator_count; bool runtime_available;",
              "    uint32_t parameter_count, preset_count; const char *key, *profile_sha256; const uint32_t *operators_per_voice;",
              "    const char *const *operator_labels; const fm_parameter_t *parameters; const fm_preset_t *presets; } fm_profile_t;", ""]
    for profile in profiles:
        prefix = f"fm_profile_{profile['id']}"
        lines += [f"static const uint32_t {prefix}_operators[] = {{" + ", ".join(str(c) for c in profile["operators_per_voice"]) + "};",
                  f"static const char *const {prefix}_labels[] = {{" + ", ".join(c_string(c) for c in profile["operator_labels"]) + "};"]
        if profile["parameters"]:
            lines += [f"static const fm_parameter_t {prefix}_parameters[] = {{"]
            for p in profile["parameters"]:
                lines += [f"    {{{p['id']}u, {c_string(p['name'])}, {p['minimum']}u, {p['maximum']}u, {p['scope_id']}u, {p['unit_id']}u, {p['apply_time_id']}u, {p['value_type_id']}u}},"]
            lines += ["};"]
        for preset in profile["presets"]:
            lines += [f"static const fm_preset_value_t {prefix}_preset_{preset['id']}_values[] = {{"]
            lines += [f"    {{{v['parameter_id']}u, {v['operator_index']}u, {v['value_u32']}u}}," for v in preset["values"]]
            if not preset["values"]: lines += ["    {0u, 0u, 0u}, /* count remains zero; portable nonempty storage */"]
            lines += ["};"]
        if profile["presets"]:
            lines += [f"static const fm_preset_t {prefix}_presets[] = {{"]
            for p in profile["presets"]:
                lines += [f"    {{{p['id']}u, {c_string(p['name'])}, {p.get('key_on_logical_mask',0)}u, {len(p['values'])}u, {prefix}_preset_{p['id']}_values}},"]
            lines += ["};"]
    lines += ["static const fm_profile_t fm_profiles[] = {"]
    for p in profiles:
        prefix = f"fm_profile_{p['id']}"
        lines += [f"    {{{p['id']}u, {p['backend_id']}u, {p['profile_version']}u, {p['voice_count']}u, {p['patch_operator_count']}u, false, {p['parameter_count']}u, {p['preset_count']}u, {c_string(p['key'])}, {c_string(p['profile_sha256'])}, {prefix}_operators, {prefix}_labels, " +
                  (f"{prefix}_parameters" if p["parameters"] else "NULL") + ", " + (f"{prefix}_presets" if p["presets"] else "NULL") + "},"]
    lines += ["};", "", "static inline const fm_profile_t *fm_profile(uint32_t id)", "{",
              "    for (size_t i = 0; i < sizeof(fm_profiles) / sizeof(fm_profiles[0]); ++i)",
              "        if (fm_profiles[i].id == id) return &fm_profiles[i];", "    return NULL;", "}", "",
              "static inline const fm_parameter_t *fm_parameter(uint32_t id, uint32_t parameter_id)", "{",
              "    const fm_profile_t *profile = fm_profile(id);", "    if (profile == NULL) return NULL;",
              "    for (uint32_t i = 0; i < profile->parameter_count; ++i)",
              "        if (profile->parameters[i].id == parameter_id) return &profile->parameters[i];", "    return NULL;", "}", "",
              "static inline const fm_preset_t *fm_preset(uint32_t id, uint32_t preset_id)", "{",
              "    const fm_profile_t *profile = fm_profile(id);", "    if (profile == NULL) return NULL;",
              "    for (uint32_t i = 0; i < profile->preset_count; ++i)",
              "        if (profile->presets[i].id == preset_id) return &profile->presets[i];", "    return NULL;", "}", "",
              "static inline bool fm_profile_operator_count(uint32_t id, uint32_t voice, uint32_t *out)", "{",
              "    const fm_profile_t *profile = fm_profile(id);",
              "    if (profile == NULL || out == NULL || voice >= profile->voice_count) return false;",
              "    *out = profile->operators_per_voice[voice];", "    return true;", "}", "",
              "static inline bool fm_validate_parameter(uint32_t id, uint32_t parameter_id, uint32_t scope, uint32_t word, uint32_t value)", "{",
              "    const fm_profile_t *profile = fm_profile(id);", "    const fm_parameter_t *parameter_info = fm_parameter(id, parameter_id);",
              "    if (profile == NULL || parameter_info == NULL || scope != parameter_info->scope_id ||",
              "        value < parameter_info->minimum || value > parameter_info->maximum) return false;",
              "    uint32_t selected = 0;", "    (void)fm_profile_operator_count(id, fm_target_voice(word), &selected);",
              "    return fm_validate_target(scope, word, profile->voice_count, selected, profile->patch_operator_count);", "}", ""]
    lines += mapping_functions(profiles, "c")
    return "\n".join(lines)


def render_java(schema, profiles, identity):
    fields = [f["name"] for f in schema["command"]["fields"]]
    lines = [f'  public static final String FM_PROFILES_SHA256 = "{identity["profiles"]}";',
             f'  public static final String FM_CONTRACT_SHA256 = "{identity["contract"]}";',
             "  private static final long[] PROFILES_SHA256_WORDS = {" + ", ".join(f"0x{identity['profiles'][i:i+8]}L" for i in range(0,64,8)) + "};",
             "  public static long profilesSha256Word(long index) { return index >= 0 && index < 8 ? PROFILES_SHA256_WORDS[(int)index] : -1; }",
             "  public static boolean abiCompatible(long word) { return isU32(word) && word == FM_ABI_VERSION_WORD; }",
             "  public static final class Command {"]
    lines += [f"    public final long {name};" for name in fields]
    lines += ["    public Command(" + ", ".join("long " + name for name in fields) + ") {",
              "      if (!(" + " && ".join(f"isU32({name})" for name in fields) + "))",
              '        throw new IllegalArgumentException("command fields must be uint32");']
    lines += [f"      this.{name} = {name};" for name in fields]
    lines += ["    }", "  }", "  public static long[] serializeCommand(Command command) {",
              '    if (command == null) throw new IllegalArgumentException("command is null");',
              "    return new long[] {" + ", ".join("command." + name for name in fields) + "};", "  }",
              "  public static Command deserializeCommand(long[] words) {",
              '    if (words == null || words.length != FM_COMMAND_WORD_COUNT) throw new IllegalArgumentException("command length");',
              "    return new Command(" + ", ".join(f"words[(int)FM_CMD_WORD_{name.upper()}]" for name in fields) + ");", "  }",
              "  public static final class Parameter {",
              "    public final long id, minimum, maximum, scope_id, unit_id, apply_time_id, value_type_id;",
              "    public final String name;",
              "    private Parameter(long id, String name, long minimum, long maximum, long scope_id, long unit_id, long apply_time_id, long value_type_id) {",
              "      this.id=id; this.name=name; this.minimum=minimum; this.maximum=maximum; this.scope_id=scope_id;",
              "      this.unit_id=unit_id; this.apply_time_id=apply_time_id; this.value_type_id=value_type_id;", "    }", "  }",
              "  public static final class PresetValue {",
              "    public final long parameter_id, operator_index, value_u32;",
              "    private PresetValue(long p, long o, long v) { parameter_id=p; operator_index=o; value_u32=v; }", "  }",
              "  public static final class Preset {",
              "    public final long id, key_on_logical_mask, values_count; public final String name;",
              "    private final PresetValue[] values;",
              "    private Preset(long id, String name, long mask, PresetValue[] values) {",
              "      this.id=id; this.name=name; key_on_logical_mask=mask; this.values=values; values_count=values.length;", "    }",
              "    public PresetValue value(long index) { return index >= 0 && index < values.length ? values[(int)index] : null; }", "  }",
              "  public static final class Profile {",
              "    public final long id, backend_id, profile_version, voice_count, patch_operator_count, parameter_count, preset_count;",
              "    public final boolean runtime_available; public final String key, profile_sha256;",
              "    private final long[] operators_per_voice; private final String[] operator_labels;",
              "    private final Parameter[] parameters; private final Preset[] presets;",
              "    private Profile(long id, long backend_id, long version, String key, String digest, long patchCount, long[] counts, String[] labels, Parameter[] parameters, Preset[] presets) {",
              "      this.id=id; this.backend_id=backend_id; profile_version=version; this.key=key; profile_sha256=digest;",
              "      patch_operator_count=patchCount; operators_per_voice=counts; operator_labels=labels; voice_count=counts.length;",
              "      this.parameters=parameters; this.presets=presets; parameter_count=parameters.length; preset_count=presets.length; runtime_available=false;", "    }",
              "    public String operatorLabel(long index) { return index >= 0 && index < operator_labels.length ? operator_labels[(int)index] : null; }", "  }",
              "  private static final Profile[] PROFILES = {"]
    for p in profiles:
        lines += [f"    new Profile({p['id']}L, {p['backend_id']}L, {p['profile_version']}L, {c_string(p['key'])}, {c_string(p['profile_sha256'])}, {p['patch_operator_count']}L,",
                  "      new long[] {" + ", ".join(f"{v}L" for v in p["operators_per_voice"]) + "},",
                  "      new String[] {" + ", ".join(c_string(label) for label in p["operator_labels"]) + "}, new Parameter[] {"]
        for parameter in p["parameters"]:
            args = [f"{parameter['id']}L", c_string(parameter["name"])] + [f"{parameter[key]}L" for key in ("minimum", "maximum", "scope_id", "unit_id", "apply_time_id", "value_type_id")]
            lines += ["        new Parameter(" + ", ".join(args) + "),"]
        lines += ["      }, new Preset[] {"]
        for preset in p["presets"]:
            lines += [f"        new Preset({preset['id']}L, {c_string(preset['name'])}, {preset.get('key_on_logical_mask',0)}L, new PresetValue[] {{"]
            lines += [f"          new PresetValue({v['parameter_id']}L, {v['operator_index']}L, {v['value_u32']}L)," for v in preset["values"]]
            lines += ["        }),"]
        lines += ["      }),"]
    lines += ["  };", "  public static Profile getProfile(long id) {",
              "    if (!isU32(id)) return null; for (Profile p : PROFILES) if (p.id == id) return p; return null;", "  }",
              "  public static Parameter getParameter(long id, long parameterId) {",
              "    Profile p=getProfile(id); if (p == null || !isU32(parameterId)) return null;",
              "    for (Parameter value : p.parameters) if (value.id == parameterId) return value; return null;", "  }",
              "  public static Preset getPreset(long id, long presetId) {",
              "    Profile p=getProfile(id); if (p == null || !isU32(presetId)) return null;",
              "    for (Preset value : p.presets) if (value.id == presetId) return value; return null;", "  }",
              "  public static long profileOperatorCount(long id, long voice) {",
              "    Profile p=getProfile(id); return p != null && isU32(voice) && voice < p.voice_count ? p.operators_per_voice[(int)voice] : -1;", "  }",
              "  public static boolean validateParameter(long id, long parameterId, long scope, long word, long value) {",
              "    if (!isU32(id) || !isU32(parameterId) || !isU32(scope) || !isU32(word) || !isU32(value)) return false;",
              "    Profile p=getProfile(id); Parameter parameter=getParameter(id, parameterId);",
              "    if (p == null || parameter == null || scope != parameter.scope_id || value < parameter.minimum || value > parameter.maximum) return false;",
              "    long selected=profileOperatorCount(id, targetVoice(word));",
              "    return validateTarget(scope, word, p.voice_count, selected < 0 ? 0 : selected, p.patch_operator_count);", "  }", ""]
    lines += mapping_functions(profiles, "java")
    return "\n".join(lines)


def render_sv(schema, profiles, identity):
    fields = [f["name"] for f in schema["command"]["fields"]]
    lines = [f"  localparam logic [255:0] FM_PROFILES_SHA256 = 256'h{identity['profiles']};",
             f"  localparam logic [255:0] FM_CONTRACT_SHA256 = 256'h{identity['contract']};",
             "  function automatic logic [31:0] fm_profiles_sha256_word(input logic [31:0] word_index);",
             "    case (word_index)"]
    lines += [f"      32'd{index}: return 32'h{identity['profiles'][index*8:(index+1)*8]};" for index in range(8)]
    lines += ["      default: return '0;", "    endcase", "  endfunction", "  typedef struct packed {"]
    lines += [f"    logic [31:0] {name};" for name in fields]
    lines += ["  } fm_command_t;", "  typedef logic [FM_COMMAND_WORD_COUNT-1:0][31:0] fm_command_words_t;",
              "  function automatic logic fm_abi_compatible(input logic [31:0] word);",
              "    return !$isunknown(word) && word == FM_ABI_VERSION_WORD;", "  endfunction",
              "  function automatic logic fm_command_valid(input fm_command_t command);",
              "    return !$isunknown(command);", "  endfunction",
              "  function automatic logic fm_command_words_valid(input fm_command_words_t words);",
              "    return !$isunknown(words);", "  endfunction",
              "  function automatic fm_command_words_t fm_serialize_command(input fm_command_t command);",
              "    fm_command_words_t words;"]
    lines += [f"    words[FM_CMD_WORD_{name.upper()}] = command.{name};" for name in fields]
    lines += ["    return words;", "  endfunction",
              "  function automatic fm_command_t fm_deserialize_command(input fm_command_words_t words);",
              "    fm_command_t command;"]
    lines += [f"    command.{name} = words[FM_CMD_WORD_{name.upper()}];" for name in fields]
    lines += ["    return command;", "  endfunction",
              "  typedef struct packed { logic valid; logic [31:0] id, backend_id, profile_version, voice_count, patch_operator_count;",
              "    logic runtime_available; logic [31:0] parameter_count, preset_count; } fm_profile_info_t;",
              "  typedef struct packed { logic valid; logic [31:0] id, minimum, maximum, scope_id, unit_id, apply_time_id, value_type_id; } fm_parameter_info_t;",
              "  typedef struct packed { logic valid; logic [31:0] id, key_on_logical_mask, values_count; } fm_preset_info_t;",
              "  typedef struct packed { logic valid; logic [31:0] parameter_id, operator_index, value_u32; } fm_preset_value_info_t;",
              "  function automatic fm_profile_info_t fm_profile_info(input logic [31:0] id);",
              "    fm_profile_info_t info; info = '0;", "    if ($isunknown(id)) return info;", "    case (id)"]
    for p in profiles:
        lines += [f"      32'd{p['id']}: begin info.valid=1'b1; info.id=32'd{p['id']}; info.backend_id=32'd{p['backend_id']};",
                  f"        info.profile_version=32'd{p['profile_version']}; info.voice_count=32'd{p['voice_count']}; info.patch_operator_count=32'd{p['patch_operator_count']};",
                  f"        info.runtime_available=1'b0; info.parameter_count=32'd{p['parameter_count']}; info.preset_count=32'd{p['preset_count']}; end"]
    lines += ["      default: ;", "    endcase", "    return info;", "  endfunction",
              "  function automatic logic [255:0] fm_profile_sha256(input logic [31:0] id);",
              "    case (id)"]
    for p in profiles: lines += [f"      32'd{p['id']}: return 256'h{p['profile_sha256']};"]
    lines += ["      default: return '0;", "    endcase", "  endfunction",
              "  function automatic logic [31:0] fm_profile_operator_count(input logic [31:0] id, input logic [31:0] voice);",
              "    if ($isunknown({id,voice})) return 32'hffffffff;", "    case (id)"]
    for p in profiles:
        lines += [f"      32'd{p['id']}: begin case (voice)"]
        lines += [f"        32'd{index}: return 32'd{count};" for index,count in enumerate(p["operators_per_voice"])]
        lines += ["        default: return 32'hffffffff; endcase end"]
    lines += ["      default: return 32'hffffffff;", "    endcase", "  endfunction",
              "  function automatic fm_parameter_info_t fm_parameter_info(input logic [31:0] id, input logic [31:0] parameter_id);",
              "    fm_parameter_info_t info; info='0;", "    if ($isunknown({id,parameter_id})) return info;", "    case (id)"]
    for p in profiles:
        if not p["parameters"]: continue
        lines += [f"      32'd{p['id']}: begin case (parameter_id)"]
        for param in p["parameters"]:
            lines += [f"        32'd{param['id']}: begin info.valid=1'b1; " + " ".join(f"info.{key}=32'd{param[key]};" for key in ("id", "minimum", "maximum", "scope_id", "unit_id", "apply_time_id", "value_type_id")) + " end"]
        lines += ["        default: ; endcase end"]
    lines += ["      default: ;", "    endcase", "    return info;", "  endfunction",
              "  function automatic fm_preset_info_t fm_preset_info(input logic [31:0] id, input logic [31:0] preset_id);",
              "    fm_preset_info_t info; info='0;", "    if ($isunknown({id,preset_id})) return info;", "    case (id)"]
    for p in profiles:
        if not p["presets"]: continue
        lines += [f"      32'd{p['id']}: begin case (preset_id)"]
        for preset in p["presets"]:
            lines += [f"        32'd{preset['id']}: begin info.valid=1'b1; info.id=32'd{preset['id']}; info.key_on_logical_mask=32'd{preset.get('key_on_logical_mask',0)}; info.values_count=32'd{len(preset['values'])}; end"]
        lines += ["        default: ; endcase end"]
    lines += ["      default: ;", "    endcase", "    return info;", "  endfunction",
              "  function automatic fm_preset_value_info_t fm_preset_value_info(input logic [31:0] id, input logic [31:0] preset_id, input logic [31:0] value_index);",
              "    fm_preset_value_info_t info; info='0;", "    if ($isunknown({id,preset_id,value_index})) return info;", "    case (id)"]
    for p in profiles:
        if not p["presets"]: continue
        lines += [f"      32'd{p['id']}: begin case (preset_id)"]
        for preset in p["presets"]:
            lines += [f"        32'd{preset['id']}: begin case (value_index)"]
            lines += [f"          32'd{index}: begin info.valid=1'b1; info.parameter_id=32'd{v['parameter_id']}; info.operator_index=32'd{v['operator_index']}; info.value_u32=32'd{v['value_u32']}; end" for index,v in enumerate(preset["values"])]
            lines += ["          default: ; endcase end"]
        lines += ["        default: ; endcase end"]
    lines += ["      default: ;", "    endcase", "    return info;", "  endfunction",
              "  function automatic logic fm_validate_parameter(input logic [31:0] id, input logic [31:0] parameter_id, input logic [31:0] scope, input logic [31:0] word, input logic [31:0] value_u32);",
              "    fm_profile_info_t profile_info; fm_parameter_info_t parameter_info; logic [31:0] selected;",
              "    if ($isunknown({id,parameter_id,scope,word,value_u32})) return 1'b0;",
              "    profile_info=fm_profile_info(id); parameter_info=fm_parameter_info(id,parameter_id);",
              "    if (!profile_info.valid || !parameter_info.valid || scope != parameter_info.scope_id ||",
              "        value_u32 < parameter_info.minimum || value_u32 > parameter_info.maximum) return 1'b0;",
              "    selected=fm_profile_operator_count(id, {16'b0,fm_target_voice(word)});",
              "    if (selected == 32'hffffffff) selected=32'd0;",
              "    return fm_validate_target(scope,word,profile_info.voice_count,selected,profile_info.patch_operator_count);", "  endfunction", ""]
    # Four-state simulation guards are not a physical CDC/legality mechanism. Gowin cannot
    # synthesize $isunknown(nonconstant); leave all known-value checks in the netlist.
    lines += mapping_functions(profiles, "sv")
    text = "\n".join(lines)
    text = text.replace("    return !$isunknown(word) && word == FM_ABI_VERSION_WORD;",
                        "    if ($isunknown(word)) return 1'b0;\n    return word == FM_ABI_VERSION_WORD;")
    text = text.replace("    return !$isunknown(command);",
                        "    if ($isunknown(command)) return 1'b0;\n    return 1'b1; // Typed width only; hardware needs CDC/legality protection.")
    text = text.replace("    return !$isunknown(words);",
                        "    if ($isunknown(words)) return 1'b0;\n    return 1'b1; // Typed width only; hardware needs CDC/legality protection.")
    text = "\n".join(("/*synthesis translate_off*/\n`ifndef SYNTHESIS\n" + line + "\n`endif\n/*synthesis translate_on*/") if "$isunknown" in line and
                      "if (" in line else line for line in text.split("\n"))
    return "  // X/Z guards apply in simulation; hardware needs independently verified CDC and legality.\n" + text
