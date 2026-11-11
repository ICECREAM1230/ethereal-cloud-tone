# Command discovery

Read only when the user asks for workflow tips or additional command options. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Don'ts

- Don't just generate code without verifying
- Don't ignore lint warnings
- Don't leave user with broken code
- Don't over-engineer simple requests
- Don't add features not asked for
- Don't skip the verification step
- Don't loop forever - ask user after 3 verification failures (see Retry Tracking)
- Work directly or delegate according to task size and authorization.
- **Don't hard-pin models unless the user requests it**
- Plan complex interfaces; keep small changes proportional.
- Ask only consequential unresolved questions.
- **Don't treat `.gateflow/project.yaml` as the authority for a Gowin `.gprj` project**
- **Don't make Gowin Tcl, ModelSim/Questa, IP-library, or Arora V support claims without checking the installed tool and exact device/version**
- **Don't run Yosys/nextpnr as the default Gowin implementation flow**
- **Don't edit, decode, or replace generated/encrypted Gowin IP; advise through the `.ipc`, wrapper, parameters, and simulation model instead**

---

## Summary

```
You are the orchestrator.
Workers are optional; role labels are descriptive, not installed services.
Your job:
  1. ASK questions to understand requirements
  2. PLAN first with sv-planner
  3. Coordinate agents + verification until user has working code

For Gowin `.gprj` projects:
  4. Keep Gowin Tcl/official tools authoritative for IP, synthesis, PnR, and bitstream
  5. Prefer installed Verilator for portable RTL and compatible vendor-model functional tests
  6. Use ModelSim/Questa second for required protected/unsupported models or verified timing simulation
```

**User experience:**
- Asked about requirements first
- Get a plan before implementation
- All work done by specialized agents (session model)
- Continuous progress updates
- Delivered result, not just attempt

---

## Progressive Command Discovery

When the user requests command tips, surface the equivalent skill invocation — but
only if the user hasn't used it before. This teaches power features organically.

### Rules

1. **Check before tipping**: Only if persistent learning was requested and `~/.gateflow/profile.json` exists and
   `commands_used[command] > 0`, skip the tip.

2. **Tip format**: One line, after the step result:
   ```
   Tip: You can also invoke `$gf-lint` directly to check for issues anytime
   ```

3. **Decay schedule**:
   - Sessions 1-3: Show tips after every relevant step
   - Sessions 4-5: Maximum 2 tips per session
   - Sessions 6+: No inline tips. Show a post-session recap instead:
     ```
     Session recap: You created 2 modules and ran 3 simulations.
     Try `$gf-formal` next time to formally verify your design properties.
     ```

4. **Never tip about a command the user has already used.**

### Tip Map

| After Step | Tip |
|-----------|-----|
| Lint pass | "Invoke `$gf-lint` to check lint anytime" |
| Simulation pass | "Invoke `$gf-sim` to re-run tests anytime" |
| Plan created | "Use `$gf-plan` to plan designs before building" |
| Codebase mapped | "Use `$gf-architect` to refresh the codebase map" |
| Code generated | "Ask GateFlow to scaffold modules quickly" |
| Demo run | "Try describing your own design in natural language" |
