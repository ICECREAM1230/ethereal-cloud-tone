`timescale 1ns/1ps
`default_nettype none

// 720p60 raster spectrum. Only per-bin values are retained; no pixel framebuffer.
// Bundled-data CDC: hold levels_i from before request_toggle_i changes until
// ack_toggle_o has returned through the source-domain synchronizer. Both ends
// must restart their toggle protocol at zero following a common reset.
module spectrum_display #(
    parameter bit SPLIT_VIEW = 1'b0,
    parameter integer SAMPLE_RATE_NUM = 91015625,
    parameter integer SAMPLE_RATE_DEN = 948,
    parameter bit PARTICLES_ENABLE = 1'b1
) (
    input  wire          clk_i,
    input  wire          rst_n_i,
    input  wire [2559:0]  levels_i,
    input  wire          request_toggle_i,
    output logic         ack_toggle_o,
    output logic [23:0]  rgb_o,
    output logic         de_o,
    output logic         hs_o,
    output logic         vs_o,
    output logic         frame_tick_o,
    output wire [10:0]    raster_x_o,
    output wire [9:0]     raster_y_o
);
    localparam logic [10:0] H_LAST = 11'd1649;
    localparam logic [9:0] V_LAST = 10'd749;
    localparam logic [5:0] PEAK_HOLD_FRAMES = 6'd30;
    // Six pixel registers; all timing outputs have the same latency as RGB.
    localparam integer PIPELINE_LATENCY = 6;
    localparam integer PLOT_X = SPLIT_VIEW ? 640 : 128;
    localparam integer PLOT_WIDTH = SPLIT_VIEW ? 512 : 1024;
    // Display a fixed 0..10 kHz range. The source still publishes natural
    // N=1024 FFT bins; each display column samples the nearest native bin to
    // its centre. All frequency arithmetic is elaboration-time only.
    localparam logic [63:0] VISIBLE_MAX_HZ = 64'd10000;
    localparam logic [63:0] RATE_NUM = SAMPLE_RATE_NUM > 0
        ? {32'd0, SAMPLE_RATE_NUM[31:0]} : 64'd1;
    localparam logic [63:0] RATE_DEN = SAMPLE_RATE_DEN > 0
        ? {32'd0, SAMPLE_RATE_DEN[31:0]} : 64'd1;
    localparam logic [63:0] RANGE_LAST_BIN = VISIBLE_MAX_HZ * 64'd1024 * RATE_DEN / RATE_NUM;
    localparam logic [63:0] VISIBLE_LAST_BIN = RANGE_LAST_BIN < 64'd255 ? RANGE_LAST_BIN : 64'd255;
    function automatic [47:0] axis_label(input integer quarter);
        logic [63:0] hz;
        begin
            hz = (VISIBLE_MAX_HZ * 64'(quarter) + 64'd2) / 64'd4;
            axis_label = {hz >= 64'd10000 ? 8'(8'd48 + hz / 64'd10000 % 64'd10) : 8'd32,
                8'(8'd48 + hz / 64'd1000 % 64'd10), 8'd46,
                8'(8'd48 + hz / 64'd100 % 64'd10),
                8'(8'd48 + hz / 64'd10 % 64'd10), 8'(8'd48 + hz % 64'd10)};
        end
    endfunction
    localparam [47:0] AXIS_Q1 = axis_label(1);
    localparam [47:0] AXIS_Q2 = axis_label(2);
    localparam [47:0] AXIS_Q3 = axis_label(3);
    localparam [47:0] AXIS_Q4 = axis_label(4);

    localparam logic [23:0] BACKGROUND = 24'h070b14;
    localparam logic [23:0] PLOT_BACKGROUND = 24'h0c1422;
    localparam logic [23:0] GRID_COLOUR = 24'h1c2b40;
    localparam logic [23:0] AXIS_COLOUR = 24'h35465d;
    localparam logic [23:0] PEAK_COLOUR = 24'he6edf7;
    localparam logic [23:0] TITLE_COLOUR = 24'he6edf7;
    localparam logic [23:0] TEXT_COLOUR = 24'h95a8bd;

    // Spread the visible-light colour order across all displayed bins:
    // violet -> blue -> cyan -> green -> yellow -> orange -> red. For p=6*bin,
    // p[10:8] selects adjacent anchors and p[7:0]/256 interpolates between them.
    // Each channel changes by floor(abs(delta)*fraction/256), in its direction.
    // Shift/add/subtract only; no palette storage or runtime multiply/divide.
    function automatic logic [23:0] spectrum_colour(input logic [7:0] index);
        logic [10:0] position;
        logic [7:0] fraction, ramp_255, ramp_128, ramp_127;
        logic [15:0] product_255;
        logic [14:0] product_127;
        begin
            position = {1'b0,index,2'b00} + {2'b00,index,1'b0};
            fraction = position[7:0];
            product_255 = {fraction,8'd0} - {8'd0,fraction};
            product_127 = {fraction,7'd0} - {7'd0,fraction};
            ramp_255 = product_255[15:8];
            ramp_128 = {1'b0,fraction[7:1]};
            ramp_127 = {1'b0,product_127[14:8]};
            if (index == 8'd255) spectrum_colour = 24'hff0000;
            else case (position[10:8])
                0: spectrum_colour = {8'd128-ramp_128, 8'd0, 8'd255};
                1: spectrum_colour = {8'd0, ramp_255, 8'd255};
                2: spectrum_colour = {8'd0, 8'd255, 8'd255-ramp_255};
                3: spectrum_colour = {ramp_255, 8'd255, 8'd0};
                4: spectrum_colour = {8'd255, 8'd255-ramp_127, 8'd0};
                5: spectrum_colour = {8'd255, 8'd128-ramp_128, 8'd0};
                default: spectrum_colour = 24'hff0000;
            endcase
        end
    endfunction
`ifndef SYNTHESIS
    initial begin
        if (SAMPLE_RATE_NUM <= 0 || SAMPLE_RATE_DEN <= 0)
            $fatal(1, "Spectrum sample rate must be positive");
        if (RATE_NUM < VISIBLE_MAX_HZ * 64'd4 * RATE_DEN)
            $fatal(1, "Spectrum source bins do not cover the 10 kHz display range");
    end
`endif


    logic [10:0] raster_x;
    logic [9:0] raster_y;
    assign raster_x_o = raster_x;
    assign raster_y_o = raster_y;
    (* async_reg = "true" *) logic [2:0] request_sync;
    // Both tables use a single synchronous-read BRAM bank. Pixel reads and
    // animation reads are mutually exclusive in time, so no read replication
    // or unsupported read-before-write mode is needed on GW5AST.
    logic init_active;
    logic [7:0] init_index;
    wire [9:0] target_read_data;
    wire [31:0] meta_read_data;
    logic updating, accept_snapshot, accepted_toggle;
    logic [7:0] update_index;
    localparam logic [2:0] UPDATE_READ = 3'd0;
    localparam logic [2:0] UPDATE_STEP = 3'd1;
    localparam logic [2:0] UPDATE_BAR = 3'd2;
    localparam logic [2:0] UPDATE_COMMIT = 3'd3;
    localparam logic [2:0] UPDATE_FETCH = 3'd4;
    logic [2:0] update_phase;
    logic [9:0] target_group [0:15];
    wire vblank_start = (raster_x == 11'd0 && raster_y == 10'd720);

    logic [9:0] selected_target;
    logic [9:0] update_target, update_peak;
    logic [5:0] update_hold;
    logic [13:0] update_bar;
    logic [13:0] desired_q4, difference_q4, computed_step_q4;
    logic computed_rising;
    logic [13:0] step_q4, next_bar_q4;
    logic rising;
    logic [9:0] next_bar;
    logic [9:0] next_peak;
    logic [5:0] next_hold;

    // BSRAM reset does not clear its array. Clear both banks in parallel after
    // reset while the raster continues from (0,0). The 256 clocks finish long
    // before the first plot line, and do not postpone a frame or its ACK.
    always_ff @(posedge clk_i) begin
        if (!rst_n_i) begin
            init_active <= 1'b1;
            init_index <= 8'd0;
        end else if (init_active) begin
            if (init_index == 8'd255) init_active <= 1'b0;
            else init_index <= init_index + 1'b1;
        end
    end

    wire column_fetch = updating && update_phase == UPDATE_FETCH;
    wire column_commit = updating && update_phase == UPDATE_COMMIT;
    wire pixel_meta_read = rst_n_i && !init_active && !updating;
    wire target_read_enable = rst_n_i && !init_active && column_fetch;
    wire meta_read_enable = pixel_meta_read || target_read_enable;
    wire target_write_enable = rst_n_i && (init_active || (column_commit && accept_snapshot));
    wire meta_write_enable = rst_n_i && (init_active || column_commit);
    wire [7:0] table_write_address = init_active ? init_index : update_index;
    wire [7:0] meta_read_address = updating ? update_index : bin_index;
    wire [9:0] target_write_data = init_active ? 10'd0 : update_target;
    // Packed metadata: [29:16] fractional bar, [15:6] peak, [5:0] hold.
    wire [31:0] meta_write_data = init_active ? 32'd0
        : {2'b00, next_bar_q4, next_peak, next_hold};

    spectrum_display_ram #(.WIDTH(10)) u_target_ram (
        .clk_i(clk_i), .rd_en_i(target_read_enable), .rd_addr_i(update_index),
        .rd_data_o(target_read_data), .wr_en_i(target_write_enable),
        .wr_addr_i(table_write_address), .wr_data_i(target_write_data)
    );
    spectrum_display_ram #(.WIDTH(32)) u_meta_ram (
        .clk_i(clk_i), .rd_en_i(meta_read_enable), .rd_addr_i(meta_read_address),
        .rd_data_o(meta_read_data), .wr_en_i(meta_write_enable),
        .wr_addr_i(table_write_address), .wr_data_i(meta_write_data)
    );

    // The effects engine only observes committed column metadata. It never
    // stalls capture, changes easing/peaks, or participates in the CDC ACK.
    wire particle_hit;
    wire [23:0] particle_rgb;
    generate if (PARTICLES_ENABLE) begin : particles_enabled
        spectrum_particles #(.SPLIT_VIEW(SPLIT_VIEW)) u_particles (
            .clk_i(clk_i), .rst_n_i(rst_n_i), .frame_start_i(vblank_start),
            .sample_valid_i(column_commit), .sample_bin_i(update_index),
            .sample_height_i(next_bar), .sample_colour_i(spectrum_colour(update_index)),
            .sample_end_i(column_commit && update_index == 8'd255),
            .raster_x_i(raster_x), .raster_y_i(raster_y),
            .hit_o(particle_hit), .rgb_o(particle_rgb)
        );
    end else begin : particles_disabled
        assign particle_hit = 1'b0;
        assign particle_rgb = 24'd0;
    end endgenerate

    function automatic [7:0] saturating_add(input logic [7:0] base,
                                           input logic [7:0] glow);
        logic [8:0] sum;
        begin
            sum = {1'b0,base} + {1'b0,glow};
            saturating_add = sum[8] ? 8'hff : sum[7:0];
        end
    endfunction
    wire [23:0] particle_overlay = {
        saturating_add(rgb_s5[23:16],particle_rgb[23:16]),
        saturating_add(rgb_s5[15:8],particle_rgb[15:8]),
        saturating_add(rgb_s5[7:0],particle_rgb[7:0])};

    // Fractional easing preserves gradual release down to the last pixel.
    // Attack: 1/4 of the remaining distance/frame; release: 1/16, minimum 1/16 px.
    // Fresh snapshots retain the two registered 16-way selections. Held targets
    // and metadata are read in FETCH and consumed in READ, using the existing
    // five phases (1280 clocks), well within the 49500-clock vblank.
    always_comb begin
        selected_target = accept_snapshot ? target_group[update_index[7:4]] : target_read_data;
        if (selected_target > 10'd512) selected_target = 10'd512;
        desired_q4 = {update_target, 4'b0000};
        difference_q4 = 14'd0;
        computed_step_q4 = 14'd0;
        computed_rising = 0;
        if (desired_q4 > update_bar) begin
            difference_q4 = desired_q4 - update_bar;
            computed_step_q4 = difference_q4 >> 2;
            if (computed_step_q4 == 0) computed_step_q4 = 14'd1;
            computed_rising = 1;
        end else if (desired_q4 < update_bar) begin
            difference_q4 = update_bar - desired_q4;
            computed_step_q4 = difference_q4 >> 4;
            if (computed_step_q4 == 0) computed_step_q4 = 14'd1;
        end
        // Rounded up for presentation; all arithmetic remains bounded at 512.
        next_bar = next_bar_q4[13:4] + (|next_bar_q4[3:0] ? 10'd1 : 10'd0);
        next_peak = update_peak;
        next_hold = update_hold;
        if (next_bar >= update_peak) begin
            next_peak = next_bar;
            next_hold = PEAK_HOLD_FRAMES;
        end else if (update_hold != 0) begin
            next_hold = update_hold - 1'b1;
        end else if (update_peak > next_bar + 10'd2) begin
            next_peak = update_peak - 10'd2;
        end else begin
            next_peak = next_bar;
        end
    end

    genvar group_index;
    generate for (group_index = 0; group_index < 16; group_index = group_index + 1) begin : target_prefetch
        wire [7:0] group_offset = {1'b0, update_index[3:0], 3'b000}
            + {3'b000, update_index[3:0], 1'b0};
        wire [159:0] mapped_levels;
        for (genvar column = 0; column < 16; column = column + 1) begin : frequency_column
            localparam logic [63:0] DISPLAY_COLUMN = 64'(group_index * 16 + column);
            localparam logic [63:0] NEAREST_BIN =
                ((DISPLAY_COLUMN * 64'd2 + 64'd1) * VISIBLE_MAX_HZ * 64'd1024 * RATE_DEN
                 + RATE_NUM * 64'd256) / (RATE_NUM * 64'd512);
            localparam integer NATIVE_BIN = integer'(NEAREST_BIN < VISIBLE_LAST_BIN
                ? NEAREST_BIN : VISIBLE_LAST_BIN);
            // Static wiring duplicates low-frequency bins before the existing
            // 16:1 prefetch mux. The pixel read and six-stage raster are unchanged.
            assign mapped_levels[column * 10 +: 10] = levels_i[NATIVE_BIN * 10 +: 10];
        end
        always_ff @(posedge clk_i) begin
            if (!rst_n_i) target_group[group_index] <= 10'd0;
            else if (column_fetch && accept_snapshot)
                target_group[group_index] <= mapped_levels[group_offset +: 10];
        end
    end endgenerate

    always_ff @(posedge clk_i) begin
        if (!rst_n_i) begin
            raster_x <= 0;
            raster_y <= 0;
            request_sync <= 0;
            ack_toggle_o <= 0;
            updating <= 0;
            accept_snapshot <= 0;
            accepted_toggle <= 0;
            update_index <= 0;
            update_phase <= UPDATE_FETCH;
            update_target <= 0;
            update_bar <= 0;
            update_peak <= 0;
            update_hold <= 0;
            step_q4 <= 0;
            rising <= 0;
            next_bar_q4 <= 0;
        end else begin
            request_sync <= {request_sync[1:0], request_toggle_i};
            if (raster_x == H_LAST) begin
                raster_x <= 0;
                if (raster_y == V_LAST) raster_y <= 0;
                else raster_y <= raster_y + 1'b1;
            end else raster_x <= raster_x + 1'b1;

            if (vblank_start && !init_active) begin
                updating <= 1;
                update_index <= 0;
                update_phase <= UPDATE_FETCH;
                accept_snapshot <= (request_sync[2] != ack_toggle_o);
                accepted_toggle <= request_sync[2];
            end else if (updating) begin
                case (update_phase)
                    UPDATE_FETCH: update_phase <= UPDATE_READ;
                    UPDATE_READ: begin
                        update_target <= selected_target;
                        update_bar <= meta_read_data[29:16];
                        update_peak <= meta_read_data[15:6];
                        update_hold <= meta_read_data[5:0];
                        update_phase <= UPDATE_STEP;
                    end
                    UPDATE_STEP: begin
                        step_q4 <= computed_step_q4;
                        rising <= computed_rising;
                        update_phase <= UPDATE_BAR;
                    end
                    UPDATE_BAR: begin
                        next_bar_q4 <= rising ? update_bar + step_q4 : update_bar - step_q4;
                        update_phase <= UPDATE_COMMIT;
                    end
                    UPDATE_COMMIT: begin
                        update_phase <= UPDATE_FETCH;
                        if (update_index == 8'd255) begin
                            updating <= 0;
                            if (accept_snapshot) ack_toggle_o <= accepted_toggle;
                        end else update_index <= update_index + 1'b1;
                    end
                    default: update_phase <= UPDATE_FETCH;
                endcase
            end
        end
    end

    // Original compact 5x7 font, rows from top to bottom, bits from left to right.
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
            "H", "h": glyph = 35'b10001_10001_10001_11111_10001_10001_10001;
            "I": glyph = 35'b11111_00100_00100_00100_00100_00100_11111;
            "J": glyph = 35'b00111_00010_00010_00010_00010_10010_01100;
            "K", "k": glyph = 35'b10001_10010_10100_11000_10100_10010_10001;
            "L": glyph = 35'b10000_10000_10000_10000_10000_10000_11111;
            "M": glyph = 35'b10001_11011_10101_10101_10001_10001_10001;
            "N": glyph = 35'b10001_11001_11001_10101_10011_10011_10001;
            "O": glyph = 35'b01110_10001_10001_10001_10001_10001_01110;
            "P": glyph = 35'b11110_10001_10001_11110_10000_10000_10000;
            "Q": glyph = 35'b01110_10001_10001_10001_10101_10010_01101;
            "R": glyph = 35'b11110_10001_10001_11110_10100_10010_10001;
            "S": glyph = 35'b01111_10000_10000_01110_00001_00001_11110;
            "T": glyph = 35'b11111_00100_00100_00100_00100_00100_00100;
            "U": glyph = 35'b10001_10001_10001_10001_10001_10001_01110;
            "X": glyph = 35'b10001_10001_01010_00100_01010_10001_10001;
            "Y": glyph = 35'b10001_10001_01010_00100_00100_00100_00100;
            "Z", "z": glyph = 35'b11111_00001_00010_00100_01000_10000_11111;
            ".": glyph = 35'b00000_00000_00000_00000_00000_00110_00110;
            "-": glyph = 35'b00000_00000_00000_11111_00000_00000_00000;
            default: glyph = 0;
        endcase
    endfunction

    logic [23:0] pixel_rgb;
    logic [7:0] bin_index;
    logic [9:0] bar_height;
    logic [9:0] peak_height;
    logic [127:0] text_word;
    logic [10:0] text_x;
    logic [9:0] text_y;
    logic [4:0] text_length;
    logic text_large;
    logic [6:0] text_column;
    logic [2:0] text_row;
    logic text_valid;
    logic [10:0] x_s1;
    logic [9:0] y_s1;
    logic [23:0] colour_s1;
    // The BRAM's synchronous read is stage 1 itself, not an extra register
    // before stage 1. Its output is only meaningful for a pixel read.
    logic pixel_meta_valid_s1;
    wire [13:0] bar_s1 = pixel_meta_valid_s1 ? meta_read_data[29:16] : 14'd0;
    wire [9:0] peak_s1 = pixel_meta_valid_s1 ? meta_read_data[15:6] : 10'd0;
    logic [10:0] text_x_s1;
    logic [9:0] text_y_s1;
    logic [4:0] text_length_s1;
    logic text_large_s1, text_large_s2, text_large_s3, text_large_s4, text_large_s5;
    logic [127:0] text_word_s1, text_word_s2, text_word_s3;
    logic [6:0] text_column_s2;
    logic [2:0] text_row_s2, text_row_s3, glyph_column_s3;
    logic [3:0] character_index_s3;
    logic [7:0] character_s4;
    logic [5:0] glyph_bit_s4, glyph_bit_s5;
    logic [34:0] glyph_s5;
    logic text_valid_s2, text_valid_s3, text_valid_s4, text_valid_s5;
    logic [3:0] timing_s1, timing_s2, timing_s3, timing_s4, timing_s5;
    logic [23:0] rgb_s2, rgb_s3, rgb_s4, rgb_s5;

    // Stage 1 input: choose text layout and address column metadata. No glyph,
    // coordinate division, or bar comparison is chained onto these reads.
    always_comb begin
        bin_index = 8'((raster_x - 11'(PLOT_X)) >> (SPLIT_VIEW ? 1 : 2));
        text_word = 0;
        text_x = 0;
        text_y = 0;
        text_length = 0;
        text_large = 0;
        if (raster_y >= 44 && raster_y < 72) begin
            text_word = "FPGA SPECTRUM   "; text_x = 11'(PLOT_X); text_y = 44; text_length = 13; text_large = 1;
            if (SPLIT_VIEW && raster_x < 600) begin
                text_word = "LISSAJOUS XY    "; text_x = 64; text_length = 12;
            end
        end else if (raster_y >= 96 && raster_y < 110) begin
            text_word = "DBFS            "; text_x = 11'(PLOT_X-64); text_y = 96; text_length = 4;
        end else if (raster_x < 11'(PLOT_X-8)) begin
            text_x = 11'(PLOT_X-52); text_length = 3;
            if (raster_y >= 122 && raster_y < 136) begin
                text_word = "0               "; text_y = 122; text_x = 11'(PLOT_X-28); text_length = 1;
            end else if (raster_y >= 207 && raster_y < 221) begin
                text_word = "-12             "; text_y = 207;
            end else if (raster_y >= 293 && raster_y < 307) begin
                text_word = "-24             "; text_y = 293;
            end else if (raster_y >= 378 && raster_y < 392) begin
                text_word = "-36             "; text_y = 378;
            end else if (raster_y >= 463 && raster_y < 477) begin
                text_word = "-48             "; text_y = 463;
            end else if (raster_y >= 549 && raster_y < 563) begin
                text_word = "-60             "; text_y = 549;
            end else if (raster_y >= 634 && raster_y < 648) begin
                text_word = "-72             "; text_y = 634;
            end
        end else if (raster_y >= 658 && raster_y < 672) begin
            text_y = 658;
            if (raster_x < 11'(PLOT_X+PLOT_WIDTH/8)) begin
                text_word = "0               "; text_x = 11'(PLOT_X); text_length = 1;
            end else if (raster_x < 11'(PLOT_X+PLOT_WIDTH*3/8)) begin
                text_word = {AXIS_Q1, 80'd0}; text_x = 11'(PLOT_X+PLOT_WIDTH/4-36); text_length = 6;
            end else if (raster_x < 11'(PLOT_X+PLOT_WIDTH*5/8)) begin
                text_word = {AXIS_Q2, 80'd0}; text_x = 11'(PLOT_X+PLOT_WIDTH/2-36); text_length = 6;
            end else if (raster_x < 11'(PLOT_X+PLOT_WIDTH*7/8)) begin
                text_word = {AXIS_Q3, 80'd0}; text_x = 11'(PLOT_X+PLOT_WIDTH*3/4-36); text_length = 6;
            end else begin
                text_word = {AXIS_Q4, 80'd0}; text_x = 11'(PLOT_X+PLOT_WIDTH-60); text_length = 6;
            end
        end else if (raster_y >= 694 && raster_y < 708) begin
            text_word = "FREQUENCY kHz   "; text_x = SPLIT_VIEW ? 816 : 568; text_y = 694; text_length = 13;
        end
    end

    // Stage 2 input: render bars/grid using registered column reads, and compute
    // text-relative coordinates using the separately registered layout.
    always_comb begin
        bar_height = bar_s1[13:4] + (|bar_s1[3:0] ? 10'd1 : 10'd0);
        peak_height = peak_s1;
        pixel_rgb = BACKGROUND;
        if (SPLIT_VIEW && x_s1 >= 64 && x_s1 < 576 && y_s1 >= 128 && y_s1 < 640) begin
            pixel_rgb = PLOT_BACKGROUND;
            if (x_s1 == 64 || x_s1 == 192 || x_s1 == 448
                || y_s1 == 128 || y_s1 == 256 || y_s1 == 512) pixel_rgb = GRID_COLOUR;
            if (x_s1 == 320 || y_s1 == 383) pixel_rgb = AXIS_COLOUR;
        end
        if (SPLIT_VIEW && ((x_s1 == 576 && y_s1 >= 128 && y_s1 <= 640)
            || (y_s1 == 640 && x_s1 >= 64 && x_s1 <= 576))) pixel_rgb = AXIS_COLOUR;
        if (x_s1 >= 11'(PLOT_X) && x_s1 < 1152 && y_s1 >= 128 && y_s1 < 640) begin
            pixel_rgb = PLOT_BACKGROUND;
            if (x_s1 == 11'(PLOT_X) || x_s1 == 11'(PLOT_X+PLOT_WIDTH/4)
                || x_s1 == 11'(PLOT_X+PLOT_WIDTH/2) || x_s1 == 11'(PLOT_X+PLOT_WIDTH*3/4)
                || y_s1 == 128 || y_s1 == 213 || y_s1 == 299 || y_s1 == 384
                || y_s1 == 469 || y_s1 == 555) pixel_rgb = GRID_COLOUR;
            if (bar_height != 0 && y_s1 >= 10'd640 - bar_height) begin
                // A whole column has one colour: height changes only its mask.
                pixel_rgb = colour_s1;
            end
            if (peak_height != 0 && y_s1 == 10'd640 - peak_height)
                pixel_rgb = PEAK_COLOUR;
        end
        if ((y_s1 == 640 && x_s1 >= 11'(PLOT_X) && x_s1 <= 1152)
            || (x_s1 == 1152 && y_s1 >= 128 && y_s1 <= 640)) pixel_rgb = AXIS_COLOUR;
        text_column = 0; text_row = 0; text_valid = 0;
        if (text_length_s1 != 0 && x_s1 >= text_x_s1 && y_s1 >= text_y_s1
            && 12'(x_s1) < 12'(text_x_s1) + 12'(text_length_s1) * (text_large_s1 ? 12'd24 : 12'd12)
            && 11'(y_s1) < 11'(text_y_s1) + (text_large_s1 ? 11'd28 : 11'd14)) begin
            text_column = 7'((x_s1 - text_x_s1) >> (text_large_s1 ? 2 : 1));
            text_row = 3'((y_s1 - text_y_s1) >> (text_large_s1 ? 2 : 1));
            text_valid = 1;
        end
    end

    // Stages 1..6: layout/read, coordinates/base RGB, character cell, character
    // selection, glyph ROM, glyph bit/overlay. Timing is delayed alongside RGB.
    always_ff @(posedge clk_i) begin
        if (!rst_n_i) begin
            rgb_o <= 0; de_o <= 0; hs_o <= 0; vs_o <= 0; frame_tick_o <= 0;
            x_s1 <= 0; y_s1 <= 0; colour_s1 <= 0; pixel_meta_valid_s1 <= 0;
            text_x_s1 <= 0; text_y_s1 <= 0; text_length_s1 <= 0;
            text_word_s1 <= 0; text_word_s2 <= 0; text_word_s3 <= 0;
            text_large_s1 <= 0; text_large_s2 <= 0; text_large_s3 <= 0;
            text_large_s4 <= 0; text_large_s5 <= 0;
            text_column_s2 <= 0; text_row_s2 <= 0; text_row_s3 <= 0;
            character_index_s3 <= 0; glyph_column_s3 <= 0; character_s4 <= 0;
            glyph_bit_s4 <= 0; glyph_bit_s5 <= 0; glyph_s5 <= 0;
            text_valid_s2 <= 0; text_valid_s3 <= 0; text_valid_s4 <= 0; text_valid_s5 <= 0;
            timing_s1 <= 0; timing_s2 <= 0; timing_s3 <= 0; timing_s4 <= 0; timing_s5 <= 0;
            rgb_s2 <= 0; rgb_s3 <= 0; rgb_s4 <= 0; rgb_s5 <= 0;
        end else begin
            x_s1 <= raster_x; y_s1 <= raster_y;
            colour_s1 <= spectrum_colour(bin_index);
            pixel_meta_valid_s1 <= pixel_meta_read;
            text_x_s1 <= text_x; text_y_s1 <= text_y; text_length_s1 <= text_length;
            text_word_s1 <= text_word; text_large_s1 <= text_large;
            timing_s1 <= {(raster_x < 1280 && raster_y < 720),
                (raster_x >= 1390 && raster_x < 1430), (raster_y >= 725 && raster_y < 730), vblank_start};

            rgb_s2 <= pixel_rgb; timing_s2 <= timing_s1;
            text_column_s2 <= text_column; text_row_s2 <= text_row; text_valid_s2 <= text_valid;
            text_word_s2 <= text_word_s1; text_large_s2 <= text_large_s1;

            character_index_s3 <= 4'(text_column_s2 / 7'd6);
            glyph_column_s3 <= 3'(text_column_s2 % 7'd6);
            text_row_s3 <= text_row_s2;
            text_valid_s3 <= text_valid_s2 && ((text_column_s2 % 7'd6) < 7'd5);
            text_word_s3 <= text_word_s2; text_large_s3 <= text_large_s2;
            rgb_s3 <= rgb_s2; timing_s3 <= timing_s2;

            character_s4 <= text_word_s3[7'd127 - {character_index_s3, 3'b000} -: 8];
            glyph_bit_s4 <= 6'd34 - 6'(text_row_s3)*6'd5 - 6'(glyph_column_s3);
            text_valid_s4 <= text_valid_s3; text_large_s4 <= text_large_s3;
            rgb_s4 <= rgb_s3; timing_s4 <= timing_s3;

            glyph_s5 <= glyph(character_s4); glyph_bit_s5 <= glyph_bit_s4;
            text_valid_s5 <= text_valid_s4; text_large_s5 <= text_large_s4;
            rgb_s5 <= rgb_s4; timing_s5 <= timing_s4;

            {de_o, hs_o, vs_o, frame_tick_o} <= timing_s5;
            rgb_o <= timing_s5[3] ? ((text_valid_s5 && glyph_s5[glyph_bit_s5])
                ? (text_large_s5 ? TITLE_COLOUR : TEXT_COLOUR)
                : (particle_hit ? particle_overlay : rgb_s5)) : 24'd0;
        end
    end
endmodule

// One write port, one synchronous read port. The caller disables reads during
// every write, including runtime initialization, so a read/write collision is
// neither required nor hidden behind a read-before-write inference setting.
module spectrum_display_ram #(
    parameter integer WIDTH = 32
) (
    input wire clk_i,
    input wire rd_en_i,
    input wire [7:0] rd_addr_i,
    output logic [WIDTH-1:0] rd_data_o,
    input wire wr_en_i,
    input wire [7:0] wr_addr_i,
    input wire [WIDTH-1:0] wr_data_i
);
    logic [WIDTH-1:0] mem [0:255] /* synthesis syn_ramstyle="block_ram" */;
    always_ff @(posedge clk_i) begin
        if (wr_en_i) mem[wr_addr_i] <= wr_data_i;
        if (rd_en_i) rd_data_o <= mem[rd_addr_i];
    end
endmodule

`default_nettype wire
