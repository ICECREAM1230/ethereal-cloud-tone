# Upstream pipeline schema excerpt

Source: `plugins/meta/skills/pipeline-orchestration/SKILL.md` at 441cad1ffb4a6c980b82a55374a4d7222a237192.
For original-pipeline compatibility only; apply the Gowin profile first.

### Constraints Schema (authoritative)

`design_state.constraints` is the single source of truth for design-intent parameters across all
domain orchestrators. Defined once here; domain SKILL.md files reference these keys and document
their default fallbacks.

```json
"constraints": {
  "clock": {
    "clk_mhz": null,
    "clk_uncertainty_ps": null
  },
  "pvt_corners": [
    { "name": "ss_setup", "process": "SS", "voltage_v": null, "temp_c": null, "checks": ["setup"] },
    { "name": "ff_hold",  "process": "FF", "voltage_v": null, "temp_c": null, "checks": ["hold"] }
  ],
  "timing": {
    "wns_ns_target": 0,
    "tns_ns_target": 0,
    "fanout_max": 32,
    "skew_ps_max": 100,
    "transition_ps_max": 200,
    "insertion_delay_ps_max": 500
  },
  "area": {
    "area_um2": null,
    "utilization_pct_target": 75,
    "utilization_pct_max": 85
  },
  "power": {
    "power_mw": null,
    "leakage_pct_max": 15,
    "ir_drop_pct_max": 5,
    "gating_coverage_pct_min": 60,
    "activity_factors": { "default": 0.15, "high": 0.40 }
  },
  "coverage": {
    "functional_pct": 100,
    "line_pct": 95,
    "branch_pct": 90,
    "toggle_pct": 85,
    "fsm_state_pct": 100,
    "fsm_transition_pct": 95,
    "assertion_pct": 100
  },
  "dft": {
    "saf_coverage_pct": 99,
    "transition_coverage_pct": 95,
    "cell_aware_coverage_pct": 95,
    "bridging_coverage_pct": 90,
    "mbist_coverage_pct": 99,
    "chain_balance_pct": 5
  },
  "hls": {
    "target_ii": null,
    "target_latency_cycles": null,
    "cosim_tolerance_pct": 5
  },
  "memory_ip": {
    "vmin_margin_mv": 50,
    "repair_yield_pct_min": 99,
    "ecc_required": false,
    "fit_target_fit_per_mb": 100,
    "max_aspect_ratio": 4.0,
    "retention_required": true
  },
  "fpga": {
    "lut_util_pct_max": 70,
    "bram_util_pct_max": 80,
    "dsp_util_pct_max": 80
  }
}
```

Non-null values are **documented defaults** matching current hardcoded SKILL.md literals.
`null` values must be supplied by the user for constraint-bearing domains.

