#!/usr/bin/env python3
"""Generate the frozen FM control contract using only the Python standard library.

The schema is the sole source for numeric values, field layout and scope rules.
This tool deliberately does not generate a hardware transport or a backend.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True
from fm_codegen_v1 import validate_extension, validate_profiles, render_extension, identities


ROOT = Path(__file__).resolve().parents[1]
U32_MAX = (1 << 32) - 1
OUTPUT_LANGUAGES = {
    "c/fm_contract.h": "c",
    "sv/fm_contract_pkg.sv": "sv",
    "js/fm-contract.mjs": "js",
    "java/cn/fpgasynth/control/generated/FmContract.java": "java",
    "python/fm_contract.py": "python",
}


class SchemaError(ValueError):
    """The input cannot safely describe the supported control ABI."""


def require(condition, message):
    if not condition:
        raise SchemaError(message)


def integer(value, label, minimum=0, maximum=U32_MAX):
    require(type(value) is int and minimum <= value <= maximum,
            f"{label} must be an integer in {minimum}..{maximum} (bool is invalid)")
    return value


def obj(value, label):
    require(isinstance(value, dict), f"{label} must be an object")
    return value


def identifier(value, label):
    require(isinstance(value, str) and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value),
            f"{label} must be an ASCII identifier")
    return value


def duplicate_free_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def validate_schema(schema):
    """Reject malformed or unsupported schemas before writing any generated file."""
    obj(schema, "schema")
    require(isinstance(schema.get("name"), str) and schema["name"], "name must be nonempty")
    require(isinstance(schema.get("schema_version"), str) and
            re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)",
                         schema["schema_version"]), "schema_version must be MAJOR.MINOR.PATCH")
    require(schema.get("status") == "contract_frozen_not_integrated", "status must be contract_frozen_not_integrated")
    constants = obj(schema.get("constants"), "constants")
    for name, value in constants.items():
        identifier(name, "constant name")
        require(name == name.upper(), f"constant {name} must be uppercase")
        integer(value, f"constants.{name}")
    needed = {"ABI_MAJOR", "ABI_MINOR", "HTTP_API_MAJOR", "HTTP_API_MINOR", "SPI_WORD_BASE",
              "SPI_WINDOW_WORDS", "SPI_WORD_BITS", "TARGET_NONE", "MAX_WIRE_INDEX",
              "MAX_DESCRIPTOR_VOICES", "MAX_OPERATORS_PER_VOICE", "MAX_PARAMETERS",
              "MAX_PRESETS", "MAX_BACKEND_PROFILES", "HTTP_COMMAND_MAX_BYTES"}
    require(needed <= constants.keys(), f"missing constants: {sorted(needed - constants.keys())}")
    for major in ("ABI_MAJOR", "HTTP_API_MAJOR"):
        integer(constants[major], major, 1, 65535)
    for minor in ("ABI_MINOR", "HTTP_API_MINOR"):
        integer(constants[minor], minor, 0, 65535)
    for name, limit in (("MAX_DESCRIPTOR_VOICES", 256), ("MAX_OPERATORS_PER_VOICE", 32),
                        ("MAX_PARAMETERS", 256), ("MAX_PRESETS", 128),
                        ("MAX_BACKEND_PROFILES", 16), ("HTTP_COMMAND_MAX_BYTES", 95),
                        ("SPI_WINDOW_WORDS", 256)):
        integer(constants[name], name, 1, limit)
    require(constants["SPI_WORD_BITS"] == 32, "SPI_WORD_BITS must be 32")
    require(constants["SPI_WORD_BASE"] + constants["SPI_WINDOW_WORDS"] - 1 <= U32_MAX,
            "SPI register window overflows uint32")

    target = obj(schema.get("target"), "target")
    require(integer(target.get("word_bits"), "target.word_bits", 1, 32) == 32,
            "target.word_bits must be 32")
    masks = []
    for name in ("voice", "operator"):
        field = obj(target.get(name), f"target.{name}")
        lsb = integer(field.get("lsb"), f"target.{name}.lsb", 0, 31)
        width = integer(field.get("width"), f"target.{name}.width", 1, 32)
        require(lsb + width <= 32, f"target.{name} exceeds word width")
        masks.append(((1 << width) - 1) << lsb)
    require(masks[0] & masks[1] == 0, "target fields overlap")
    require(target["voice"] == {"lsb": 16, "width": 16} and
            target["operator"] == {"lsb": 0, "width": 16},
            "this ABI requires voice in high 16 bits and operator in low 16 bits")
    none_name = target.get("none_constant")
    require(none_name in constants, "target.none_constant must reference a constant")
    require(constants[none_name] == 65535 and constants["TARGET_NONE"] == 65535,
            "target NONE must be 0xffff")
    require(constants["MAX_WIRE_INDEX"] == constants[none_name] - 1,
            "MAX_WIRE_INDEX must exclude the NONE sentinel")

    enums = obj(schema.get("enums"), "enums")
    require(enums, "enums must not be empty")
    for group, members in enums.items():
        identifier(group, "enum group")
        obj(members, f"enums.{group}")
        require(members, f"enums.{group} must not be empty")
        seen = set()
        for name, value in members.items():
            identifier(name, f"enums.{group} member")
            integer(value, f"enums.{group}.{name}")
            require(value not in seen, f"enums.{group} has duplicate value {value}")
            seen.add(value)
    scopes = obj(enums.get("scope"), "enums.scope")
    expected_scope = {"ENGINE": 0, "VOICE": 1, "VOICE_OPERATOR": 2, "PATCH": 3,
                      "PATCH_OPERATOR": 4}
    require(scopes == expected_scope, "scope members and values do not match this ABI")
    rules = obj(target.get("scope_rules"), "target.scope_rules")
    require(rules.keys() == scopes.keys(), "scope_rules must cover each scope exactly once")
    for name, rule in rules.items():
        obj(rule, f"target.scope_rules.{name}")
        require(rule.keys() == {"voice", "operator"}, f"scope {name} requires voice/operator rules")
        require(all(value in ("none", "required") for value in rule.values()),
                f"scope {name} has unsupported field rule")
    validation = obj(target.get("validation_rules"), "target.validation_rules")
    for kind, minimum, maximum in (("voice", 1, 256), ("selected_operator", 0, 32),
                                    ("patch_operator", 0, 32)):
        require(integer(validation.get(f"{kind}_count_min"), f"{kind}_count_min") == minimum,
                f"{kind}_count_min must be {minimum}")
        reference = validation.get(f"{kind}_count_max_constant")
        require(reference in constants, f"{kind}_count_max_constant must reference a constant")
        integer(constants[reference], reference, minimum, maximum)
        field = target["voice" if kind == "voice" else "operator"]
        require(constants[reference] <= (1 << field["width"]) - 1,
                f"{kind} descriptor budget overlaps sentinel")
    require(validation.get("unknown_scope") == "reject", "unknown_scope must be reject")
    require(validation.get("out_of_budget_descriptor") == "reject",
            "out_of_budget_descriptor must be reject")

    spi = obj(schema.get("spi"), "spi")
    require(spi.get("address_unit") == "word", "SPI address_unit must be word")
    require(spi.get("implemented_in_active_project") is False,
            "this prototype cannot declare SPI integration")
    require(spi.get("native_registers_exposed_by_default") is False,
            "native backend registers must not be exposed by default")
    require(obj(schema.get("http"), "http").get("status") == "proposal_not_frozen",
            "HTTP remains proposal_not_frozen")
    registers = obj(spi.get("registers"), "spi.registers")
    require(registers, "registers must not be empty")
    seen = set()
    for name, offset in registers.items():
        identifier(name, "register name")
        integer(offset, f"spi.registers.{name}", 0, constants["SPI_WINDOW_WORDS"] - 1)
        require(offset not in seen, f"duplicate register offset {offset}")
        seen.add(offset)
    codegen = obj(schema.get("codegen"), "codegen")
    outputs = codegen.get("outputs")
    require(isinstance(outputs, list) and all(isinstance(item, str) for item in outputs),
            "codegen.outputs must be a list of paths")
    require(len(outputs) == len(set(outputs)), "duplicate codegen output path")
    require(set(outputs) == OUTPUT_LANGUAGES.keys(), "codegen.outputs must list the five supported outputs")
    validate_extension(schema, require, integer, identifier)
    generated_names = [name for name, _ in numeric_constants(schema)]
    require(len(generated_names) == len(set(generated_names)), "generated constant names collide")
    require(not ({"FM_SCHEMA_VERSION", "FM_SCHEMA_SHA256", "FM_PROFILES_SHA256", "FM_CONTRACT_SHA256",
                  "FM_COMMAND_FIELDS", "FM_PROFILE_IDS", "FM_PROFILES_SHA256_WORDS"} & set(generated_names)),
            "numeric constants collide with generated schema metadata")
    return schema


def numeric_constants(schema):
    """One ordered inventory drives each language, including complete word addresses."""
    result = [(f"FM_{name}", value) for name, value in schema["constants"].items()]
    result += [("FM_ABI_VERSION_WORD", schema["constants"]["ABI_MAJOR"] << 16 |
                schema["constants"]["ABI_MINOR"]),
               ("FM_HTTP_API_VERSION_WORD", schema["constants"]["HTTP_API_MAJOR"] << 16 |
                schema["constants"]["HTTP_API_MINOR"])]
    for group, members in schema["enums"].items():
        result += [(f"FM_{group.upper()}_{name}", value) for name, value in members.items()]
    result += [(f"FM_REG_{name}", schema["constants"]["SPI_WORD_BASE"] + offset)
               for name, offset in schema["spi"]["registers"].items()]
    command = schema.get("command", {})
    if command:
        result += [("FM_COMMAND_WORD_COUNT", command["word_count"]),
                   ("FM_COMMAND_BYTE_COUNT", command["word_count"] * 4)]
        result += [("FM_CMD_WORD_" + field["name"].upper(), field["word_index"])
                   for field in command["fields"]]
    for group, members in schema.get("catalog", {}).items():
        prefix = {"units": "UNIT", "apply_times": "APPLY_TIME", "value_types": "VALUE_TYPE"}.get(group)
        if prefix:
            result += [(f"FM_{prefix}_{name.upper()}", value) for name, value in members.items()]
    return result


def banner(schema, digest, prefix):
    return "\n".join(f"{prefix} {line}" for line in (
        "CONTRACT FROZEN / HARDWARE NOT INTEGRATED; 请勿手改 / DO NOT EDIT.",
        "Generated from schema + backend profiles; transport/backend lifecycle is not implemented by this codec.",
        f"Schema version: {schema['schema_version']}",
        f"Schema SHA256: {digest}",
    )) + "\n"


def scope_conditions(schema, language):
    """Translate scope_rules; no per-language handwritten scope table."""
    equal = "===" if language == "js" else "=="
    conjunction = "and" if language == "python" else "&&"
    none = "FM_" + schema["target"]["none_constant"]
    operator_variable = "operator_index" if language in ("c", "sv") else "operator"
    conditions = []
    for name, rule in schema["target"]["scope_rules"].items():
        voice = f"voice {equal} {none}" if rule["voice"] == "none" else "voice < voice_count"
        if rule["operator"] == "none":
            operator = f"{operator_variable} {equal} {none}"
        else:
            count = "selected_operator_count" if rule["voice"] == "required" else "patch_operator_count"
            operator = f"{operator_variable} < {count}"
        conditions.append((f"FM_SCOPE_{name}", f"({voice}) {conjunction} ({operator})"))
    return conditions


def bounds(schema):
    rules = schema["target"]["validation_rules"]
    return [(f"{kind}_count", rules[f"{kind}_count_min"],
             "FM_" + rules[f"{kind}_count_max_constant"])
            for kind in ("voice", "selected_operator", "patch_operator")]


def field_parameters(schema):
    return {name: (schema["target"][name]["lsb"], (1 << schema["target"][name]["width"]) - 1)
            for name in ("voice", "operator")}


def render_python(schema, digest):
    fields = field_parameters(schema)
    lines = [banner(schema, digest, "#"),
             f'FM_SCHEMA_VERSION = "{schema["schema_version"]}"',
             f'FM_SCHEMA_SHA256 = "{digest}"', ""]
    lines += [f"{name} = 0x{value:08x}" for name, value in numeric_constants(schema)]
    lines += ["", "", "def _is_u32(value):", "    return type(value) is int and 0 <= value <= 0xffffffff",
              "", "", "def pack_target(voice, operator):", "    \"\"\"Pack two unsigned 16-bit indices; NONE is accepted.\"\"\"",
              f"    if not (_is_u32(voice) and voice <= 0x{fields['voice'][1]:x} and",
              f"            _is_u32(operator) and operator <= 0x{fields['operator'][1]:x}):",
              '        raise ValueError("target fields must be unsigned 16-bit integers, not bool")',
              f"    return (voice << {fields['voice'][0]}) | (operator << {fields['operator'][0]})",
              "", "", "def unpack_target(word):", "    \"\"\"Return (voice, operator), each unsigned.\"\"\"",
              "    if not _is_u32(word):", '        raise ValueError("target word must be a uint32 integer, not bool")',
              f"    return ((word >> {fields['voice'][0]}) & 0x{fields['voice'][1]:x},",
              f"            (word >> {fields['operator'][0]}) & 0x{fields['operator'][1]:x})", "", "",
              "def validate_target(scope, word, voice_count, selected_operator_count, patch_operator_count):",
              '    """Validate indices using the selected voice and explicit patch topology counts."""',
              "    if not all(_is_u32(value) for value in",
              "               (scope, word, voice_count, selected_operator_count, patch_operator_count)):",
              "        return False"]
    for name, minimum, maximum in bounds(schema):
        lines += [f"    if not {minimum} <= {name} <= {maximum}:", "        return False"]
    lines += ["    voice, operator = unpack_target(word)"]
    for name, expression in scope_conditions(schema, "python"):
        lines += [f"    if scope == {name}:", f"        return {expression}"]
    lines += ["    return False", ""]
    return "\n".join(lines)


def render_js(schema, digest):
    fields = field_parameters(schema)
    lines = [banner(schema, digest, "//"),
             f'export const FM_SCHEMA_VERSION = "{schema["schema_version"]}";',
             f'export const FM_SCHEMA_SHA256 = "{digest}";', ""]
    lines += [f"export const {name} = 0x{value:08x};" for name, value in numeric_constants(schema)]
    lines += ["", "function isU32(value) {", "  return Number.isInteger(value) && value >= 0 && value <= 0xffffffff;",
              "}", "", "export function packTarget(voice, operator) {",
              f"  if (!isU32(voice) || voice > 0x{fields['voice'][1]:x} ||",
              f"      !isU32(operator) || operator > 0x{fields['operator'][1]:x}) {{",
              '    throw new RangeError("target fields must be unsigned 16-bit integers");', "  }",
              f"  return (voice * (2 ** {fields['voice'][0]}) + operator * (2 ** {fields['operator'][0]})) >>> 0;",
              "}", "", "export function unpackTarget(word) {", "  if (!isU32(word)) {",
              '    throw new RangeError("target word must be a uint32 integer");', "  }",
              f"  return {{ voice: (word >>> {fields['voice'][0]}) & 0x{fields['voice'][1]:x},",
              f"           operator: (word >>> {fields['operator'][0]}) & 0x{fields['operator'][1]:x} }};",
              "}", "", "export function validateTarget(scope, word, voice_count, selected_operator_count, patch_operator_count) {",
              "  if (![scope, word, voice_count, selected_operator_count, patch_operator_count].every(isU32)) {",
              "    return false;", "  }"]
    for name, minimum, maximum in bounds(schema):
        lines += [f"  if ({name} < {minimum} || {name} > {maximum}) return false;"]
    lines += ["  const { voice, operator } = unpackTarget(word);"]
    for name, expression in scope_conditions(schema, "js"):
        lines += [f"  if (scope === {name}) return {expression};"]
    lines += ["  return false;", "}", ""]
    return "\n".join(lines)


def render_java(schema, digest):
    fields = field_parameters(schema)
    lines = [banner(schema, digest, "//"), "package cn.fpgasynth.control.generated;", "",
             "public final class FmContract {", "  private FmContract() {}",
             f'  public static final String FM_SCHEMA_VERSION = "{schema["schema_version"]}";',
             f'  public static final String FM_SCHEMA_SHA256 = "{digest}";', ""]
    lines += [f"  public static final long {name} = 0x{value:08x}L;" for name, value in numeric_constants(schema)]
    lines += ["", "  private static boolean isU32(long value) {", "    return value >= 0 && value <= 0xffffffffL;",
              "  }", "", "  public static long packTarget(long voice, long operator) {",
              f"    if (!isU32(voice) || voice > 0x{fields['voice'][1]:x}L ||",
              f"        !isU32(operator) || operator > 0x{fields['operator'][1]:x}L) {{",
              '      throw new IllegalArgumentException("target fields must be unsigned 16-bit integers");', "    }",
              f"    return (voice << {fields['voice'][0]}) | (operator << {fields['operator'][0]});", "  }", "",
              "  private static void requireWord(long word) {", "    if (!isU32(word)) {",
              '      throw new IllegalArgumentException("target word must be a uint32 integer");', "    }", "  }", "",
              "  public static long targetVoice(long word) {", "    requireWord(word);",
              f"    return (word >>> {fields['voice'][0]}) & 0x{fields['voice'][1]:x}L;", "  }", "",
              "  public static long targetOperator(long word) {", "    requireWord(word);",
              f"    return (word >>> {fields['operator'][0]}) & 0x{fields['operator'][1]:x}L;", "  }", "",
              "  public static boolean validateTarget(long scope, long word, long voice_count,",
              "                                       long selected_operator_count, long patch_operator_count) {",
              "    if (!isU32(scope) || !isU32(word) || !isU32(voice_count) ||",
              "        !isU32(selected_operator_count) || !isU32(patch_operator_count)) return false;"]
    for name, minimum, maximum in bounds(schema):
        lines += [f"    if ({name} < {minimum} || {name} > {maximum}) return false;"]
    lines += ["    long voice = targetVoice(word);", "    long operator = targetOperator(word);"]
    for name, expression in scope_conditions(schema, "java"):
        lines += [f"    if (scope == {name}) return {expression};"]
    lines += ["    return false;", "  }", "}", ""]
    return "\n".join(lines)


def render_c(schema, digest):
    fields = field_parameters(schema)
    lines = [banner(schema, digest, "//"), "#ifndef FM_CONTRACT_H", "#define FM_CONTRACT_H", "",
             "#include <stdbool.h>", "#include <stddef.h>", "#include <stdint.h>", "",
             f'#define FM_SCHEMA_VERSION "{schema["schema_version"]}"',
             f'#define FM_SCHEMA_SHA256 "{digest}"', ""]
    lines += [f"#define {name} UINT32_C(0x{value:08x})" for name, value in numeric_constants(schema)]
    lines += ["", "/* Validates before narrowing; failure never writes *out. */",
              "static inline bool fm_pack_target(uint32_t voice, uint32_t operator_index, uint32_t *out)", "{",
              f"    if (out == NULL || voice > UINT32_C(0x{fields['voice'][1]:x}) ||",
              f"        operator_index > UINT32_C(0x{fields['operator'][1]:x})) return false;",
              f"    *out = (voice << {fields['voice'][0]}) | (operator_index << {fields['operator'][0]});",
              "    return true;", "}", "", "static inline uint32_t fm_target_voice(uint32_t word)", "{",
              f"    return (word >> {fields['voice'][0]}) & UINT32_C(0x{fields['voice'][1]:x});", "}", "",
              "static inline uint32_t fm_target_operator(uint32_t word)", "{",
              f"    return (word >> {fields['operator'][0]}) & UINT32_C(0x{fields['operator'][1]:x});", "}", "",
              "static inline bool fm_validate_target(uint32_t scope, uint32_t word, uint32_t voice_count,",
              "                                      uint32_t selected_operator_count, uint32_t patch_operator_count)", "{"]
    for name, minimum, maximum in bounds(schema):
        # Avoid tautological unsigned < 0 warnings under -Werror.
        test = f"{name} > {maximum}" if minimum == 0 else f"{name} < UINT32_C({minimum}) || {name} > {maximum}"
        lines += [f"    if ({test}) return false;"]
    lines += ["    const uint32_t voice = fm_target_voice(word);", "    const uint32_t operator_index = fm_target_operator(word);"]
    for name, expression in scope_conditions(schema, "c"):
        lines += [f"    if (scope == {name}) return {expression};"]
    lines += ["    return false;", "}", "", "#endif /* FM_CONTRACT_H */", ""]
    return "\n".join(lines)


def render_sv(schema, digest):
    fields = field_parameters(schema)
    lines = [banner(schema, digest, "//"), "package fm_contract_pkg;",
             f'  localparam logic [{len(schema["schema_version"])*8-1}:0] FM_SCHEMA_VERSION = "{schema["schema_version"]}";',
             f"  localparam logic [255:0] FM_SCHEMA_SHA256 = 256'h{digest};", ""]
    lines += [f"  localparam logic [31:0] {name} = 32'h{value:08x};" for name, value in numeric_constants(schema)]
    lines += ["", "  // Check wide fields before converting them to fm_pack_target's 16-bit inputs.",
              "  function automatic logic fm_fields_valid(input logic [31:0] voice, input logic [31:0] operator_index);",
              "/*synthesis translate_off*/", "`ifndef SYNTHESIS", "    if ($isunknown({voice, operator_index})) return 1'b0;", "`endif", "/*synthesis translate_on*/",
              f"    return voice <= 32'h{fields['voice'][1]:08x} && operator_index <= 32'h{fields['operator'][1]:08x};", "  endfunction", "",
              "  function automatic logic [31:0] fm_pack_target(input logic [15:0] voice, input logic [15:0] operator_index);",
              f"    return ({{16'b0, voice}} << {fields['voice'][0]}) | ({{16'b0, operator_index}} << {fields['operator'][0]});",
              "  endfunction", "",
              "  function automatic logic [15:0] fm_target_voice(input logic [31:0] word);",
              f"    return word[{fields['voice'][0] + 15}:{fields['voice'][0]}];", "  endfunction", "",
              "  function automatic logic [15:0] fm_target_operator(input logic [31:0] word);",
              f"    return word[{fields['operator'][0] + 15}:{fields['operator'][0]}];", "  endfunction", "",
              "  function automatic logic fm_validate_target(",
              "      input logic [31:0] scope, input logic [31:0] word, input logic [31:0] voice_count,",
              "      input logic [31:0] selected_operator_count, input logic [31:0] patch_operator_count);",
              "    logic [31:0] voice;", "    logic [31:0] operator_index;",
              "/*synthesis translate_off*/", "`ifndef SYNTHESIS",
              "    if ($isunknown({scope, word, voice_count, selected_operator_count, patch_operator_count})) return 1'b0;",
              "`endif", "/*synthesis translate_on*/"]
    for name, minimum, maximum in bounds(schema):
        test = f"{name} > {maximum}" if minimum == 0 else f"{name} < 32'd{minimum} || {name} > {maximum}"
        lines += [f"    if ({test}) return 1'b0;"]
    lines += ["    voice = {16'b0, fm_target_voice(word)};", "    operator_index = {16'b0, fm_target_operator(word)};"]
    for name, expression in scope_conditions(schema, "sv"):
        lines += [f"    if (scope == {name}) return {expression};"]
    lines += ["    return 1'b0;", "  endfunction", "endpackage", ""]
    return "\n".join(lines)


RENDERERS = {"c": render_c, "sv": render_sv, "js": render_js,
             "java": render_java, "python": render_python}


def generated_files(schema, raw_schema, profiles=None, raw_profiles=None):
    digest = hashlib.sha256(raw_schema).hexdigest()
    if profiles is None or raw_profiles is None:
        raise SchemaError("schema and backend profiles are both required")
    identity = identities(raw_schema, raw_profiles)
    result = {}
    for path in schema["codegen"]["outputs"]:
        language = OUTPUT_LANGUAGES[path]
        content = RENDERERS[language](schema, digest)
        extension = render_extension(schema, profiles, identity, language)
        if language == "c":
            content = content.replace("#endif /* FM_CONTRACT_H */", extension + "\n#endif /* FM_CONTRACT_H */")
        elif language == "sv":
            content = content.replace("endpackage", extension + "\nendpackage")
        elif language == "java":
            position = content.rfind("}")
            content = content[:position] + extension + "\n" + content[position:]
        else:
            content += "\n" + extension
        result[path] = content.encode("utf-8")
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema", type=Path, default=ROOT / "schema" / "fm-control.schema.json")
    parser.add_argument("--profiles", type=Path, default=ROOT / "schema" / "backend-profiles.json")
    parser.add_argument("--out", type=Path, default=ROOT / "generated")
    parser.add_argument("--check", action="store_true", help="read-only check for missing or drifted outputs")
    arguments = parser.parse_args(argv)
    try:
        raw_schema = arguments.schema.read_bytes()
        schema = validate_schema(json.loads(raw_schema, object_pairs_hook=duplicate_free_object))
        raw_profiles = arguments.profiles.read_bytes()
        profiles = validate_profiles(json.loads(raw_profiles, object_pairs_hook=duplicate_free_object),
                                     schema, require, integer, identifier)
        expected = generated_files(schema, raw_schema, profiles, raw_profiles)
        if arguments.check:
            problems = []
            for relative, content in expected.items():
                path = arguments.out / relative
                if not path.is_file():
                    problems.append(f"missing: {relative}")
                elif path.read_bytes() != content:
                    problems.append(f"drift: {relative}")
            if problems:
                print("\n".join(problems), file=sys.stderr)
                return 1
            print(f"checked {len(expected)} generated files; schema SHA256 {hashlib.sha256(raw_schema).hexdigest()}")
        else:
            for relative, content in expected.items():
                path = arguments.out / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
            print(f"generated {len(expected)} files; schema SHA256 {hashlib.sha256(raw_schema).hexdigest()}")
        return 0
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"FM contract generation failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
