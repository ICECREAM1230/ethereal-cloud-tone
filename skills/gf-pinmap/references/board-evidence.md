# Board evidence and mapping

Read only when finding authoritative pin data or requiring confirmation of fallback evidence. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Workflow

1. **Identify relevant hardware** — Reuse the exact board/revision, device/package and affected pin/bank facts from verified user/project evidence. Ask only if a required fact remains unresolved.
2. **Look up missing facts** — Check an actually present curated database if useful; a known local mapping does not require database discovery:
   ```powershell
    Test-Path -LiteralPath '<GateFlow skill root>/boards/<board>/board.yaml'
   ```
3. **If needed data is present**: Read only the relevant board.yaml entries and cross-check applicability to the actual board/package.
4. **If needed data is missing**: use applicable authoritative board/device documentation; unresolved or unverified fallback pin data needs user confirmation before use.
5. **Cross-reference** the affected constraints with current RTL ports, pin conflicts and bank/electrical conditions.
6. **Output** only the requested assignments/file in the target tool's format; preserve unrelated mappings and settings.

## Curated Board Flow

1. Read `boards/<board>/board.yaml` for pin data
2. Read `boards/<board>/constraints.*` for template
3. Map RTL ports → board pins based on user's peripheral selection
4. Generate only the requested mappings using target-tool syntax and verified board/device data for package pins, I/O standards and bank voltage. Select DRIVE/SLEW or pulls only when the affected electrical requirements call for them; do not invent defaults from output type or active-low polarity. A local edit preserves unrelated assignments and valid electrical settings.

## Web Search Fallback

Only when board is NOT in curated database:

1. Search: `"<board name>" constraint file site:github.com`
2. Search: `"<board name>" pinout .xdc OR .pcf OR .lpf`
3. Present findings to user with WARNING:
   ```
   Found constraint data for <board> via web search.
   Source: <url>

   WARNING: This pin data has NOT been verified against the official
   board documentation. Incorrect pin assignments can damage hardware.

   Please review before applying:
   [show pin assignments]

   Apply these constraints? [Y/n]
   ```
4. NEVER auto-apply web-searched data
