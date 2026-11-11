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
// Framing only. The write tuple is combinational at the final received bit;
// no subsequent configuration clock edge is required by the consumers.
module spi_transport #(parameter [7:0] ID_BYTE=synth_pkg::SPI_ID_BYTE)(
    input wire sclk,cs,mosi,
    output wire miso,
    output wire cfg_write_o,
    output wire [15:0] cfg_addr_o,
    output wire [31:0] cfg_data_o,
    output wire cfg_read_req_toggle_o,
    output wire [15:0] cfg_read_addr_o,
    input wire [31:0] cfg_read_data_i
);
    logic [7:0] rx_shift;
    logic [2:0] bit_count;

    wire [7:0] rx_byte  = {rx_shift[6:0], mosi};
    wire       byte_end = (bit_count == 3'd7);

    initial begin
        rx_shift  = '0;
        bit_count = '0;
    end

    always_ff @(posedge sclk or posedge cs) begin
        if (cs) begin
            rx_shift  <= '0;
            bit_count <= '0;
        end else begin
            rx_shift  <= rx_byte;
            bit_count <= bit_count + 3'd1;
        end
    end

    //--------------------------------------------------------------------
    // Frame decode (sclk domain)
    //   phase 0 cmd · 1 addr-hi · 2 addr-lo · 3 read turnaround · 4 data
    //--------------------------------------------------------------------
    logic [2:0]  frame_phase;
    logic        is_read, auto_increment;
    logic [15:0] word_addr;
    logic        first_dummy_done;  // second turnaround byte pending
    logic [1:0]  data_byte_index;   // byte index within the current word
    logic [23:0] partial_word;      // first three data bytes of a word


    // fetch request: toggle + quasi-static address (settle-time CDC,
    // same pattern as spi_slave_regs' hardware-proven STATUS capture)
    logic        fetch_toggle;
    logic [15:0] fetch_addr;
    logic [31:0] stream_word;   // the word streaming out NOW — latched at
                                // each word boundary so prefetches into
                                // cfg_read_data_i cannot clip the tail bytes

    initial begin
        frame_phase      = '0;
        is_read          = 1'b0;
        auto_increment   = 1'b0;
        word_addr        = '0;
        first_dummy_done = 1'b0;
        data_byte_index  = '0;
        partial_word     = '0;
        fetch_toggle     = 1'b0;
        fetch_addr       = '0;
        stream_word      = '0;
    end

    always_ff @(posedge sclk or posedge cs) begin
        if (cs) begin
            frame_phase      <= '0;
            first_dummy_done <= 1'b0;
            data_byte_index  <= '0;
        end else if (byte_end) begin
            case (frame_phase)
                3'd0: begin                       // command
                    is_read        <= rx_byte[7];
                    auto_increment <= rx_byte[6];
                    frame_phase    <= 3'd1;
                end
                3'd1: begin                       // address high
                    word_addr[15:8] <= rx_byte;
                    frame_phase     <= 3'd2;
                end
                3'd2: begin                       // address low
                    word_addr[7:0]  <= rx_byte;
                    fetch_addr      <= {word_addr[15:8], rx_byte};
                    fetch_toggle    <= ~fetch_toggle;   // fetch word 0
                    data_byte_index <= '0;
                    first_dummy_done <= 1'b0;
                    frame_phase     <= is_read ? 3'd3 : 3'd4;
                end
                3'd3: begin                       // read turnaround (2 bytes)
                    first_dummy_done <= 1'b1;
                    if (first_dummy_done) begin
                        frame_phase <= 3'd4;
                        stream_word <= cfg_read_data_i;  // word 0, fetched
                                                      // >=2 byte-slots ago
                    end
                end
                3'd4: begin                       // data words
                    partial_word    <= {partial_word[15:0], rx_byte};
                    data_byte_index <= data_byte_index + 2'd1;
                    // prefetch the NEXT word two byte-slots before it
                    // streams (>=500 ns at 40 MHz against ~60 ns needed)
                    if (data_byte_index == 2'd1) begin
                        fetch_addr   <= auto_increment ? word_addr + 16'd1
                                                       : word_addr;
                        fetch_toggle <= ~fetch_toggle;
                    end
                    if (data_byte_index == 2'd3) begin
                        stream_word <= cfg_read_data_i;  // latch the
                                                      // prefetched next
                                                      // word; cfg_read_data_i
                                                      // is free to change
                                                      // under later
                                                      // prefetches
                        if (auto_increment)
                            word_addr <= word_addr + 16'd1;
                    end
                end
                default: ;
            endcase
        end
    end

    assign cfg_write_o=byte_end && frame_phase==3'd4 && data_byte_index==2'd3 && !is_read;
    assign cfg_addr_o=word_addr;
    assign cfg_data_o={partial_word,rx_byte};
    assign cfg_read_req_toggle_o=fetch_toggle;
    assign cfg_read_addr_o=fetch_addr;
    //--------------------------------------------------------------------
    // Transmit — falling edge.  frame_phase/data_byte_index have already
    // advanced at the RE8 before this FE8 load, so they index the slot
    // about to stream.
    //--------------------------------------------------------------------
    wire [7:0] tx_byte =
        (frame_phase == 3'd0)      ? ID_BYTE :
        (frame_phase != 3'd4)      ? 8'h00   :
        !is_read                   ? 8'h00   :
        (data_byte_index == 2'd0)  ? stream_word[31:24] :
        (data_byte_index == 2'd1)  ? stream_word[23:16] :
        (data_byte_index == 2'd2)  ? stream_word[15:8]  :
                                     stream_word[7:0];

    logic [7:0] tx_shift;
    initial tx_shift = ID_BYTE;

    always_ff @(negedge sclk or posedge cs) begin
        if (cs)
            tx_shift <= ID_BYTE;
        else if (bit_count == 3'd0)
            tx_shift <= tx_byte;
        else
            tx_shift <= {tx_shift[6:0], 1'b0};
    end

    assign miso = cs ? 1'b0 : tx_shift[7];

endmodule
`default_nettype wire
