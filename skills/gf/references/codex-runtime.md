# GateFlow execution contract for Codex

## Scope and authority
User instructions and repository AGENTS.md take precedence. Skills guide the current task; they do not authorize extra actions.
- **analysis**: read and explain; do not persist maps/config/source without a request.
- **plan**: propose changes. “先做 plan，不要改动” permits no file edits, including helper/config creation. A requested saved plan permits that artifact only.
- **annotation**: edit comments/documentation within scope; preserve HDL tokens, directives, constants, IPs and constraints, encoding and newlines. Report bugs separately.
- **implementation**: complete authorized changes and relevant checks.
- **verification**: execute checks; do not silently repair functional logic.
Existing plans do not authorize implementation. Configuration does not authorize downloads, installations, programming or publication. Do not ask again for already authorized actions. Actual filesystem escalation still uses the host approval mechanism.

## Codex tools and host
Use tools actually exposed in the session: shell/files through exec_command, edits through apply_patch or safe native operations, and collaboration through available subagent tools. A skill is a SKILL.md to read, not a callable specialist service. Do not invent tools or agent roles. Ask questions through a currently available mechanism and follow its mode contract.
Resolve Markdown links relative to the file containing them; sibling skills belong to this bundle. If a sibling is absent, search the installed catalog, then report the dependency gap or use the documented underlying workflow where possible.

## Execution platform
Choose the actual execution host automatically unless the user specifies another target. No OS-selection question is needed. Reuse confirmed task tools and environments; discover/activate only a requested dependency, without repeated installation or unrelated system probes.
- Linux software execution: read [Linux host calls](host-linux.md) only when a call is needed. Native Linux does not need WSL device instructions.
- Windows software execution: read [Windows host calls](host-windows.md) only for that target. Shared RTL analysis needs neither host-call leaf.
Avoid shell string interpolation and broad cleanup globs. Generated scratch data belongs in a dedicated scratch/build directory; never delete user output to troubleshoot a tool.

## Delegation and model selection
Direct work is appropriate for small tasks. Delegate bounded independent work only when delegation is available and authorized by user/applicable instructions. Give each agent its goal, owned files, read-only inputs, exclusions and success criteria. One writer per file; agree interfaces before parallel coding. Do not create user-visible tasks for internal subtasks.
Follow the user's model preference. This installation's user requests lower-tier models by difficulty: consider available GPT-5.5/5.6 models for inventory, documentation and bounded coding; reserve the main model for difficult architecture and integration. Use actual available model IDs and scoped context when the API requires it for overrides. Without a user preference, inherit the session model. No fixed foreign-provider model, minimum agent count, fictional pricing or recursive chains.

## Git and side effects
Discover the actual repository root, which may be below the workspace. Inspect staged/unstaged/untracked changes before writing. Preserve user work and restrict commits to task-owned paths. Use Git for code projects and a scoped baseline for a new maintained component. Never blanket-add unrelated examples or generated bitstreams. Keep scratch output and secrets out of Git. Do not push, publish or program a board without authorization.

## Evidence contract
Verification statuses are **PASS**, **FAIL**, **BLOCKED**, **NOT_RUN**:
- PASS: the stated check actually ran and met its criteria, within its declared scope.
- FAIL: an executed check found a design/property/build failure.
- BLOCKED: a required tool, library, permission or input prevented the check.
- NOT_RUN: outside scope or not executed; never infer success.
These are report statuses, not calls to a goal-management tool.
Include scope, tool/version, procedure/command, source identity (commit plus dirty state or hashes), exit status, relevant log/report, checks performed, assumptions/model substitutions and exclusions. Scale details to the task. Human explanations need no machine block; structured reports use these fields consistently. Formatting errors are not design failures: inspect actual evidence.
A normal exit or $finish establishes execution completion only. Functional PASS requires expected outcomes and confirmation that intended checks ran. Tool absence, blackboxes, empty stubs, estimated timing and stale reports cannot establish implementation correctness. A Python cycle model is not an RTL simulation; a behavioral IP model is not the physical IP.

## Communication and documentation
Use the user's language. Prefer available project context and local official documentation; browse primary sources when needed by the task or current tool policy. Separate official requirements, commentary, calculations, simulations and board measurements. Do not write repository instructions, global learner profiles or project metadata merely because code was read. Do not turn one example's parameters into universal defaults.

## Reference loading
Read the selected skill entrypoint, then only the reference/section whose trigger matches the current operation. A list of links is a menu, not a load-all checklist. Reuse this contract within the task. Load host-call leaves only for software execution; read output schemas only for requested artifacts and diagnostic catalogues only for actual findings. Small focused RTL/FSM work uses the direct path; bounded edits to existing RTL or constraints use [local edits](local-edits.md) before domain-specific workflow routing. Judge scope by changed behavior and dependencies, not file count or the words "create", "Gowin", "constraints" or "test". Mapping, formal, parallel build and saved plans are separate conditional branches. Invalidated downstream reports limit claims; they do not automatically request their regeneration.
