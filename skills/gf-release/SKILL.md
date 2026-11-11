---
name: gf-release
description: >
  GateFlow release readiness workflow. Validates plugin manifests, marketplace
  metadata, docs index coverage, root mirrors, release notes, and component
  counts before a version tag is created. Use when preparing, checking, or
  cutting a GateFlow plugin release.
---
Read [the Codex execution contract](../gf/references/codex-runtime.md) before this workflow. Preserve analysis/plan/annotation scope. The `sv-*`, `pcb-designer`, and `worker_role` labels below are responsibilities, not registered tools or agent types. Perform work directly unless bounded delegation is available and authorized; pass ordinary task prompts using the actual host tool. Read named skills from the catalog, not a fictional `Skill` API. Shell examples marked `bash` require a POSIX shell; Windows uses the PowerShell discovery in the contract. Persist maps, plans, profiles and configuration only when requested or necessary for authorized implementation.


# GF-Release -- Release Readiness

Use this workflow before tagging or publishing GateFlow.

## Required Inputs

- Target version, for example `2.5.0`
- Whether this is check-only or a release-prep edit

First discover the actual package/repository layout and version authority. Do not assume that `plugins/gateflow/.codex-plugin/plugin.json`, a marketplace file, release index, validator, or test exists. If a real version source is found and no version was provided, propose the next semver version based on the changes:

| Change Type | Version Bump |
|---|---|
| Metadata, docs, or packaging fix | Patch |
| New command, skill, agent, IP, or board | Minor |
| Breaking plugin layout or workflow change | Major |

## Release Checks

For the current skill-bundle layout, run `skill-creator`'s `quick_validate.py` against every skill, resolve authored local Markdown links, check duplicate names/dependencies, verify relative paths and the recorded SHA-256 baseline, and run every actually present changed script test. If the repository contains a deterministic release validator, inspect its help and then run it from the repo root. The following legacy command is valid only when that script exists:

```bash
python3 tools/validate_gateflow.py --version <target-version>
```

When present, the legacy validator may check:
- plugin and marketplace JSON version consistency
- component counts in descriptions and READMEs
- `docs/gateflow.index` coverage for every command, skill, and agent
- root-level mirrors for every command-adjacent skill and agent file
- a release note entry for the target version

## Prep Workflow

1. Count actual components from discovered directories. The following legacy layout commands apply only when those paths exist:

```bash
find plugins/gateflow/agents -maxdepth 1 -name '*.md' | wc -l
find plugins/gateflow/skills -maxdepth 2 -name 'SKILL.md' | wc -l
find plugins/gateflow/commands -maxdepth 1 -name '*.md' | wc -l
```

2. Update version strings only in discovered, authoritative metadata. Legacy candidates include:
- `plugins/gateflow/.codex-plugin/plugin.json`
- the repository's actual marketplace metadata file, if one exists; do not assume a provider-specific directory

3. Update only release-facing docs that actually exist and are authoritative. Legacy candidates include:
- `README.md`
- `plugins/gateflow/README.md`
- `docs/gateflow.index`
- `releases.md`

4. Run the current-layout checks above plus any discovered validator and focused tests. These legacy commands are conditional on the files existing:

```bash
python3 -m unittest tests/test_validate_gateflow.py
python3 tools/validate_gateflow.py --version <target-version>
```

5. Only after validation passes, tag and release:

```bash
git tag v<target-version>
gh release create v<target-version> --title "GateFlow v<target-version>" --notes-file <notes-file>
```

## Report Format

```text
---GATEFLOW-RESULT---
STATUS: PASS | FAIL | BLOCKED | NOT_RUN
VERSION: <target-version>
INVENTORY: <agents> agents, <skills> skills, <commands> commands, <ip> IP, <boards> boards
FILES: <changed release-facing files>
DETAILS: <summary or validation failures>
---END-GATEFLOW-RESULT---
```
