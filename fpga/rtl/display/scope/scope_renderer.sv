`timescale 1ns/1ps
`default_nettype none

// 1280x720 / 1650x750 audio scope. No pixel framebuffer or per-line history scan.
// The compact 5x7 glyphs are derived from the frozen visualization baseline's
// src/visual/display/spectrum_display.sv; see verification/render/README.md.
//
// Bundled-data CDC: publish_meta_i and its feature bank must be held stable from
// before publish_toggle_i changes until the core observes the returned ack.
// Common reset restarts both toggle endpoints at zero; RAM need not be cleared.
//
// Six registered pixel stages. The RAM samples feature_rd_* at stage 1's edge,
// and stage 2 consumes its one-clock synchronous output. Thus the first column
// of every plot row is requested with its stage-1 coordinates before comparison;
// no previous-row cache, porch preload, or asynchronous RAM read is assumed.
module scope_renderer #(
    parameter integer COLS = 512,
    parameter integer SAMPLE_RATE_NUM = 91015625,
    parameter integer SAMPLE_RATE_DEN = 948
) (
    input  wire         clk_i,
    input  wire         rst_n_i,
    input  wire         publish_toggle_i,
    input  wire [95:0]  publish_meta_i,
    output wire         feature_rd_en_o,
    output wire [9:0]   feature_rd_addr_o,
    input  wire [35:0]  feature_rd_data_i,
    output logic        ack_toggle_o,
    output logic [23:0] rgb_o,
    output logic        de_o,
    output logic        hs_o,
    output logic        vs_o,
    output wire         frame_start_o,
    output logic        image_valid_o,
    output logic [95:0] active_meta_o,
    output logic [31:0] frame_count_o,
    output wire [10:0]  raw_x_o,
    output wire [9:0]   raw_y_o
);
    localparam logic [23:0] BACKGROUND = 24'h050c14;
    localparam logic [23:0] PLOT_BG = 24'h091522;
    localparam logic [23:0] GRID_MINOR = 24'h173244;
    localparam logic [23:0] GRID_MAJOR = 24'h294451;
    localparam logic [23:0] GRID_ZERO = 24'h426878;
    localparam logic [23:0] TRIGGER_MARK = 24'h8f6a38;
    localparam logic [23:0] LEFT_WAVE = 24'h54e9c5;
    localparam logic [23:0] RIGHT_WAVE = 24'hb69cff;
    localparam logic [23:0] TEXT = 24'he4eef4;
    localparam logic [23:0] MUTED_TEXT = 24'h99b5c7;

    localparam logic [3:0] TXT_NONE = 4'd0, TXT_TITLE = 4'd1,
        TXT_STATUS = 4'd2, TXT_SCALE = 4'd3, TXT_AMP = 4'd4,
        TXT_LEFT = 4'd5, TXT_RIGHT = 4'd6, TXT_TIME = 4'd7,
        TXT_FOOT = 4'd8, TXT_FS = 4'd9;

    // These functions are called only by localparams. All rate arithmetic and
    // decimal formatting runs at elaboration; the pixel path selects constants.
    // Use 64-bit unsigned intermediates: the largest supported parameter product
    // is 4096 * 1000 * 100 * (2**31-1), which fits without truncation.
    localparam logic [63:0] RATE_NUM = SAMPLE_RATE_NUM > 0
        ? {32'd0, SAMPLE_RATE_NUM[31:0]} : 64'd1;
    localparam logic [63:0] RATE_DEN = SAMPLE_RATE_DEN > 0
        ? {32'd0, SAMPLE_RATE_DEN[31:0]} : 64'd1;
    function automatic logic [63:0] rounded_time(
        input logic [63:0] samples, input logic [63:0] scale);
        rounded_time = (samples * 64'd1000 * scale * RATE_DEN + RATE_NUM / 64'd2) / RATE_NUM;
    endfunction

    // Left-aligned decimal plus spaces, with round-to-nearest, ties upwards.
    // The fixed nine-character word is sliced to the existing five/six-character
    // fields below. No string type, dynamic allocation, or run-time division.
    function automatic logic [71:0] decimal_word(
        input logic [63:0] value, input integer places);
        logic [63:0] scale, whole, divisor, remaining;
        logic [71:0] word;
        integer digits, position, i;
        begin
            scale = 64'd1;
            for (i = 0; i < places; i = i + 1) scale = scale * 64'd10;
            whole = value / scale;
            digits = 1;
            for (i = 0; i < 5; i = i + 1) begin
                if (whole >= 64'd10) begin
                    digits = digits + 1;
                    whole = whole / 64'd10;
                end
            end
            divisor = 64'd1;
            for (i = 0; i < digits + places - 1; i = i + 1) divisor = divisor * 64'd10;
            remaining = value;
            word = {9{8'h20}};
            for (position = 0; position < 9; position = position + 1) begin
                if (position == digits) word[71 - position*8 -: 8] = ".";
                else if (position < digits + places + 1) begin
                    word[71 - position*8 -: 8] = 8'(64'd48 + remaining / divisor);
                    remaining = remaining % divisor;
                    divisor = divisor / 64'd10;
                end
            end
            decimal_word = word;
        end
    endfunction
    function automatic logic [39:0] division_word(input logic [63:0] samples);
        begin
            // A carry after rounding also selects the wider integer format.
            if (rounded_time(samples, 64'd1000) < 64'd10000)
                division_word = 40'(decimal_word(rounded_time(samples, 64'd1000), 3) >> 32);
            else if (rounded_time(samples, 64'd100) < 64'd10000)
                division_word = 40'(decimal_word(rounded_time(samples, 64'd100), 2) >> 32);
            else division_word = 40'(decimal_word(rounded_time(samples, 64'd10), 1) >> 32);
        end
    endfunction
    function automatic logic [3071:0] axis_rom();
        logic [3071:0] words;
        integer tb, tick;
        begin
            words = {384{8'h20}};
            for (tb = 0; tb < 4; tb = tb + 1) begin
                for (tick = 0; tick < 9; tick = tick + 1) begin
                    words[(tb*16 + tick)*48 +: 48] = 48'(decimal_word(
                        rounded_time(64'(tick) * (64'd64 << tb), 64'd100), 2) >> 24);
                end
            end
            axis_rom = words;
        end
    endfunction
    localparam logic [159:0] DIVISION_ROM = {division_word(64'd512), division_word(64'd256),
                                            division_word(64'd128), division_word(64'd64)};
    localparam logic [3071:0] AXIS_ROM = axis_rom();
    localparam logic [63:0] SAMPLE_RATE_MILLIS = (RATE_NUM * 64'd1000 + RATE_DEN / 64'd2) / RATE_DEN;
    localparam logic [71:0] SAMPLE_RATE_WORD = decimal_word(SAMPLE_RATE_MILLIS, 3);
    localparam logic [63:0] MAX_AXIS_HUNDREDTHS = rounded_time(64'd4096, 64'd100);
    logic [10:0] raw_x;
    logic [9:0] raw_y;
    (* async_reg = "true" *) logic [2:0] request_sync;
    logic [8:0] threshold_y;
    wire in_left = raw_y >= 10'd112 && raw_y <= 10'd368;
    wire in_right = raw_y >= 10'd416 && raw_y <= 10'd672;
    wire in_plot = raw_x >= 11'd128 && raw_x <= 11'd1151 && (in_left || in_right);
    wire [10:0] plot_x = raw_x - 11'd128;
    wire [9:0] plot_y = raw_y - (in_right ? 10'd416 : 10'd112);
    assign raw_x_o = raw_x;
    assign raw_y_o = raw_y;
    assign frame_start_o = raw_x == 11'd0 && raw_y == 10'd720;
    assign feature_rd_en_o = rst_n_i && image_valid_o && in_plot;
    assign feature_rd_addr_o = {active_meta_o[56], plot_x[9:1]};

    // COLS is fixed by the physical raster contract. Smaller acquisition-only
    // simulations do not imply a different physical display geometry.
    // synthesis translate_off
    initial begin
        if (COLS != 512) $fatal(1, "scope_renderer physical raster requires COLS=512");
        if (SAMPLE_RATE_NUM <= 0 || SAMPLE_RATE_DEN <= 0)
            $fatal(1, "scope_renderer sample-rate parameters must be positive signed 32-bit integers");
        // Six characters admit 999.99 ms at tick 8/tb 3; the nine-character
        // footer admits 99999.999 sample/s. Fail rather than silently clip text.
        if (MAX_AXIS_HUNDREDTHS > 64'd99999 || SAMPLE_RATE_MILLIS > 64'd99999999)
            $fatal(1, "scope_renderer sample rate exceeds fixed axis/footer label width");
    end
    // synthesis translate_on

    function automatic logic [8:0] threshold_position(
        input logic signed [23:0] pcm, input logic [1:0] gain);
        logic signed [24:0] shifted, position;
        begin
            shifted = $signed({pcm[23], pcm}) >>> (5'd16 - {3'd0, gain});
            position = 25'sd128 - shifted;
            if (position < 0) threshold_position = 9'd0;
            else if (position > 25'sd256) threshold_position = 9'd256;
            else threshold_position = position[8:0];
        end
    endfunction

    always_ff @(posedge clk_i) begin
        if (!rst_n_i) begin
            raw_x <= 0;
            raw_y <= 0;
            request_sync <= 0;
            ack_toggle_o <= 0;
            active_meta_o <= 0;
            image_valid_o <= 0;
            threshold_y <= 9'd128;
            frame_count_o <= 0;
        end else begin
            request_sync <= {request_sync[1:0], publish_toggle_i};
            if (raw_x == 11'd1649) begin
                raw_x <= 0;
                if (raw_y == 10'd749) raw_y <= 0;
                else raw_y <= raw_y + 1'b1;
            end else raw_x <= raw_x + 1'b1;
            // Accept only at the raw vertical-blank start. All outgoing active
            // pixels have drained before this edge, including the RGB pipeline.
            if (frame_start_o) begin
                frame_count_o <= frame_count_o + 1'b1;
                if (request_sync[2] != ack_toggle_o) begin
                    active_meta_o <= publish_meta_i;
                    threshold_y <= threshold_position($signed(publish_meta_i[87:64]), publish_meta_i[51:50]);
                    image_valid_o <= 1;
                    ack_toggle_o <= request_sync[2];
                end
            end
        end
    end

    function automatic logic [7:0] hex_digit(input logic [3:0] digit);
        hex_digit = digit < 4'd10 ? 8'd48 + {4'd0, digit} : 8'd55 + {4'd0, digit};
    endfunction

    // Each word is a short constant; character selection precedes the glyph ROM
    // and pixel-bit selection in separate stages. No coordinate division.
    // Gowin may map the one-port constant glyph ROM to one BSRAM; there is no
    // multi-read-port font replication, and the final resource report is binding.
    function automatic logic [39:0] time_step(input logic [1:0] tb);
        case (tb)
            0: time_step = DIVISION_ROM[0 +: 40];
            1: time_step = DIVISION_ROM[40 +: 40];
            2: time_step = DIVISION_ROM[80 +: 40];
            default: time_step = DIVISION_ROM[120 +: 40];
        endcase
    endfunction
    function automatic logic [39:0] fs_range(input logic [1:0] gain);
        case (gain)
            0: fs_range = "1.000";
            1: fs_range = "0.500";
            2: fs_range = "0.250";
            default: fs_range = "0.125";
        endcase
    endfunction
    function automatic logic [55:0] amplitude_word(input logic [2:0] tick, input logic [1:0] gain);
        logic [55:0] full, half;
        begin
            case (gain)
                0: begin full = "+1.0000"; half = "+0.5000"; end
                1: begin full = "+0.5000"; half = "+0.2500"; end
                2: begin full = "+0.2500"; half = "+0.1250"; end
                default: begin full = "+0.1250"; half = "+0.0625"; end
            endcase
            case (tick)
                0: amplitude_word = full;
                1: amplitude_word = half;
                2: amplitude_word = " 0.0000";
                3: amplitude_word = {8'h2d, half[47:0]};
                default: amplitude_word = {8'h2d, full[47:0]};
            endcase
        end
    endfunction
    function automatic logic [47:0] time_word(input logic [3:0] tick, input logic [1:0] tb);
        // Milliseconds from window start, rounded to 0.01 ms at elaboration.
        // Every part-select below is constant; the runtime path is just a mux.
        case ({tb, tick})
            6'h00, 6'h10, 6'h20, 6'h30: time_word = AXIS_ROM[0*48 +: 48];
            6'h01: time_word = AXIS_ROM[1*48 +: 48];
            6'h02, 6'h11: time_word = AXIS_ROM[2*48 +: 48];
            6'h03: time_word = AXIS_ROM[3*48 +: 48];
            6'h04, 6'h12, 6'h21: time_word = AXIS_ROM[4*48 +: 48];
            6'h05: time_word = AXIS_ROM[5*48 +: 48];
            6'h06, 6'h13: time_word = AXIS_ROM[6*48 +: 48];
            6'h07: time_word = AXIS_ROM[7*48 +: 48];
            6'h08, 6'h14, 6'h22, 6'h31: time_word = AXIS_ROM[8*48 +: 48];
            6'h15: time_word = AXIS_ROM[21*48 +: 48];
            6'h16, 6'h23: time_word = AXIS_ROM[22*48 +: 48];
            6'h17: time_word = AXIS_ROM[23*48 +: 48];
            6'h18, 6'h24, 6'h32: time_word = AXIS_ROM[24*48 +: 48];
            6'h25: time_word = AXIS_ROM[37*48 +: 48];
            6'h26, 6'h33: time_word = AXIS_ROM[38*48 +: 48];
            6'h27: time_word = AXIS_ROM[39*48 +: 48];
            6'h28, 6'h34: time_word = AXIS_ROM[40*48 +: 48];
            6'h35: time_word = AXIS_ROM[53*48 +: 48];
            6'h36: time_word = AXIS_ROM[54*48 +: 48];
            6'h37: time_word = AXIS_ROM[55*48 +: 48];
            6'h38: time_word = AXIS_ROM[56*48 +: 48];
            default: time_word = "      ";
        endcase
    endfunction

    function automatic logic [7:0] label_character(
        input logic [3:0] id, input logic [5:0] column, input logic [3:0] tick,
        input logic [95:0] meta, input logic valid_image);
        logic [511:0] line;
        logic [47:0] mode_word, tick_word;
        logic [31:0] state_word, edge_word;
        logic [39:0] step_word, range_word;
        logic [55:0] amp_word;
        logic [3:0] hex_value;
        begin
            line = {64{8'h20}};
            mode_word = meta[55] ? "AUTO  " : "NORMAL";
            state_word = !valid_image ? "WAIT" : (!meta[55] ? "HOLD" : (meta[52] ? "TRIG" : "FREE"));
            edge_word = meta[53] ? "FALL" : "RISE";
            step_word = time_step(meta[49:48]);
            range_word = fs_range(meta[51:50]);
            amp_word = amplitude_word(tick[2:0], meta[51:50]);
            tick_word = time_word(tick, meta[49:48]);
            hex_value = 0;
            label_character = 8'h20;
            case (id)
                TXT_TITLE: begin
                    line[511 -: 232] = "AUDIO WAVEFORM    DIGITAL PCM";
                    label_character = line[511 - {column, 3'b000} -: 8];
                end
                TXT_STATUS: begin
                    line[511 -: 48] = mode_word;
                    line[455 -: 32] = state_word; // column 7
                    line[415 -: 32] = edge_word;  // column 12
                    line[375 -: 8] = meta[54] ? "R" : "L"; // column 17
                    line[351 -: 48] = "THR 0X";   // column 20
                    line[247 -: 24] = "PCM";      // column 33
                    line[199 -: 48] = "CAP 0X";   // column 39
                    label_character = line[511 - {column, 3'b000} -: 8];
                    if (!valid_image) begin
                        line = {64{8'h20}};
                        line[511 -: 176] = "WAIT FOR FIRST CAPTURE";
                        label_character = line[511 - {column, 3'b000} -: 8];
                    end else if (column >= 26 && column <= 31) begin
                        hex_value = meta[87 - (column - 6'd26)*4 -: 4];
                        label_character = hex_digit(hex_value);
                    end else if (column >= 45 && column <= 48) begin
                        hex_value = meta[47 - (column - 6'd45)*4 -: 4];
                        label_character = hex_digit(hex_value);
                    end
                end
                TXT_SCALE: begin
                    line[511 -: 40] = "TIME ";
                    line[471 -: 40] = step_word;
                    line[431 -: 56] = " MS/DIV";
                    line[351 -: 48] = "RANGE ";
                    line[303 -: 24] = "+/-";
                    line[279 -: 40] = range_word;
                    line[239 -: 24] = " FS";
                    line[191 -: 56] = meta[49:48] == 0 ? "POINTS " : "MINMAX ";
                    line[135 -: 8] = 8'd49 + ((8'd1 << meta[49:48]) - 8'd1);
                    line[127 -: 32] = "/COL";
                    label_character = line[511 - {column, 3'b000} -: 8];
                end
                TXT_AMP: if (column < 7) label_character = amp_word[55 - {column[2:0], 3'b000} -: 8];
                TXT_LEFT: begin line[511 -: 32] = "LEFT"; label_character = line[511 - {column, 3'b000} -: 8]; end
                TXT_RIGHT: begin line[511 -: 40] = "RIGHT"; label_character = line[511 - {column, 3'b000} -: 8]; end
                TXT_TIME: if (column < 6) label_character = tick_word[47 - {column[2:0], 3'b000} -: 8];
                TXT_FOOT: begin
                    line[511 -: 352] = {"MS FROM WINDOW START   FS=", SAMPLE_RATE_WORD, " SAMPLE/S"};
                    label_character = line[511 - {column, 3'b000} -: 8];
                end
                TXT_FS: begin line[511 -: 16] = "FS"; label_character = line[511 - {column, 3'b000} -: 8]; end
                default: label_character = 8'h20;
            endcase
        end
    endfunction

    // Font row order is top to bottom, each row left to right. The five-bit
    // columns occupy a 8x8 cell, expanded 2x to a 16x16 display cell.
    function automatic logic [34:0] glyph(input logic [7:0] character);
        case (character)
            "0": glyph = 35'b01110_10001_10011_10101_11001_10001_01110;
            "1": glyph = 35'b00100_01100_00100_00100_00100_00100_01110;
            "2": glyph = 35'b01110_10001_00001_00010_00100_01000_11111;
            "3": glyph = 35'b11110_00001_00001_01110_00001_00001_11110;
            "4": glyph = 35'b00010_00110_01010_10010_11111_00010_00010;
            "5": glyph = 35'b11111_10000_10000_11110_00001_00001_11110;
            "6": glyph = 35'b01110_10000_10000_11110_10001_10001_01110;
            "7": glyph = 35'b11111_00001_00010_00100_01000_01000_01000;
            "8": glyph = 35'b01110_10001_10001_01110_10001_10001_01110;
            "9": glyph = 35'b01110_10001_10001_01111_00001_00001_01110;
            "A": glyph = 35'b01110_10001_10001_11111_10001_10001_10001;
            "B": glyph = 35'b11110_10001_10001_11110_10001_10001_11110;
            "C": glyph = 35'b01111_10000_10000_10000_10000_10000_01111;
            "D": glyph = 35'b11110_10001_10001_10001_10001_10001_11110;
            "E": glyph = 35'b11111_10000_10000_11110_10000_10000_11111;
            "F": glyph = 35'b11111_10000_10000_11110_10000_10000_10000;
            "G": glyph = 35'b01111_10000_10000_10111_10001_10001_01111;
            "H": glyph = 35'b10001_10001_10001_11111_10001_10001_10001;
            "I": glyph = 35'b11111_00100_00100_00100_00100_00100_11111;
            "L": glyph = 35'b10000_10000_10000_10000_10000_10000_11111;
            "M": glyph = 35'b10001_11011_10101_10101_10001_10001_10001;
            "N": glyph = 35'b10001_11001_11001_10101_10011_10011_10001;
            "O": glyph = 35'b01110_10001_10001_10001_10001_10001_01110;
            "P": glyph = 35'b11110_10001_10001_11110_10000_10000_10000;
            "R": glyph = 35'b11110_10001_10001_11110_10100_10010_10001;
            "S": glyph = 35'b01111_10000_10000_01110_00001_00001_11110;
            "T": glyph = 35'b11111_00100_00100_00100_00100_00100_00100;
            "U": glyph = 35'b10001_10001_10001_10001_10001_10001_01110;
            "V": glyph = 35'b10001_10001_10001_10001_10001_01010_00100;
            "W": glyph = 35'b10001_10001_10001_10101_10101_10101_01010;
            "X": glyph = 35'b10001_10001_01010_00100_01010_10001_10001;
            "=": glyph = 35'b00000_00000_11111_00000_11111_00000_00000;
            "+": glyph = 35'b00000_00100_00100_11111_00100_00100_00000;
            "-": glyph = 35'b00000_00000_00000_11111_00000_00000_00000;
            "/": glyph = 35'b00001_00001_00010_00100_01000_10000_10000;
            ".": glyph = 35'b00000_00000_00000_00000_00000_00110_00110;
            default: glyph = 0;
        endcase
    endfunction

    // Do not use row*5 here: Gowin otherwise maps the small constant multiply
    // into a MULT12X12. The seven legal row starts require only a small decoder.
    function automatic logic [5:0] glyph_row_start(input logic [2:0] row);
        case (row)
            0: glyph_row_start = 6'd34;
            1: glyph_row_start = 6'd29;
            2: glyph_row_start = 6'd24;
            3: glyph_row_start = 6'd19;
            4: glyph_row_start = 6'd14;
            5: glyph_row_start = 6'd9;
            6: glyph_row_start = 6'd4;
            default: glyph_row_start = 0;
        endcase
    endfunction

    logic [3:0] text_id, text_tick;
    logic [5:0] text_column;
    logic [2:0] text_row, text_glyph_column;
    logic [10:0] text_dx;
    logic [9:0] text_dy;
    wire unused_coordinate_bits = &{1'b0, text_dx[10], text_dx[0], text_dy[9:4], text_dy[0]};
    logic text_valid;
    // All text cells have a power-of-two pitch. Layout chooses a short label and
    // cell coordinates here; dynamic text and glyph selection are downstream.
    always_comb begin
        text_id = TXT_NONE;
        text_tick = 0;
        text_dx = 0;
        text_dy = 0;
        if (raw_x >= 128 && raw_x < 1152) begin
            text_dx = raw_x - 11'd128;
            if (raw_y >= 24 && raw_y < 38) begin text_id = TXT_TITLE; text_dy = raw_y - 10'd24; end
            else if (raw_y >= 56 && raw_y < 70) begin text_id = TXT_STATUS; text_dy = raw_y - 10'd56; end
            else if (raw_y >= 80 && raw_y < 94) begin text_id = TXT_SCALE; text_dy = raw_y - 10'd80; end
            else if (raw_y >= 704 && raw_y < 718) begin text_id = TXT_FOOT; text_dy = raw_y - 10'd704; end
        end
        if (raw_x >= 8 && raw_x < 120) begin
            text_dx = raw_x - 11'd8;
            if (raw_y >= 108 && raw_y < 122) begin text_id = TXT_AMP; text_tick = 0; text_dy = raw_y - 10'd108; end
            else if (raw_y >= 172 && raw_y < 186) begin text_id = TXT_AMP; text_tick = 1; text_dy = raw_y - 10'd172; end
            else if (raw_y >= 236 && raw_y < 250) begin text_id = TXT_AMP; text_tick = 2; text_dy = raw_y - 10'd236; end
            else if (raw_y >= 300 && raw_y < 314) begin text_id = TXT_AMP; text_tick = 3; text_dy = raw_y - 10'd300; end
            else if (raw_y >= 364 && raw_y < 378) begin text_id = TXT_AMP; text_tick = 4; text_dy = raw_y - 10'd364; end
            else if (raw_y >= 412 && raw_y < 426) begin text_id = TXT_AMP; text_tick = 0; text_dy = raw_y - 10'd412; end
            else if (raw_y >= 476 && raw_y < 490) begin text_id = TXT_AMP; text_tick = 1; text_dy = raw_y - 10'd476; end
            else if (raw_y >= 540 && raw_y < 554) begin text_id = TXT_AMP; text_tick = 2; text_dy = raw_y - 10'd540; end
            else if (raw_y >= 604 && raw_y < 618) begin text_id = TXT_AMP; text_tick = 3; text_dy = raw_y - 10'd604; end
            else if (raw_y >= 668 && raw_y < 682) begin text_id = TXT_AMP; text_tick = 4; text_dy = raw_y - 10'd668; end
        end
        if (raw_x >= 1168 && raw_x < 1264) begin
            if (raw_y >= 116 && raw_y < 130) begin text_id = TXT_LEFT; text_dx = raw_x - 11'd1168; text_dy = raw_y - 10'd116; end
            else if (raw_y >= 420 && raw_y < 434) begin text_id = TXT_RIGHT; text_dx = raw_x - 11'd1168; text_dy = raw_y - 10'd420; end
        end
        if (raw_x >= 24 && raw_x < 56 && raw_y >= 80 && raw_y < 94) begin
            text_id = TXT_FS; text_dx = raw_x - 11'd24; text_dy = raw_y - 10'd80;
        end
        if (raw_x >= 128 && raw_x < 1248 && raw_y >= 688 && raw_y < 702) begin
            text_id = TXT_TIME;
            text_dx = {4'd0, plot_x[6:0]};
            text_tick = plot_x[10:7];
            text_dy = raw_y - 10'd688;
        end
        text_column = text_dx[9:4];
        text_glyph_column = text_dx[3:1];
        text_row = text_dy[3:1];
        text_valid = text_id != TXT_NONE && text_glyph_column < 5 && text_row < 7;
    end

    logic plot_s1, right_s1, valid_s1, minor_s1, major_s1, zero_s1, mark_s1;
    logic [8:0] plot_y_s1;
    logic [3:0] text_id_s1, text_tick_s1;
    logic [5:0] text_column_s1, glyph_bit_s1, glyph_bit_s2, glyph_bit_s3;
    logic text_valid_s1, text_valid_s2, text_valid_s3, text_valid_s4;
    logic muted_s1, muted_s2, muted_s3, muted_s4;
    logic wave_s2, right_s2, plot_s2, minor_s2, major_s2, zero_s2, mark_s2;
    logic [7:0] character_s2;
    logic [34:0] glyph_s3;
    logic text_on_s4;
    logic [23:0] rgb_s3, rgb_s4, rgb_s5;
    logic [2:0] timing_s1, timing_s2, timing_s3, timing_s4, timing_s5;
    wire [8:0] feature_min = right_s1 ? feature_rd_data_i[26:18] : feature_rd_data_i[8:0];
    wire [8:0] feature_max = right_s1 ? feature_rd_data_i[35:27] : feature_rd_data_i[17:9];

    always_ff @(posedge clk_i) begin
        if (!rst_n_i) begin
            plot_s1 <= 0; right_s1 <= 0; valid_s1 <= 0;
            minor_s1 <= 0; major_s1 <= 0; zero_s1 <= 0; mark_s1 <= 0; plot_y_s1 <= 0;
            text_id_s1 <= 0; text_tick_s1 <= 0; text_column_s1 <= 0;
            glyph_bit_s1 <= 0; glyph_bit_s2 <= 0; glyph_bit_s3 <= 0;
            text_valid_s1 <= 0; text_valid_s2 <= 0; text_valid_s3 <= 0; text_valid_s4 <= 0;
            muted_s1 <= 0; muted_s2 <= 0; muted_s3 <= 0; muted_s4 <= 0;
            wave_s2 <= 0; right_s2 <= 0; plot_s2 <= 0;
            minor_s2 <= 0; major_s2 <= 0; zero_s2 <= 0; mark_s2 <= 0;
            character_s2 <= 0; glyph_s3 <= 0; text_on_s4 <= 0;
            rgb_s3 <= 0; rgb_s4 <= 0; rgb_s5 <= 0;
            timing_s1 <= 0; timing_s2 <= 0; timing_s3 <= 0; timing_s4 <= 0; timing_s5 <= 0;
            rgb_o <= 0; de_o <= 0; hs_o <= 0; vs_o <= 0;
        end else begin
            // 1: request synchronous RAM and register the matching coordinates.
            plot_s1 <= in_plot; right_s1 <= in_right; valid_s1 <= image_valid_o;
            plot_y_s1 <= plot_y[8:0];
            minor_s1 <= plot_x[5:0] == 0 || plot_y[4:0] == 0;
            major_s1 <= plot_x[6:0] == 0 || plot_y[5:0] == 0 || raw_x == 1151;
            zero_s1 <= plot_y == 10'd128;
            mark_s1 <= image_valid_o && ((active_meta_o[52] && raw_x == 384)
                || (in_right == active_meta_o[54] && plot_y == {1'b0, threshold_y} && plot_x[4:0] < 16));
            text_id_s1 <= text_id; text_tick_s1 <= text_tick; text_column_s1 <= text_column;
            glyph_bit_s1 <= glyph_row_start(text_row) - {3'd0, text_glyph_column};
            text_valid_s1 <= text_valid;
            muted_s1 <= text_id != TXT_TITLE && text_id != TXT_STATUS;
            timing_s1 <= {(raw_x < 1280 && raw_y < 720),
                (raw_x >= 1390 && raw_x < 1430), (raw_y >= 725 && raw_y < 730)};
            // 2: consume RAM data after its single synchronous read clock.
            wave_s2 <= plot_s1 && valid_s1 && plot_y_s1 >= feature_min && plot_y_s1 <= feature_max;
            right_s2 <= right_s1; plot_s2 <= plot_s1;
            minor_s2 <= minor_s1; major_s2 <= major_s1; zero_s2 <= zero_s1; mark_s2 <= mark_s1;
            character_s2 <= label_character(text_id_s1, text_column_s1, text_tick_s1, active_meta_o, image_valid_o);
            glyph_bit_s2 <= glyph_bit_s1; text_valid_s2 <= text_valid_s1; muted_s2 <= muted_s1;
            timing_s2 <= timing_s1;
            // 3: layer geometry and decode a small constant glyph.
            rgb_s3 <= !plot_s2 ? BACKGROUND : (wave_s2 ? (right_s2 ? RIGHT_WAVE : LEFT_WAVE)
                : (mark_s2 ? TRIGGER_MARK : (zero_s2 ? GRID_ZERO : (major_s2 ? GRID_MAJOR : (minor_s2 ? GRID_MINOR : PLOT_BG)))));
            glyph_s3 <= glyph(character_s2); glyph_bit_s3 <= glyph_bit_s2;
            text_valid_s3 <= text_valid_s2; muted_s3 <= muted_s2; timing_s3 <= timing_s2;
            // 4: bit selection, 5: text overlay, 6: uniform RGB/timing output.
            text_on_s4 <= text_valid_s3 && glyph_s3[glyph_bit_s3];
            text_valid_s4 <= text_valid_s3; muted_s4 <= muted_s3;
            rgb_s4 <= rgb_s3; timing_s4 <= timing_s3;
            rgb_s5 <= text_valid_s4 && text_on_s4 ? (muted_s4 ? MUTED_TEXT : TEXT) : rgb_s4;
            timing_s5 <= timing_s4;
            {de_o, hs_o, vs_o} <= timing_s5;
            rgb_o <= timing_s5[2] ? rgb_s5 : 24'd0;
        end
    end
endmodule

`default_nettype wire
