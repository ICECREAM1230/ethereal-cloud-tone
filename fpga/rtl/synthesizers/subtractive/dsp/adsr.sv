// Copyright (c) 2026 ICECREAM1230
//------------------------------------------------------------------------
// adsr.sv -- the RC envelope, as its own module (#146)
//
// Copyright (C) 2026  Thor Johannes Hoeyer
// SPDX-License-Identifier: MIT
//
// Thor (#146): the envelope should be separated out. It is the one
// instruction with a real state machine, so it earns a module; the LFO is an
// adder and the SEND is a wire.
//
// THE RECURRENCE (#127, Thor: "RC for everything, no more LUTs for ADSR").
// The level is a LINEAR AMPLITUDE in UQ12.14 across [25:0]; full scale
// 0x400000 is the gain bus's Q4.14 unity (0x4000) carrying eight extra
// fractional bits so a slow step does not truncate away. Every segment is
// the same recurrence,
//
//     y += (target - y) * k
//
// and the stage chooses nothing but the target and the rate. Sustain is the
// only straight line in the envelope, and it is straight because it is a
// FIXED POINT of that recurrence, not a special case.
//
// Attack charges toward 1.3x full scale and a comparator ends it at full
// scale -- the CEM3310 / SSM2056 trick. What you hear is the first 77% of an
// RC charge, which is convex, instead of the tail, which flattens into a soft
// attack nobody wants.
//
// RATES ARRIVE AS COEFFICIENTS (#145). Firmware sends an 18-bit k per segment
// and this module multiplies by it and shifts by a FIXED K_SHIFT. It used to
// decode a mantissa and a variable barrel shift here, and that shifter -- a
// LUT mux tree feeding a fabric carry chain -- was the design's critical path
// (#147: it is where the ~8 MHz of headroom went, and placement moved it
// enough that 2 of 4 seeds glitched audibly). Rate decoding is control-rate
// work; it belongs on the ESP32, which is where it now lives.
//
// ARORA V EXPLICIT DSP MAC. The registered 27-bit signed difference feeds
// adsr_mac54_gw5a, which uses three official generated MULTALU27X18 IP instances. Splitting
// delta and the shifted level at bit 17 preserves the unsigned 18-bit rate,
// all 54 result bits, and the existing two-cycle step_en -> state_we latency.
// The delta subtractor remains fabric. This replaces the historical GW2AR
// ALU54D/MULTALU36X18 description; those primitives do not exist on GW5AST.
//
// Wire format, MIRRORED IN app/main/patch.h -- change both or neither:
//
//   rates   [17:0] kA          [31:18] kD[13:0]
//   rates2  [3:0]  kD[17:14]   [21:4]  kR       [31:22] sustain
//
// Sustain arrives as a plain 10-bit level too. Two decodes went with it: the
// linear one, and a log one selected by CFG[26] that nothing has ever set.
//
// PIPELINING. Registered subtract, then registered multiply-accumulate;
// state_out lands TWO cycles after the inputs are presented and the
// caller must delay its state-write address to match. That is the SAME depth
// as the fabric version this replaces -- measured, 2 clock edges -- so moving
// into the DSP blocks costs no re-alignment. An instruction is visited once
// per pass, 256 entries apart, so a delayed write can never race its own read.
//------------------------------------------------------------------------
`default_nettype none
module adsr #(
    parameter int K_SHIFT   = 24,     // firmware scales k by 2**K_SHIFT
    parameter int SUS_SHIFT = 12      // = ADSR_SUS_SHIFT in patch.h
) (
    input  wire        clk,
    input  wire        rst_n,

    // presented together, registered by the caller
    input  wire        step_en,      // advance this envelope now
    input  wire [27:0] state_in,     // {stage[1:0], level[25:0]}
    input  wire        gate,         // watched bus level > 0 = held
    input  wire [31:0] rates,        // kA and the low 14 bits of kD
    input  wire [31:0] rates2,       // the rest of kD, then kR and sustain

    // the envelope's contribution, combinational from state_in
    output wire signed [17:0] level_out,

    // next state, valid two cycles after step_en
    output wire [27:0] state_out,
    output wire        state_we
);
    localparam [1:0] AST_IDLE = 2'd0, AST_ATT = 2'd1,
                     AST_DEC  = 2'd2, AST_REL = 2'd3;
    localparam [25:0] ENV_FULL = 26'h400000;
    localparam [25:0] ENV_OVER = 26'h533333;   // 1.3 x full scale

    // Gowin's behavioural models all read `GSR.GSRO` as an UPWARD hierarchical
    // reference, so simulation needs an instance named literally GSR somewhere
    // up the hierarchy -- putting it here means no testbench has to know about
    // it. Simulation-only on purpose: in silicon the global set/reset network
    // is device infrastructure that the primitive is wired to implicitly, and
    // instantiating a second GSR in the synthesised design would fight the one
    // the tools already provide. The models are vendor code, so the Makefile
    // references them where the toolchain installs them rather than vendoring
    // a copy into a MIT tree.
`ifdef SIM_GOWIN_PRIM
    GSR GSR (.GSRI(1'b1));
`endif

    wire [1:0]  stage_prev = state_in[27:26];
    wire [25:0] level_prev = state_in[25:0];

    // The contribution. The caller computes (operand * DEPTH) >>> 16, so
    // "unity" for an operand is 0x10000 -- a depth of +8 octaves must then
    // move the bus by exactly +8 octaves. ENV_FULL is 2^22, so the level
    // shifts right by 6 to land on 2^16 at full scale.
    //
    // #127 took level[24:8] here, which lands on 0x4000 -- the gain bus's
    // Q4.14 unity. That went with #127's phase-1 linear-gain work, which is
    // not on this branch, and against the current bus convention it makes
    // every envelope a quarter of its intended depth (measured: the bench's
    // +8 octave envelope moved the bus by 2, not 8).
    assign level_out = $signed({1'b0, level_prev[22:6]});

    // ---- which segment this step belongs to -----------------------------
    wire [1:0] stage_sel = !gate                  ? AST_REL
                         : (stage_prev == AST_ATT) ? AST_ATT
                         : (stage_prev == AST_DEC) ? AST_DEC
                         :                           AST_ATT;   // from idle

    // RETRIGGER. The gate is a LEVEL and the stage is this envelope's only
    // memory, so a voice stolen mid-release arrives here as AST_REL with a
    // non-zero level and the attack above continues FROM that level instead of
    // from silence -- audibly, an attack that starts part-way up and is much
    // shorter, because the attack charges toward 1.3x full scale. Thor heard it
    // playing chords in succession, which is exactly the case that hits it:
    // each chord's note-offs leave releasing voices for the next chord to
    // steal. A released envelope whose gate goes high again is a NEW NOTE, so
    // it starts from zero.
    //
    // AST_IDLE does not need this -- the release latches {AST_IDLE, 26'd0} on
    // arrival, so an idle envelope's level is already 0.
    //
    // Not fixed here, and deliberately: stealing a voice that is still HELD.
    // Its gate never drops, so nothing in the gateware can tell that apart from
    // the same note continuing; that one needs firmware to drop the gate for a
    // pass first.
    wire retrig = gate && (stage_prev == AST_REL);
    wire [25:0] level_now = retrig ? 26'd0 : level_prev;

    // kD straddles the two words: 14 bits in one, 4 in the other. Firmware
    // packs it that way because three 18-bit coefficients plus a sustain
    // level do not fit into 64 bits any other way.
    wire [17:0] k_dec = {rates2[3:0], rates[31:18]};
    wire [17:0] k_seg = !gate                   ? rates2[21:4]   // release
                      : (stage_sel == AST_ATT)  ? rates[17:0]    // attack
                      :                           k_dec;         // decay

    // Sustain is a plain level now: the top 10 bits of the 22-bit envelope
    // scale. SUS_SHIFT must equal ADSR_SUS_SHIFT in app/main/patch.h -- I had
    // 13 here against firmware's 12 and every sustain came out twice its
    // level, which for a high sustain sits above full scale and makes the
    // decay segment climb instead of settle.
    wire [25:0] sus = 26'(rates2[31:22]) << SUS_SHIFT;

    wire [25:0] target = !gate                  ? 26'd0
                       : (stage_sel == AST_ATT) ? ENV_OVER
                       :                          sus;

    // ---- stage 1: registered signed difference --------------------------
    // GW5AST has no ALU54D primitive. Keep the upstream stage boundary and
    // exact 27-bit signed delta using a fabric subtractor.
    // Preserve this existing fabric boundary rather than absorbing its FFs
    // into the distant multiplier B input after the gate/target subtractor.
    (* syn_preserve = 1 *) logic signed [26:0] delta_q;
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) delta_q <= '0;
        else delta_q <= $signed({1'b0, target}) - $signed({1'b0, level_now});

    // Everything the later stages need, carried alongside the DSP pipeline.
    logic [17:0] k_q;
    logic [1:0]  stage_q;
    logic [25:0] level_q;
    logic        gate_q, v1;
    logic [1:0]  stg_in_q;        // the stage we came FROM
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            k_q <= '0; stage_q <= AST_IDLE; level_q <= '0;
            gate_q <= 1'b0; v1 <= 1'b0; stg_in_q <= AST_IDLE;
        end else begin
            v1       <= step_en;
            k_q      <= k_seg;
            stage_q  <= stage_sel;
            stg_in_q <= stage_prev;
            level_q  <= level_now;
            gate_q   <= gate;
        end
    end

    // ---- stage 2: registered multiply and accumulate --------------------
    // The wrapper's high DSP OREG and low remainder register capture on
    // this same edge; no input or intermediate DSP register adds latency.
    wire signed [53:0] mac_out;
    adsr_mac54_gw5a #(.K_SHIFT(K_SHIFT)) u_mac (
        .clk(clk), .rst_n(rst_n), .delta(delta_q),
        .coefficient(k_q), .level(level_q), .result(mac_out)
    );

    wire [25:0] y_dsp = mac_out[K_SHIFT+25 : K_SHIFT];

    // The rest of the stage-2 context, aligned to the MAC's registered output.
    logic [1:0] stg_q;
    logic [25:0] lvl_q;
    logic        gt_q, v2;
    logic [1:0]  stg_in_d;
    logic        prod_nz_q, prod_neg_q;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            stg_q <= AST_IDLE; lvl_q <= '0; gt_q <= 1'b0; v2 <= 1'b0;
            stg_in_d <= AST_IDLE; prod_nz_q <= 1'b0; prod_neg_q <= 1'b0;
        end else begin
            v2       <= v1;
            stg_q    <= stage_q;
            lvl_q    <= level_q;
            gt_q     <= gate_q;
            stg_in_d <= stg_in_q;
            prod_nz_q  <= (delta_q != 27'd0) && (k_q != 18'd0);
            prod_neg_q <= delta_q[26];
        end
    end

    // ---- stage 3: the creep, and the segment decision --------------------
    // Fixed-point RC STALLS: once the step truncates to zero the level freezes
    // short of its target, and on release that is a DC tail and a voice that
    // never frees -- heard as a stuck note, not as an envelope bug. One LSB of
    // creep bounds the arrival, and 1 LSB of 26 is far below anything audible.
    //
    // The DSP has already produced level + (delta*k >>> K_SHIFT), so a stall
    // shows up as y_dsp being unchanged from the level that went in. That
    // makes the creep a MUX on an incremented level rather than an extra add
    // in the arithmetic path.
    wire stalled = (y_dsp == lvl_q) && prod_nz_q;
    wire [25:0] crept = prod_neg_q ? (lvl_q - 26'd1) : (lvl_q + 26'd1);
    // A release that has already reached zero must not creep below it, and an
    // attack at the top must not creep past the comparator's reach.
    wire [25:0] y = !stalled                        ? y_dsp
                  : (prod_neg_q && lvl_q == 26'd0)  ? 26'd0
                  :                                   crept;

    logic [27:0] next;
    always_comb begin
        if (!gt_q)
            // release: target is zero, and IDLE latches on arrival
            next = (y == 26'd0) ? {AST_IDLE, 26'd0} : {AST_REL, y};
        else if (stg_q == AST_ATT)
            // attack: the comparator, not the target, ends the segment
            next = (y >= ENV_FULL) ? {AST_DEC, ENV_FULL} : {AST_ATT, y};
        else
            // decay ARRIVES at sustain. An RC segment cannot overshoot its
            // target, so the three-way compare a linear ramp needed is gone.
            next = {AST_DEC, y};
    end

    assign state_out = next;
    assign state_we  = v2;

endmodule
`default_nettype wire
