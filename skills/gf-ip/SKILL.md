---
name: gf-ip
description: >
  Inspect and use actually installed GateFlow IP blocks when their metadata,
  sources, and verification evidence are present; otherwise explain that the
  requested block is unavailable rather than treating a static catalog as inventory.
  Example: "add a FIFO to my project", "$gf-ip add uart"
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow installed IP

For `list`, `info` or `add`, first discover actual `ip/<name>/` directories and inspect `block.yaml`, RTL, TB/formal files and applicable verification evidence. A catalog row is not installed or verified inventory. If required files are absent, report unavailable; do not invent a verified replacement.

Inspect/describe blocks read-only unless implementation is requested. Copy/adapt only a discovered block within that authorization. Existing Gowin IP uses `.ipc`, wrappers/templates and `.gprj` evidence; a portable replacement or simulation stub requires the user's explicit choice.

- For actual block metadata, comparison or catalog interpretation: [catalog and schema](references/catalog-and-metadata.md).
- After availability/evidence and implementation authorization are established: [adding and instantiating](references/adding-and-instantiating.md).

Keep current-source verification distinct from supplied historical evidence. Preserve generated/protected implementation and source-list authority.
