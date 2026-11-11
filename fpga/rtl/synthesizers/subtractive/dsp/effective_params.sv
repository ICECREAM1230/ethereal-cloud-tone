// Copyright (c) 2026 ICECREAM1230
// Saturating base + control-bus decode; purely combinational S2 -> S3.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
`default_nettype none
module effective_params (
    input element_types_pkg::element_config_t config_in,
    input element_types_pkg::element_modulation_t modulation_in,
    output element_types_pkg::resolved_config_t config_out
);
    wire signed [18:0] reso_sum =
        $signed({5'b0, config_in.resonance}) + {modulation_in.resonance[17], modulation_in.resonance};
    wire [13:0] eff_reso =
        reso_sum[18]            ? 14'd0    :
        (reso_sum > 19'sd16383) ? 14'h3FFF : reso_sum[13:0];

    // Cutoff clamps to the flat FC_MAX (14.4 kHz, measured clean at
    // the Butterworth worst case — see synth_pkg).
    wire signed [18:0] fc_sum =
        $signed({5'b0, config_in.cutoff}) + {modulation_in.cutoff[17], modulation_in.cutoff};
    wire [13:0] eff_fc =
        fc_sum[18] ? 14'd0 :
        (fc_sum > 19'($signed({5'b0, synth_pkg::FC_MAX})))
            ? synth_pkg::FC_MAX :
        fc_sum[13:0];

    wire signed [18:0] pitch_sum =
        $signed({5'b0, config_in.pitch}) + {modulation_in.pitch[17], modulation_in.pitch};
    wire [13:0] eff_pitch =
        pitch_sum[18]            ? 14'd0    :
        (pitch_sum > 19'sd16383) ? 14'h3FFF : pitch_sum[13:0];

    wire signed [31:0] duty_sum =
        {{8{config_in.duty[23]}}, config_in.duty}
        + {{1{modulation_in.duty[17]}}, modulation_in.duty, 13'b0};
    wire signed [23:0] eff_duty =
        (duty_sum >  32'sd8388607) ? 24'sd8388607  :
        (duty_sum < -32'sd8388608) ? -24'sd8388608 : duty_sum[23:0];

    // Keep the bus's six fractional code bits until the audio gain decode.
    // Arithmetic right shift preserves the original integer node for both
    // signs; -1 becomes the previous node plus 63/64, rather than a full step.
    wire signed [18:0] gl_sum_q6 =
        $signed({5'b0, config_in.volume_l, 6'b0})
        + $signed({modulation_in.gain_l[17], modulation_in.gain_l});
    wire signed [18:0] gr_sum_q6 =
        $signed({5'b0, config_in.volume_r, 6'b0})
        + $signed({modulation_in.gain_r[17], modulation_in.gain_r});
    wire signed [18:0] gl_sum =
        gl_sum_q6 >>> 6;
    wire signed [18:0] gr_sum =
        gr_sum_q6 >>> 6;
    // volume in, attenuation code out (the one subtract of issue #40)
    wire [7:0] eff_gl =
        (config_in.volume_l == 8'h00)      ? 8'hFF :             // base mute wins
        (gl_sum[18] || gl_sum == 19'sd0)
                              ? 8'hFE :             // quietest audible
        (gl_sum > 19'sd255)   ? 8'h00 :             // full volume
        8'hFF - gl_sum[7:0];
    wire [7:0] eff_gr =
        (config_in.volume_r == 8'h00)      ? 8'hFF :
        (gr_sum[18] || gr_sum == 19'sd0)
                              ? 8'hFE :
        (gr_sum > 19'sd255)   ? 8'h00 :
        8'hFF - gr_sum[7:0];

    assign config_out.pitch = eff_pitch;
    assign config_out.wave = config_in.wave;
    assign config_out.duty = eff_duty;
    assign config_out.cutoff = eff_fc;
    assign config_out.resonance = eff_reso;
    assign config_out.atten_l = eff_gl;
    assign config_out.atten_r = eff_gr;
    // A base mute still wins over every modulation. The quietest audible
    // and unity rails have no fractional interval beyond their old limits.
    assign config_out.gain_fraction_l =
        (config_in.volume_l != 8'h00 && gl_sum_q6 > 19'sd64
            && gl_sum_q6 < 19'sd16320) ? gl_sum_q6[5:0] : 6'd0;
    assign config_out.gain_fraction_r =
        (config_in.volume_r != 8'h00 && gr_sum_q6 > 19'sd64
            && gr_sum_q6 < 19'sd16320) ? gr_sum_q6[5:0] : 6'd0;
    assign config_out.dual = config_in.dual;
    assign config_out.filter_type = config_in.filter_type;
endmodule
