# Local RTL and constraint edits

Use for a bounded change to an existing design when the affected behavior, ports and clock/reset/electrical facts can be resolved locally. Multiple edited files, a Gowin project, or a request to "change and test" do not by themselves make the work architectural.

## Direct path

1. Inspect Git state and preserve overlapping user work. Read only the selected manifest entries, affected RTL/constraint statements, nearby conventions and their immediate consumers. Reuse confirmed project/tool context.
2. Establish the changed contract from that evidence. If a required latency, reset, clock-domain, pin or voltage choice remains unresolved, ask one focused question and pause; ordinary implementation choices need no separate approval.
3. Make the scoped edit directly. A saved plan, architecture map, IP inventory, worker, new test framework or full-flow checklist is not a prerequisite. Use the corresponding topic below; do not load the other topics.
4. Run the smallest checks that establish the requested result and examine the diff. Stop when those checks pass and the requested outcome is satisfied; broaden only for an actual failure, uncovered affected behavior, project requirement or explicit user request.
5. Report the change, performed checks and remaining scope in a short answer. Source inspection, RTL behavior, official implementation and board measurement are distinct evidence levels. Building does not authorize programming.

## Output register or local pipeline adjustment

Trace the affected output and its immediate consumer. Match clock/reset polarity and reset value; account for the added cycle, align valid/enable/sideband signals, and preserve hold/backpressure behavior. A requested one-cycle register is not a reason to redesign the surrounding interface.

Prefer the existing focused test/lint command. Extend the existing relevant test for reset, latency, data/control alignment and hold behavior. If none exists, use a minimal focused check when needed to substantiate behavior; do not build a general verification environment. Load `gf-lint` or `gf-sim` only for a check actually selected; consult host details only when execution needs them.

## Existing physical or timing constraint

Confirm the active constraint file and the specific object. For top-level ports use current RTL; for placed internal cells/nets use the appropriate synthesis evidence. Check syntax, duplicate/conflicting assignments and the affected package pins, banks, voltage/electrical settings or timing selectors. Read only the corresponding CST/SDC statement section from the installed vendor skill when needed. Reuse supplied verified board data; do not scan a board/IP catalog to rediscover it.

A constraint-only edit does not need an RTL testbench. Invalidate the affected prior PnR/STA/bitstream claims, but stale downstream evidence is not an instruction to rebuild. For an edit-only request report the source/static checks and implementation as NOT_RUN. If the user requires implemented pin placement or timing closure, run the necessary official stages and their fresh prerequisites; a source check cannot establish that result.

## A few status LEDs

Trace the named status signals/states, polarity and existing clock/reset. Reuse the supplied verified pin/electrical data and existing LED wiring style. Same-clock status decode plus top-level ports and CST edits remains a local change even across several files. Check state-to-LED mapping, polarity, port/constraint agreement and duplicate pins; use an existing targeted check for changed logic.

Pulse visibility, added blink counters, new crossings or changes to functional control are separate requirements. Add them only when requested or necessary to meet the stated LED behavior. An ambiguous or unsafe crossing needs a focused decision/CDC analysis, not an automatic whole-project redesign.

## Expand only where the evidence requires it

- A new or changed generated memory/FIFO/PLL/DSP/PHY interface: load the vendor IP guidance for that resource.
- A changed clock/reset topology, system-wide protocol/latency contract, broad timing exception or inseparable multi-module redesign: load the relevant planning/CDC/timing topic; use `gf-plan` if an architecture decision is actually needed.
- Requested official synthesis/PnR/STA/bitstream evidence: load the vendor stage and execution guides; select stages by dependencies, not by the number of files edited.
- A real failing check: inspect its diagnostic and expand to the affected dependencies; a small patch does not become a full workflow merely because a check exists.

Follow explicit user/repository checks and higher-priority requirements. Small scope reduces unrelated work; it does not waive needed behavioral or electrical validation.
