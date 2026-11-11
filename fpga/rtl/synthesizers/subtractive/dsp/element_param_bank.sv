// Copyright (c) 2026 ICECREAM1230
// Extracted from element_pipeline.sv without changing registered behavior.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
`default_nettype none
module element_param_bank #(
    parameter bit AE_NATIVE_WRITE=1'b0,
    parameter int NUM_ELEMENTS = synth_pkg::NUM_ELEMENTS,
    parameter P0_HEX = "rtl/synthesizers/subtractive/data/boot_p0.hex",
    parameter P1_HEX = "rtl/synthesizers/subtractive/data/boot_p1.hex",
    parameter P2_HEX = "rtl/synthesizers/subtractive/data/boot_p2.hex",
    parameter P3_HEX = "rtl/synthesizers/subtractive/data/boot_p3.hex"
) (
    input wire clk, rst_n, sclk,
    input wire [9:0] slot,
    input wire [$clog2(NUM_ELEMENTS)-1:0] read_index,
    input wire elem_write_enable,
    input wire [2:0] elem_write_word,
    input wire [7:0] elem_write_index,
    input wire [31:0] elem_write_data,
    input wire swap_req,
    output logic bank_active,
    output wire bank_shadow,
    output element_types_pkg::element_config_t config_out,
    output element_types_pkg::element_pointers_t pointers_out
);
    localparam int BANKS=AE_NATIVE_WRITE?1:2;
    reg [35:0] osc_param_ram [0:BANKS*NUM_ELEMENTS-1];
    reg [35:0] duty_param_ram [0:BANKS*NUM_ELEMENTS-1];
    reg [35:0] filter_param_ram [0:BANKS*NUM_ELEMENTS-1];
    reg [35:0] gain_param_ram [0:BANKS*NUM_ELEMENTS-1];

    // Pointer words: per-element bus pointers, three 10-bit fields
    // each. +5 (PTRS0): [9:0] pitch, [19:10] duty, [29:20] cutoff.
    // +6 (PTRS1): [9:0] Q, [19:10] gain L, [29:20] gain R. Pointers
    // are wiring, so they ride the ping-pong banks like every
    // parameter. Init 0: every parameter points at bus 0 (hardwired
    // zero), so boot behavior is exactly the pre-bus behavior.
    reg [29:0] ptrs0_param_ram [0:BANKS*NUM_ELEMENTS-1];
    reg [29:0] ptrs1_param_ram [0:BANKS*NUM_ELEMENTS-1];
    integer pi;
    initial for (pi = 0; pi < BANKS*NUM_ELEMENTS; pi = pi + 1) begin
        ptrs0_param_ram[pi] = 30'd0;
        ptrs1_param_ram[pi] = 30'd0;
    end

    // GATE word (map offset +4): [0] gate, [1] retrig (reserved).
    // Gate 0 silences the element (gain decode forced to exact mute);
    // the oscillator and filters free-run regardless. NOTE (Thor,
    // #98): this never became and will never become an ADSR trigger —
    // envelopes are sequencer SOURCES, gated by a control input (a gate
    // bus shared across a voice's elements), not element traits.
    // Today GATE's only job
    // is the exact-mute path (#68); Thor has proposed dropping GATE
    // and RETRIG as element parameters entirely (#98 discussion).
    // Both banks boot gated ON so the boot image keeps sounding (the
    // power-up liveness check).
    reg [1:0] gate_param_ram [0:BANKS*NUM_ELEMENTS-1];
    integer gi;
    initial for (gi = 0; gi < BANKS*NUM_ELEMENTS; gi = gi + 1)
        gate_param_ram[gi] = AE_NATIVE_WRITE?2'b00:2'b01;

    initial begin
        $readmemh(P0_HEX, osc_param_ram, 0, NUM_ELEMENTS-1);
        $readmemh(P1_HEX, duty_param_ram, 0, NUM_ELEMENTS-1);
        $readmemh(P2_HEX, filter_param_ram, 0, NUM_ELEMENTS-1);
        $readmemh(P3_HEX, gain_param_ram, 0, NUM_ELEMENTS-1);
        if(!AE_NATIVE_WRITE)begin
        $readmemh(P0_HEX, osc_param_ram, NUM_ELEMENTS, 2*NUM_ELEMENTS-1);
        $readmemh(P1_HEX, duty_param_ram, NUM_ELEMENTS, 2*NUM_ELEMENTS-1);
        $readmemh(P2_HEX, filter_param_ram, NUM_ELEMENTS, 2*NUM_ELEMENTS-1);
        $readmemh(P3_HEX, gain_param_ram, NUM_ELEMENTS, 2*NUM_ELEMENTS-1);
        end
    end

    //----------------------------------------------------------------
    // Bank control: active half (sysclk) flips at drum slot 512 when a
    // swap is pending; the sclk write side steers by a synced
    // complement of the active bank (memory map wording, literally).
    //----------------------------------------------------------------
    generate if(AE_NATIVE_WRITE)begin:g_native_bank
        assign bank_active=1'b0;assign bank_shadow=1'b0;
    end else begin:g_spi_banks
    logic swap_toggle_meta, swap_toggle_sync, swap_toggle_prev;          // swap_req toggle sync (sysclk)
    logic swap_pending;

    initial begin
        bank_active  = 1'b0;
        {swap_toggle_meta, swap_toggle_sync, swap_toggle_prev} = '0;
        swap_pending = 1'b0;
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            bank_active  <= 1'b0;
            swap_toggle_meta <= 1'b0; swap_toggle_sync <= 1'b0; swap_toggle_prev <= 1'b0;
            swap_pending <= 1'b0;
        end else begin
            swap_toggle_meta <= swap_req;
            swap_toggle_sync <= swap_toggle_meta;
            swap_toggle_prev <= swap_toggle_sync;
            if (swap_toggle_sync != swap_toggle_prev)
                swap_pending <= 1'b1;
            else if (swap_pending && slot == synth_pkg::SWAP_SLOT[9:0]) begin
                bank_active  <= ~bank_active;
                swap_pending <= 1'b0;
            end
        end
    end

    // write side: shadow = complement of active, synced into sclk
    logic bank_active_meta, bank_active_sync;
    initial {bank_active_meta, bank_active_sync} = '0;
    always_ff @(posedge sclk) begin
        bank_active_meta <= bank_active;
        bank_active_sync <= bank_active_meta;
    end
    assign bank_shadow = ~bank_active_sync;

    end endgenerate
    wire [$clog2(BANKS*NUM_ELEMENTS)-1:0] read_address=AE_NATIVE_WRITE ? $clog2(BANKS*NUM_ELEMENTS)'(read_index):$clog2(BANKS*NUM_ELEMENTS)'({bank_active,read_index});
    wire [$clog2(BANKS*NUM_ELEMENTS)-1:0] write_address=AE_NATIVE_WRITE ? $clog2(BANKS*NUM_ELEMENTS)'(elem_write_index):$clog2(BANKS*NUM_ELEMENTS)'({bank_shadow,elem_write_index});
    logic [35:0] s1_osc_word, s1_duty_word, s1_filter_word, s1_gain_word;
    logic [1:0]  s1_gate_word;
    logic [29:0] s1_ptrs0_word, s1_ptrs1_word;
    always_ff @(posedge clk) begin
        s1_osc_word     <= osc_param_ram[read_address];
        s1_duty_word     <= duty_param_ram[read_address];
        s1_filter_word     <= filter_param_ram[read_address];
        s1_gain_word     <= gain_param_ram[read_address];
        s1_gate_word     <= gate_param_ram[read_address];
        s1_ptrs0_word     <= ptrs0_param_ram[read_address];
        s1_ptrs1_word     <= ptrs1_param_ram[read_address];
    end

    // SPI-side write ports (sclk domain) — sync-only, one per bank
    generate if(AE_NATIVE_WRITE)begin:g_native_write
    always_ff @(posedge clk)
        if (elem_write_enable && elem_write_word == 3'd0) osc_param_ram[write_address] <= {4'b0, elem_write_data};
    always_ff @(posedge clk)
        if (elem_write_enable && elem_write_word == 3'd1) duty_param_ram[write_address] <= {4'b0, elem_write_data};
    always_ff @(posedge clk)
        if (elem_write_enable && elem_write_word == 3'd2) filter_param_ram[write_address] <= {4'b0, elem_write_data};
    always_ff @(posedge clk)
        if (elem_write_enable && elem_write_word == 3'd3) gain_param_ram[write_address] <= {4'b0, elem_write_data};
    always_ff @(posedge clk)
        if (elem_write_enable && elem_write_word == 3'd4) gate_param_ram[write_address] <= elem_write_data[1:0];
    always_ff @(posedge clk)
        if (elem_write_enable && elem_write_word == 3'd5) ptrs0_param_ram[write_address] <= elem_write_data[29:0];
    always_ff @(posedge clk)
        if (elem_write_enable && elem_write_word == 3'd6) ptrs1_param_ram[write_address] <= elem_write_data[29:0];

    end else begin:g_spi_write
    always_ff @(posedge sclk)
        if (elem_write_enable && elem_write_word == 3'd0) osc_param_ram[write_address] <= {4'b0, elem_write_data};
    always_ff @(posedge sclk)
        if (elem_write_enable && elem_write_word == 3'd1) duty_param_ram[write_address] <= {4'b0, elem_write_data};
    always_ff @(posedge sclk)
        if (elem_write_enable && elem_write_word == 3'd2) filter_param_ram[write_address] <= {4'b0, elem_write_data};
    always_ff @(posedge sclk)
        if (elem_write_enable && elem_write_word == 3'd3) gain_param_ram[write_address] <= {4'b0, elem_write_data};
    always_ff @(posedge sclk)
        if (elem_write_enable && elem_write_word == 3'd4) gate_param_ram[write_address] <= elem_write_data[1:0];
    always_ff @(posedge sclk)
        if (elem_write_enable && elem_write_word == 3'd5) ptrs0_param_ram[write_address] <= elem_write_data[29:0];
    always_ff @(posedge sclk)
        if (elem_write_enable && elem_write_word == 3'd6) ptrs1_param_ram[write_address] <= elem_write_data[29:0];

    end endgenerate

    // Combinational field views preserve the registered-word boundary.
    assign config_out.pitch = s1_osc_word[13:0];
    assign config_out.wave = s1_osc_word[15:14];
    assign config_out.duty = s1_duty_word[23:0];
    assign config_out.cutoff = s1_filter_word[13:0];
    assign config_out.resonance = s1_filter_word[27:14];
    assign config_out.volume_l = s1_gate_word[0] ? s1_gain_word[7:0] : 8'h00;
    assign config_out.volume_r = s1_gate_word[0] ? s1_gain_word[15:8] : 8'h00;
    assign config_out.dual = s1_gain_word[16];
    assign config_out.filter_type = s1_gain_word[18:17];
    // Bit 9 of each stored 10-bit pointer field is ignored as before.
    assign pointers_out.pitch = s1_ptrs0_word[8:0];
    assign pointers_out.duty = s1_ptrs0_word[18:10];
    assign pointers_out.cutoff = s1_ptrs0_word[28:20];
    assign pointers_out.resonance = s1_ptrs1_word[8:0];
    assign pointers_out.gain_l = s1_ptrs1_word[18:10];
    assign pointers_out.gain_r = s1_ptrs1_word[28:20];
endmodule
`default_nettype wire
