#!/usr/bin/env python3
"""Verify frozen S3 HTTP v2 handlers and decode vectors with the production C parser.

No App build, ESP-IDF build, source regeneration, device or shared-owner write occurs.
Compilers and test processes only create outputs in build/http-v2-handler/.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys
import tempfile

APP_ROOT = pathlib.Path(__file__).resolve().parents[1]
BUNDLE_DIRECTORY = "contracts/s3-http-v2-20261011"
UINT32_MAX = 0xffffffff


class HandlerError(ValueError):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def relative_path(value):
    if not isinstance(value, str):
        raise HandlerError("manifest path must be a string")
    path = pathlib.PurePosixPath(value)
    if (not value or path.is_absolute() or ".." in path.parts
            or path.as_posix() != value or value == "."):
        raise HandlerError("invalid manifest path: " + value)
    return path


def file_inventory(directory):
    if not directory.is_dir() or directory.is_symlink():
        raise HandlerError("missing or linked frozen directory: " + str(directory))
    files, directories = set(), set()
    for path in directory.rglob("*"):
        if path.is_symlink():
            raise HandlerError("linked frozen input: " + str(path))
        relative = path.relative_to(directory).as_posix()
        if path.is_file():
            files.add(relative)
        elif path.is_dir():
            directories.add(relative)
        else:
            raise HandlerError("nonregular frozen input: " + relative)
    return files, directories


def read_exact(path, entry):
    if path.is_symlink() or not path.is_file():
        raise HandlerError("missing or linked frozen file: " + str(path))
    data = path.read_bytes()
    if type(entry["bytes"]) is not int or len(data) != entry["bytes"]:
        raise HandlerError("frozen byte length changed: " + str(path))
    if sha256(data) != entry["sha256"]:
        raise HandlerError("frozen SHA256 drift: " + str(path))
    return data


def check_frozen_inputs(root=APP_ROOT, verify_origin=False):
    """Read-only check: reject missing, extra, linked or changed inputs; never repair."""
    root = pathlib.Path(root).resolve()
    bundle = root / BUNDLE_DIRECTORY
    manifest_bytes = (bundle / "manifest.json").read_bytes()
    manifest = json.loads(manifest_bytes)
    if manifest["manifest_format"] != 1:
        raise HandlerError("unsupported handler manifest format")
    entries = manifest["files"]
    expected = {str(relative_path(entry["path"])) for entry in entries}
    if len(expected) != len(entries) or "manifest.json" in expected:
        raise HandlerError("duplicate frozen input path")
    expected.add("manifest.json")
    actual_files, actual_directories = file_inventory(bundle)
    expected_directories = {
        parent.as_posix() for value in expected
        for parent in pathlib.PurePosixPath(value).parents if parent.as_posix() != "."
    }
    if actual_files != expected or actual_directories != expected_directories:
        raise HandlerError("frozen file set changed: missing="
                           + repr(sorted(expected - actual_files)) + "; extra="
                           + repr(sorted(actual_files - expected)) + "; directories="
                           + repr(sorted(actual_directories ^ expected_directories)))
    for entry in entries:
        read_exact(bundle / relative_path(entry["path"]), entry)
        if entry["kind"] == "s3_original":
            source_relative = str(relative_path(entry["source_relative_path"]))
            if entry["path"] != "s3/" + source_relative:
                raise HandlerError("source path binding changed: " + entry["path"])
            if verify_origin:
                read_exact(pathlib.Path(manifest["source"]["root"]) / source_relative, entry)

    tools = manifest["app_tools"]
    expected_tools = {"tools/HttpV2ParseBridge.c", "tools/test_http_v2_handler.py"}
    if {str(relative_path(entry["path"])) for entry in tools} != expected_tools or len(tools) != 2:
        raise HandlerError("handler adapter tool set changed")
    for entry in tools:
        data = read_exact(root / relative_path(entry["path"]), entry)
        if "frozen_copy" in entry:
            frozen = bundle / relative_path(entry["frozen_copy"])
            if frozen.read_bytes() != data:
                raise HandlerError("adapter source differs from its frozen copy")

    identity = manifest["identity"]
    if (identity["schema_version"], identity["abi_major"], identity["abi_minor"],
            identity["api_major"], identity["api_minor"], identity["profile_id"],
            identity["profile_version"]) != ("1.0.1", 1, 1, 2, 0, 1, 65536):
        raise HandlerError("unexpected frozen S3 contract identity")
    header = (bundle / "s3/contract/fm/generated/c/fm_contract.h").read_text(encoding="utf-8")
    for name, value in (("FM_SCHEMA_VERSION", identity["schema_version"]),
                        ("FM_SCHEMA_SHA256", identity["schema_sha256"]),
                        ("FM_PROFILES_SHA256", identity["profiles_sha256"]),
                        ("FM_CONTRACT_SHA256", identity["contract_sha256"])):
        if '#define ' + name + ' "' + value + '"' not in header:
            raise HandlerError("generated C identity mismatch: " + name)
    profile = json.loads((bundle / "s3/docs/fm/http-v2-fixtures/v2-profile.json").read_bytes())
    if (profile["profile_id"], profile["profile_version"], profile["backend_id"]) != (1, 65536, 1):
        raise HandlerError("fixture profile identity changed")
    if profile["profiles_sha256"] != identity["profiles_sha256"]:
        raise HandlerError("fixture profiles SHA changed")
    frozen_fixtures = {"s3/docs/fm/http-v2-fixtures/" + name for name in manifest["fixtures"]}
    actual_fixtures = {entry["path"] for entry in entries
                       if entry["path"].startswith("s3/docs/fm/http-v2-fixtures/")}
    if frozen_fixtures != actual_fixtures or len(manifest["fixtures"]) != 8:
        raise HandlerError("fixture names or count changed")
    manifest["verified_manifest_sha256"] = sha256(manifest_bytes)
    return manifest


def read_vectors(path):
    data = pathlib.Path(path).read_bytes()
    vectors = json.loads(data)
    if not isinstance(vectors, list):
        raise HandlerError("vectors must be a JSON array")
    for index, vector in enumerate(vectors):
        if (not isinstance(vector, dict)
                or not {"body", "words", "valid"}.issubset(vector)
                or not isinstance(vector["body"], str)
                or type(vector["valid"]) is not bool
                or not isinstance(vector["words"], list) or len(vector["words"]) != 7
                or any(type(word) is not int or not 0 <= word <= UINT32_MAX
                       for word in vector["words"])):
            raise HandlerError("invalid vector record at index " + str(index))
        if len(vector["body"].encode("utf-8")) > 1024 * 1024:
            raise HandlerError("vector body exceeds bridge frame limit")
    return vectors, sha256(data)


def execute(command, cwd, transcript, input_bytes=None):
    result = subprocess.run(command, cwd=cwd, input=input_bytes, capture_output=True)
    transcript.append({"command": [str(arg) for arg in command], "cwd": str(cwd),
                       "exit_code": result.returncode,
                       "stdout": result.stdout.decode("utf-8", errors="replace"),
                       "stderr": result.stderr.decode("utf-8", errors="replace")})
    if result.returncode:
        raise HandlerError("process failed: " + repr(command) + "\n"
                           + transcript[-1]["stdout"] + transcript[-1]["stderr"])
    return result.stdout


def check_vectors(binary, vectors, cwd, transcript):
    framed = bytearray()
    for vector in vectors:
        body = vector["body"].encode("utf-8")
        framed.extend(struct.pack(">I", len(body)))
        framed.extend(body)
    output = execute([str(binary)], cwd, transcript, bytes(framed))
    lines = output.decode("ascii").splitlines()
    if len(lines) != len(vectors):
        raise HandlerError("bridge output count differs from input vector count")
    results = []
    for index, (line, vector) in enumerate(zip(lines, vectors)):
        # Parse the bridge's own numeric output; never parse the request body in Python.
        fields = line.split(" ")
        if len(fields) != 8 or any(not re.fullmatch(r"[0-9]+", field) for field in fields):
            raise HandlerError("malformed bridge result at index " + str(index))
        decoded = [int(field) for field in fields]
        if decoded[0] not in (0, 1):
            raise HandlerError("invalid bridge success flag")
        if bool(decoded[0]) != vector["valid"]:
            raise HandlerError("C acceptance mismatch at vector " + str(index)
                               + ": " + repr(vector["body"]))
        if vector["valid"] and decoded[1:] != vector["words"]:
            raise HandlerError("C full-width decode mismatch at vector " + str(index)
                               + ": expected=" + repr(vector["words"])
                               + "; decoded=" + repr(decoded[1:]))
        if not vector["valid"] and any(decoded[1:]):
            raise HandlerError("rejected vector unexpectedly produced an event")
        results.append({"index": index, "name": vector.get("name", str(index)),
                        "valid": bool(decoded[0]), "decoded_words": decoded[1:],
                        "body_sha256": sha256(vector["body"].encode("utf-8"))})
    return results


def run_handler_checks(vectors_file=None, verify_origin=False):
    manifest = check_frozen_inputs(verify_origin=verify_origin)
    bundle = APP_ROOT / BUNDLE_DIRECTORY
    source = bundle / "s3"
    builtins, builtin_sha = read_vectors(bundle / "parser-vectors.json")
    external, external_sha = (read_vectors(vectors_file) if vectors_file else ([], None))
    build_root = APP_ROOT / "build/http-v2-handler"
    build_root.mkdir(parents=True, exist_ok=True)
    output = pathlib.Path(tempfile.mkdtemp(prefix="run-", dir=build_root))
    transcript = []
    report = {"date_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "scope": "actual C handlers/parser; SDK boundary host inputs; no device discovery or sound validation",
              "output_directory": str(output), "identity": manifest["identity"],
              "closure_files": len(manifest["files"]),
              "manifest_sha256": manifest["verified_manifest_sha256"],
              "origin_rechecked": verify_origin, "builtin_vectors_sha256": builtin_sha,
              "app_vectors_sha256": external_sha, "app_vectors_file": str(vectors_file) if vectors_file else None}
    try:
        execute(["cc", "--version"], source, transcript)
        includes = ["-Itests/host/http_stubs", "-Itests/host/stubs", "-Imain"]
        handler = output / "http_v2"
        execute(["cc", "-std=gnu17", "-Wall", "-Wextra", "-Werror",
                 "-ffunction-sections", "-fdata-sections", *includes,
                 "tests/host/test_http_v2.c", "main/web_control.c", "main/patch.c",
                 "-Wl,--gc-sections", "-lm", "-o", str(handler)], source, transcript)
        handler_stdout = execute([str(handler), *[str(output / name) for name in manifest["fixtures"]]],
                                 source, transcript).decode("utf-8")
        report["handler_stdout"] = handler_stdout
        fixture_results = []
        for name in manifest["fixtures"]:
            actual = (output / name).read_bytes()
            original = (source / "docs/fm/http-v2-fixtures" / name).read_bytes()
            if actual != original:
                raise HandlerError("handler output differs byte-for-byte from original fixture: " + name)
            fixture_results.append({"name": name, "bytes": len(actual), "sha256": sha256(actual),
                                    "byte_exact": True, "newline_difference": False})
        report["fixtures"] = fixture_results
        bridge = output / "http_v2_parse_bridge"
        execute(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *includes,
                 str(bundle / "bridge/HttpV2ParseBridge.c"), "-o", str(bridge)], source, transcript)
        report["builtin_vectors"] = check_vectors(bridge, builtins, source, transcript)
        if vectors_file:
            report["app_vectors"] = check_vectors(bridge, external, source, transcript)
            if sha256(pathlib.Path(vectors_file).read_bytes()) != external_sha:
                raise HandlerError("App vector file changed during C validation")
        # Catch source/tool drift during compilation without modifying any input.
        final_manifest = check_frozen_inputs(verify_origin=verify_origin)
        if final_manifest["verified_manifest_sha256"] != manifest["verified_manifest_sha256"]:
            raise HandlerError("frozen manifest changed during validation")
        report["result"] = "PASS"
        print(handler_stdout.strip())
        print("RESULT HTTP v2 handler: " + str(len(manifest["files"]))
              + " SHA-bound files; 8/8 byte-exact fixtures; " + str(len(builtins))
              + " fixed C envelope/parser vectors; " + str(len(external)) + " provided vectors PASS")
        print("EVIDENCE " + str(output / "summary.json"))
        return report
    except Exception as error:
        report["result"] = "FAIL"
        report["error"] = str(error)
        raise
    finally:
        report["commands"] = transcript
        (output / "summary.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vectors", type=pathlib.Path,
                        help="JSON [{body:string, words:[7 uint32], valid:boolean}]; actual App bodies")
    parser.add_argument("--check-only", action="store_true", help="only read/verify the frozen closure; create no output")
    parser.add_argument("--verify-origin", action="store_true", help="also verify every S3 original against its source SHA")
    args = parser.parse_args()
    try:
        if args.check_only:
            manifest = check_frozen_inputs(verify_origin=args.verify_origin)
            print("HTTP v2 frozen input check PASS: " + str(len(manifest["files"]))
                  + " files; manifest SHA256 " + manifest["verified_manifest_sha256"])
        else:
            run_handler_checks(args.vectors, args.verify_origin)
    except (OSError, ValueError, TypeError, KeyError, UnicodeError) as error:
        print("HTTP v2 handler check failed: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
