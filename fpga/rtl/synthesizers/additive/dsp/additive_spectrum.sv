`timescale 1ns/1ps
// Spectral control, L1 normalization and finite one-pole smoothing.
// Ideas: Mutable Instruments Plaits AdditiveEngine, pichenettes/eurorack
// f4cfa7697095a28d292bff758beb51deb49b5a86 (MIT).
// This is an independent fixed-point circuit, not a translation of its formula.
module additive_spectrum #(
    parameter integer PARTIALS = 32
) (
    input  logic clk,
    input  logic rst_n,
    input  logic sample_tick,
    input  logic [12:0] centroid,
    input  logic [15:0] slope,
    input  logic [4:0] comb_period,
    input  logic [15:0] comb_depth,
    input  logic [PARTIALS*16-1:0] weights,
    output logic [PARTIALS*16-1:0] gains,
    output logic busy,
    output logic valid
);
    localparam integer INDEX_W = (PARTIALS < 2) ? 1 : $clog2(PARTIALS);
    localparam integer COUNT_W = $clog2(PARTIALS+1);
    localparam logic [20:0] GAIN_LIMIT = 21'd32768;
    typedef enum logic [3:0] {
        IDLE, RAW, DIV_INIT, DIVIDE, NORMALIZE,
        LIMIT_INIT, LIMIT_READ, LIMIT_DIFF, LIMIT_SELECT, LIMIT_WRITE, PUBLISH
    } state_t;
    state_t state;

    logic [12:0] centroid_q;
    logic [15:0] slope_q;
    logic [4:0] comb_period_q;
    logic [15:0] comb_depth_q;
    logic [PARTIALS*16-1:0] weights_q;
    logic [COUNT_W-1:0] issued;
    logic [4:0] comb_count;

    // Three registered multipliers accept one partial per cycle. A separate
    // store/add cycle keeps the raw-sum carry chain off the multiplier path.
    logic raw_v1, raw_v2, raw_v3;
    logic [INDEX_W-1:0] raw_i1, raw_i2, raw_i3;
    logic [28:0] distance_slope;
    logic [15:0] raw_weight1, comb1, comb2;
    logic [31:0] weight_window;
    logic [31:0] comb_product;
    logic [15:0] raw_weights [0:PARTIALS-1];
    logic [20:0] raw_sum;
    logic [12:0] issue_position, issue_distance;
    logic [15:0] window_amplitude;

    // Restoring division computes floor(2^31 / raw_sum) in 32 clocks.
    // A 22-bit subtractor replaces a combinational variable divider.
    logic [31:0] dividend_shift, quotient, reciprocal;
    logic [21:0] remainder;
    logic [5:0] divide_left;
    logic [21:0] trial_remainder;
    logic trial_fits;

    logic [INDEX_W-1:0] index_q;
    logic [47:0] normalized_product;
    logic [15:0] target_now, candidate_now;
    // One partial enters per clock; all data and its index advance together.
    // Register the gain mux before difference/rounding, then register the
    // candidate before accumulating it. None of those carry chains share a
    // register-to-register path.
    logic norm_v1, norm_v2, norm_v3, norm_v4, norm_v5;
    logic [INDEX_W-1:0] norm_i1, norm_i2, norm_i3, norm_i4, norm_i5;
    logic [15:0] norm_old1, norm_old2, norm_old3, norm_old4;
    logic [15:0] norm_target2, norm_target3, norm_target4, norm_target5;
    logic norm_rising3, norm_rising4;
    logic [16:0] delta_abs, slew_step;
    logic [15:0] target_mem [0:PARTIALS-1];
    logic [15:0] candidate_mem [0:PARTIALS-1];
    logic [20:0] candidate_sum, excess;
    logic [15:0] limit_candidate, limit_target;
    logic [15:0] removable;
    logic [20:0] correction;
    logic [PARTIALS*16-1:0] publish_vector;

    always_comb begin
        issue_position = 13'(issued) << 8;
        issue_distance = issue_position >= centroid_q ?
                         issue_position-centroid_q : centroid_q-issue_position;
        window_amplitude = distance_slope[28:8] >= 21'd65535 ?
                           16'd0 : 16'd65535-distance_slope[23:8];
        trial_remainder = {remainder[20:0], dividend_shift[31]};
        trial_fits = trial_remainder >= {1'b0, raw_sum};

        target_now = normalized_product[47:16] >= 32'd32768 ?
                     16'd32768 : normalized_product[31:16];
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= IDLE;
            gains <= '0;
            busy <= 1'b0;
            valid <= 1'b0;
            centroid_q <= '0;
            slope_q <= '0;
            comb_period_q <= '0;
            comb_depth_q <= '0;
            weights_q <= '0;
            issued <= '0;
            comb_count <= '0;
            raw_v1 <= 1'b0;
            raw_v2 <= 1'b0;
            raw_v3 <= 1'b0;
            raw_i1 <= '0;
            raw_i2 <= '0;
            raw_i3 <= '0;
            distance_slope <= '0;
            raw_weight1 <= '0;
            comb1 <= '0;
            comb2 <= '0;
            weight_window <= '0;
            comb_product <= '0;
            raw_sum <= '0;
            dividend_shift <= '0;
            quotient <= '0;
            reciprocal <= '0;
            remainder <= '0;
            divide_left <= '0;
            index_q <= '0;
            normalized_product <= '0;
            norm_v1 <= 1'b0;
            norm_v2 <= 1'b0;
            norm_v3 <= 1'b0;
            norm_v4 <= 1'b0;
            norm_v5 <= 1'b0;
            norm_i1 <= '0;
            norm_i2 <= '0;
            norm_i3 <= '0;
            norm_i4 <= '0;
            norm_i5 <= '0;
            norm_old1 <= '0;
            norm_old2 <= '0;
            norm_old3 <= '0;
            norm_old4 <= '0;
            norm_target2 <= '0;
            norm_target3 <= '0;
            norm_target4 <= '0;
            norm_target5 <= '0;
            norm_rising3 <= 1'b0;
            norm_rising4 <= 1'b0;
            delta_abs <= '0;
            slew_step <= '0;
            candidate_now <= '0;
            limit_candidate <= '0;
            limit_target <= '0;
            removable <= '0;
            correction <= '0;
            candidate_sum <= '0;
            excess <= '0;
            publish_vector <= '0;
        end else begin
            valid <= 1'b0;
            case (state)
                IDLE: if (sample_tick) begin
                    centroid_q <= centroid;
                    slope_q <= slope;
                    comb_period_q <= comb_period;
                    comb_depth_q <= comb_depth;
                    weights_q <= weights;
                    issued <= '0;
                    comb_count <= '0;
                    raw_v1 <= 1'b0;
                    raw_v2 <= 1'b0;
                    raw_v3 <= 1'b0;
                    raw_sum <= '0;
                    busy <= 1'b1;
                    state <= RAW;
                end
                RAW: begin
                    raw_v1 <= issued < COUNT_W'(PARTIALS);
                    raw_v2 <= raw_v1;
                    raw_v3 <= raw_v2;
                    if (issued < COUNT_W'(PARTIALS)) begin
                        distance_slope <= issue_distance*slope_q;
                        raw_weight1 <= weights_q[issued*16 +: 16];
                        raw_i1 <= INDEX_W'(issued);
                        comb1 <= comb_period_q == 0 || comb_count == 0 ?
                                 16'd65535 : 16'd65535-comb_depth_q;
                        if (comb_period_q == 0 || comb_count == comb_period_q-1'b1)
                            comb_count <= '0;
                        else
                            comb_count <= comb_count+1'b1;
                        issued <= issued+1'b1;
                    end
                    if (raw_v1) begin
                        weight_window <= raw_weight1*window_amplitude;
                        comb2 <= comb1;
                        raw_i2 <= raw_i1;
                    end
                    if (raw_v2) begin
                        comb_product <= weight_window[31:16]*comb2;
                        raw_i3 <= raw_i2;
                    end
                    if (raw_v3) begin
                        raw_weights[raw_i3] <= comb_product[31:16];
                        raw_sum <= raw_sum+{5'd0,comb_product[31:16]};
                        if (raw_i3 == INDEX_W'(PARTIALS-1)) state <= DIV_INIT;
                    end
                end
                DIV_INIT: begin
                    index_q <= '0;
                    issued <= '0;
                    norm_v1 <= 1'b0;
                    norm_v2 <= 1'b0;
                    norm_v3 <= 1'b0;
                    norm_v4 <= 1'b0;
                    norm_v5 <= 1'b0;
                    candidate_sum <= '0;
                    dividend_shift <= 32'h8000_0000;
                    quotient <= '0;
                    remainder <= '0;
                    divide_left <= 6'd32;
                    if (raw_sum == 0) begin
                        reciprocal <= '0;
                        state <= NORMALIZE;
                    end else state <= DIVIDE;
                end
                DIVIDE: begin
                    dividend_shift <= {dividend_shift[30:0],1'b0};
                    quotient <= {quotient[30:0],trial_fits};
                    remainder <= trial_fits ? trial_remainder-{1'b0,raw_sum} :
                                             trial_remainder;
                    divide_left <= divide_left-1'b1;
                    if (divide_left == 1) begin
                        reciprocal <= {quotient[30:0],trial_fits};
                        state <= NORMALIZE;
                    end
                end
                NORMALIZE: begin
                    norm_v1 <= issued < COUNT_W'(PARTIALS);
                    norm_v2 <= norm_v1;
                    norm_v3 <= norm_v2;
                    norm_v4 <= norm_v3;
                    norm_v5 <= norm_v4;
                    if (issued < COUNT_W'(PARTIALS)) begin
                        normalized_product <= raw_weights[INDEX_W'(issued)]*reciprocal;
                        norm_old1 <= gains[issued*16 +: 16];
                        norm_i1 <= INDEX_W'(issued);
                        issued <= issued+1'b1;
                    end
                    if (norm_v1) begin
                        norm_target2 <= target_now;
                        norm_old2 <= norm_old1;
                        norm_i2 <= norm_i1;
                    end
                    if (norm_v2) begin
                        delta_abs <= norm_target2 >= norm_old2 ?
                            {1'b0,norm_target2}-{1'b0,norm_old2} :
                            {1'b0,norm_old2}-{1'b0,norm_target2};
                        norm_rising3 <= norm_target2 >= norm_old2;
                        norm_target3 <= norm_target2;
                        norm_old3 <= norm_old2;
                        norm_i3 <= norm_i2;
                    end
                    if (norm_v3) begin
                        // ceil(abs(delta)/8) retains exact finite convergence.
                        slew_step <= (delta_abs+17'd7) >> 3;
                        norm_rising4 <= norm_rising3;
                        norm_target4 <= norm_target3;
                        norm_old4 <= norm_old3;
                        norm_i4 <= norm_i3;
                    end
                    if (norm_v4) begin
                        candidate_now <= norm_rising4 ?
                            norm_old4+slew_step[15:0] : norm_old4-slew_step[15:0];
                        norm_target5 <= norm_target4;
                        norm_i5 <= norm_i4;
                    end
                    if (norm_v5) begin
                        target_mem[norm_i5] <= norm_target5;
                        candidate_mem[norm_i5] <= candidate_now;
                        candidate_sum <= candidate_sum+{5'd0,candidate_now};
                        if (norm_i5 == INDEX_W'(PARTIALS-1)) state <= LIMIT_INIT;
                    end
                end
                LIMIT_INIT: begin
                    index_q <= '0;
                    excess <= candidate_sum > GAIN_LIMIT ?
                              candidate_sum-GAIN_LIMIT : 21'd0;
                    state <= LIMIT_READ;
                end
                LIMIT_READ: begin
                    limit_candidate <= candidate_mem[index_q];
                    limit_target <= target_mem[index_q];
                    state <= LIMIT_DIFF;
                end
                LIMIT_DIFF: begin
                    removable <= limit_candidate > limit_target ?
                                 limit_candidate-limit_target : 16'd0;
                    state <= LIMIT_SELECT;
                end
                LIMIT_SELECT: begin
                    correction <= excess < {5'd0,removable} ?
                                  excess : {5'd0,removable};
                    state <= LIMIT_WRITE;
                end
                LIMIT_WRITE: begin
                    // Rounding a mixed rise/fall transition can exceed the L1
                    // limit. Remove only gains still above their targets, so
                    // this correction accelerates decay without undershoot.
                    publish_vector[index_q*16 +: 16] <=
                        limit_candidate-correction[15:0];
                    excess <= excess-correction;
                    if (index_q == INDEX_W'(PARTIALS-1)) state <= PUBLISH;
                    else begin
                        index_q <= index_q+1'b1;
                        state <= LIMIT_READ;
                    end
                end
                PUBLISH: begin
                    gains <= publish_vector;
                    busy <= 1'b0;
                    valid <= 1'b1;
                    state <= IDLE;
                end
                default: begin
                    state <= IDLE;
                    busy <= 1'b0;
                end
            endcase
        end
    end

`ifndef SYNTHESIS
    initial begin
        if (PARTIALS < 1 || PARTIALS > 32)
            $fatal(1, "additive_spectrum supports PARTIALS 1..32");
    end
`endif
endmodule
