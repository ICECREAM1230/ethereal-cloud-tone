`timescale 1ns/1ps
`default_nettype none

// Small display-only particle pool. Input samples are the renderer's committed
// heights, in natural-bin order, during vertical blank. No pixel storage,
// backpressure, external memory or extra clock domain is used.
module spectrum_particles #(
    parameter bit SPLIT_VIEW = 1'b0
) (
    input wire clk_i,
    input wire rst_n_i,
    input wire frame_start_i,
    input wire sample_valid_i,
    input wire [7:0] sample_bin_i,
    input wire [9:0] sample_height_i,
    input wire [23:0] sample_colour_i,
    input wire sample_end_i,
    input wire [10:0] raster_x_i,
    input wire [9:0] raster_y_i,
    output logic hit_o,
    output logic [23:0] rgb_o
);
    localparam integer PLOT_X = SPLIT_VIEW ? 640 : 128;
    localparam integer BIN_SHIFT = SPLIT_VIEW ? 1 : 2;
    localparam logic [2:0] SPAWN_IDLE = 3'd0;
    localparam logic [2:0] SPAWN_CHECK = 3'd1;
    localparam logic [2:0] SPAWN_FIND = 3'd2;

    // Q4 positions and velocities: 1/16 pixel per frame. Only the selected
    // slot is moved each blanking clock. All slots are stable during the frame.
    logic signed [15:0] pos_x_q4 [0:31];
    logic signed [15:0] pos_y_q4 [0:31];
    logic signed [8:0] velocity_x_q4 [0:31];
    logic signed [8:0] velocity_y_q4 [0:31];
    logic [5:0] life [0:31];
    logic [23:0] particle_colour [0:31];
    logic moving;
    logic [4:0] move_index;
    // Two clocks per slot retain the shared adders without the old
    // variable read -> arithmetic/clip -> variable write feedback cone.
    logic move_read_phase;
    logic [4:0] move_slot_q;
    logic signed [15:0] move_x_q, move_y_q;
    logic signed [8:0] move_vx_q, move_vy_q;
    logic [5:0] move_life_q;
    logic signed [16:0] moved_x, moved_y;

    // Eight groups of 32 bins coalesce simultaneous releases. The strongest
    // column represents each group; its emission colour is retained in flight.
    logic [9:0] previous_height [0:7];
    logic [7:0] previous_bin [0:7];
    logic [23:0] previous_colour [0:7];
    logic [7:0] armed;
    logic [2:0] pending_age [0:7];
    logic [9:0] pending_height [0:7];
    logic [7:0] pending_bin [0:7];
    logic [23:0] pending_colour [0:7];
    logic [9:0] group_max;
    logic [7:0] group_bin;
    logic [23:0] group_colour;
    logic [9:0] selected_height;
    logic [7:0] selected_bin;
    logic [23:0] selected_colour;
    // Register the committed sample before selecting a group maximum, then
    // register complete group events before armed/drop decisions. The end
    // marker travels with the group event, so spawning sees its final update.
    logic sample_valid_q, sample_end_q, frame_start_q, frame_start_d;
    logic [7:0] sample_bin_q;
    logic [9:0] sample_height_q;
    logic [23:0] sample_colour_q;
    logic group_valid_d, sample_end_d;
    logic [2:0] group_index_d;
    logic [9:0] group_height_d;
    logic [7:0] group_bin_d;
    logic [23:0] group_colour_d;

    logic [2:0] spawn_state;
    logic [2:0] round_robin, scan_group;
    logic [3:0] groups_scanned;
    logic [1:0] bursts;
    logic [4:0] free_cursor, slots_scanned;
    logic second_drop;
    logic [7:0] emit_bin;
    logic [9:0] emit_height;
    logic [23:0] emit_colour;
    logic [15:0] random_state;
    logic [10:0] emit_x;
    logic signed [8:0] emit_speed_x, emit_speed_y;

    // White-free glow: full colour, then 1/2, 1/4 and 1/8 during the final
    // sixteen life frames. The 5x5 diamond halo is another quarter intensity.
    function automatic [23:0] faded_colour(input logic [23:0] colour,
                                           input logic [5:0] remaining);
        if (remaining >= 6'd16) faded_colour = colour;
        else if (remaining >= 6'd8)
            faded_colour = {1'b0,colour[23:17],1'b0,colour[15:9],1'b0,colour[7:1]};
        else if (remaining >= 6'd4)
            faded_colour = {2'b0,colour[23:18],2'b0,colour[15:10],2'b0,colour[7:2]};
        else faded_colour = {3'b0,colour[23:19],3'b0,colour[15:11],3'b0,colour[7:3]};
    endfunction

    always_comb begin
        moved_x = {move_x_q[15],move_x_q} + {{8{move_vx_q[8]}},move_vx_q};
        moved_y = {move_y_q[15],move_y_q} + {{8{move_vy_q[8]}},move_vy_q};
        selected_height = group_max;
        selected_bin = group_bin;
        selected_colour = group_colour;
        if (sample_bin_q[4:0] == 0 || sample_height_q > group_max) begin
            selected_height = sample_height_q;
            selected_bin = sample_bin_q;
            selected_colour = sample_colour_q;
        end
        emit_x = 11'(PLOT_X) + (11'(emit_bin) << BIN_SHIFT)
            + (SPLIT_VIEW ? 11'd1 : 11'd2);
        // 1..1.9375 px/frame sideways, 3.5..4.4375 px/frame upward.
        emit_speed_x = 9'sd16 + $signed({5'd0,random_state[3:0]});
        if (!second_drop) emit_speed_x = -emit_speed_x;
        emit_speed_y = -9'sd56 - $signed({5'd0,random_state[7:4]});
    end

    always_ff @(posedge clk_i) begin
        if (!rst_n_i) begin
            sample_valid_q <= 0; sample_end_q <= 0;
            frame_start_q <= 0; frame_start_d <= 0;
            sample_bin_q <= 0; sample_height_q <= 0; sample_colour_q <= 0;
            group_valid_d <= 0; sample_end_d <= 0; group_index_d <= 0;
            group_height_d <= 0; group_bin_d <= 0; group_colour_d <= 0;
        end else begin
            sample_valid_q <= sample_valid_i;
            sample_end_q <= sample_end_i;
            frame_start_q <= frame_start_i;
            frame_start_d <= frame_start_q;
            if (sample_valid_i) begin
                sample_bin_q <= sample_bin_i;
                sample_height_q <= sample_height_i;
                sample_colour_q <= sample_colour_i;
            end
            group_valid_d <= sample_valid_q && sample_bin_q[4:0] == 5'd31;
            sample_end_d <= sample_valid_q && sample_end_q;
            if (sample_valid_q) begin
                group_index_d <= sample_bin_q[7:5];
                group_height_d <= selected_height;
                group_bin_d <= selected_bin;
                group_colour_d <= selected_colour;
            end
        end
    end

    integer reset_index;
    integer decision_group;
    always_ff @(posedge clk_i) begin
        if (!rst_n_i) begin
            moving <= 0; move_index <= 0; move_read_phase <= 1;
            move_slot_q <= 0; move_x_q <= 0; move_y_q <= 0;
            move_vx_q <= 0; move_vy_q <= 0; move_life_q <= 0;
            armed <= 0; group_max <= 0; group_bin <= 0; group_colour <= 0;
            spawn_state <= SPAWN_IDLE; round_robin <= 0; scan_group <= 0;
            groups_scanned <= 0; bursts <= 0; free_cursor <= 0; slots_scanned <= 0;
            second_drop <= 0; emit_bin <= 0; emit_height <= 0; emit_colour <= 0;
            random_state <= 16'h1ace;
            for (reset_index=0; reset_index<32; reset_index=reset_index+1) begin
                pos_x_q4[reset_index] <= 0; pos_y_q4[reset_index] <= 0;
                velocity_x_q4[reset_index] <= 0; velocity_y_q4[reset_index] <= 0;
                life[reset_index] <= 0; particle_colour[reset_index] <= 0;
            end
            for (reset_index=0; reset_index<8; reset_index=reset_index+1) begin
                previous_height[reset_index] <= 0; previous_bin[reset_index] <= 0;
                previous_colour[reset_index] <= 0; pending_age[reset_index] <= 0;
                pending_height[reset_index] <= 0; pending_bin[reset_index] <= 0;
                pending_colour[reset_index] <= 0;
            end
        end else begin
            if (frame_start_i) begin
                // Any old in-flight read is discarded at a new frame start.
                moving <= 1; move_index <= 0; move_read_phase <= 1;
            end else if (moving) begin
                if (move_read_phase) begin
                    move_slot_q <= move_index;
                    move_x_q <= pos_x_q4[move_index];
                    move_y_q <= pos_y_q4[move_index];
                    move_vx_q <= velocity_x_q4[move_index];
                    move_vy_q <= velocity_y_q4[move_index];
                    move_life_q <= life[move_index];
                    move_read_phase <= 0;
                end else begin
                    if (move_life_q != 0) begin
                        // +6/16 px/frame^2 gravity; same clipping/death rule.
                        if (move_life_q == 1 || moved_x < 17'(PLOT_X*16)
                            || moved_x >= 17'sd18432 || moved_y < 17'sd0
                            || moved_y >= 17'sd10240) life[move_slot_q] <= 0;
                        else begin
                            pos_x_q4[move_slot_q] <= moved_x[15:0];
                            pos_y_q4[move_slot_q] <= moved_y[15:0];
                            velocity_y_q4[move_slot_q] <= move_vy_q + 9'sd6;
                            life[move_slot_q] <= move_life_q - 1'b1;
                        end
                    end
                    move_read_phase <= 1;
                    if (move_index == 31) moving <= 0;
                    else move_index <= move_index + 1'b1;
                end
            end

            if (frame_start_q) begin
                group_max <= 0; group_bin <= 0; group_colour <= 0;
            end
            if (sample_valid_q) begin
                group_max <= selected_height;
                group_bin <= selected_bin;
                group_colour <= selected_colour;
            end

            // Age and complete-group decisions are delayed by the same two
            // clocks. Preserve original priority: age, group event, spawn.
            if (frame_start_d) begin
                for (reset_index=0; reset_index<8; reset_index=reset_index+1)
                    if (pending_age[reset_index] != 0)
                        pending_age[reset_index] <= pending_age[reset_index] - 1'b1;
            end
            for (decision_group=0; decision_group<8; decision_group=decision_group+1) begin
                if (group_valid_d && group_index_d == 3'(decision_group)) begin
                    previous_height[decision_group] <= group_height_d;
                    previous_bin[decision_group] <= group_bin_d;
                    previous_colour[decision_group] <= group_colour_d;
                    if (11'(group_height_d) >= 11'(previous_height[decision_group]) + 11'd2)
                        armed[decision_group] <= 1;
                    else if (armed[decision_group] && previous_height[decision_group] >= 10'd32
                        && 11'(group_height_d) + 11'd2 <= 11'(previous_height[decision_group])) begin
                        pending_age[decision_group] <= 3'd4;
                        pending_height[decision_group] <= previous_height[decision_group];
                        pending_bin[decision_group] <= previous_bin[decision_group];
                        pending_colour[decision_group] <= previous_colour[decision_group];
                        armed[decision_group] <= 0;
                    end
                end
            end

            if (sample_end_d) begin
                spawn_state <= SPAWN_CHECK; scan_group <= round_robin;
                groups_scanned <= 0; bursts <= 0;
                // Advance by two groups even on quiet/full frames. No low-bin
                // preference and no backlog older than four frame starts.
                round_robin <= round_robin + 3'd2;
            end else begin
                case (spawn_state)
                    SPAWN_CHECK: begin
                        if (groups_scanned == 8 || bursts == 2) spawn_state <= SPAWN_IDLE;
                        else if (pending_age[scan_group] != 0) begin
                            emit_bin <= pending_bin[scan_group];
                            emit_height <= pending_height[scan_group];
                            emit_colour <= pending_colour[scan_group];
                            second_drop <= 0; slots_scanned <= 0;
                            spawn_state <= SPAWN_FIND;
                        end else begin
                            scan_group <= scan_group + 1'b1;
                            groups_scanned <= groups_scanned + 1'b1;
                        end
                    end
                    SPAWN_FIND: begin
                        // Each allocation examines at most 32 slots. A full
                        // pool drops this new burst without replacing live data.
                        free_cursor <= free_cursor + 1'b1;
                        if (life[free_cursor] == 0) begin
                            pos_x_q4[free_cursor] <= $signed({1'b0,emit_x,4'b0000});
                            pos_y_q4[free_cursor] <= $signed({2'b00,(10'd640-emit_height),4'b0000});
                            velocity_x_q4[free_cursor] <= emit_speed_x;
                            velocity_y_q4[free_cursor] <= emit_speed_y;
                            life[free_cursor] <= 6'd28 + {3'd0,random_state[10:8]};
                            particle_colour[free_cursor] <= emit_colour;
                            random_state <= {random_state[14:0],random_state[15]
                                ^ random_state[13] ^ random_state[12] ^ random_state[10]};
                            if (!second_drop) begin
                                second_drop <= 1; slots_scanned <= 0;
                            end else begin
                                pending_age[scan_group] <= 0;
                                bursts <= bursts + 1'b1;
                                scan_group <= scan_group + 1'b1;
                                groups_scanned <= groups_scanned + 1'b1;
                                spawn_state <= SPAWN_CHECK;
                            end
                        end else if (slots_scanned == 31) begin
                            pending_age[scan_group] <= 0;
                            bursts <= bursts + 1'b1;
                            scan_group <= scan_group + 1'b1;
                            groups_scanned <= groups_scanned + 1'b1;
                            spawn_state <= SPAWN_CHECK;
                        end else slots_scanned <= slots_scanned + 1'b1;
                    end
                    default: spawn_state <= SPAWN_IDLE;
                endcase
            end
        end
    end

    // Five pixel-register stages. The parent consumes these outputs in its
    // sixth/final register, aligned with its unchanged base RGB and text.
    logic [31:0] hit_s1;
    logic [23:0] colour_s1 [0:31];
    logic [7:0] hit_s2;
    logic [23:0] colour_s2 [0:7];
    logic [1:0] hit_s3;
    logic [23:0] colour_s3 [0:1];
    logic hit_s4;
    logic [23:0] colour_s4;
    wire in_plot = raster_x_i >= 11'(PLOT_X) && raster_x_i < 11'd1152
        && raster_y_i >= 10'd128 && raster_y_i < 10'd640;
    genvar particle;
    generate for (particle=0; particle<32; particle=particle+1) begin : raster_particle
        wire [10:0] centre_x = pos_x_q4[particle][14:4];
        wire [9:0] centre_y = pos_y_q4[particle][13:4];
        wire [10:0] dx = raster_x_i >= centre_x ? raster_x_i-centre_x : centre_x-raster_x_i;
        wire [9:0] dy = raster_y_i >= centre_y ? raster_y_i-centre_y : centre_y-raster_y_i;
        wire is_core = dx <= 1 && dy <= 1;
        wire is_halo = dx <= 2 && dy <= 2 && (12'(dx)+12'(dy)) <= 3;
        wire [23:0] faded = faded_colour(particle_colour[particle],life[particle]);
        always_ff @(posedge clk_i) begin
            if (!rst_n_i) begin hit_s1[particle] <= 0; colour_s1[particle] <= 0; end
            else begin
                hit_s1[particle] <= in_plot && life[particle] != 0 && is_halo;
                colour_s1[particle] <= is_core ? faded
                    : {2'b0,faded[23:18],2'b0,faded[15:10],2'b0,faded[7:2]};
            end
        end
    end endgenerate
    genvar group;
    generate for (group=0; group<8; group=group+1) begin : raster_group
        // A balanced pair-of-pairs priority tree, never a 32-slot chain.
        wire left_hit = hit_s1[group*4] || hit_s1[group*4+1];
        wire right_hit = hit_s1[group*4+2] || hit_s1[group*4+3];
        wire [23:0] left_colour = hit_s1[group*4] ? colour_s1[group*4] : colour_s1[group*4+1];
        wire [23:0] right_colour = hit_s1[group*4+2] ? colour_s1[group*4+2] : colour_s1[group*4+3];
        always_ff @(posedge clk_i) begin
            if (!rst_n_i) begin hit_s2[group] <= 0; colour_s2[group] <= 0; end
            else begin
                hit_s2[group] <= left_hit || right_hit;
                colour_s2[group] <= left_hit ? left_colour : right_colour;
            end
        end
    end endgenerate
    genvar half;
    generate for (half=0; half<2; half=half+1) begin : raster_half
        wire left_hit = hit_s2[half*4] || hit_s2[half*4+1];
        wire right_hit = hit_s2[half*4+2] || hit_s2[half*4+3];
        wire [23:0] left_colour = hit_s2[half*4] ? colour_s2[half*4] : colour_s2[half*4+1];
        wire [23:0] right_colour = hit_s2[half*4+2] ? colour_s2[half*4+2] : colour_s2[half*4+3];
        always_ff @(posedge clk_i) begin
            if (!rst_n_i) begin hit_s3[half] <= 0; colour_s3[half] <= 0; end
            else begin
                hit_s3[half] <= left_hit || right_hit;
                colour_s3[half] <= left_hit ? left_colour : right_colour;
            end
        end
    end endgenerate
    always_ff @(posedge clk_i) begin
        if (!rst_n_i) begin hit_s4 <= 0; colour_s4 <= 0; hit_o <= 0; rgb_o <= 0; end
        else begin
            hit_s4 <= |hit_s3;
            colour_s4 <= hit_s3[0] ? colour_s3[0] : colour_s3[1];
            hit_o <= hit_s4;
            rgb_o <= hit_s4 ? colour_s4 : 24'd0;
        end
    end
endmodule
`default_nettype wire
