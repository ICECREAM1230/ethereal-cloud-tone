# CH0/CH1 Control Mapping Implementation Plan

> **For agentic workers:** Execute inline with superpowers:executing-plans. User authorization covers this reversible independent prototype; no stage, commit, hardware write or main-project integration.

**Goal:** Produce and replay an independent C99 pitch/volume algorithm for AE350 using the existing CSV.

**Architecture:** Fixed provisional calibration with independent causal per-channel filters and timed output controls. CSV fitting/replay never accesses a device. Optional plotting consumes saved CSV only.

**Tech Stack:** C99 core/test/replay; Python standard library test driver and optional matplotlib plots.

**Spec:** `docs/superpowers/specs/2026-10-30-control-mapping-design.md`

## Global Constraints

- Source: `/home/ice/下载/fdc2214-raw-session-17 (1).csv`, SHA256 `8458b352465caf6078c000575d95127db6707d9c9323ed9764d154092070d1ae`.
- No main-project/firmware/GUI edits, serial access, hardware programming, staging or commits.
- CH0 pitch, CH1 volume; amplitude warnings accepted for preview with quality retained.
- Experimental quantile endpoints are not identified physical near/far endpoints. Frozen during replay.
- Results and recoverable pre-edit notes under the current `verification/control_mapping_*` directory.

## Review Focus

- A warning cannot hide a watchdog or hard error.
- Loss of one lane must not prevent the other lane from updating; stale audio must fade.
- First samples, long gaps and time rollback cannot reintroduce old filter state.
- Flat/noisy/invalid calibration cannot divide by zero or silently expand sensitivity.
- CLI cannot overwrite source/profile/results or relabel dataset fitting as independent validation.

## Task 1: Core algorithm (C delivery selected by user)

- [x] Write `tests/host/test_theremin_control.c` before C implementation; exercise real filter/controller behavior, rejection, stale and numerical boundaries.
- [x] Retain expected RED evidence for missing module, then behavioral RED→GREEN regressions for hard_error without fresh data and gap recovery gain.
- [x] Implement `ae350/theremin_control/theremin_control.c/.h`: config, independent causal lanes, 1€filter, output controls and explicit rebaseline.
- [x] Verify native and ASan/UBSan GREEN, including static-jitter and steps at synthetic10/142Hz.

## Task 2: Offline replay and fixed profile

- [x] Preserve the task-created Python prototype as historical evidence after user selected C; active deliverable is C.
- [x] Implement `tests/host/replay_theremin_control.c` and `tools/test_control_mapping.py`, preserving source and non-overwriting result paths.
- [x] Freeze provisional endpoints in C defaults; export actual C config with replay; replay600 rows and save metrics/CSV.
- [x] Add `tools/plot_control_mapping.py`, render and inspect saved plots.

## Task 3: Documentation and validation

- [x] Add `docs/control_mapping.md`, short README entry, current Chinese `.ai/` notes.
- [x] Run host Python, GUI and existing C host validation; no ESP-IDF or FPGA rebuild needed.
- [x] Independent read-only review of the algorithm; no substantive findings. Known gap/fresh diagnostics fixes already have RED→GREEN evidence.
- [x] Review diffs and whitespace, verify unchanged input/main fingerprints, preserve important results and remove only owned unnecessary temporary files. Existing fixed host regression report refresh is disclosed in notes and final report.
