// Copyright (c) 2026 ICECREAM1230
//------------------------------------------------------------------------
// csp.sv -- the Control Signal Processor (#136)
//
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
//
// Split out of element_pipeline.sv, which was carrying two unrelated
// jobs in 1,473 lines: the element DSP lane pipeline, and this. They
// shared a file because they shared the drum schedule -- the sequencer was
// armed at slot 299 purely so its writes could not collide with lane
// reads on a single-ported RAM. #134 removed that coupling by
// double-buffering the bus generation, so the two are now genuinely
// independent during execution. The slot counter now bounds BASE restoration
// after the lane drains, before its consumed generation is reused.
//
// What lives here: the six bus replicas plus dmem_init and dmem_local,
// the SPI mailbox, the ping-pong generation bit, the instruction and
// state RAMs, the sequencer state machine, the LFO and ADSR step logic,
// and the chain-summing arithmetic.
//
// The interface to the lane pipeline is six read ports: an address in
// (from the S1 pointers) and registered data out (landing at S2),
// which is exactly the timing the pipeline had when the RAMs were
// inline.
//------------------------------------------------------------------------
`default_nettype none
module csp #(
    parameter bit ENABLE_MIDI = 1'b0,
    parameter bit LOCAL_MIDI_BOOT = 1'b0,
    parameter bit ENABLE_EXPRESSION = 1'b0,
    parameter bit ENABLE_PREPARED_PATCH = 1'b0
) (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         sample_tick,
    input  wire [9:0]   slot,
    input  wire         sclk,
    input  wire         bank_active,
    input  wire         bank_shadow,

    // SPI bus-base mailbox (sclk domain, crossed by a toggle)
    input  wire [9:0]   dmem_wr_addr,
    input  wire [17:0]  dmem_wr_data,
    input  wire         dmem_wr_toggle,
    output logic        dmem_wr_ack,
    output logic [15:0] control_pass_count,

    // SPI instruction-memory writes
    input  wire         imem_write_enable,
    input  wire [9:0]   imem_write_addr,
    input  wire [31:0]  imem_write_data,

    // Runtime voice overlay is read on the same edges as the existing RAMs.
    // The controller still owns rates, routing and patch buses.
    input wire midi_enable,
    input wire [31:0] midi_voice_gate,
    input wire [575:0] midi_amp_depth, midi_mod_depth,
    output logic [31:0] midi_tail_done,

    // Same-domain, frame-boundary program selector. The caller delays its
    // audio selection until this pass's bus generation has been published.
    input wire local_program_active_i,
    input wire prepared_patch_valid_i,
    input wire [319:0] prepared_patch_i,
    output wire signed [17:0] local_gain_o,

    // Six sink read ports -- address in at S1, data out at S2
    input  wire [8:0]   rd_pitch_a,
    input  wire [8:0]   rd_duty_a,
    input  wire [8:0]   rd_fc_a,
    input  wire [8:0]   rd_q_a,
    input  wire [8:0]   rd_gl_a,
    input  wire [8:0]   rd_gr_a,
    output wire signed [17:0] rd_pitch_d,
    output wire signed [17:0] rd_duty_d,
    output wire signed [17:0] rd_fc_d,
    output wire signed [17:0] rd_q_d,
    output wire signed [17:0] rd_gl_d,
    output wire signed [17:0] rd_gr_d,

    output logic        test_tone_en,
    input wire signed [15:0] midi_wheel_depth,
    output logic signed [17:0] midi_wheel_wave_o
);
    wire local_program_active = LOCAL_MIDI_BOOT && local_program_active_i;
    wire prepared_program_active = ENABLE_PREPARED_PATCH && local_program_active && prepared_patch_valid_i;
    // These are bases, not additional per-voice copies of patch coefficients.
    function automatic logic signed [17:0] prepared_base(input logic [9:0] address);
        begin
            if(address>=16&&address<48)prepared_base=$signed(prepared_patch_i[245:228]);
            else if(address==3)prepared_base=$signed(prepared_patch_i[281:264]);
            else if(address==4)prepared_base=$signed(prepared_patch_i[263:246]);
            else prepared_base=0;
        end
    endfunction
    localparam signed [17:0] LOCAL_GAIN_FLOOR = -18'sd10240;
    //----------------------------------------------------------------
    // Bus RAM — the uniform Q8.10 pool (bus_architecture.md).
    // One replica in the B1 pilot (only cutoff reads it); replicas
    // are added per sink at B2. Written ONLY on sysclk: SPI writes
    // arrive through the toggle mailbox below and commit in an idle
    // slot (lane bus reads issue during slots 1..~257, so a commit at
    // slot >258 can never collide with a read — the BSRAM
    // read-during-write corruption class is impossible by schedule).
    // Bus 0 is hardwired zero (writes to it are ignored).
    //----------------------------------------------------------------
    // Six replicas of the one uniform pool — one read port per sink
    // (see bus_architecture.md "Why six replicas"). Broadcast writes
    // keep them identical.
    // Tang Mega: seven existing 18x1024 Gowin SDPX9B instances serve the
    // six lane reads and the CSP source read. Keep the upstream write order.
    // Reserved control bus 1023 is accepted by SPI for test_tone_en, but
    // must never alias ordinary pool bus511 through the 9-bit sink address.
    wire dmem_commit_pool = dmem_commit && dmem_mbox_addr < 10'd512;
    wire dmem_replica_we = dmem_commit_pool || dmem_we || restore_valid;
    wire [9:0] dmem_replica_waddr = dmem_commit
        ? {dmem_commit_half_sel, dmem_mbox_addr[8:0]}
        : dmem_we ? {~dmem_gen, dmem_waddr[8:0]}
                  : {restore_gen, restore_addr};
    wire [17:0] dmem_replica_wdata = dmem_commit ? dmem_mbox_data
        : dmem_we ? dmem_wdata : restore_data;
    wire dmem_local_we = dmem_commit || dmem_we || source_restore_valid;
    wire [9:0] dmem_local_waddr = dmem_commit ? dmem_mbox_addr
        : dmem_we ? dmem_waddr : {1'b0, source_restore_addr};
    wire [17:0] dmem_local_wdata = dmem_commit ? dmem_mbox_data
        : dmem_we ? dmem_wdata : gate_q;

    // SPI bus-base writes now land in a dedicated BASE RAM as well as
    // the value replicas: sinks read the replicas, the program counter
    // reads the base and writes base + contribution into the replicas
    // (the spec's "bus = base register + instruction contributions",
    // realized). A bus no instruction targets keeps value = base via the
    // mailbox's own replica write.
    reg signed [17:0] dmem_init [0:2*synth_pkg::DMEM_WORDS-1];
    // SECOND COPY, for the same reason imem was split: the sequencer needs
    // TWO different addresses out of the init image in one cycle -- the
    // watched gate bus (from CFG) and the target's base (from the
    // destination field). One port served both by phase muxing, which cost
    // a cycle. Written by the identical mailbox strobe, so the two are
    // always the same memory; only the read address differs.
    reg signed [17:0] dmem_gate [0:2*synth_pkg::DMEM_WORDS-1];
    integer bbi;
    initial for (bbi = 0; bbi < 2*synth_pkg::DMEM_WORDS; bbi = bbi + 1) begin
        dmem_init[bbi] = 18'sd0;
        dmem_gate[bbi] = 18'sd0;
    end

    // BUS-SUM RAM (#92/#98): the sequencer-facing mirror of a bus's
    // OUTPUT SUM — written by the same strobes as the replicas, read
    // at P1 by SEND entries. This is what makes the node graph's
    // edges real (Thor, #98): a send references the bus's summed
    // output (firmware base + every source contribution written so
    // far), not the firmware base alone. With sources ordered before
    // their sends in the table, propagation is same-sample.
    // Program counter replica-write strobes (driven below)
    logic               dmem_we;
    logic [9:0]         dmem_waddr;
    logic signed [17:0] dmem_wdata;

    // #134: ping-pong generation bit. The sequencer writes generation
    // ~dmem_gen while the pipeline reads dmem_gen, and they swap at the
    // sample boundary -- so every element sees one coherent generation
    // and a read can never collide with a write. Costs no extra BSRAM:
    // the second generation lives in the half of each block that the
    // 512-entry pool leaves unused.
    logic dmem_gen;   // flipped once per sample by the process below

    // Register validity-map updates separately from the forwarding/writeback
    // address. This removes the 1536-flag decode fanout from wb_addr (also
    // used by SEND forwarding) and registers the legal-address predicate.
    // One extra control clock completes well before restoration starts.
    logic mark_valid, mark_gen;
    logic [8:0] mark_addr;
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) begin
            mark_valid <= 1'b0; mark_gen <= 1'b0; mark_addr <= '0;
        end else begin
            mark_valid <= dmem_we && dmem_waddr > 10'd0 && dmem_waddr < 10'd512;
            mark_gen <= ~dmem_gen;
            mark_addr <= dmem_waddr[8:0];
        end

    // Track destinations written into each generation. These are control
    // validity bits, not a replacement for the existing Gowin bus RAMs.
    // After the lane has consumed the current generation, restore its
    // produced buses to BASE before that half becomes the next writer.
    // OFF or a changed destination therefore cannot leave an old sum alive.
    // Scan eight flags per group and retire at most one set flag per clock:
    // 128 group-load/advance clocks + at most 256 destinations + one read cycle fit
    // between the lane drain and the next 768-clock sample boundary.
    logic [511:0] produced_mask [0:1];
    logic [6:0] restore_group;
    logic restore_loaded;
    logic [7:0] restore_flags;
    logic sample_seen;
    logic restore_valid, restore_gen;
    logic [8:0] restore_addr;
    wire signed [17:0] restore_data = base_q;
    logic [2:0] restore_bit;
    logic restore_found;
    always_comb begin
        restore_found = 1'b0;
        restore_bit = 3'd0;
        for (int ri = 0; ri < 8; ri = ri + 1)
            if (restore_flags[ri] && !restore_found) begin
                restore_found = 1'b1;
                restore_bit = 3'(ri);
            end
    end
    // Decode the next slot's restore range before its existing first edge.
    // Both scans drain within the frame; idle slot 767 needs no new work.
    logic restore_slot_q;
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) restore_slot_q <= 1'b0;
        else restore_slot_q <=
            (slot >= 10'(synth_pkg::LANE_SPAN + 1)) &&
            (slot < 10'(synth_pkg::DRUM_CYCLES - 1));
    wire restore_window = restore_slot_q && (restore_group < 7'd64);
    wire restore_load = restore_window && !restore_loaded;
    wire restore_issue = restore_window && restore_loaded && restore_found;
    wire [8:0] restore_issue_addr = {restore_group[5:0], restore_bit};

    // Static eight-bit writers keep restore/mark enables local. A variable
    // slice write to the whole 512-bit bank otherwise becomes a shared enable
    // with hundreds of loads. Clear and mark still commit on the same edge,
    // with a simultaneous mark winning over its group's restore clear.
    for (genvar mg = 0; mg < 2; mg++) begin : g_produced_bank
        for (genvar mb = 0; mb < 64; mb++) begin : g_group
            (* syn_keep = 1 *) wire clear_group = restore_load &&
                (dmem_gen == 1'(mg)) && (restore_group == 7'(mb));
            (* syn_keep = 1 *) wire mark_group = mark_valid &&
                (mark_gen == 1'(mg)) && (mark_addr[8:3] == 6'(mb));
            always_ff @(posedge clk or negedge rst_n) begin
                if (!rst_n) produced_mask[mg][mb*8 +: 8] <= 8'd0;
                else begin
                    if (clear_group) produced_mask[mg][mb*8 +: 8] <= 8'd0;
                    if (mark_group)
                        produced_mask[mg][mb*8 +: 8] <=
                            (clear_group ? 8'd0 : produced_mask[mg][mb*8 +: 8])
                            | (8'b1 << mark_addr[2:0]);
                end
            end
        end
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            restore_group <= '0;
            restore_flags <= '0;
            restore_loaded <= 1'b0;
            sample_seen <= 1'b0;
            restore_valid <= 1'b0;
            restore_gen <= 1'b0;
            restore_addr <= '0;
        end else begin
            restore_valid <= restore_issue;
            if (sample_tick) begin
                restore_group <= '0;
                restore_flags <= '0;
                restore_loaded <= 1'b0;
                sample_seen <= 1'b1;
            end
            else if (restore_load) begin
                restore_flags <= produced_mask[dmem_gen][{restore_group[5:0], 3'b000} +: 8];
                restore_loaded <= 1'b1;
            end else if (restore_window && restore_loaded && !restore_found) begin
                restore_group <= restore_group + 7'd1;
                restore_loaded <= 1'b0;
            end
            if (restore_issue) begin
                restore_flags[restore_bit] <= 1'b0;
                restore_addr <= restore_issue_addr;
                restore_gen <= dmem_gen;
            end
        end
    end
    // The source mirror has no generations: it holds THIS pass's writes,
    // while replicas above restore the generation the lane just consumed.
    // Track it separately so a newly created target used for one pass only
    // cannot leak into a surviving SEND on the next pass. The two existing
    // RAM write ports and BASE copies restore independently in parallel;
    // each scan needs at most 256 destinations + 128 group clocks + one read cycle.
    logic [511:0] source_mask;
    logic [6:0] source_restore_group;
    logic source_restore_loaded;
    logic [7:0] source_restore_flags;
    logic source_restore_valid;
    logic [8:0] source_restore_addr;
    logic [2:0] source_restore_bit;
    logic source_restore_found;
    always_comb begin
        source_restore_found = 1'b0;
        source_restore_bit = 3'd0;
        for (int si = 0; si < 8; si = si + 1)
            if (source_restore_flags[si] && !source_restore_found) begin
                source_restore_found = 1'b1;
                source_restore_bit = 3'(si);
            end
    end
    wire source_restore_window = restore_slot_q && (source_restore_group < 7'd64);
    wire source_restore_load = source_restore_window && !source_restore_loaded;
    wire source_restore_issue = source_restore_window && source_restore_loaded && source_restore_found;
    wire [8:0] source_restore_issue_addr = {source_restore_group[5:0], source_restore_bit};
    // As with produced_mask, give each static byte its own local write enable.
    // A same-edge mark wins over clearing that byte, including its other bits.
    for (genvar sb = 0; sb < 64; sb = sb + 1) begin : g_source_mask_byte
        (* syn_keep = 1 *) wire clear_group = source_restore_load &&
            (source_restore_group == 7'(sb));
        (* syn_keep = 1 *) wire mark_group = mark_valid &&
            (mark_addr[8:3] == 6'(sb));
        always_ff @(posedge clk or negedge rst_n) begin
            if (!rst_n) source_mask[sb*8 +: 8] <= 8'd0;
            else begin
                if (clear_group) source_mask[sb*8 +: 8] <= 8'd0;
                if (mark_group)
                    source_mask[sb*8 +: 8] <=
                        (clear_group ? 8'd0 : source_mask[sb*8 +: 8])
                        | (8'b1 << mark_addr[2:0]);
            end
        end
    end
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            source_restore_group <= '0;
            source_restore_loaded <= 1'b0;
            source_restore_flags <= '0;
            source_restore_valid <= 1'b0;
            source_restore_addr <= '0;
        end else begin
            source_restore_valid <= source_restore_issue;
            if (sample_tick) begin
                source_restore_group <= '0;
                source_restore_loaded <= 1'b0;
                source_restore_flags <= '0;
            end else if (source_restore_load) begin
                source_restore_flags <= source_mask[{source_restore_group[5:0], 3'b000} +: 8];
                source_restore_loaded <= 1'b1;
            end else if (source_restore_window && source_restore_loaded && !source_restore_found) begin
                source_restore_group <= source_restore_group + 7'd1;
                source_restore_loaded <= 1'b0;
            end
            if (source_restore_issue) begin
                source_restore_flags[source_restore_bit] <= 1'b0;
                source_restore_addr <= source_restore_issue_addr;
            end
        end
    end

// synthesis translate_off
`ifndef SYNTHESIS
    always @(posedge clk) if (rst_n) begin
        if ((restore_valid || source_restore_valid) && dmem_we)
            $fatal(1, "CSP restore overlaps an instruction write");
        if (sample_tick && sample_seen && (restore_valid || restore_group != 7'd64 ||
            source_restore_valid || source_restore_group != 7'd64))
            $fatal(1, "CSP base restoration did not drain before the sample boundary");
    end
`endif
// synthesis translate_on

    logic dmem_wr_toggle_meta, dmem_wr_toggle_sync, dmem_wr_toggle_prev;
    logic dmem_mbox_pending;
    logic [9:0]  dmem_mbox_addr;
    logic [17:0] dmem_mbox_data;
    // #134: a mailbox entry commits over TWO takes, one per generation.
    // The sequencer rewrites its target buses every sample, so ITS writes
    // can live in one half. A mailbox write sets a PERSISTENT base that
    // nothing refreshes, so a single-half write alternates with stale
    // data on every swap -- silence, in practice (caught by
    // tb_prog_pingpong's persistence check). dmem_commit_half latches the
    // half written first, so a sample boundary falling between the two
    // takes cannot make the second write repeat it.
    //
    // This does not weaken atomicity where it matters: the property we
    // need is that the SEQUENCER's pass is seen as one complete
    // generation. Firmware writes were always asynchronous and
    // mid-sample, before ping-pong and after it.
    logic dmem_commit_phase;
    logic dmem_commit_half;
    // Mailbox commits happen in any idle slot where the sequencer is not
    // writing the replicas THIS cycle (dmem_we below); a commit colliding
    // with a sequencer write defers a cycle.
    //
    // A full 256-entry SEND program can hold dmem_we for 256 clocks,
    // longer than a 10 MHz burst's 32-bit word interval. The sender therefore
    // holds address/data until dmem_wr_ack matches the request toggle, and
    // reports any unaccepted burst word through STATUS rather than replacing
    // this payload. Correctness no longer depends on the default 96-write
    // program or on an assumed SPI inter-word gap.
    // dmem_mbox_take is the SINGLE condition for both committing and
    // clearing pending. An earlier version cleared pending on
    // dmem_wr_window alone while the commit also required !dmem_we — when a
    // write's first idle cycle coincided with a sequencer write (~1 in
    // 3 during the sequencer span), the write was silently dropped:
    // a lost gate-off was a stuck note, a lost gate-on a dead key.
    // #134: ping-pong put reads and writes in different generations,
    // so the window that used to keep them apart by schedule is gone.
    // A commit still defers a cycle when the sequencer is writing, since
    // they share the write port.
    wire  dmem_wr_window   = 1'b1;
    wire  dmem_mbox_take   = dmem_mbox_pending && dmem_wr_window && !dmem_we &&
        !restore_valid && !source_restore_valid;
    wire  dmem_commit = dmem_mbox_take && (dmem_mbox_addr != 10'd0);
    wire  dmem_commit_half_sel = dmem_commit_phase ? ~dmem_commit_half : ~dmem_gen;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dmem_wr_toggle_meta <= 1'b0; dmem_wr_toggle_sync <= 1'b0; dmem_wr_toggle_prev <= 1'b0;
            dmem_mbox_pending <= 1'b0;
            dmem_mbox_addr <= '0; dmem_mbox_data <= '0;
            dmem_wr_ack <= 1'b0;
        end else begin
            dmem_wr_toggle_meta <= dmem_wr_toggle; dmem_wr_toggle_sync <= dmem_wr_toggle_meta; dmem_wr_toggle_prev <= dmem_wr_toggle_sync;
            if (dmem_wr_toggle_sync != dmem_wr_toggle_prev && !dmem_mbox_pending) begin
                dmem_mbox_pending <= 1'b1;         // payload is stable: it was
                dmem_mbox_addr   <= dmem_wr_addr;      // written before the toggle,
                dmem_mbox_data   <= dmem_wr_data;      // 2 sync FFs ago
            end else if (dmem_mbox_take && dmem_commit_phase) begin
                dmem_mbox_pending <= 1'b0;
                dmem_wr_ack <= dmem_wr_toggle_sync;
            end
        end
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dmem_commit_phase <= 1'b0;
            dmem_commit_half  <= 1'b0;
        end else if (dmem_mbox_take) begin
            if (!dmem_commit_phase) begin
                dmem_commit_half  <= ~dmem_gen;   // the half written now
                dmem_commit_phase <= 1'b1;
            end else
                dmem_commit_phase <= 1'b0;
        end
    end

    always_ff @(posedge clk)
        if (dmem_commit) dmem_init[dmem_mbox_addr] <= $signed(dmem_mbox_data);
    always_ff @(posedge clk)
        if (dmem_commit) dmem_gate[dmem_mbox_addr] <= $signed(dmem_mbox_data);

    // Bus 1023 doubles as the test-tone control latch (issue #81).
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n)                                       test_tone_en <= 1'b0;
        else if (dmem_commit && dmem_mbox_addr == 10'd1023)
            test_tone_en <= dmem_mbox_data[0];

    // Replica writes use one physical port per Gowin SDPX9B. The source
    // mirror uses the same write strobe but its original 10-bit address.
    sdp_ram_1024x18 u_dmem_pitch (
        .dout(rd_pitch_d), .clka(clk), .cea(dmem_replica_we),
        .clkb(clk), .ceb(1'b1), .oce(1'b1), .reset(1'b0),
        .ada(dmem_replica_waddr), .din(dmem_replica_wdata),
        .adb({dmem_gen, rd_pitch_a}));
    sdp_ram_1024x18 u_dmem_duty (
        .dout(rd_duty_d), .clka(clk), .cea(dmem_replica_we),
        .clkb(clk), .ceb(1'b1), .oce(1'b1), .reset(1'b0),
        .ada(dmem_replica_waddr), .din(dmem_replica_wdata),
        .adb({dmem_gen, rd_duty_a}));
    sdp_ram_1024x18 u_dmem_fc (
        .dout(rd_fc_d), .clka(clk), .cea(dmem_replica_we),
        .clkb(clk), .ceb(1'b1), .oce(1'b1), .reset(1'b0),
        .ada(dmem_replica_waddr), .din(dmem_replica_wdata),
        .adb({dmem_gen, rd_fc_a}));
    sdp_ram_1024x18 u_dmem_q (
        .dout(rd_q_d), .clka(clk), .cea(dmem_replica_we),
        .clkb(clk), .ceb(1'b1), .oce(1'b1), .reset(1'b0),
        .ada(dmem_replica_waddr), .din(dmem_replica_wdata),
        .adb({dmem_gen, rd_q_a}));
    sdp_ram_1024x18 u_dmem_gl (
        .dout(rd_gl_d), .clka(clk), .cea(dmem_replica_we),
        .clkb(clk), .ceb(1'b1), .oce(1'b1), .reset(1'b0),
        .ada(dmem_replica_waddr), .din(dmem_replica_wdata),
        .adb({dmem_gen, rd_gl_a}));
    sdp_ram_1024x18 u_dmem_gr (
        .dout(rd_gr_d), .clka(clk), .cea(dmem_replica_we),
        .clkb(clk), .ceb(1'b1), .oce(1'b1), .reset(1'b0),
        .ada(dmem_replica_waddr), .din(dmem_replica_wdata),
        .adb({dmem_gen, rd_gr_a}));
    sdp_ram_1024x18 u_dmem_local (
        .dout(src_q), .clka(clk), .cea(dmem_local_we),
        .clkb(clk), .ceb(1'b1), .oce(1'b1), .reset(1'b0),
        .ada(dmem_local_waddr), .din(dmem_local_wdata),
        .adb(cfg_q[25:16]));

    //----------------------------------------------------------------
    // Program counter (B4/B5, bus_architecture.md) -- the table executor.
    // 256 entries x 3 config words (stride 4 in the RAM), ONE cycle per
    // entry since #138; the six-stage pipeline is documented at the pc
    // below. Law 1: the one instruction multiply sits alone in its stage
    // with registered operands. Law 3: entries execute in table order, once
    // per sample. An instruction's OUTPUT uses the PREVIOUS sample's state --
    // the one-sample lag is inaudible at control rates and it keeps both the
    // sine's internal multiply and the instruction multiply fed by registers
    // only.
    //
    // ADSR state word: [27:26] stage, [25:0] level in UQ12.14 -- the RC
    // envelope's format (#127), now living in dsp/adsr.sv. The envelope is
    // the one instruction with a real state machine, so it is its own module
    // (Thor, #146); the LFO is an adder and the SEND is a wire, and both stay
    // here. An LFO uses [24:0] as its phase accumulator and leaves [27:25]
    // alone, so the two never collide -- an instruction is one or the other.
    //----------------------------------------------------------------
    localparam [1:0] AST_IDLE = 2'd0, AST_ATT = 2'd1,
                     AST_DEC  = 2'd2, AST_REL = 2'd3;

    // THE 3-CYCLE BOTTLENECK, REMOVED (#138). This was one RAM holding
    // {bank, entry, word} and read through a single port with the address
    // muxed by phase: CFG at P0, RATES at P1, DEPTH at P2. Three words
    // through one port costs three cycles however the logic is arranged, so
    // the instruction rate was capped at one per three cycles regardless of
    // how well the stages overlapped.
    //
    // Three RAMs, same total bits, read with the SAME address in the SAME
    // cycle. The SPI write path is unchanged from the firmware's point of
    // view: imem_write_addr is still {entry[7:0], word[1:0]} and the low two
    // bits now select which RAM the word lands in.
    reg [31:0] imem_cfg   [0:2*synth_pkg::NUM_INSTR-1];   // {bank, entry[7:0]}
    reg [31:0] imem_rate  [0:2*synth_pkg::NUM_INSTR-1];
    reg [31:0] imem_depth [0:2*synth_pkg::NUM_INSTR-1];
    // #145: word 3, the second ADSR rate word. The address encoding always
    // had this slot -- imem_write_addr is {entry, word[1:0]} and only 0/1/2
    // were decoded -- and firmware's flush already strided by four.
    reg [31:0] imem_rate2 [0:2*synth_pkg::NUM_INSTR-1];
    // State word: LFO uses [23:0] as its phase; ADSR uses [27:26] as
    // the stage and [25:0] as the level in UQ22.4 — FOUR FRACTIONAL
    // BITS, so rate increments are in 1/16-LSB units and the 8-bit
    // log2 rate byte decodes as ONE uniform expression with no
    // truncation anywhere: all 256 codes are distinct equal-ratio
    // steps (Thor's perceptual-linearity rule; a MIDI CC maps as
    // cc << 1). The fractional bits ARE the "binary point moved four
    // left" — in the accumulator, where it belongs.
    reg [27:0] istate [0:synth_pkg::NUM_INSTR-1];
    integer wi;
    initial begin
        for (wi = 0; wi < 2*synth_pkg::NUM_INSTR; wi = wi + 1) begin
            imem_cfg[wi]   = 32'd0;            // opcode 0 = off
            imem_rate[wi]  = 32'd0;
            imem_depth[wi] = 32'd0;
            imem_rate2[wi] = 32'd0;
        end
        for (wi = 0; wi < synth_pkg::NUM_INSTR; wi = wi + 1)
            istate[wi] = 28'd0;
    end

    // Route the word to its RAM by the low address bits. Same wire format.
    wire [8:0] imem_wr_entry = {bank_shadow, imem_write_addr[9:2]};
    always_ff @(posedge sclk)
        if (imem_write_enable) begin
            case (imem_write_addr[1:0])
                2'd0: imem_cfg[imem_wr_entry]   <= imem_write_data;
                2'd1: imem_rate[imem_wr_entry]  <= imem_write_data;
                2'd2: imem_depth[imem_wr_entry] <= imem_write_data;
                2'd3: imem_rate2[imem_wr_entry] <= imem_write_data;
                default: ;                      // word 3 unused (stride 4)
            endcase
        end

    // ONE INSTRUCTION PER CYCLE (#138). Six stages, one instruction deep
    // each, retiring one per cycle once full:
    //
    //   F  present pc to imem_cfg/rate/depth and istate
    //   D  those four are out; present the gate/source address (from CFG)
    //      and the target address to dmem_gate / dmem_local / dmem_init
    //   R  gate, send-source and target base are out; decode the rate
    //      bytes, resolve the gate, choose the operand
    //   X  capture the instruction operands in DSP A/B registers and
    //      align the state writeback
    //   A  DSP multiply-add with the current forwarded addend, saturated
    //   W  write the replicas
    //
    // FULL RATE IS BACK. #100 put the sequencer on half rate because
    // 256 instructions x 3 cycles = 768 did not fit beside the lane
    // pipeline in a 768-cycle sample. At one per cycle a full 256-entry
    // pass costs 256 cycles, so every instruction runs every sample: 96 kHz
    // control instead of 48, and the allocator's "a chain must live inside
    // one half" rule is gone along with pc_half itself.
    //
    // Field map, unchanged from the 3-cycle version so the firmware format
    // is untouched (traced from the old phase-delayed reads):
    //   CFG   [3:0] opcode, [5:4] LFO shape, [15:6] target, [25:16] source
    //         bus (also the ADSR's watched gate), [31:16] LFO phase
    //         increment -- overlapping the source field, which an LFO does
    //         not use
    //   RATES [7:0] attack, [15:8] decay, [23:16] sustain LEVEL, [31:24]
    //         release -- the universal A, D, S, R order
    //   DEPTH [17:0] signed coefficient, 0x10000 = unity
    logic [8:0] pc;             // 0..NUM_INSTR, one per cycle

    // The generation now flips once per SAMPLE, because a complete pass is
    // once per sample again. Under half rate it had to flip every other
    // sample (a pass spanned two), and flipping early published a
    // half-written generation -- halved depth and a stale link in any chain.
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n)           dmem_gen <= 1'b0;
        else if (sample_tick) dmem_gen <= ~dmem_gen;

    wire pc_active = (pc < 9'(synth_pkg::NUM_INSTR));
    // Drain: the last instruction fetched still has to reach W, which is the
    // full pipeline depth now rather than the 2 slots the 3-cycle version
    // needed. Without it the final entries' writes never land -- #97's
    // stale-cutoff bug was exactly this off-by-a-pipeline-length.
    localparam int PIPE_DRAIN = 5;
    logic [2:0] drain_cnt;
    wire pc_running = pc_active || (drain_cnt != 3'd0);
    wire [7:0] pc_addr = pc[7:0];

    // A fence for firmware: increments only after every instruction,
    // including the last gate read and state writeback, has completed.
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) control_pass_count <= 16'd0;
        else if (!pc_active && drain_cnt == 3'd1)
            control_pass_count <= control_pass_count + 16'd1;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc <= 9'(synth_pkg::NUM_INSTR); drain_cnt <= 3'd0;
        end else if (sample_tick) begin
            pc <= 9'd0; drain_cnt <= 3'(PIPE_DRAIN);
        end else if (pc_running) begin
            if (pc_active) pc <= pc + 9'd1;
            else if (drain_cnt != 3'd0) drain_cnt <= drain_cnt - 3'd1;
        end
    end

    // ---- RAM outputs, all arriving in the same cycle --------------------
    logic [31:0] cfg_q, rate_q, depth_q, rate2_q;
    logic [27:0] istate_q;
    logic signed [17:0] gate_q, base_q;
    wire signed [17:0] src_q;
    logic [7:0]  pc_addr_d;     // the pc that goes with cfg_q
    logic        d_valid;

    // ---- D stage registers ---------------------------------------------
    logic        r_valid;
    logic [7:0]  r_pc;
    logic [3:0]  r_opcode;
    // decoded enables, so no stage switches on the opcode value
    wire r_src_en = r_opcode[synth_pkg::OPC_SOURCE];
    wire r_st_en  = r_opcode[synth_pkg::OPC_STATE];
    wire r_mul_en = r_opcode[synth_pkg::OPC_MUL];
    wire r_acc_en = r_opcode[synth_pkg::OPC_ACCUM];
    wire r_is_env = r_st_en &&  r_src_en;     // watches a gate -> envelope
    wire r_is_lfo = r_st_en && !r_src_en;     // free-running phase
    logic [1:0]  r_shape;
    logic [9:0]  r_dest, r_src;
    logic [15:0] r_lfo_rate;
    logic [31:0] r_rate, r_depth, r_rate2;
    logic [27:0] r_istate;

    // ---- R stage registers ---------------------------------------------
    logic        x_valid;
    logic [7:0]  x_pc;
    logic [3:0]  x_opcode;
    wire x_st_en  = x_opcode[synth_pkg::OPC_STATE];
    wire x_mul_en = x_opcode[synth_pkg::OPC_MUL];
    wire x_acc_en = x_opcode[synth_pkg::OPC_ACCUM];
    wire x_is_lfo = x_st_en && !x_opcode[synth_pkg::OPC_SOURCE];
    logic [9:0]  x_dest;
    logic [27:0] x_istate;
    // Keep the existing operand boundary in fabric: absorbing it into the
    // multiplier's input register lengthens the forwarding/saturation route.
    (* syn_preserve = 1 *) logic signed [17:0] x_operand;
    logic signed [17:0] x_depth, x_base;
    logic [15:0] x_lfo_rate;

    // ---- X stage registers ---------------------------------------------
    logic        a_valid;
    logic        a_acc_en;
    logic [9:0]  a_dest;
    // the state write rides to A so the envelope's two-cycle result and the
    // LFO's one-cycle accumulate land together, from one port
    logic        a_st_en, a_is_lfo;
    logic [7:0]  a_st_pc;
    logic [27:0] a_lfo_next;
    logic signed [17:0] a_base;

    // ---- A stage registers: the write ----------------------------------
    logic        wb_valid;
    logic [9:0]  wb_addr;
    logic signed [17:0] wb_value;

    // Keep the autonomous AMP output apart from SPI's live BASE writes.
    // Reuse the official 18x1024 RAM configuration, with one generation bit
    // and five voice bits. Validity is resettable; RAM contents need no reset
    // or assumed power-up value. Legacy builds instantiate no extra RAM.
    generate if (LOCAL_MIDI_BOOT) begin : g_local_gain
        logic [63:0] valid_map;
        logic read_valid;
        wire [4:0] read_voice = rd_gl_a[4:0] - 5'd16;
        wire [4:0] write_voice = wb_addr[4:0] - 5'd16;
        wire cache_write = local_program_active && wb_valid &&
            wb_addr >= 10'd16 && wb_addr < 10'd48;
        wire [17:0] cache_data;
        always_ff @(posedge clk or negedge rst_n) begin
            if (!rst_n) begin
                valid_map <= 64'd0;
                read_valid <= 1'b0;
            end else begin
                if (cache_write) valid_map[{~dmem_gen, write_voice}] <= 1'b1;
                read_valid <= valid_map[{dmem_gen, read_voice}];
            end
        end
        sdp_ram_1024x18 u_ram (
            .dout(cache_data), .clka(clk), .cea(cache_write),
            .clkb(clk), .ceb(1'b1), .oce(1'b1), .reset(1'b0),
            .ada({4'd0, ~dmem_gen, write_voice}), .din(wb_value),
            .adb({4'd0, dmem_gen, read_voice})
        );
        assign local_gain_o = read_valid ? $signed(cache_data) : LOCAL_GAIN_FLOOR;
    end else begin : g_no_local_gain
        assign local_gain_o = 18'sd0;
    end endgenerate

    //----------------------------------------------------------------
    // FORWARDING. At three cycles per instruction there was slack enough
    // that comparing against the immediately previous entry covered every
    // case, which is why the allocator had to group sources sharing a target
    // into adjacent slots. At one per cycle several instructions are in
    // flight, so a short history of completed results is kept and the MOST
    // RECENT match wins. That relaxes the allocator rule rather than
    // tightening it: a chain now tolerates gaps of up to three slots.
    //
    // The base read needs no hazard logic -- dmem_init is the pass's initial
    // image and only the SPI mailbox ever writes it.
    //----------------------------------------------------------------
    logic        h1_valid, h2_valid;
    logic [9:0]  h1_addr,  h2_addr;
    logic signed [17:0] h1_value, h2_value;

    // Resolve addresses one existing stage ahead of the arithmetic. At
    // X -> A, the matching history addresses advance A -> WB -> H1 -> H2;
    // at D -> R, SEND's source advances with X -> A -> WB -> H1 -> H2.
    // Include the next stage's validity/accumulate enable in these predicates.
    // Those controls are already known here; qualifying them after the register
    // would add decode before and after the combinational DSP/forwarding path.
    // Data and newest-match priority still resolve in the consuming stage.
    logic [2:0] a_acc_forward;
    logic [3:0] r_send_forward;

    // bit 3 makes chaining EXPLICIT. It used to be inferred from program
    // order -- adjacent entries sharing a target summed, scattered ones did
    // last-write-wins -- which is a correctness rule the allocator could not
    // check. With the bit clear an instruction always starts from the
    // target's initial value, whatever its neighbours do.
    wire signed [17:0] accum_in =
          a_acc_forward[0] ? wb_value
        : a_acc_forward[1] ? h1_value
        : a_acc_forward[2] ? h2_value
        :                                               a_base;
    wire signed [20:0] accum_sum;
    // The DSP input registers replace only the old X->A product register.
    // Its C input sees the same current A-stage forwarding choice above;
    // SEND still consumes this cycle's combinational saturated sum below.
    csp_mac_gw5a u_instruction_mac (
        .clk(clk), .rst_n(rst_n), .operand(x_operand), .depth(x_depth),
        .multiply_enable(x_mul_en), .accumulator(accum_in), .sum(accum_sum)
    );
    wire signed [17:0] accum_sat =
        (accum_sum > 21'sd131071)  ? 18'sd131071  :
        (accum_sum < -21'sd131072) ? -18'sd131072 : accum_sum[17:0];

    // A SEND reads a bus's OUTPUT SUM from dmem_local, and at one instruction
    // per cycle the bus it wants may have been written by an instruction
    // still in flight -- the read would silently be stale. Same history,
    // same most-recent-wins rule. The instruction one ahead is in A right
    // now, so its contribution is the combinational accum_sat.
    wire signed [17:0] send_src =
          r_send_forward[0] ? accum_sat
        : r_send_forward[1] ? wb_value
        : r_send_forward[2] ? h1_value
        : r_send_forward[3] ? h2_value
        :                                  src_q;

    // ---- the envelope, in its own module ---------------------------------
    // Fed at R from registers; its state_out lands two cycles later, at A.
    wire signed [17:0] adsr_level;
    wire [27:0]        adsr_state_out;
    logic midi_gate_q;
    wire runtime_enabled = ENABLE_MIDI && midi_enable;
    wire runtime_gate_selected = runtime_enabled && r_src >= 10'd80 && r_src < 10'd112;
    wire effective_gate = runtime_gate_selected ? midi_gate_q : gate_q > 18'sd0;
    // Observe the actual normalized envelope, rather than estimating a tail
    // from wall time. 2^22 / 1024 is the firmware's conservative threshold.
    always_comb begin
        midi_tail_done = 32'd0;
        if (runtime_enabled && r_valid && r_is_env && r_pc >= 8'd32 && r_pc < 8'd64 &&
            runtime_gate_selected && !effective_gate && r_istate[25:0] <= 26'd4096)
            midi_tail_done[r_pc[4:0]] = 1'b1;
    end
    adsr u_adsr (
        .clk(clk), .rst_n(rst_n),
        .step_en   (r_valid && r_is_env),
        .state_in  (r_istate),
        .gate      (effective_gate),
        .rates     (r_rate),
        .rates2    (r_rate2),
        .level_out (adsr_level),
        .state_out (adsr_state_out),
        .state_we  ()                  // the CSP tracks validity itself
    );

    // Read the quarter-wave ROM alongside D -> R, using the same istate_q
    // captured by r_istate. This keeps the existing R -> X -> A latency,
    // but removes the asynchronous LUT decode from the multiply input path.
    // No reset on the ROM output, matching the other synchronous ROMs.
    reg [23:0] lfo_sine_lut [0:255];
    initial $readmemh("rtl/synthesizers/common/oscillator/data/sine_lut.hex", lfo_sine_lut);
    wire [7:0] lfo_sine_addr = istate_q[23] ? ~istate_q[22:15] : istate_q[22:15];
    reg [23:0] lfo_sine_mag_r;
    always_ff @(posedge clk) lfo_sine_mag_r <= lfo_sine_lut[lfo_sine_addr];

    // Same signed arithmetic and truncation as osc_core with duty=0.
    // bits [24:1] retain the fractional half-step of the LFO accumulator.
    wire signed [23:0] lfo_phase = $signed(r_istate[24:1]);
    wire signed [23:0] lfo_sine = lfo_phase[23] ? -$signed(lfo_sine_mag_r)
                                                          : $signed(lfo_sine_mag_r);
    wire signed [23:0] lfo_triangle = (lfo_phase < 24'h800000)
        ? (lfo_phase << 1) - 24'h800000
        : 24'h7fffff - ((lfo_phase - 24'h800000) << 1);
    logic signed [17:0] lfo_wave;
    // Same unscaled producer-0 waveform as the patch LFO, including depth=0.
    // Publish alongside the matching bus generation, never restart on CC1.
    logic signed [17:0] wheel_wave_bank[0:1];
    logic wheel_update_x, wheel_lfo_x, wheel_gen_x;
    logic signed [15:0] wheel_depth_x;
    // Reuse R -> X's registered waveform, then multiply at X -> A. The bank
    // being prepared is inactive until the next sample boundary; the public
    // wheel waveform still changes with exactly the same bus generation.
    wire signed [33:0] wheel_wave_product=x_operand * wheel_depth_x;
    always_ff @(posedge clk or negedge rst_n)
        if(!rst_n)begin
            wheel_wave_bank[0]<=0;wheel_wave_bank[1]<=0;midi_wheel_wave_o<=0;
            wheel_update_x<=0;wheel_lfo_x<=0;wheel_gen_x<=0;wheel_depth_x<=0;
        end else begin
            wheel_update_x<=ENABLE_EXPRESSION && r_valid && r_pc==0;
            wheel_lfo_x<=r_is_lfo;
            wheel_gen_x<=~dmem_gen;
            wheel_depth_x<=ENABLE_EXPRESSION ? midi_wheel_depth:16'sd0;
            if(wheel_update_x)
                wheel_wave_bank[wheel_gen_x]<=wheel_lfo_x ? 18'(wheel_wave_product >>> 15):18'sd0;
            midi_wheel_wave_o<=ENABLE_EXPRESSION ? wheel_wave_bank[dmem_gen]:18'sd0;
        end
    always_comb begin
        case (r_shape)
            2'd0: lfo_wave = 18'(lfo_phase >>> 8);
            2'd1: lfo_wave = lfo_phase < 0 ? 18'sd32767 : -18'sd32768;
            2'd2: lfo_wave = 18'(lfo_triangle >>> 8);
            default: lfo_wave = 18'(lfo_sine >>> 8);
        endcase
    end

    // the LFO's next phase: a plain accumulate, so it needs no stage of
    // its own. The envelope's next state comes out of u_adsr, two cycles
    // after its inputs, which is why both are written at the accumulate
    // stage rather than here.
    logic [27:0] lfo_next;
    always_comb
        // free-running phase accumulator in [24:0]; the extra low bit is the
        // fractional half-step that keeps the frequency unchanged now that a
        // pass runs every sample (#145)
        lfo_next = {x_istate[27:25], x_istate[24:0] + {9'b0, x_lfo_rate}};

    //----------------------------------------------------------------
    // The pipeline.
    //----------------------------------------------------------------
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            d_valid <= 1'b0; r_valid <= 1'b0; x_valid <= 1'b0;
            a_valid <= 1'b0; wb_valid <= 1'b0;
            h1_valid <= 1'b0; h2_valid <= 1'b0;
            pc_addr_d <= '0;
            r_pc <= '0; r_opcode <= '0; r_shape <= '0; r_dest <= '0;
            r_src <= '0; r_lfo_rate <= '0; r_rate <= '0; r_depth <= '0;
            r_istate <= '0; r_rate2 <= '0;
            x_pc <= '0; x_opcode <= '0; x_dest <= '0; x_istate <= '0;
            x_operand <= '0; x_depth <= '0; x_base <= '0;
            x_lfo_rate <= '0;
            a_dest <= '0; a_base <= '0; a_acc_en <= 1'b0;
            a_st_en <= 1'b0; a_is_lfo <= 1'b0; a_st_pc <= '0; a_lfo_next <= '0;
            wb_addr <= '0; wb_value <= '0;
            h1_addr <= '0; h1_value <= '0; h2_addr <= '0; h2_value <= '0;
            a_acc_forward <= '0; r_send_forward <= '0;
        end else begin
            // F -> D
            d_valid   <= pc_active;
            pc_addr_d <= pc_addr;

            // D -> R: the three config words are out of the RAMs this cycle
            r_valid    <= d_valid;
            r_pc       <= pc_addr_d;
            r_opcode   <= cfg_q[3:0];
            r_shape    <= cfg_q[5:4];
            r_dest     <= cfg_q[15:6];
            r_src      <= cfg_q[25:16];
            r_send_forward[0] <= x_valid && (cfg_q[25:16] == x_dest);
            r_send_forward[1] <= a_valid && (a_dest != 10'd0)
                && (cfg_q[25:16] == a_dest);
            r_send_forward[2] <= wb_valid && (cfg_q[25:16] == wb_addr);
            r_send_forward[3] <= h1_valid && (cfg_q[25:16] == h1_addr);
            r_lfo_rate <= cfg_q[31:16];
            // An ADSR uses CFG[25:16] as its gate bus, so bit 26 is free.
            r_rate2    <= rate2_q;
            r_rate     <= rate_q;
            r_depth    <= depth_q;
            if (runtime_enabled && cfg_q[3:0] == synth_pkg::OPC_ADSR) begin
                if (pc_addr_d >= 8'd32 && pc_addr_d < 8'd64)
                    r_depth <= {14'd0, midi_amp_depth[pc_addr_d[4:0]*18 +: 18]};
                else if (pc_addr_d >= 8'd64 && pc_addr_d < 8'd128 && !pc_addr_d[0])
                    r_depth <= {14'd0, midi_mod_depth[pc_addr_d[5:1]*18 +: 18]};
            end
            r_istate   <= istate_q;

            // R -> X: gate / source / base are out; decode and choose
            x_valid   <= r_valid && (r_opcode != synth_pkg::OPC_OFF);
            x_pc      <= r_pc;
            x_opcode  <= r_opcode;
            x_dest    <= r_dest;
            x_istate  <= r_istate;
            x_base    <= base_q;
            x_depth   <= $signed(r_depth[17:0]);
            x_lfo_rate <= r_lfo_rate;
            x_operand <= r_is_lfo ? lfo_wave
                       : r_is_env ? adsr_level
                       :            send_src;

            // X -> A: context aligned with the DSP AREG/BREG operand capture
            a_valid    <= x_valid;
            a_acc_en   <= x_acc_en;
            a_dest     <= x_dest;
            a_acc_forward[0] <= x_acc_en && a_valid && (a_dest != 10'd0)
                && (x_dest == a_dest);
            a_acc_forward[1] <= x_acc_en && wb_valid && (x_dest == wb_addr);
            a_acc_forward[2] <= x_acc_en && h1_valid && (x_dest == h1_addr);
            a_st_en    <= x_valid && x_st_en;
            a_is_lfo   <= x_is_lfo;
            a_st_pc    <= x_pc;
            a_lfo_next <= lfo_next;
            a_base    <= x_base;

            // A -> W: the saturated sum, and push the forwarding history
            wb_valid <= a_valid && (a_dest != 10'd0);
            wb_addr  <= a_dest;
            wb_value <= accum_sat;
            h1_valid <= wb_valid; h1_addr <= wb_addr; h1_value <= wb_value;
            h2_valid <= h1_valid; h2_addr <= h1_addr; h2_value <= h1_value;
        end
    end

    assign dmem_wdata = wb_value;
    assign dmem_waddr = wb_addr;
    assign dmem_we    = wb_valid;

    // Memory reads. The four imem RAMs share one address, so CFG, both RATE
    // words and DEPTH all arrive together instead of over three cycles. dmem_gate and
    // dmem_init are the same image read at two different addresses in the
    // same cycle, which is the other thing that used to cost a phase.
    always_ff @(posedge clk) begin
        midi_gate_q <= midi_voice_gate[cfg_q[20:16] - 5'd16];
        if (prepared_program_active) begin
            cfg_q<=0;rate_q<=0;rate2_q<=0;depth_q<=0;
            if(pc_addr==0)begin
                cfg_q<=prepared_patch_i[159:128];depth_q<={14'd0,prepared_patch_i[209:192]};
            end else if(pc_addr==1)begin
                cfg_q<=prepared_patch_i[191:160];depth_q<={14'd0,prepared_patch_i[227:210]};
            end else if(pc_addr>=2&&pc_addr<=4)begin
                // LFO0 always writes BUS2. Publish the other bases even
                // when the second LFO targets a different channel bus.
                if(prepared_patch_i[175:166]!=(pc_addr==2?10'd1:pc_addr==3?10'd3:10'd4))
                    cfg_q<=32'(synth_pkg::OPC_SEND)|((pc_addr==2?32'd1:pc_addr==3?32'd3:32'd4)<<6);
            end else if(pc_addr>=32&&pc_addr<64)begin
                cfg_q<=32'(synth_pkg::OPC_ADSR)|((32'd16+{27'd0,pc_addr[4:0]})<<6)|
                    ((32'd80+{27'd0,pc_addr[4:0]})<<16);
                rate_q<=prepared_patch_i[31:0];rate2_q<=prepared_patch_i[63:32];
            end else if(pc_addr>=64&&pc_addr<128)begin
                if(!pc_addr[0])begin
                    cfg_q<=32'(synth_pkg::OPC_ADSR)|((32'd48+{27'd0,pc_addr[5:1]})<<6)|
                        ((32'd80+{27'd0,pc_addr[5:1]})<<16);
                    rate_q<=prepared_patch_i[95:64];rate2_q<=prepared_patch_i[127:96];
                end else begin
                    cfg_q<=32'(synth_pkg::OPC_SEND)|((32'd48+{27'd0,pc_addr[5:1]})<<6)|(32'd4<<16);
                    depth_q<=32'h00010000;
                end
            end
        end else if (local_program_active) begin
            if (pc_addr >= 8'd32 && pc_addr < 8'd64) begin
                cfg_q <= 32'(synth_pkg::OPC_ADSR)
                    | ((32'd16 + {27'd0, pc_addr[4:0]}) << 6)
                    | ((32'd80 + {27'd0, pc_addr[4:0]}) << 16);
                // kA=32768, kD=512, kR=4096, sustain=768/1024:
                // about 8 ms attack and 0.3 s to the 1/1024 release floor.
                rate_q <= 32'h08008000;
                rate2_q <= 32'hc0010000;
            end else begin
                cfg_q <= 32'd0;
                rate_q <= 32'd0;
                rate2_q <= 32'd0;
            end
            depth_q <= 32'd0; // MIDI's existing per-voice depth overlay applies.
        end else begin
            cfg_q    <= imem_cfg[{bank_active, pc_addr}];
            rate_q   <= imem_rate[{bank_active, pc_addr}];
            depth_q  <= imem_depth[{bank_active, pc_addr}];
            rate2_q  <= imem_rate2[{bank_active, pc_addr}];
        end
        istate_q <= istate[pc_addr];
        // Reuse both existing single read ports after the CSP drains;
        // no additional BASE RAM or guessed generated IP is introduced.
        if(source_restore_issue&&prepared_program_active)
            gate_q<=prepared_base({1'b0,source_restore_issue_addr});
        else gate_q <= dmem_gate[source_restore_issue ? {1'b0,source_restore_issue_addr} : cfg_q[25:16]];
        if(prepared_program_active)
            base_q<=prepared_base(restore_issue?{1'b0,restore_issue_addr}:cfg_q[15:6]);
        else if (!restore_issue && local_program_active &&
            cfg_q[3:0] == synth_pkg::OPC_ADSR &&
            cfg_q[15:6] >= 10'd16 && cfg_q[15:6] < 10'd48)
            base_q <= LOCAL_GAIN_FLOOR;
        else
            base_q <= dmem_init[restore_issue ? {1'b0,restore_issue_addr} : cfg_q[15:6]];
    end

    // one write port, two producers: the LFO's accumulate and the
    // envelope's recurrence, both valid at A for the same instruction
    always_ff @(posedge clk)
        if (a_st_en)
            istate[a_st_pc] <= a_is_lfo ? a_lfo_next : adsr_state_out;

endmodule
`default_nettype wire
