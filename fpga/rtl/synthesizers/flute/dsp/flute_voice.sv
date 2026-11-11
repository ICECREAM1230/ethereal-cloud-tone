`default_nettype none
module flute_voice (
    input wire clk_i, rst_n_i, sample_tick_i,
    input wire command_valid_i,
    output wire command_ready_o,
    input wire [1:0] command_op_i,
    input wire [6:0] note_i,
    input wire [1:0] preset_i,
    input wire [31:0] seed_i,
    output logic signed [23:0] pcm_o,
    output logic pcm_valid_o, active_o,
    output wire initializing_o,
    output logic reject_o,
    output logic [31:0] deadline_count_o
);
    import flute_math_pkg::*;
    typedef enum logic [3:0] {CLEAR, IDLE, ENV, ISSUE, CONSUME,
        JET_START, JET_WAIT, BORE_START, BORE_WAIT} state_t;
    state_t state;
    logic [8:0] clear_addr;
    logic [3:0] op;
    wire rom_valid;
    wire [8:0] rom_bore_int, rom_jet_int;
    wire [15:0] rom_bore_frac, rom_jet_frac;
    wire signed [31:0] rom_pressure, rom_noise_gain, rom_attack_step;
    logic [8:0] bore_int, jet_int;
    logic [15:0] bore_frac, jet_frac;
    logic signed [31:0] pressure, noise_gain;
    flute_coeff_rom coefficients (
        .note_i(note_i), .preset_i(preset_i), .valid_o(rom_valid),
        .bore_int_o(rom_bore_int), .jet_int_o(rom_jet_int),
        .bore_frac_o(rom_bore_frac), .jet_frac_o(rom_jet_frac),
        .pressure_o(rom_pressure), .noise_gain_o(rom_noise_gain),
        .attack_step_o(rom_attack_step)
    );
    assign command_ready_o = state == IDLE && !sample_tick_i;
    assign initializing_o = state == CLEAR;
    wire command_take = command_valid_i && command_ready_o;
    wire note_on = command_take && command_op_i == 1 && rom_valid;
    wire note_off = command_take && command_op_i == 2;
    wire panic = command_take && command_op_i == 3;
    wire signed [31:0] env, noise;
    flute_noise_env excitation (
        .clk_i(clk_i), .rst_n_i(rst_n_i), .note_on_i(note_on),
        .note_off_i(note_off), .panic_i(panic), .sample_i(state == ENV),
        .seed_i(seed_i), .attack_step_i(rom_attack_step),
        .env_o(env), .noise_o(noise)
    );
    logic signed [31:0] jet_input, bore_input;
    wire signed [31:0] jet_newer, jet_older, bore_newer, bore_older;
    wire jet_valid, bore_valid;
    flute_delay_pair jet_memory (
        .clk_i(clk_i), .rst_n_i(rst_n_i), .clear_i(state == CLEAR),
        .clear_addr_i(clear_addr), .start_i(state == JET_START),
        .sample_i(jet_input), .delay_int_i(jet_int),
        .newer_o(jet_newer), .older_o(jet_older), .valid_o(jet_valid)
    );
    flute_delay_pair bore_memory (
        .clk_i(clk_i), .rst_n_i(rst_n_i), .clear_i(state == CLEAR),
        .clear_addr_i(clear_addr), .start_i(state == BORE_START),
        .sample_i(bore_input), .delay_int_i(bore_int),
        .newer_o(bore_newer), .older_o(bore_older), .valid_o(bore_valid)
    );

    logic signed [31:0] mod_noise, base, breath, lp_part, lp_prev, temp;
    logic signed [31:0] jet_value, square, jet_clip, dc_xprev, dc_prev, bore_last;
    logic signed [31:0] mul_a, mul_b;
    logic signed [63:0] product, interpolation_acc;
    wire signed [64:0] product_wide = {product[63], product};
    wire signed [31:0] product_q = sat32(product_wide >>> 26);
    wire signed [31:0] lp_sum = sat32(wide(lp_part) + wide(product_q));
    wire signed [31:0] temp_next = sat32(-wide(lp_sum));
    wire signed [31:0] dc_next = sat32(wide(jet_clip) - wide(dc_xprev) + wide(product_q));
    wire signed [64:0] interpolation_sum =
        {interpolation_acc[63], interpolation_acc} + product_wide;
    // Exactly one multiply expression in the audio datapath, shared over time.
    always_comb begin
        mul_a = 0; mul_b = 0;
        case (op)
            0: begin mul_a = noise_gain; mul_b = noise; end
            1: begin mul_a = pressure; mul_b = env; end
            2: begin mul_a = base; mul_b = sat32(wide(ONE) + wide(mod_noise)); end
            3: begin mul_a = ONE - POLE; mul_b = bore_last; end
            4: begin mul_a = POLE; mul_b = lp_prev; end
            5: begin mul_a = jet_newer; mul_b = 32'sd65536 - $signed({16'b0,jet_frac}); end
            6: begin mul_a = jet_older; mul_b = $signed({16'b0,jet_frac}); end
            7: begin mul_a = jet_value; mul_b = jet_value; end
            8: begin mul_a = jet_value; mul_b = sat32(wide(square) - wide(ONE)); end
            9: begin mul_a = DC_POLE; mul_b = dc_prev; end
            10: begin mul_a = bore_newer; mul_b = 32'sd65536 - $signed({16'b0,bore_frac}); end
            11: begin mul_a = bore_older; mul_b = $signed({16'b0,bore_frac}); end
            12: begin mul_a = OUTPUT_GAIN; mul_b = bore_last; end
            default: begin end
        endcase
    end
    always_ff @(posedge clk_i) begin
        if (state == ISSUE) product <= mul_a * mul_b;
    end
    always_ff @(posedge clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            state <= CLEAR; clear_addr <= 0; op <= 0;
            pcm_o <= 0; pcm_valid_o <= 0; active_o <= 0; reject_o <= 0;
            deadline_count_o <= 0;
            bore_int <= 0; jet_int <= 0; bore_frac <= 0; jet_frac <= 0;
            pressure <= 0; noise_gain <= 0;
            mod_noise <= 0; base <= 0; breath <= 0; lp_part <= 0;
            lp_prev <= 0; temp <= 0; jet_value <= 0; square <= 0;
            jet_clip <= 0; dc_xprev <= 0; dc_prev <= 0; bore_last <= 0;
            jet_input <= 0; bore_input <= 0; interpolation_acc <= 0;
        end else begin
            pcm_valid_o <= 0; reject_o <= 0;
            if (sample_tick_i && state != IDLE && state != CLEAR)
                deadline_count_o <= deadline_count_o + 32'd1;
            if (note_on || panic) begin
                pcm_o <= 0;
                mod_noise <= 0; base <= 0; breath <= 0; lp_part <= 0;
                lp_prev <= 0; temp <= 0; jet_value <= 0; square <= 0;
                jet_clip <= 0; dc_xprev <= 0; dc_prev <= 0; bore_last <= 0;
                jet_input <= 0; bore_input <= 0; interpolation_acc <= 0;
                if (note_on) begin
                    active_o <= 1; state <= CLEAR; clear_addr <= 0;
                    bore_int <= rom_bore_int; jet_int <= rom_jet_int;
                    bore_frac <= rom_bore_frac; jet_frac <= rom_jet_frac;
                    pressure <= rom_pressure; noise_gain <= rom_noise_gain;
                end else active_o <= 0;
            end else begin
                if (command_take && command_op_i != 2)
                    reject_o <= 1; // illegal ON, or reserved operation zero
                case (state)
                    CLEAR: begin
                        if (clear_addr == 511) state <= IDLE;
                        else clear_addr <= clear_addr + 9'd1;
                        if (sample_tick_i) begin pcm_o <= 0; pcm_valid_o <= 1; end
                    end
                    IDLE: if (sample_tick_i) begin
                        if (active_o) state <= ENV;
                        else begin pcm_o <= 0; pcm_valid_o <= 1; end
                    end
                    ENV: begin op <= 0; state <= ISSUE; end
                    ISSUE: state <= CONSUME;
                    CONSUME: begin
                        op <= op + 4'd1; state <= ISSUE;
                        case (op)
                            0: mod_noise <= product_q;
                            1: base <= product_q;
                            2: breath <= product_q;
                            3: lp_part <= product_q;
                            4: begin
                                lp_prev <= lp_sum; temp <= temp_next;
                                jet_input <= sat32(wide(breath) - (wide(temp_next) >>> 1));
                                state <= JET_START;
                            end
                            5,10: interpolation_acc <= product;
                            6: jet_value <= sat32(interpolation_sum >>> 16);
                            7: square <= product_q;
                            8: jet_clip <= product_q > ONE ? ONE : (product_q < -ONE ? -ONE : product_q);
                            9: begin
                                dc_prev <= dc_next; dc_xprev <= jet_clip;
                                bore_input <= sat32(wide(dc_next) + (wide(temp) >>> 1));
                                state <= BORE_START;
                            end
                            11: bore_last <= sat32(interpolation_sum >>> 16);
                            12: begin
                                pcm_o <= sat24(wide(product_q) >>> 3);
                                pcm_valid_o <= 1; state <= IDLE;
                            end
                            default: begin state <= IDLE; end
                        endcase
                    end
                    JET_START: state <= JET_WAIT;
                    JET_WAIT: if (jet_valid) state <= ISSUE;
                    BORE_START: state <= BORE_WAIT;
                    BORE_WAIT: if (bore_valid) state <= ISSUE;
                    default: state <= CLEAR;
                endcase
            end
        end
    end
endmodule
`default_nettype wire
