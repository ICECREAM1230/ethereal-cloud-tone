# Gowin IP evidence

Read only when inventorying an official Gowin project. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Gowin Official-IP Handling

When a `.gprj` is present, treat the Gowin `.gprj` `FileList`, `.ipc`, generated wrapper/template, and `.vo/.vho` simulation model as the authoritative IP evidence. For Arora V, read the exact part and device version before making resource or primitive claims.

- Classify a known Gowin IP or primitive (`rPLL`, `PLLVR`, `SDPB`, `DPB`, `pROM`, `EMCU`, `DHCEN`, `IBUF`, `OBUF`, and similar) as a vendor dependency, not a missing GateFlow IP.
- Check that its project entry is enabled, its `.ipc`/generated outputs exist, and its wrapper ports match the instantiation.
- Report missing protected dependencies explicitly; do not decode, edit, or auto-replace `.vp/.vhdp/.vgp` or generated IP implementation.
- Give integration advice such as FIFO width/depth, clock-domain selection, latency, flag semantics, and BSRAM/resource trade-offs. The final parameter names and port list must come from the `.ipc`, generated template, and installed IP documentation.
- Do not auto-fill a GateFlow replacement for an existing Gowin IP. Suggest a portable substitute only when the user explicitly requests a redesign or simulation-only stub.

---
