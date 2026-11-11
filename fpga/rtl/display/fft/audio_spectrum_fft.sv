// 1024-point Hann-window spectrum. PCM acquisition never waits for display.
// Official FFT_Top: fixed 1024 points, 16-bit, Low Resource, Forward,
// Natural output, RS111 scaling and truncation. clk_i is the FFT clock domain.
`default_nettype none
module audio_spectrum_fft (
    input  wire               clk_i,
    input  wire               rst_n_i,
    input  wire signed [23:0] pcm_i,
    input  wire               pcm_valid_i,
    input  wire               publish_ready_i,
    output reg                column_valid_o,
    output reg         [7:0]  column_index_o,
    output reg         [9:0]  column_level_o,
    output reg                frame_done_o,
    output reg         [31:0] frame_count_o,
    output reg         [31:0] skipped_count_o
);
    localparam [3:0] IDLE = 4'd0, PRELOAD = 4'd1, PRIME = 4'd2,
        START = 4'd3, FEED = 4'd4, DRAIN = 4'd5, TAIL = 4'd6,
        PUBLISH = 4'd7, DONE = 4'd8;
    reg [3:0] state;
    reg preload_select, prime_select;
    reg signed [15:0] first_sample, second_sample;
    wire fft_ipd;
    wire [9:0] fft_index;
    reg [10:0] write_address, filled_count, frame_start;
    reg [8:0] hop_count;
    reg [9:0] fft_output_count;
    // ipd/idx describes the sample accepted on the next active clock edge.
    // RAM and Hann each contribute one edge, so request two samples ahead.
    wire [9:0] read_index = state == PRELOAD ? {9'd0, preload_select} : fft_index + 10'd2;
    reg [7:0] publish_index;
    wire hop_due = pcm_valid_i && hop_count == 9'd511 && filled_count >= 11'd1023;
    wire read_enable = state == PRELOAD ||
        (state == FEED && fft_ipd && fft_index < 10'd1022);
    wire [10:0] read_address = frame_start + {1'b0, read_index};
    wire [15:0] sample_data;

    // Official generated RAM wrapper is owned by the integration layer. A
    // behavioral latency-equivalent model exists ONLY in sim/visual/fft/.
    spectrum_sample_ram samples (
        .clk_i(clk_i), .rst_n_i(rst_n_i),
        .wr_en_i(pcm_valid_i), .wr_addr_i(write_address), .wr_data_i(pcm_i[23:8]),
        .rd_en_i(read_enable), .rd_addr_i(read_address), .rd_data_o(sample_data)
    );
    wire [15:0] hann_coefficient;
    spectrum_hann1024 hann (.index_i(read_index), .coefficient_o(hann_coefficient));
    reg memory_valid, window_valid;
    reg [15:0] coefficient_q;
    wire signed [32:0] window_product = $signed(sample_data) * $signed({1'b0, coefficient_q});
    wire signed [32:0] rounded_product = window_product + 33'sd16384;
    reg signed [15:0] window_sample;
    always @(posedge clk_i) begin
        if (!rst_n_i) begin
            memory_valid <= 1'b0;
            window_valid <= 1'b0;
            coefficient_q <= 16'd0;
            window_sample <= 16'sd0;
        end else begin
            memory_valid <= read_enable;
            window_valid <= memory_valid;
            if (read_enable) coefficient_q <= hann_coefficient;
            if (memory_valid) window_sample <= rounded_product[30:15];
        end
    end
    // Keep the IP reset while the two leading windowed samples are prepared.
    // Once started, drain all 1024 outputs: busy deasserts before output ends.
    wire fft_reset = !rst_n_i || state == IDLE || state == PRELOAD ||
        state == PRIME || state == PUBLISH || state == DONE;
    wire fft_output_valid;
    wire signed [15:0] fft_real, fft_imag;
    wire signed [15:0] fft_input = fft_index == 10'd0 ? first_sample :
        fft_index == 10'd1 ? second_sample : window_sample;
    FFT_Top fft (
        .clk(clk_i), .rst(fft_reset), .start(state == START),
        .xn_re(fft_input), .xn_im(16'd0), .idx(fft_index),
        .xk_re(fft_real), .xk_im(fft_imag), .ipd(fft_ipd),
        .opd(fft_output_valid), .sod(), .eod(), .busy(), .soud(), .eoud()
    );
    wire [9:0] natural_index = fft_index;

    // Fully unsigned 33-bit power sum prevents signed overflow at -32768.
    reg square_valid, power_valid, normalize_valid, mapping_valid;
    reg [7:0] square_index, power_index, normalize_index, mapping_index;
    reg [31:0] real_square, imag_square;
    reg [32:0] power_sum;
    function automatic [5:0] leading_one(input [32:0] value);
        integer b;
        begin
            leading_one = 6'd0;
            for (b = 0; b < 33; b = b + 1)
                if (value[b]) leading_one = 6'(b);
        end
    endfunction
    wire [5:0] power_exponent = leading_one(power_sum);
    wire [32:0] shifted_power = power_sum << (6'd32 - power_exponent);
    reg [5:0] exponent_q, mantissa_q;
    reg zero_power_q;
    wire [7:0] log_fraction;
    spectrum_log_mantissa logarithm (.mantissa_i(mantissa_q), .log2_fraction_o(log_fraction));
    wire signed [15:0] reference_q8 = normalize_index == 8'd0 ? 16'sd7168 : 16'sd6656;
    wire signed [15:0] logarithm_q8 = $signed({2'b00, exponent_q, log_fraction});
    wire signed [15:0] relative_log_q8 = logarithm_q8 - reference_q8;
    // 5480/65536 maps Q8 log2(power/ref) to 512/72 * 10log10(power/ref).
    // Reference power: DC 16384^2; ordinary bins 8192^2 (Hann CG = 1/2).
    wire signed [31:0] mapped_product = relative_log_q8 * 16'sd5480;
    reg signed [31:0] mapped_product_q;
    reg mapped_zero_q;
    wire signed [31:0] mapped_level = 32'sd512 + ((mapped_product_q + 32'sd32768) >>> 16);
    reg [9:0] levels [0:255];
    always @(posedge clk_i) begin
        if (!rst_n_i) begin
            square_valid <= 1'b0;
            power_valid <= 1'b0;
            normalize_valid <= 1'b0;
            mapping_valid <= 1'b0;
            square_index <= 8'd0;
            power_index <= 8'd0;
            normalize_index <= 8'd0;
            mapping_index <= 8'd0;
            real_square <= 32'd0;
            imag_square <= 32'd0;
            power_sum <= 33'd0;
            exponent_q <= 6'd0;
            mantissa_q <= 6'd0;
            zero_power_q <= 1'b1;
            mapped_product_q <= 32'sd0;
            mapped_zero_q <= 1'b1;
        end else begin
            square_valid <= fft_output_valid && natural_index[9:8] == 2'b00;
            power_valid <= square_valid;
            normalize_valid <= power_valid;
            mapping_valid <= normalize_valid;
            if (fft_output_valid) begin
                square_index <= natural_index[7:0];
                real_square <= fft_real * fft_real;
                imag_square <= fft_imag * fft_imag;
            end
            if (square_valid) begin
                power_index <= square_index;
                power_sum <= {1'b0, real_square} + {1'b0, imag_square};
            end
            if (power_valid) begin
                normalize_index <= power_index;
                exponent_q <= power_exponent;
                mantissa_q <= shifted_power[31:26];
                zero_power_q <= power_sum == 33'd0;
            end
            if (normalize_valid) begin
                mapping_index <= normalize_index;
                mapped_product_q <= mapped_product;
                mapped_zero_q <= zero_power_q;
            end
            if (mapping_valid) begin
                if (mapped_zero_q || mapped_level <= 32'sd0) levels[mapping_index] <= 10'd0;
                else if (mapped_level >= 32'sd512) levels[mapping_index] <= 10'd512;
                else levels[mapping_index] <= mapped_level[9:0];
            end
        end
    end

    always @(posedge clk_i) begin
        if (!rst_n_i) begin
            state <= IDLE;
            write_address <= 11'd0;
            filled_count <= 11'd0;
            frame_start <= 11'd0;
            hop_count <= 9'd0;
            preload_select <= 1'b0;
            prime_select <= 1'b0;
            first_sample <= 16'sd0;
            second_sample <= 16'sd0;
            fft_output_count <= 10'd0;
            publish_index <= 8'd0;
            column_valid_o <= 1'b0;
            column_index_o <= 8'd0;
            column_level_o <= 10'd0;
            frame_done_o <= 1'b0;
            frame_count_o <= 32'd0;
            skipped_count_o <= 32'd0;
        end else begin
            column_valid_o <= 1'b0;
            frame_done_o <= 1'b0;
            if (pcm_valid_i) begin
                write_address <= write_address + 11'd1;
                hop_count <= hop_count + 9'd1;
                if (filled_count < 11'd1024) filled_count <= filled_count + 11'd1;
            end
            if (hop_due && (state != IDLE || !publish_ready_i))
                skipped_count_o <= skipped_count_o + 32'd1;
            case (state)
                IDLE: if (hop_due && publish_ready_i) begin
                    // Includes this edge's sample, already committed before first read.
                    frame_start <= write_address + 11'd1025;
                    preload_select <= 1'b0;
                    prime_select <= 1'b0;
                    fft_output_count <= 10'd0;
                    state <= PRELOAD;
                end
                PRELOAD: begin
                    if (preload_select) state <= PRIME;
                    else preload_select <= 1'b1;
                end
                PRIME: if (window_valid) begin
                    if (!prime_select) begin
                        first_sample <= window_sample;
                        prime_select <= 1'b1;
                    end else begin
                        second_sample <= window_sample;
                        state <= START;
                    end
                end
                START: state <= FEED;
                FEED: if (fft_ipd && fft_index == 10'd1023) state <= DRAIN;
                DRAIN: begin end
                TAIL: if (!fft_output_valid && !square_valid && !power_valid &&
                          !normalize_valid && !mapping_valid) begin
                    publish_index <= 8'd0;
                    state <= PUBLISH;
                end
                PUBLISH: begin
                    column_valid_o <= 1'b1;
                    column_index_o <= publish_index;
                    column_level_o <= levels[publish_index];
                    if (publish_index == 8'd255) state <= DONE;
                    else publish_index <= publish_index + 8'd1;
                end
                DONE: begin
                    frame_done_o <= 1'b1;
                    frame_count_o <= frame_count_o + 32'd1;
                    state <= IDLE;
                end
                default: state <= IDLE;
            endcase
            // Count all N outputs, including undisplayed bins: a burst FFT's
            // output drain continues long after the N input samples finish.
            if (fft_output_valid && (state == FEED || state == DRAIN)) begin
                if (fft_output_count == 10'd1023) state <= TAIL;
                else fft_output_count <= fft_output_count + 10'd1;
            end
        end
    end
endmodule
`default_nettype wire
