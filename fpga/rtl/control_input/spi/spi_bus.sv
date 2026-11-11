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
module spi_bus #(
    parameter int   AW_BACKED = synth_pkg::MAP_AW_BACKED,  // 2048 words backed
    parameter [7:0] ID_BYTE   = synth_pkg::SPI_ID_BYTE
) (
    // ---- SPI side (master's clock) ----
    input wire logic        sclk,
    input wire logic        cs,          // active low
    input wire logic        mosi,
    output logic        miso,

    // ---- fabric side ----
    input wire logic        sysclk,
    input wire logic        rst_n,

    // ---- per-element parameter writes (sclk domain) ----------------
    // The memory map's 0x2000 + v*64 range, offsets 0..6 (OSC, DUTY,
    // FILTER, GAIN, GATE, PTRS0, PTRS1). Combinational decode, valid
    // exactly on the sclk edge that completes a data word — the
    // consumer writes its RAM on that same edge (there may be no
    // further edges: a master stops clocking after the last bit).
    // Offsets 7..63 are dropped for now; per-element read-back is TBD
    // (reads in this range return zero).
    output logic        elem_write_enable,
    output logic [2:0]  elem_write_word,   // 0..6 = OSC..PTRS1 param RAM

    // ---- bus base writes (sclk domain, mailbox toward sysclk) ------
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

    // ---- instruction table writes (sclk domain, banked like params) ---
    // Instruction config is wiring: it rides the ping-pong banks and
    // takes effect at the swap, same as the per-element words.
    output logic        imem_write_enable,
    output logic [9:0]  imem_write_addr,   // {entry[7:0], word[1:0]} (#100)
    output logic [31:0] imem_write_data,
    output logic [7:0]  elem_write_index,
    output logic [31:0] elem_write_data,

    // CTRL@0x0002 bit 0: bank swap request. Toggle semantics
    // (sclk domain); the consumer syncs the toggle and flips its
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
    output wire [31:0] perf_write_data
);
    wire cfg_write,cfg_read_req_toggle;
    wire [15:0] cfg_addr,cfg_read_addr;
    wire [31:0] cfg_data,cfg_read_data;
    spi_transport #(.ID_BYTE(ID_BYTE)) u_transport(
        .sclk,.cs,.mosi,.miso,.cfg_write_o(cfg_write),.cfg_addr_o(cfg_addr),.cfg_data_o(cfg_data),
        .cfg_read_req_toggle_o(cfg_read_req_toggle),.cfg_read_addr_o(cfg_read_addr),.cfg_read_data_i(cfg_read_data));
    synth_control_registers #(.AW_BACKED(AW_BACKED),.ID_BYTE(ID_BYTE)) u_regs(
        .cfg_clk_i(sclk),.cfg_write_i(cfg_write),.cfg_addr_i(cfg_addr),.cfg_data_i(cfg_data),
        .cfg_read_req_toggle_i(cfg_read_req_toggle),.cfg_read_addr_i(cfg_read_addr),.cfg_read_data_o(cfg_read_data),
        .sysclk(sysclk),
        .rst_n(rst_n),
        .elem_write_enable(elem_write_enable),
        .elem_write_word(elem_write_word),
        .dmem_wr_addr(dmem_wr_addr),
        .dmem_wr_data(dmem_wr_data),
        .dmem_wr_toggle(dmem_wr_toggle),
        .dmem_wr_ack(dmem_wr_ack),
        .bank_active_status(bank_active_status),
        .control_pass_count(control_pass_count),
        .imem_write_enable(imem_write_enable),
        .imem_write_addr(imem_write_addr),
        .imem_write_data(imem_write_data),
        .elem_write_index(elem_write_index),
        .elem_write_data(elem_write_data),
        .swap_req(swap_req),
        .ks_write_enable(ks_write_enable),
        .ks_write_addr(ks_write_addr),
        .ks_write_data(ks_write_data),
        .ks_read_addr(ks_read_addr),
        .ks_read_data(ks_read_data),
        .fm_write_enable(fm_write_enable),
        .fm_write_addr(fm_write_addr),
        .fm_write_data(fm_write_data),
        .fm_read_addr(fm_read_addr),
        .fm_read_data(fm_read_data),
        .midi_write_enable(midi_write_enable),
        .midi_write_addr(midi_write_addr),
        .midi_read_addr(midi_read_addr),
        .midi_write_data(midi_write_data),
        .midi_read_data(midi_read_data),
        .perf_write_enable(perf_write_enable),
        .perf_write_addr(perf_write_addr),
        .perf_write_data(perf_write_data));
endmodule
`default_nettype wire
