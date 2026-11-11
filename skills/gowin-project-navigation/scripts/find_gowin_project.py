#!/usr/bin/env python3
"""Read-only Gowin project inventory for Linux and Windows (Python 3.9+)."""

import argparse
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path, PureWindowsPath


SECRET_KEY = re.compile(r"key|password|secret|token|credential", re.I)
SAFE_TEXT_KEY = re.compile(
    r"^(TopModule|IncludePath|show_all_warnings|VCC|VCCX|VCCAUX|"
    r"COMPRESS|CRC_CHECK|SECURITY_BIT|SSPI|MSPI|JTAG|Clock_Route_Order|"
    r"Place_Option|Route_Option|Route_Maxfan|Global_Freq|Optimization_Goal|"
    r"Synthesize_tool|CPU|Correct_Hold_Violation|POWER_ON_RESET_MONITOR|Unused_Pin)$",
    re.I,
)
OUTPUT_FILE = re.compile(r"\.(vg|rpt\.html|rpt\.txt|tr\.html|timing_paths|power\.html|pin\.html|fs|bin|db)$", re.I)


def read_gowin_xml(path):
    content = path.read_text(encoding="utf-8-sig")
    content = re.sub(r'(?i)<\?xml\s+version\s*=\s*(["\'])1\1', '<?xml version="1.0"', content, count=1)
    # The normal Gowin DOCTYPE only names the document. Reject other DTDs and
    # entities before parsing rather than resolving untrusted project content.
    content = re.sub(r"(?i)<!DOCTYPE\s+[A-Za-z_][\w:.-]*\s*>", "", content, count=1)
    if re.search(r"(?i)<!\s*(DOCTYPE|ENTITY)", content):
        raise ValueError(f"Unsupported DTD or entity declaration in {path}")
    try:
        return ET.fromstring(content)
    except ET.ParseError as exc:
        raise ValueError(f"Malformed XML in {path}: {exc}") from exc


def nodes(parent, name):
    return (node for node in parent.iter() if node.tag.rsplit("}", 1)[-1] == name)


def reference_exists(project_root, raw_path):
    if PureWindowsPath(raw_path).is_absolute() and not Path(raw_path).is_absolute():
        return False  # A Windows absolute path is not a Linux relative path.
    candidate = Path(raw_path.replace("\\", "/"))
    if not candidate.is_absolute():
        candidate = project_root / candidate
    return candidate.exists()


def relative(path, base):
    try:
        return path.relative_to(base).as_posix() or "."
    except ValueError:
        return path.as_posix()


def artifact_class(path):
    lower = path.lower()
    if lower.startswith("impl/gwsynthesis/"):
        return "synthesis"
    if lower.startswith("impl/pnr/"):
        return "place-and-route"
    if lower.endswith((".gprj", ".gprj.user")):
        return "project"
    if lower.endswith((".vp", ".vhdp", ".vgp")):
        return "encrypted-source"
    if lower.startswith("src/") or lower.endswith((
        ".v", ".sv", ".vh", ".svh", ".vhd", ".vhdl", ".cst",
        ".sdc", ".gsc", ".ipc", ".mod", ".gao", ".rao",
    )):
        return "source-or-constraint"
    return "other"


class JsonObjectPairs(list):
    """Keep duplicate JSON keys visible to the recursive redaction check."""


def has_secret_key(value):
    if isinstance(value, JsonObjectPairs):
        return any(SECRET_KEY.search(key) or has_secret_key(item)
                   for key, item in value)
    if isinstance(value, dict):
        return any(SECRET_KEY.search(key) or has_secret_key(item)
                   for key, item in value.items())
    if isinstance(value, list):
        return any(has_secret_key(item) for item in value)
    return False


def process_config(path):
    text = path.read_text(encoding="utf-8-sig")
    try:
        if not isinstance(json.loads(text), dict):
            raise ValueError("top level must be a JSON object")
        decoder = json.JSONDecoder(object_pairs_hook=JsonObjectPairs)
        fields = []
        cursor = text.index("{") + 1
        while True:
            while cursor < len(text) and text[cursor].isspace():
                cursor += 1
            if text[cursor] == "}":
                break
            key, cursor = decoder.raw_decode(text, cursor)
            if not isinstance(key, str):
                raise ValueError("object key must be a string")
            while text[cursor].isspace():
                cursor += 1
            if text[cursor] != ":":
                raise ValueError("expected colon")
            cursor += 1
            while text[cursor].isspace():
                cursor += 1
            start = cursor
            decoded, cursor = decoder.raw_decode(text, cursor)
            value = "<redacted>" if SECRET_KEY.search(key) or has_secret_key(decoded) else text[start:cursor]
            fields.append({"Key": key, "Value": value})
            while text[cursor].isspace():
                cursor += 1
            if text[cursor] == "}":
                break
            if text[cursor] != ",":
                raise ValueError("expected comma")
            cursor += 1
        seen = {}
        repeated = set()
        for field in fields:
            folded = field["Key"].casefold()
            if folded in seen:
                repeated.add(seen[folded])
            else:
                seen[folded] = field["Key"]
        return {"ParseStatus": "valid-json/top-level-raw-values", "Fields": fields,
                "CaseVariantKeyGroups": sorted(repeated)}
    except (ValueError, IndexError, json.JSONDecodeError) as exc:
        return {"ParseStatus": f"diagnostic-only: {exc}", "Fields": [],
                "CaseVariantKeyGroups": []}


def collect_files(project_root):
    files = []
    nested_projects = []
    for directory, children, names in os.walk(project_root):
        children[:] = [name for name in children if name not in {".git", ".svn", "node_modules"}]
        directory = Path(directory)
        projects = [directory / name for name in names if name.endswith(".gprj")]
        if directory != project_root and projects:
            nested_projects.extend(projects)
            children[:] = []
            continue
        files.extend(directory / name for name in names if (directory / name).is_file())
    return sorted(files), sorted(nested_projects)


def inspect_project(project, scan_root):
    project_root = project.parent
    xml = read_gowin_xml(project)
    device_node = next(nodes(xml, "Device"), None)
    device_part = (device_node.get("pn", "").strip() if device_node is not None else "") or None
    device_name = (device_node.get("name", "").strip() if device_node is not None else "") or None
    device_identifier = ((device_node.text or "").strip() if device_node is not None else "") or None
    device = device_part or device_name or device_identifier or "<not declared>"
    project_files = []
    for file_list in nodes(xml, "FileList"):
        for entry in file_list:
            if entry.tag.rsplit("}", 1)[-1] != "File":
                continue
            raw = entry.get("path")
            if raw:
                project_files.append({"Path": raw, "Type": entry.get("type", ""),
                                      "Enabled": entry.get("enable", ""),
                                      "Exists": reference_exists(project_root, raw)})

    user_state = project.with_name(project.name + ".user")
    registered_results = []
    flow_processes = []
    if user_state.exists():
        user_xml = read_gowin_xml(user_state)
        for entry in nodes(user_xml, "ResultFile"):
            raw = entry.get("ResultFilePath")
            if raw:
                registered_results.append({"Type": entry.get("ResultFileType", ""),
                                           "Path": raw,
                                           "Exists": reference_exists(project_root, raw)})
        for entry in nodes(user_xml, "Process"):
            if entry.get("ID"):
                flow_processes.append({"Id": entry.get("ID"), "State": entry.get("State", "")})

    files, nested_projects = collect_files(project_root)
    relative_files = [relative(path, project_root) for path in files]
    configs = [path for path in files if path.name == "process_config.json"
               or path.name.endswith("_process_config.json")]
    config_details = [{"Path": relative(path, scan_root), **process_config(path)}
                      for path in configs]
    project_relative = relative(project_root, scan_root)
    counts = {name: sum(artifact_class(path) == name for path in relative_files)
              for name in ("project", "source-or-constraint", "encrypted-source",
                           "synthesis", "place-and-route", "other")}
    return {
        "ProjectFile": relative(project, scan_root), "ProjectRoot": project_relative,
        "Device": device, "UserStateFile": relative(user_state, scan_root) if user_state.exists() else None,
        "DevicePartNumber": device_part, "DeviceName": device_name,
        "DeviceIdentifier": device_identifier,
        "NestedProjects": [relative(path, scan_root) for path in nested_projects],
        "SourceDirectory": relative(project_root / "src", scan_root) if (project_root / "src").is_dir() else None,
        "SynthesisDirectory": relative(project_root / "impl/gwsynthesis", scan_root)
        if (project_root / "impl/gwsynthesis").is_dir() else None,
        "PnrDirectory": relative(project_root / "impl/pnr", scan_root)
        if (project_root / "impl/pnr").is_dir() else None,
        "SimulationDirectories": sorted({relative(path.parent, scan_root) for path in files
                                         if any(part in {"sim", "simulation"} for part in path.relative_to(project_root).parts[:-1])}),
        "ProcessConfigFiles": [relative(path, scan_root) for path in configs],
        "ProcessConfigDetails": config_details, "ProjectFiles": project_files,
        "RegisteredResults": registered_results, "FlowProcesses": flow_processes,
        "FileCounts": counts,
        "EncryptedFiles": [path for path in relative_files if path.lower().endswith((".vp", ".vhdp", ".vgp"))],
        "RepresentativeOutputs": [path for path in relative_files if OUTPUT_FILE.search(path)][:30],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="Scoped directory or one .gprj")
    parser.add_argument("--project", type=Path, help="Selected .gprj inside --root")
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON")
    args = parser.parse_args()
    root = args.root.expanduser().resolve(strict=True)
    if args.project:
        if not root.is_dir():
            parser.error("--root must be a directory when --project is given")
        project = args.project.expanduser().resolve(strict=True)
        if project.suffix != ".gprj" or not project.is_relative_to(root):
            parser.error("--project must be a .gprj inside --root")
        projects = [project]
        scan_root = root
    elif root.is_file():
        if root.suffix != ".gprj":
            parser.error("--root file must be a .gprj")
        projects = [root]
        scan_root = root.parent
    else:
        scan_root = root
        projects = sorted(path for path in root.rglob("*.gprj") if not any(
            part in {".git", ".svn", "node_modules"} for part in path.relative_to(root).parts))

    records = [inspect_project(project, scan_root) for project in projects]
    if args.json:
        print(json.dumps(records[0] if len(records) == 1 else records, ensure_ascii=False, indent=2))
    elif not records:
        print(f"No .gprj files found below {scan_root}")
    else:
        for record in records:
            print(f"Project: {record['ProjectFile']}\n  Root: {record['ProjectRoot']}\n  Device: {record['Device']}")
            print(f"  Part number: {record['DevicePartNumber'] or '<not declared>'}; identifier: {record['DeviceIdentifier'] or '<not declared>'}")
            if record["NestedProjects"]:
                print(f"  Separate nested projects: {', '.join(record['NestedProjects'])}")
            print(f"  Configured files: {len(record['ProjectFiles'])}; registered results: {len(record['RegisteredResults'])}")
            print(f"  Synthesis: {record['SynthesisDirectory'] or '<missing>'}")
            print(f"  PnR: {record['PnrDirectory'] or '<missing>'}")
            for config in record["ProcessConfigDetails"]:
                print(f"  Process config: {config['Path']} [{config['ParseStatus']}]")
                for field in config["Fields"]:
                    if SAFE_TEXT_KEY.match(field["Key"]):
                        print(f"    {field['Key']}={field['Value']}")
                if config["CaseVariantKeyGroups"]:
                    print(f"    case-variant key groups: {', '.join(config['CaseVariantKeyGroups'])}")
            print(f"  Encrypted HDL/IP files: {', '.join(record['EncryptedFiles']) or '<none detected>'}")
            print(f"  Counts: {json.dumps(record['FileCounts'])}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
