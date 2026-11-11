// Copyright (c) 2026 ICECREAM1230
// Shared value types for the time-multiplexed element render path.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
package element_types_pkg;
    import synth_pkg::*;
    typedef struct packed {
        logic [CTRL_W-1:0] pitch;
        logic [1:0] wave;
        logic signed [OSC_W-1:0] duty;
        logic [CTRL_W-1:0] cutoff, resonance;
        logic [7:0] volume_l, volume_r;
        logic dual;
        logic [1:0] filter_type;
    } element_config_t;
    typedef struct packed {
        logic [8:0] pitch, duty, cutoff, resonance, gain_l, gain_r;
    } element_pointers_t;
    typedef struct packed {
        logic signed [OSC_W-1:0] phase;
        logic signed [SVF_W-1:0] ic1a, ic2a, ic1b, ic2b;
    } element_state_t;
    typedef struct packed {
        logic signed [17:0] pitch, duty, cutoff, resonance, gain_l, gain_r;
    } element_modulation_t;
    typedef struct packed {
        logic [CTRL_W-1:0] pitch;
        logic [1:0] wave;
        logic signed [OSC_W-1:0] duty;
        logic [CTRL_W-1:0] cutoff, resonance;
        logic [7:0] atten_l, atten_r;
        // Fraction toward the next louder attenuation node (1/64 code).
        // Internal render context only; element/preset SPI words stay intact.
        logic [5:0] gain_fraction_l, gain_fraction_r;
        logic dual;
        logic [1:0] filter_type;
    } resolved_config_t;
endpackage
