// Copyright (c) 2026 ICECREAM1230
//------------------------------------------------------------------------
// spi_bus.sv — the memory-map wire protocol (docs/memory_map.md)
//
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
//
// Wire format (SPI Mode 0, MSB first — one CS-framed transaction):
//
//   byte 0      command: [7] 1 = read, 0 = write
//                        [6] 1 = auto-increment address per word
//                        [5:0] reserved (ignored)
//   bytes 1..2  16-bit word address, MSB first
//   WRITE:  bytes 3..          32-bit data words, MSB first, streaming
//   READ:   bytes 3..4         two dummy bytes (turnaround)
//           bytes 5..          32-bit data words, MSB first, streaming
//
//   MISO: byte 0 = ID_BYTE (link check on every frame); address and
//   dummy slots = 0x00; read data slots = the addressed words.
//
// Why the read turnaround exists: the store's read port is in the
// sysclk domain (dual-clock semi dual-port BSRAM — true dual-port does
// not map on this toolchain, measured), so a fetch costs ~60 ns of
// synchronizer + BSRAM read. The first data byte's shift register loads
// HALF a bit period after the address completes — 12.5 ns at the
// measured 40 MHz SPI ceiling — so without turnaround the first word
// could never be fresh. Two dummy bytes give 400 ns. Burst words
// prefetch two byte-slots early for the same margin. (The very first
// firmware header specified "cmd + 2 dummy + N data"; it was right.)
//
// Wire format ≠ storage geometry: 16-bit addresses / 32-bit words on
// the wire; BACKED_WORDS × 36-bit BSRAM behind it (11-bit space today).
// Beyond the backed window: writes are dropped, reads return zero — no
// aliasing.
//
// Edge discipline as proven in spi_slave_regs.sv: MOSI sampled and
// decoded on the RISING edge, MISO loaded on the FALLING edge,
// posedge-CS async framing, power-up state declared.
//------------------------------------------------------------------------
`default_nettype none
module synth_control_registers #(
    parameter int   AW_BACKED = synth_pkg::MAP_AW_BACKED,  // 2048 words backed
    parameter [7:0] ID_BYTE   = synth_pkg::SPI_ID_BYTE
) (
    // ---- SPI side (master's clock) ----
    input wire logic        cfg_clk_i,

    // ---- fabric side ----
    input wire logic        sysclk,
    input wire logic        rst_n,

    // ---- per-element parameter writes (cfg_clk_i domain) ----------------
    // The memory map's 0x2000 + v*64 range, offsets 0..6 (OSC, DUTY,
    // FILTER, GAIN, GATE, PTRS0, PTRS1). Combinational decode, valid
    // exactly on the cfg_clk_i edge that completes a data word — the
    // consumer writes its RAM on that same edge (there may be no
    // further edges: a master stops clocking after the last bit).
    // Offsets 7..63 are dropped for now; per-element read-back is TBD
    // (reads in this range return zero).
    output logic        elem_write_enable,
    output logic [2:0]  elem_write_word,   // 0..6 = OSC..PTRS1 param RAM

    // ---- bus base writes (cfg_clk_i domain, mailbox toward sysclk) ------
    // A request/ack toggle handshake holds each base payload stable until
    // both bus generations commit. SPI has no clock-stretching mechanism:
    // a burst word received while busy is rejected and counted at STATUS.
    // The caller must fence/retry; a 32-bit burst interval cannot be assumed
    // safe merely because the default instruction table has only 96 writes.
    output logic [9:0]  dmem_wr_addr,
    output logic [17:0] dmem_wr_data,
    output logic        dmem_wr_toggle,  // toggles once per bus write
    input  wire         dmem_wr_ack,     // clk-domain, both generations committed
    input  wire         bank_active_status,
    input  wire [15:0]  control_pass_count,

    // ---- instruction table writes (cfg_clk_i domain, banked like params) ---
    // Instruction config is wiring: it rides the ping-pong banks and
    // takes effect at the swap, same as the per-element words.
    output logic        imem_write_enable,
    output logic [9:0]  imem_write_addr,   // {entry[7:0], word[1:0]} (#100)
    output logic [31:0] imem_write_data,
    output logic [7:0]  elem_write_index,
    output logic [31:0] elem_write_data,

    // CTRL@0x0002 bit 0: bank swap request. Toggle semantics
    // (cfg_clk_i domain); the consumer syncs the toggle and flips its
    // active bank at the next drum slot 512. The write also lands in
    // the plain window RAM, so reading CTRL back shows the last value.
    output logic        swap_req,

    // KS extension: committing-edge decode and sysclk-side readback.
    output wire         ks_write_enable,
    output wire [7:0]   ks_write_addr,
    output wire [31:0]  ks_write_data,
    output wire [7:0]   ks_read_addr,
    input  wire [31:0]  ks_read_data,
    // FM ABI word window: captured on the final SPI bit, same as KS.
    output wire         fm_write_enable,
    output wire [7:0]   fm_write_addr,
    output wire [31:0]  fm_write_data,
    output wire [7:0]   fm_read_addr,
    input  wire [31:0]  fm_read_data,
    // Raw MIDI mailbox and patch-template extensions; existing wire framing.
    output wire midi_write_enable,
    output wire [7:0] midi_write_addr, midi_read_addr,
    output wire [31:0] midi_write_data,
    input wire [31:0] midi_read_data,
    output wire perf_write_enable,
    output wire [15:0] perf_write_addr,
    output wire [31:0] perf_write_data,
    input wire cfg_write_i,
    input wire [15:0] cfg_addr_i,
    input wire [31:0] cfg_data_i,
    input wire cfg_read_req_toggle_i,
    input wire [15:0] cfg_read_addr_i,
    output wire [31:0] cfg_read_data_o
);

    localparam int NWORDS = 1 << AW_BACKED;
    // Read-only fence register. [0] active bank, [1] swap pending,
    // [2] bus mailbox busy, [3] bus write rejected since reset,
    // [15:8] rejected-write sequence, [31:16] completed control passes.
    localparam logic [15:0] CSP_STATUS_ADDR = 16'h0003;


    wire sclk=cfg_clk_i;
    wire [15:0] word_addr=cfg_addr_i,fetch_addr=cfg_read_addr_i;
    wire [7:0] rx_byte=cfg_data_i[7:0];
    wire [23:0] partial_word=cfg_data_i[31:8];
    wire fetch_toggle=cfg_read_req_toggle_i;
    logic [31:0] fetch_result;
    assign cfg_read_data_o=fetch_result;
    wire addr_in_backed=(word_addr[15:AW_BACKED]=='0);
    logic [35:0] store_ram [0:NWORDS-1];

    integer i0;
    initial begin
        for (i0 = 0; i0 < NWORDS/2; i0 = i0 + 1) store_ram[i0] = '0;
        for (i0 = NWORDS/2; i0 < NWORDS; i0 = i0 + 1) store_ram[i0] = '0;
    end

    wire store_write_enable = cfg_write_i && addr_in_backed
                              && (word_addr[15:8] != 8'h06)
                              && (word_addr[15:8] != 8'h07);

    // ---- per-element write decode (MAP_ELEM_BASE + 256×64 words,
    //      offsets 0..6) -------------------------------------------
    localparam [15:0] ELEM_BASE = synth_pkg::MAP_ELEM_BASE;
    localparam [15:0] ELEM_END  = synth_pkg::MAP_ELEM_BASE
                                + 16'(synth_pkg::NUM_ELEMENTS
                                      * synth_pkg::MAP_ELEM_STRIDE);
    wire        in_elem_range = (word_addr >= ELEM_BASE)
                             && (word_addr <  ELEM_END);
    wire [15:0] elem_rel_addr = word_addr - ELEM_BASE; // 0..0x3FFF in range
    wire [13:0] elem_offset   = elem_rel_addr[13:0];
    assign elem_write_enable = cfg_write_i && in_elem_range
                               && (elem_offset[5:3] == 3'd0)
                               && (elem_offset[2:0] < 3'd7);
    assign elem_write_word   = elem_offset[2:0];

    // ---- instruction table write decode (0x0100..0x04FF, #100) --------
    localparam [15:0] PROD_BASE = synth_pkg::MAP_IMEM_BASE;
    localparam [15:0] PROD_END  = synth_pkg::MAP_IMEM_BASE
                                + 16'(4 * synth_pkg::NUM_INSTR);
    // All FOUR words per entry are writable since #145: word 3 carries the
    // second ADSR rate word (kD's high bits, kR and sustain). It used to be
    // excluded here even though the address map reserved the slot, so a write
    // to it was silently dropped -- which showed up as an envelope that would
    // not release, because kR stayed zero.
    wire in_instruction_range = (word_addr >= PROD_BASE)
                          && (word_addr <  PROD_END);
    assign imem_write_enable = cfg_write_i && in_instruction_range;
    // Region-relative offset — word_addr[8:0] alone is WRONG here: the
    // region starts at 0x0100, whose bit 8 is set, so a raw slice
    // lands writes 64 entries off.
    assign imem_write_addr = 10'(word_addr - synth_pkg::MAP_IMEM_BASE);
    assign imem_write_data = {partial_word, rx_byte};

    // ---- bus base write capture (see mailbox note at the ports) ----
    // Address 1023 remains the test-tone control latch even though #134
    // reduced the ordinary bus pool to 512 words.
    wire in_bus_range = ((word_addr >= synth_pkg::MAP_BUS_BASE)
                     && (word_addr < synth_pkg::MAP_BUS_BASE
                                     + 16'(synth_pkg::DMEM_WORDS)))
                     || (word_addr == synth_pkg::MAP_BUS_BASE + 16'd1023);
    logic dmem_ack_meta, dmem_ack_sync;
    logic [7:0] dmem_reject_count, dmem_reject_gray;
    wire dmem_sender_busy = (dmem_wr_toggle != dmem_ack_sync);
    always_ff @(posedge sclk or negedge rst_n) begin
        if (!rst_n) begin
            dmem_ack_meta <= 1'b0;
            dmem_ack_sync <= 1'b0;
        end else begin
            dmem_ack_meta <= dmem_wr_ack;
            dmem_ack_sync <= dmem_ack_meta;
        end
    end
    initial begin
        dmem_wr_toggle = 1'b0;
        dmem_wr_addr = '0;
        dmem_wr_data = '0;
        dmem_reject_count = '0;
        dmem_reject_gray = '0;
    end
    always_ff @(posedge sclk or negedge rst_n) begin
        if (!rst_n) begin
            dmem_wr_toggle <= 1'b0;
            dmem_wr_addr <= '0;
            dmem_wr_data <= '0;
            dmem_reject_count <= '0;
            dmem_reject_gray <= '0;
        end else
        if (cfg_write_i && in_bus_range) begin
            if (!dmem_sender_busy) begin
                // Hold this payload until the receiver acknowledges both
                // RAM generations. A faster burst can never overwrite it.
                dmem_wr_addr   <= word_addr[9:0];
                dmem_wr_data   <= {partial_word[9:0], rx_byte};
                dmem_wr_toggle <= ~dmem_wr_toggle;
            end else begin
                // SPI cannot stall the master's clock. Reject explicitly;
                // STATUS lets the caller retry the idempotent base write.
                dmem_reject_count <= dmem_reject_count + 8'd1;
                dmem_reject_gray <= ((dmem_reject_count + 8'd1) >> 1)
                                  ^ (dmem_reject_count + 8'd1);
            end
        end
    end
    assign elem_write_index = elem_offset[13:6];
    assign elem_write_data  = {partial_word, rx_byte};
    assign ks_write_enable = cfg_write_i
                             && (word_addr[15:8] == 8'h05);
    assign ks_write_addr = word_addr[7:0];
    assign ks_write_data = {partial_word, rx_byte};
    assign ks_read_addr = fetch_addr[7:0];
    assign fm_write_enable = cfg_write_i
                             && (word_addr[15:8] == 8'h06);
    assign fm_write_addr = word_addr[7:0];
    assign fm_write_data = {partial_word, rx_byte};
    assign fm_read_addr = fetch_addr[7:0];
    assign midi_write_enable = cfg_write_i && word_addr[15:8]==8'h07;
    assign midi_write_addr = word_addr[7:0];
    assign midi_read_addr = fetch_addr[7:0];
    assign midi_write_data = {partial_word,rx_byte};
    assign perf_write_enable = cfg_write_i && word_addr[15];
    assign perf_write_addr = word_addr;
    assign perf_write_data = {partial_word,rx_byte};


    always_ff @(posedge sclk) begin
        if (store_write_enable)
            store_ram[word_addr[AW_BACKED-1:0]] <= {4'b0, partial_word, rx_byte};
    end

    // CTRL decode: a write to 0x0002 with bit 0 set toggles swap_req.
    // Registered on the committing edge itself — no later edge is
    // guaranteed (framing rule).
    initial swap_req = 1'b0;
    always_ff @(posedge sclk or negedge rst_n) begin
        if (!rst_n)
            swap_req <= 1'b0;
        else if (store_write_enable && (word_addr == synth_pkg::MAP_CTRL_ADDR)
            && rx_byte[0])
            swap_req <= ~swap_req;
    end

    //--------------------------------------------------------------------
    // Fetch service (sysclk domain)
    //--------------------------------------------------------------------
    logic fetch_toggle_meta, fetch_toggle_sync, fetch_toggle_prev;
    logic        fetch_pending;
    logic [35:0] store_read_data;
    logic req_meta, req_sync, swap_meta, swap_sync;
    logic [7:0] reject_gray_meta, reject_gray_sync;
    logic reject_sticky;
    logic [7:0] reject_sequence;
    always_comb begin
        reject_sequence[7] = reject_gray_sync[7];
        for (int gi = 6; gi >= 0; gi = gi - 1)
            reject_sequence[gi] = reject_sequence[gi+1] ^ reject_gray_sync[gi];
    end
    always_ff @(posedge sysclk or negedge rst_n) begin
        if (!rst_n) begin
            req_meta <= 1'b0; req_sync <= 1'b0;
            swap_meta <= 1'b0; swap_sync <= 1'b0;
            reject_gray_meta <= '0; reject_gray_sync <= '0;
            reject_sticky <= 1'b0;
        end else begin
            req_meta <= dmem_wr_toggle; req_sync <= req_meta;
            swap_meta <= swap_req; swap_sync <= swap_meta;
            reject_gray_meta <= dmem_reject_gray;
            reject_gray_sync <= reject_gray_meta;
            if (reject_gray_sync != 8'd0) reject_sticky <= 1'b1;
        end
    end
    wire [31:0] csp_status = {control_pass_count, reject_sequence, 4'd0,
        reject_sticky, (req_sync != dmem_wr_ack),
        (swap_sync != bank_active_status), bank_active_status};

    initial begin
        {fetch_toggle_meta, fetch_toggle_sync, fetch_toggle_prev} = '0;
        fetch_pending   = 1'b0;
        store_read_data = '0;
        fetch_result    = '0;
    end

    always_ff @(posedge sysclk) begin
        fetch_toggle_meta <= fetch_toggle;
        fetch_toggle_sync <= fetch_toggle_meta;
        fetch_toggle_prev <= fetch_toggle_sync;

        store_read_data <= store_ram[fetch_addr[AW_BACKED-1:0]]; // BSRAM sync read
        fetch_pending   <= (fetch_toggle_sync != fetch_toggle_prev);
        if (fetch_pending)
            fetch_result <= (fetch_addr == CSP_STATUS_ADDR) ? csp_status
                          : (fetch_addr[15:8] == 8'h05) ? ks_read_data
                          : (fetch_addr[15:8] == 8'h06) ? fm_read_data
                          : (fetch_addr[15:8] == 8'h07) ? midi_read_data
                          : (fetch_addr[15:AW_BACKED] == '0)
                          ? store_read_data[31:0] : 32'd0;
    end

endmodule
`default_nettype wire
