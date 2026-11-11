// Sixteen independent 4096-sample Karplus--Strong rings in the existing
// official 65536x16 SDP RAM. VOICES=4 retains the legacy numeric oracle.
// The foreground completes feedback within the current audio frame.
// Initialization uses otherwise idle clocks, with generation-checked completion.
`default_nettype none
module physical_model_top #(
    parameter bit ENABLE_AE350_VOICE_OWNER=1'b0,
    parameter integer VOICES = 16,
    parameter bit ENABLE_KS_LIMITER = (VOICES == 16),
    parameter bit ENABLE_KS_TIMBRES = 0
) (
    input wire clk_i, rst_n_i, sample_tick_i,
    input wire [9:0] slot_i,
    input wire command_valid_i,
    output wire command_ready_o,
    input wire [2:0] command_op_i,
    input wire [3:0] voice_i,
    input wire mode_i,
    input wire [14:0] length_i,
    input wire signed [15:0] tuning_i,
    input wire [15:0] loss_i, amplitude_i, gain_left_i, gain_right_i,
    input wire [31:0] seed_i, sequence_i,
    output reg signed [23:0] pcm_left_o, pcm_right_o,
    output reg pcm_valid_o, mode_o,
    output reg [31:0] mode_sequence_o, accepted_sequence_o,
    output reg [31:0] applied_sequence_o,
    output wire [VOICES*32-1:0] started_sequences_o,
    output wire [VOICES-1:0] active_mask_o, init_mask_o, release_mask_o,
    output reg [31:0] frame_count_o, deadline_count_o, reject_count_o,
    input wire model_valid_i,
    output wire model_ready_o,
    input wire [2047:0] model_words_i,
    input wire [31:0] command_model_generation_i, command_template_generation_i, command_note_identity_i,
    output reg [31:0] model_completed_generation_o, model_result_o,
    output reg [31:0] model_applied_generation_o, model_applied_crc_o,
    output reg [1:0] model_active_id_o,
    output reg [3:0] model_state_o,
    output reg [31:0] pcm_model_generation_o, pcm_frame_o
);
    // Only these two power-of-two partitions are supported. The legacy
    // configuration is for regression, not a second production capacity.
    localparam integer RING_BITS = VOICES == 4 ? 14 : 12;
    localparam integer MAX_LENGTH = 1 << RING_BITS;
    // Legacy release loads 960 and only counts down. New timbres support
    // release lengths through 192017; keep their full eighteen-bit range.
    localparam integer RELEASE_COUNT_BITS = ENABLE_KS_TIMBRES ? 18 : 10;
    localparam [5:0] MIX_SHIFT = VOICES == 4 ? 2 : 4;
    // synthesis translate_off
    initial begin
        if (VOICES != 4 && VOICES != 16) $error("KS VOICES must be 4 or 16");
    end
    // synthesis translate_on
    localparam [2:0] ON=1, OFF=2, PANIC=3, MODE=4, GAIN=5, LOSS=6, KILL=7;
    localparam [2:0] I_IDLE=0, I_FILL=1, I_DIV=2, I_DC=3, I_DONE=4, I_SETUP=5;
    localparam [5:0] IDLE=0, F_CHOOSE=1, F_READ=2, F_CAPTURE=3,
        F_RHO=4, F_X=5, F_DELTA=6, F_AP=7, F_A=8,
        F_GAIN=9, F_GAIN_TRUNC=10, F_FADE=11, F_FADE_TRUNC=12,
        F_RELEASE=13, F_RELEASE_TRUNC=14, F_STEAL=15,
        F_STEAL_TRUNC=16, F_WRITE=17, F_ACCUM=18, F_NEXT=19,
        F_OUTPUT=20, B_RANDOM=21, B_MUL=22, B_WRITE=23,
        B_READ=24, B_CAPTURE=25, B_DC_WRITE=26,
        F_LIM_ABS=27, F_LIM_LEVEL=28, F_LIM_WAIT=29,
        F_LIM_GAIN=30, F_LIM_MUL=31, F_LIM_PUBLISH=32,
        F_LOSS=33, F_DISP_MUL=34, F_DISP=35, F_BODY=36, F_BODY_WAIT=37, F_PUBLISH_INPUT=38;
    localparam [3:0] M_IDLE=0,M_FADING=1,M_APPLYING=2,M_FIRST=3,M_FAILED=4;
    reg [2047:0] model_words, pending_model_words, previous_model_words;
    reg [31:0] current_model_generation, previous_model_generation, canceled_model_generation;
    reg [9:0] model_fade_left;
    wire new_model = ENABLE_KS_TIMBRES && model_words[2*32+:32]!=0;
    wire shaped = new_model && model_words[20*32+2];
    wire dispersion = new_model && model_words[20*32];
    wire body_enabled = new_model && model_words[20*32+1];
    wire model_busy = ENABLE_KS_TIMBRES && model_state_o!=M_IDLE && model_state_o!=M_FAILED;
    wire safety_command = cmd_pending && (cmd_op==PANIC || cmd_op==MODE);
    wire incoming_safety = command_valid_i && command_ready_o && command_legal &&
                           (command_op_i==PANIC || command_op_i==MODE);
    wire model_apply_tick = ENABLE_KS_TIMBRES && sample_tick_i && model_state_o==M_APPLYING && !safety_command && !incoming_safety;
    assign model_ready_o = ENABLE_KS_TIMBRES && !model_busy;

    // Parent validates canonical image and CRC. Enforce arithmetic bounds here
    // as well, so an independently connected core never accepts unstable state.
    function automatic valid_model(input [2047:0] w);
        reg good;
        reg signed [63:0] c,sq,gain,radius2,bound;
        reg [63:0] weights,release_check;
        integer k;
        begin
            good=w[0+:32]==32'h4b535431 && w[32+:32]==32'h00010001 &&
                w[2*32+:32]<=2 && w[3*32+:32]!=0 &&
                w[5*32+:32]==91015625 && w[6*32+:32]==948 && w[63*32+:32]==64 &&
                w[20*32+:32]<=7 && w[21*32+:32]==480;
            if(w[2*32+:32]!=0) begin
                good=good && w[7*32+:32]>=107374182 && w[7*32+:32]<=966367642 &&
                    w[8*32+:32]<=1342177280 && w[9*32+:32]<=122016117 &&
                    w[8*32+:32]>=149130808 && w[9*32+:32]>=70640909 &&
                    w[10*32+:32]<=32768 && w[11*32+:32]<=32767 && w[12*32+:32]<=32768 &&
                    w[13*32+:32]<=16384 && $signed(w[14*32+:32])>=-26214 && $signed(w[14*32+:32])<=26214 &&
                    w[15*32+:32]>=480 && w[15*32+:32]<=192017 &&
                    w[16*32+:32]<=137 && w[17*32+:32]<w[15*32+:32] && w[18*32+:32]<=32768;
                release_check=64'(w[16*32+:32])*64'(w[15*32+:32])+64'(w[17*32+:32]);
                good=good && release_check==65536;
                weights={32'd0,w[18*32+:32]};
                for(k=0;k<4;k=k+1) begin
                    c=64'($signed(w[(24+k*4)*32+:32]));sq=64'($signed(w[(25+k*4)*32+:32]));
                    gain=64'($signed(w[(26+k*4)*32+:32]));
                    radius2=c*c+sq*sq;bound=(64'sd131072-gain)*(64'sd131072-gain);
                    good=good && c>=-131072 && c<=131071 && sq>=-131072 && sq<=131071 &&
                        gain>=0 && gain<=131072 && radius2<64'sd17179869184 && radius2<=bound &&
                        w[(27+k*4)*32+:32]<=32768;
                    weights=weights+{32'd0,w[(27+k*4)*32+:32]};
                end
                good=good && weights<=32768;
            end else good=good && w[20*32+:32]==0;
            valid_model=good;
        end
    endfunction


    reg active[0:VOICES-1];
    reg [2:0] init_phase[0:VOICES-1];
    reg [15:0] generation[0:VOICES-1];
    reg [15:0] done_generation[0:VOICES-1];
    reg [14:0] ring_length[0:VOICES-1], cursor[0:VOICES-1], init_cursor[0:VOICES-1];
    reg signed [15:0] tuning[0:VOICES-1], prev_read[0:VOICES-1], mean[0:VOICES-1];
    reg [15:0] loss[0:VOICES-1], amplitude[0:VOICES-1], gain_left[0:VOICES-1], gain_right[0:VOICES-1];
    reg signed [23:0] x_prev[0:VOICES-1], a_prev[0:VOICES-1];
    reg [31:0] rng[0:VOICES-1], started[0:VOICES-1], note_sequence[0:VOICES-1];
    reg signed [31:0] init_sum[0:VOICES-1];
    reg [5:0] fade_step[0:VOICES-1];
    reg [RELEASE_COUNT_BITS-1:0] release_left[0:VOICES-1];
    reg [17:0] release_remainder[0:VOICES-1];
    reg [16:0] release_gain[0:VOICES-1];
    reg [7:0] steal_left[0:VOICES-1];
    reg pending_note[0:VOICES-1];
    reg [14:0] pending_length[0:VOICES-1];
    reg signed [15:0] pending_tuning[0:VOICES-1];
    reg [15:0] pending_loss[0:VOICES-1], pending_amplitude[0:VOICES-1], pending_left[0:VOICES-1], pending_right[0:VOICES-1];
    reg [31:0] pending_seed[0:VOICES-1], pending_sequence[0:VOICES-1];
    reg [31:0] note_identity[0:VOICES-1], pending_identity[0:VOICES-1];
    reg [30:0] init_u[0:VOICES-1], init_step[0:VOICES-1];
    reg signed [15:0] noise_prev[0:VOICES-1];
    reg signed [23:0] disp_x_prev[0:VOICES-1], disp_y_prev[0:VOICES-1];

    reg cmd_pending;
    reg [2:0] cmd_op;
    reg [3:0] cmd_voice;
    reg cmd_mode;
    reg [14:0] cmd_length;
    reg signed [15:0] cmd_tuning;
    reg [15:0] cmd_loss, cmd_amplitude, cmd_left, cmd_right;
    reg [31:0] cmd_seed, cmd_sequence, cmd_model, cmd_template, cmd_identity;
    wire command_legal = sequence_i != 0 && voice_i < VOICES && command_op_i >= ON && (ENABLE_AE350_VOICE_OWNER || command_op_i <= LOSS) &&
        (command_op_i != ON || (length_i >= 2 && length_i <= MAX_LENGTH &&
         tuning_i != -16'sd32768 && loss_i <= 32767 && amplitude_i <= 32767)) &&
        (command_op_i != LOSS || loss_i <= 32767) &&
        (!ENABLE_KS_TIMBRES || command_op_i==PANIC || command_op_i==MODE ||
          ((command_op_i==OFF || !model_busy) && command_model_generation_i==current_model_generation &&
           (!new_model || command_note_identity_i!=0)));
    assign command_ready_o = !cmd_pending;
    genvar g;
    generate for (g=0; g<VOICES; g=g+1) begin: status
        assign active_mask_o[g] = active[g];
        assign init_mask_o[g] = init_phase[g] != I_IDLE || pending_note[g];
        assign release_mask_o[g] = active[g] && (release_left[g] != 0 || steal_left[g] != 0);
        assign started_sequences_o[g*32 +: 32] = started[g];
    end endgenerate

    function automatic [15:0] ring_address(input [3:0] ring_voice, input [14:0] offset);
        if (VOICES == 4) ring_address = {ring_voice[1:0],offset[13:0]};
        else ring_address = {ring_voice,offset[11:0]};
    endfunction

    function automatic signed [47:0] trunc_sym(input signed [47:0] value, input [5:0] shift);
        trunc_sym = value < 0 ? -((-value) >>> shift) : value >>> shift;
    endfunction
    function automatic signed [23:0] sat24(input signed [47:0] value);
        if (value > 8388607) sat24 = 24'sh7fffff;
        else if (value < -8388608) sat24 = 24'sh800000;
        else sat24 = value[23:0];
    endfunction
    function automatic signed [15:0] sat16(input signed [47:0] value);
        if (value > 32767) sat16 = 16'sh7fff;
        else if (value < -32768) sat16 = 16'sh8000;
        else sat16 = value[15:0];
    endfunction
    function automatic [31:0] xorshift32(input [31:0] value);
        reg [31:0] a,b;
        begin a=value^(value<<13); b=a^(a>>17); xorshift32=b^(b<<5); end
    endfunction

    reg [5:0] state;
    reg frame_pending, frame_running, work_epoch_valid;
    reg [31:0] frame_epoch, working_epoch;
    reg [3:0] frame_voice;
    reg [3:0] bg_voice, bg_rr;
    reg [15:0] work_generation, bg_generation;
    reg [15:0] frame_address, bg_address;
    // Snapshot per-voice operands once. Dynamic metadata lookup must not
    // feed the generated RAM enable/bank-select or the final state writeback.
    reg work_valid, bg_write_authorized, bg_read_authorized;
    reg [VOICES-1:0] work_onehot;
    reg bg_epoch_valid;
    reg [VOICES-1:0] bg_onehot;
    reg [14:0] bg_length, bg_cursor;
    reg [31:0] bg_rng, bg_next_rng;
    reg [15:0] bg_amplitude;
    reg signed [31:0] bg_sum;
    reg signed [15:0] bg_mean, bg_dc_data, frame_ram_data;
    reg [14:0] work_length, work_cursor, work_pending_length;
    reg signed [15:0] work_tuning, work_prev, work_pending_tuning;
    reg signed [23:0] work_x_prev, work_a_prev, work_disp_x, work_disp_y;
    reg signed [23:0] disp_value, loss_value;
    reg signed [40:0] disp_product;
    reg [31:0] work_pending_identity;
    reg [30:0] bg_u,bg_step;
    reg signed [15:0] bg_noise;
    reg div_setup;
    reg [15:0] work_loss, work_left, work_right;
    reg work_pending_note;
    reg [15:0] work_pending_loss, work_pending_amplitude, work_pending_left, work_pending_right;
    reg [31:0] work_pending_seed, work_pending_sequence;
    reg signed [15:0] sample_read, bg_raw, bg_read;
    reg signed [33:0] rho_product;
    reg signed [23:0] x_value, a_value;
    reg signed [24:0] delta;
    reg signed [40:0] ap_product;
    reg signed [32:0] gain_product_left, gain_product_right;
    reg signed [23:0] term_left, term_right;
    reg signed [41:0] envelope_product_left, envelope_product_right;
    reg [16:0] frame_release_gain, frame_fade_gain, frame_steal_gain;
    // State-exclusive operands share two explicit gain/envelope DSPs. Keep
    // the three envelope multiplications and trunc_sym boundaries distinct.
    wire [16:0] envelope_gain = state==F_FADE ? frame_fade_gain :
        state==F_RELEASE ? frame_release_gain : frame_steal_gain;
    wire signed [25:0] gain_operand_left = state==F_GAIN ?
        {{10{sample_read[15]}},sample_read} : {{2{term_left[23]}},term_left};
    wire signed [25:0] gain_operand_right = state==F_GAIN ?
        {{10{sample_read[15]}},sample_read} : {{2{term_right[23]}},term_right};
    wire signed [17:0] gain_factor_left = state==F_GAIN ? {2'b00,work_left} : {1'b0,envelope_gain};
    wire signed [17:0] gain_factor_right = state==F_GAIN ? {2'b00,work_right} : {1'b0,envelope_gain};
    wire signed [47:0] gain_envelope_left, gain_envelope_right;
    ks_dsp_mac27x18 u_gain_envelope_left(.a_i(gain_operand_left),.b_i(gain_factor_left),
        .d_i(26'd0),.c_i(48'd0),.presub_i(1'b0),.y_o(gain_envelope_left));
    ks_dsp_mac27x18 u_gain_envelope_right(.a_i(gain_operand_right),.b_i(gain_factor_right),
        .d_i(26'd0),.c_i(48'd0),.presub_i(1'b0),.y_o(gain_envelope_right));
    // The foreground feedback states are mutually exclusive. The DSP preadder
    // performs the original rho addition and dispersion subtraction exactly.
    wire dispersion_multiply = ENABLE_KS_TIMBRES && state==F_DISP_MUL;
    wire signed [25:0] feedback_a = state==F_RHO ? {{10{sample_read[15]}},sample_read} :
        dispersion_multiply ? {{2{loss_value[23]}},loss_value} : {delta[24],delta};
    wire signed [25:0] feedback_d = state==F_RHO ? {{10{work_prev[15]}},work_prev} :
        dispersion_multiply ? {{2{work_disp_y[23]}},work_disp_y} : 26'd0;
    wire signed [17:0] feedback_b = state==F_RHO ? {2'b00,work_loss} :
        dispersion_multiply ? {{2{model_words[14*32+15]}},model_words[14*32+:16]} : {{2{work_tuning[15]}},work_tuning};
    wire signed [47:0] feedback_product;
    ks_dsp_mac27x18 #(.PREADD(1'b1),.DYNAMIC_PRESUB(ENABLE_KS_TIMBRES)) u_feedback_mac(
        .a_i(feedback_a),.b_i(feedback_b),.d_i(feedback_d),.c_i(48'd0),
        .presub_i(dispersion_multiply),.y_o(feedback_product));
    reg [RELEASE_COUNT_BITS-1:0] work_release_left;
    reg [17:0] work_release_remainder;
    reg [7:0] work_steal_left;
    // One foreground voice owns these shared next-state adders. A sample
    // boundary revokes frame_current, so a stale snapshot cannot commit.
    wire [14:0] work_cursor_inc = work_cursor + 15'd1;
    wire [14:0] work_cursor_next = work_cursor_inc == work_length ? 15'd0 : work_cursor_inc;
    wire [5:0] work_fade_next = frame_fade_gain[16:11] + 6'd1;
    wire [RELEASE_COUNT_BITS-1:0] work_release_left_next = work_release_left - RELEASE_COUNT_BITS'(1);
    wire [18:0] release_rem_sum={1'b0,work_release_remainder}+{1'b0,model_words[17*32+:18]};
    wire new_release_carry=release_rem_sum>={1'b0,model_words[15*32+:18]};
    wire work_release_carry = work_release_remainder < 10'd256;
    wire [16:0] work_release_gain_next = new_model ?
        (work_release_left==1 ? 17'd0 : frame_release_gain-model_words[16*32+:17]-17'(new_release_carry)) :
        frame_release_gain - (work_release_carry ? 17'd69 : 17'd68);
    // The ten-bit sum wraps; +768 is the original subtraction by 256.
    wire [17:0] work_release_remainder_next = new_model ?
        18'(release_rem_sum-(new_release_carry ? {1'b0,model_words[15*32+:18]} : 19'd0)) :
        {8'd0,10'(work_release_remainder + (work_release_carry ? 10'd704 : 10'd768))};
    wire [7:0] work_steal_next = work_steal_left - 8'd1;
    // Keep the pre-clip sum wide; the limiter owns the final output scale.
    reg signed [31:0] sum_left, sum_right;
    reg signed [31:0] lim_acc_l, lim_acc_r, lim_product_l, lim_product_r;
    reg [31:0] lim_abs_l, lim_abs_r, lim_level;
    reg [18:0] lim_gain_q, lim_candidate_q;
    reg [16:0] lim_gain;
    reg [2:0] lim_wait;
    wire [18:0] lim_gain_q_next;
    wire [16:0] lim_gain_lin;
    // sum * 64 restores the old /4 reference scale (before limiting).
    // Detect |sum|/2: log code 240 = 32768, hence sum 65536 = -6.02 dBFS.
    // Only the detector shifts; audio keeps every pre-multiply sum bit.
    generate if (ENABLE_KS_LIMITER) begin : g_master_limiter
        limiter #(.LEVEL_W(32), .SUB(11), .INSTANT_PEAK(1'b1),
                  .HEADROOM_CODES(2)) u_limiter (
            .clk(clk_i), .rst_n(rst_n_i), .level(lim_level),
            .gain_q_in(lim_gain_q), .thresh_code(8'd240),
            .attack_q(19'd256), .release_q(19'd1),
            .gain_q_out(lim_gain_q_next), .gain_lin(lim_gain_lin)
        );
    end else begin : g_no_master_limiter
        assign lim_gain_q_next = 19'd0;
        assign lim_gain_lin = 17'h10000;
    end endgenerate
    wire signed [49:0] lim_mul_l = lim_acc_l * $signed({1'b0,lim_gain});
    wire signed [49:0] lim_mul_r = lim_acc_r * $signed({1'b0,lim_gain});
    // Constant-gate the complete timbre arithmetic cone. Unreachable FSM
    // states alone do not remove explicitly instantiated DSPs in Gowin.
    wire signed [47:0] lp, new_rho;
    wire signed [15:0] color_sample, excitation_sample;
    generate if (ENABLE_KS_TIMBRES) begin : g_timbre_arithmetic
    wire signed [47:0] loss_weighted;
    ks_dsp_mac27x18 #(.PREADD(1'b1),.PRESUB(1'b1)) u_loss_weighted(
        .a_i({{10{work_prev[15]}},work_prev}),.b_i({2'b00,model_words[13*32+:16]}),
        .d_i({{10{sample_read[15]}},sample_read}),.c_i(48'd0),.presub_i(1'b0),.y_o(loss_weighted));
    assign lp=$signed({{32{sample_read[15]}},sample_read})+trunc_sym(loss_weighted,15);
    assign new_rho=lp*$signed({1'b0,work_loss});
    wire signed [47:0] shape_pos=$signed({1'b0,bg_u}) < $signed({1'b0,model_words[7*32+:31]}) ?
        $signed({1'b0,bg_u}) : $signed({1'b0,(31'h7fffffff-bg_u)});
    wire [31:0] shape_inv=bg_u<model_words[7*32+:31] ? model_words[8*32+:32] : model_words[9*32+:32];
    wire signed [79:0] triangle_product=shape_pos*$signed({1'b0,shape_inv});
    wire signed [47:0] triangle_level=48'(triangle_product>>>42);
    wire signed [15:0] triangle_sample=sat16((triangle_level<<<1)-48'sd32768);
    wire [31:0] brightness_raw=model_words[11*32+:32]+((model_words[12*32+:32]*(bg_amplitude>8192 ? 32'd8192 : {16'd0,bg_amplitude}))>>13);
    wire [15:0] brightness=brightness_raw>32767 ? 16'd32767 : brightness_raw[15:0];
    wire signed [47:0] color_product;
    ks_dsp_mac27x18 #(.PREADD(1'b1),.PRESUB(1'b1)) u_color_mac(
        .a_i({{10{bg_raw[15]}},bg_raw}),.b_i({2'b00,brightness}),
        .d_i({{10{bg_noise[15]}},bg_noise}),.c_i(48'd0),.presub_i(1'b0),.y_o(color_product));
    assign color_sample=sat16($signed({{32{bg_noise[15]}},bg_noise})+trunc_sym(color_product,15));
    // validate_model restricts this mix coefficient to 0..32768. Both
    // products and their post-add fit signed 34 bits inside the 48-bit ALU.
    wire [16:0] triangle_mix_gain = 17'd32768-{1'b0,model_words[10*32+:16]};
    wire signed [47:0] noise_mix, excitation_mix;
    ks_dsp_mac27x18 u_noise_mix(.a_i({{10{color_sample[15]}},color_sample}),
        .b_i({2'b00,model_words[10*32+:16]}),.d_i(26'd0),.c_i(48'd0),.presub_i(1'b0),.y_o(noise_mix));
    ks_dsp_mac27x18 u_excitation_mac(.a_i({{10{triangle_sample[15]}},triangle_sample}),
        .b_i({1'b0,triangle_mix_gain}),.d_i(26'd0),.c_i(noise_mix),.presub_i(1'b0),.y_o(excitation_mix));
    assign excitation_sample=sat16(trunc_sym(excitation_mix,15));
    end else begin : g_no_timbre_arithmetic
        assign lp = 48'sd0;
        assign new_rho = 48'sd0;
        assign color_sample = 16'sd0;
        assign excitation_sample = 16'sd0;
    end endgenerate
    reg signed [31:0] bg_product;
    wire signed [47:0] bg_scaled = trunc_sym({{16{bg_product[31]}},bg_product},15);
    // Epoch counters are constant within a frame. Every sample boundary
    // revokes this registered authorization before any old transaction can
    // commit; avoid putting a 32-bit comparison on the generated RAM ports.
    wire frame_current = frame_running && work_epoch_valid && !sample_tick_i && (!ENABLE_KS_TIMBRES || (!safety_command && !incoming_safety));
    wire voice_current = active[frame_voice] && generation[frame_voice] == work_generation;
    wire bg_current = generation[bg_voice] == bg_generation;
    // An ordinary sample tick preempts foreground work, not an authorized
    // background RAM transaction. Revoke RAM and metadata together only when
    // control actually cancels that initializer at this sample boundary.
    wire bg_transaction_current = bg_epoch_valid && !(sample_tick_i &&
        (model_apply_tick || safety_command || incoming_safety ||
         (cmd_pending && (cmd_op==ON || cmd_op==OFF) && cmd_voice==bg_voice)));
    reg ram_write, ram_read;
    reg [15:0] ram_write_address, ram_read_address;
    reg [15:0] ram_write_data;
    wire [15:0] ram_read_data;
    always @* begin
        ram_write=0; ram_read=0;
        // CEA/CEB authorize a transaction. Disabled ports need no address
        // masking; keep authorization out of the vendor bank-decode cone.
        ram_write_address=state == F_WRITE ? frame_address : bg_address;
        ram_read_address=state == F_READ ? frame_address : bg_address;
        ram_write_data=state == F_WRITE ? frame_ram_data : state == B_WRITE ? bg_scaled[15:0] : bg_dc_data;
        if (state == F_READ && frame_current && work_valid) begin
            ram_read=1;
        end else if (state == F_WRITE && frame_current && work_valid) begin
            ram_write=1;
        end else if (state == B_WRITE && bg_write_authorized && (!ENABLE_KS_TIMBRES || bg_transaction_current)) begin
            ram_write=1;
        end else if (state == B_READ && bg_read_authorized && (!ENABLE_KS_TIMBRES || bg_transaction_current)) begin
            ram_read=1;
        end else if (state == B_DC_WRITE && bg_read_authorized && (!ENABLE_KS_TIMBRES || bg_transaction_current)) begin
            ram_write=1;
        end
    end
    ks_delay_ram delay_ram (
        .dout(ram_read_data), .clka(clk_i), .cea(ram_write),
        .clkb(clk_i), .ceb(ram_read), .oce(1'b1), .reset(!rst_n_i),
        .ada(ram_write_address), .din(ram_write_data), .adb(ram_read_address)
    );
    wire body_done;
    wire signed [31:0] body_left,body_right;
    wire publish_now=frame_current && working_epoch==frame_epoch &&
        ((ENABLE_KS_LIMITER && state==F_LIM_PUBLISH) || (!ENABLE_KS_LIMITER && state==F_PUBLISH_INPUT) ||
         (!ENABLE_KS_TIMBRES && !ENABLE_KS_LIMITER && state==F_OUTPUT));
    generate if(ENABLE_KS_TIMBRES) begin:g_timbre_body
        ks_timbre_body u_body(.clk_i(clk_i),.rst_n_i(rst_n_i),
            .clear_i(model_apply_tick || (sample_tick_i && (safety_command || incoming_safety))),
            .cancel_i(sample_tick_i),.start_i(state==F_BODY && frame_current),
            .commit_i(publish_now && body_enabled),.words_i(model_words),
            .left_i(sum_left),.right_i(sum_right),.done_o(body_done),.left_o(body_left),.right_o(body_right));
    end else begin:g_no_timbre_body
        assign body_done=0;assign body_left=0;assign body_right=0;
    end endgenerate
    reg div_start;
    reg [3:0] div_voice;
    reg [15:0] div_generation;
    reg signed [31:0] div_sum;
    reg [14:0] div_length;
    wire div_busy, div_done;
    wire signed [31:0] div_quotient;
    ks_mean_divider mean_divider (
        .clk_i(clk_i), .rst_n_i(rst_n_i), .start_i(div_start),
        .dividend_i(div_sum), .divisor_i(div_length),
        .busy_o(div_busy), .done_o(div_done), .quotient_o(div_quotient)
    );

    integer i, scan;
    reg found;
    reg [3:0] selected;
    wire [VOICES-1:0] bg_request;
    wire [3:0] bg_rr_base = (bg_rr + 4'd0) & 4'(VOICES-1);
    reg found_after, found_before;
    reg [3:0] selected_after, selected_before;
    generate for (genvar r=0; r<VOICES; r=r+1) begin: background_request
        assign bg_request[r] = init_phase[r] == I_FILL || init_phase[r] == I_DC;
    end endgenerate
    always @* begin
        found_after=0; found_before=0;
        selected_after=bg_rr; selected_before=bg_rr;
        // Constant-index phase decoding feeds two static priority encoders.
        // Descending traversal leaves the lowest eligible index in each half.
        for (scan=VOICES-1; scan>=0; scan=scan-1) begin
            if (bg_request[scan] && scan >= {28'd0,bg_rr_base}) begin
                found_after=1; selected_after=4'(scan);
            end
            if (bg_request[scan] && scan < {28'd0,bg_rr_base}) begin
                found_before=1; selected_before=4'(scan);
            end
        end
        found=found_after || found_before;
        selected=found_after ? selected_after : selected_before;
    end

    always @(posedge clk_i) begin
        if (!rst_n_i) begin
            model_words<=0;pending_model_words<=0;previous_model_words<=0;
            model_completed_generation_o<=0;model_result_o<=0;model_applied_generation_o<=0;model_applied_crc_o<=0;
            model_active_id_o<=0;model_state_o<=M_IDLE;model_fade_left<=0;
            current_model_generation<=0;previous_model_generation<=0;canceled_model_generation<=0;pcm_model_generation_o<=0;pcm_frame_o<=0;
            cmd_model<=0;cmd_template<=0;cmd_identity<=0;
            work_disp_x<=0;work_disp_y<=0;disp_value<=0;loss_value<=0;disp_product<=0;work_pending_identity<=0;
            bg_u<=0;bg_step<=0;bg_noise<=0;div_setup<=0;
            state<=IDLE; frame_pending<=0; frame_running<=0; work_epoch_valid<=0; frame_epoch<=0; working_epoch<=0;
            frame_voice<=0; bg_voice<=0; bg_rr<=0; work_generation<=0; bg_generation<=0;
            frame_address<=0; bg_address<=0; sample_read<=0; bg_raw<=0; bg_read<=0;
            work_valid<=0; bg_write_authorized<=0; bg_read_authorized<=0; work_onehot<=0;
            bg_epoch_valid<=0; bg_onehot<=0; bg_length<=2; bg_cursor<=0; bg_rng<=1; bg_next_rng<=1;
            bg_amplitude<=0; bg_sum<=0; bg_mean<=0; bg_dc_data<=0; frame_ram_data<=0;
            work_length<=2; work_cursor<=0; work_pending_length<=2;
            work_tuning<=0; work_prev<=0; work_pending_tuning<=0; work_x_prev<=0; work_a_prev<=0;
            work_loss<=0; work_left<=0; work_right<=0; work_pending_note<=0;
            work_pending_loss<=0; work_pending_amplitude<=0; work_pending_left<=0; work_pending_right<=0;
            work_pending_seed<=1; work_pending_sequence<=0;
            rho_product<=0; x_value<=0; a_value<=0; delta<=0; ap_product<=0;
            gain_product_left<=0; gain_product_right<=0; term_left<=0; term_right<=0;
            envelope_product_left<=0; envelope_product_right<=0; frame_release_gain<=0;
            work_release_left<=0; work_release_remainder<=0; work_steal_left<=0;
            frame_fade_gain<=0; frame_steal_gain<=0; sum_left<=0; sum_right<=0; bg_product<=0;
            lim_acc_l<=0; lim_acc_r<=0; lim_product_l<=0; lim_product_r<=0;
            lim_abs_l<=0; lim_abs_r<=0; lim_level<=0; lim_wait<=0;
            lim_gain_q<=0; lim_candidate_q<=0; lim_gain<=17'h10000;
            div_start<=0; div_voice<=0; div_generation<=0; div_sum<=0; div_length<=2;
            cmd_pending<=0; cmd_op<=0; cmd_voice<=0; cmd_mode<=0; cmd_length<=2;
            cmd_tuning<=0; cmd_loss<=0; cmd_amplitude<=0; cmd_left<=0; cmd_right<=0; cmd_seed<=1; cmd_sequence<=0;
            pcm_left_o<=0; pcm_right_o<=0; pcm_valid_o<=0; mode_o<=0;
            mode_sequence_o<=0; accepted_sequence_o<=0;applied_sequence_o<=0; frame_count_o<=0; deadline_count_o<=0; reject_count_o<=0;
            for (i=0;i<VOICES;i=i+1) begin
                active[i]<=0; init_phase[i]<=I_IDLE; generation[i]<=0; done_generation[i]<=0; ring_length[i]<=2;
                cursor[i]<=0; init_cursor[i]<=0; tuning[i]<=0; prev_read[i]<=0; mean[i]<=0;
                loss[i]<=0; amplitude[i]<=0; gain_left[i]<=0; gain_right[i]<=0;
                x_prev[i]<=0; a_prev[i]<=0; rng[i]<=1; started[i]<=0; note_sequence[i]<=0; init_sum[i]<=0;
                fade_step[i]<=1; release_left[i]<=0; release_remainder[i]<=0; release_gain[i]<=65536;
                steal_left[i]<=0; pending_note[i]<=0; pending_length[i]<=2; pending_tuning[i]<=0;
                pending_loss[i]<=0; pending_amplitude[i]<=0; pending_left[i]<=0; pending_right[i]<=0;
                pending_seed[i]<=1; pending_sequence[i]<=0;
                note_identity[i]<=0;pending_identity[i]<=0;init_u[i]<=0;init_step[i]<=0;noise_prev[i]<=0;
                disp_x_prev[i]<=0;disp_y_prev[i]<=0;
            end
        end else begin
            pcm_valid_o<=0; div_start<=0;
            if(ENABLE_KS_TIMBRES && model_valid_i && model_ready_o) begin
                if(model_words_i[3*32+:32]!=0 && model_words_i[3*32+:32]==canceled_model_generation) begin
                    model_completed_generation_o<=model_words_i[3*32+:32];model_result_o<=1;model_state_o<=M_IDLE;
                end else if(!valid_model(model_words_i)) begin
                    model_completed_generation_o<=model_words_i[3*32+:32];model_result_o<=2;model_state_o<=M_FAILED;
                end else begin
                    pending_model_words<=model_words_i;previous_model_words<=model_words;
                    previous_model_generation<=current_model_generation;model_fade_left<=480;model_state_o<=M_FADING;
                end
            end
            if(publish_now) begin
                pcm_model_generation_o<=current_model_generation;pcm_frame_o<=frame_epoch;
                if(ENABLE_KS_TIMBRES && model_state_o==M_FIRST) begin
                    model_applied_generation_o<=current_model_generation;model_applied_crc_o<=model_words[60*32+:32];
                    model_active_id_o<=model_words[2*32+:2];model_completed_generation_o<=current_model_generation;
                    model_result_o<=0;model_state_o<=M_IDLE;
                end
            end
            if (command_valid_i && command_ready_o) begin
                if (!command_legal) reject_count_o<=reject_count_o+1'b1;
                else begin
                    cmd_pending<=1; cmd_op<=command_op_i; cmd_voice<=voice_i; cmd_mode<=mode_i;
                    cmd_length<=length_i; cmd_tuning<=tuning_i; cmd_loss<=loss_i; cmd_amplitude<=amplitude_i;
                    cmd_left<=gain_left_i; cmd_right<=gain_right_i; cmd_seed<=seed_i; cmd_sequence<=sequence_i;
                    accepted_sequence_o<=sequence_i;
                    cmd_model<=command_model_generation_i;cmd_template<=command_template_generation_i;cmd_identity<=command_note_identity_i;
                end
            end
            // The divider is independent of foreground frame work.
            if (div_done && generation[div_voice] == div_generation) begin
                if(div_setup && init_phase[div_voice]==I_SETUP) begin
                    init_step[div_voice]<=div_quotient[30:0];init_phase[div_voice]<=I_FILL;
                end else if(!div_setup && init_phase[div_voice]==I_DIV) begin
                    mean[div_voice]<=div_quotient[15:0];init_phase[div_voice]<=I_DC;init_cursor[div_voice]<=0;
                end
            end
            if (!div_busy && !div_start && !div_done) begin
                for (i=VOICES-1;i>=0;i=i-1) begin
                    if (init_phase[i] == I_DIV || (shaped && init_phase[i]==I_SETUP)) begin
                        div_start<=1; div_voice<=i; div_generation<=generation[i];
                        div_setup<=init_phase[i]==I_SETUP;
                        div_sum<=init_phase[i]==I_SETUP ? 32'sh7fffffff : init_sum[i]; div_length<=ring_length[i];
                    end
                end
            end
            case (state)
                IDLE: begin
                    if (frame_pending) begin
                        frame_pending<=0; frame_running<=1; work_epoch_valid<=1; working_epoch<=frame_epoch;
                        frame_voice<=0; sum_left<=0; sum_right<=0; state<=F_CHOOSE;
                    end else if (found && mode_o && !sample_tick_i) begin
                        bg_voice<=selected; bg_generation<=generation[selected]; bg_rr<=(selected+1'b1) & (VOICES-1);
                        bg_epoch_valid<=1; bg_onehot<={{(VOICES-1){1'b0}},1'b1} << selected;
                        bg_length<=ring_length[selected]; bg_cursor<=init_cursor[selected];
                        bg_u<=init_u[selected];bg_step<=init_step[selected];bg_noise<=noise_prev[selected];
                        bg_rng<=rng[selected]; bg_amplitude<=amplitude[selected]; bg_sum<=init_sum[selected]; bg_mean<=mean[selected];
                        bg_address<=ring_address(selected,init_cursor[selected]);
                        bg_write_authorized<=init_phase[selected] == I_FILL;
                        bg_read_authorized<=init_phase[selected] == I_DC;
                        state<=init_phase[selected] == I_FILL ? B_RANDOM : B_READ;
                    end
                end
                F_CHOOSE: begin
                    if (active[frame_voice] && mode_o) begin
                        work_valid<=1; work_onehot<={{(VOICES-1){1'b0}},1'b1} << frame_voice;
                        work_generation<=generation[frame_voice];
                        work_length<=ring_length[frame_voice]; work_cursor<=cursor[frame_voice];
                        work_tuning<=tuning[frame_voice]; work_loss<=loss[frame_voice];
                        work_prev<=prev_read[frame_voice];
                        work_x_prev<=x_prev[frame_voice]; work_a_prev<=a_prev[frame_voice];
                        work_disp_x<=disp_x_prev[frame_voice];work_disp_y<=disp_y_prev[frame_voice];
                        work_left<=gain_left[frame_voice]; work_right<=gain_right[frame_voice];
                        work_pending_note<=pending_note[frame_voice];
                        work_pending_length<=pending_length[frame_voice]; work_pending_tuning<=pending_tuning[frame_voice];
                        work_pending_loss<=pending_loss[frame_voice]; work_pending_amplitude<=pending_amplitude[frame_voice];
                        work_pending_left<=pending_left[frame_voice]; work_pending_right<=pending_right[frame_voice];
                        work_pending_seed<=pending_seed[frame_voice]; work_pending_sequence<=pending_sequence[frame_voice];
                        work_pending_identity<=pending_identity[frame_voice];
                        frame_address<=ring_address(frame_voice,cursor[frame_voice]);
                        frame_release_gain<=release_gain[frame_voice];
                        work_release_left<=release_left[frame_voice];
                        work_release_remainder<=release_remainder[frame_voice];
                        work_steal_left<=steal_left[frame_voice];
                        frame_fade_gain<={fade_step[frame_voice],11'b0};
                        frame_steal_gain<=steal_left[frame_voice] == 0 ? 17'd65536 : {steal_left[frame_voice],9'b0};
                        state<=F_READ;
                    end else begin work_valid<=0; work_onehot<=0; state<=F_NEXT; end
                end
                F_READ: state<=F_CAPTURE;
                F_CAPTURE: begin sample_read<=$signed(ram_read_data); state<=new_model ? F_LOSS : F_RHO; end
                F_LOSS: if (ENABLE_KS_TIMBRES) begin loss_value<=sat24(trunc_sym(new_rho,15));state<=F_DISP_MUL;end
                F_DISP_MUL: if (ENABLE_KS_TIMBRES) begin disp_product<=feedback_product[40:0];state<=F_DISP;end
                F_DISP: if (ENABLE_KS_TIMBRES) begin
                    disp_value<=dispersion ? sat24(trunc_sym({{7{disp_product[40]}},disp_product},15)+$signed({{24{work_disp_x[23]}},work_disp_x})) : loss_value;
                    x_value<=dispersion ? sat24(trunc_sym({{7{disp_product[40]}},disp_product},15)+$signed({{24{work_disp_x[23]}},work_disp_x})) : loss_value;state<=F_DELTA;end
                F_RHO: begin
                    rho_product<=feedback_product[33:0];
                    state<=F_X;
                end
                F_X: begin x_value<=24'(trunc_sym({{14{rho_product[33]}},rho_product},16)); state<=F_DELTA; end
                F_DELTA: begin delta<=$signed({x_value[23],x_value}) - $signed({work_a_prev[23],work_a_prev}); state<=F_AP; end
                F_AP: begin ap_product<=feedback_product[40:0]; state<=F_A; end
                F_A: begin a_value<=sat24(trunc_sym({{7{ap_product[40]}},ap_product},15)+$signed({{24{work_x_prev[23]}},work_x_prev})); state<=F_GAIN; end
                F_GAIN: begin
                    gain_product_left<=gain_envelope_left[32:0];
                    gain_product_right<=gain_envelope_right[32:0]; state<=F_GAIN_TRUNC;
                end
                F_GAIN_TRUNC: begin
                    term_left<=24'(trunc_sym({{15{gain_product_left[32]}},gain_product_left},12));
                    term_right<=24'(trunc_sym({{15{gain_product_right[32]}},gain_product_right},12)); state<=F_FADE;
                end
                F_FADE: begin
                    envelope_product_left<=gain_envelope_left[41:0];
                    envelope_product_right<=gain_envelope_right[41:0]; state<=F_FADE_TRUNC;
                end
                F_FADE_TRUNC: begin
                    term_left<=24'(trunc_sym({{6{envelope_product_left[41]}},envelope_product_left},16));
                    term_right<=24'(trunc_sym({{6{envelope_product_right[41]}},envelope_product_right},16)); state<=F_RELEASE;
                end
                F_RELEASE: begin
                    envelope_product_left<=gain_envelope_left[41:0];
                    envelope_product_right<=gain_envelope_right[41:0]; state<=F_RELEASE_TRUNC;
                end
                F_RELEASE_TRUNC: begin
                    term_left<=24'(trunc_sym({{6{envelope_product_left[41]}},envelope_product_left},16));
                    term_right<=24'(trunc_sym({{6{envelope_product_right[41]}},envelope_product_right},16)); state<=F_STEAL;
                end
                F_STEAL: begin
                    envelope_product_left<=gain_envelope_left[41:0];
                    envelope_product_right<=gain_envelope_right[41:0]; state<=F_STEAL_TRUNC;
                end
                F_STEAL_TRUNC: begin
                    term_left<=24'(trunc_sym({{6{envelope_product_left[41]}},envelope_product_left},16));
                    term_right<=24'(trunc_sym({{6{envelope_product_right[41]}},envelope_product_right},16));
                    frame_ram_data<=sat16({{24{a_value[23]}},a_value}); state<=F_WRITE;
                end
                F_WRITE: state<=F_ACCUM;
                F_ACCUM: begin
                    if (frame_current && work_valid) begin
                        sum_left<=sum_left+term_left; sum_right<=sum_right+term_right;
                    end
                    for (i=0;i<VOICES;i=i+1) begin
                        // Control can change generation/active only at tick,
                        // which revokes frame_current with highest priority.
                        if (frame_current && work_valid && work_onehot[i]) begin
                            prev_read[i]<=sample_read;
                            x_prev[i]<=x_value; a_prev[i]<=a_value;
                            if(new_model) begin disp_x_prev[i]<=loss_value;disp_y_prev[i]<=disp_value;end
                            cursor[i]<=work_cursor_next;
                            if (frame_fade_gain[16:11] < 32) fade_step[i]<=work_fade_next;
                            if (work_release_left != 0) begin
                                release_left[i]<=work_release_left_next;
                                release_gain[i]<=work_release_gain_next;
                                release_remainder[i]<=work_release_remainder_next;
                                if (work_release_left == 1) begin active[i]<=0; started[i]<=0; end
                            end
                            if (work_steal_left != 0) begin
                                steal_left[i]<=work_steal_next;
                                if (work_steal_left == 1) begin
                                    active[i]<=0; started[i]<=0;
                                    if (work_pending_note) begin
                                        ring_length[i]<=work_pending_length;
                                        tuning[i]<=work_pending_tuning; loss[i]<=work_pending_loss;
                                        amplitude[i]<=work_pending_amplitude;
                                        gain_left[i]<=work_pending_left; gain_right[i]<=work_pending_right;
                                        rng[i]<=work_pending_seed; note_sequence[i]<=work_pending_sequence;
                                        note_identity[i]<=work_pending_identity;init_u[i]<=0;noise_prev[i]<=0;disp_x_prev[i]<=0;disp_y_prev[i]<=0;
                                        init_phase[i]<=shaped ? I_SETUP : I_FILL; init_cursor[i]<=0; init_sum[i]<=0;
                                        prev_read[i]<=0; x_prev[i]<=0; a_prev[i]<=0;
                                        release_left[i]<=0; release_gain[i]<=65536; release_remainder[i]<=0;
                                        fade_step[i]<=1; pending_note[i]<=0; cursor[i]<=0;
                                    end
                                end
                            end
                        end
                    end
                    state<=F_NEXT;
                end
                F_NEXT: begin
                    if (frame_voice == VOICES-1) state<=F_OUTPUT;
                    else begin frame_voice<=frame_voice+1'b1; state<=F_CHOOSE; end
                end
                F_OUTPUT: begin
                    if(ENABLE_KS_TIMBRES) state<=body_enabled ? F_BODY : F_PUBLISH_INPUT;
                    else if (ENABLE_KS_LIMITER && frame_current && working_epoch == frame_epoch) begin
                        lim_acc_l<=sum_left; lim_acc_r<=sum_right;
                        state<=F_LIM_ABS;
                    end else begin
                    if (!ENABLE_KS_LIMITER && frame_current && working_epoch == frame_epoch) begin
                        pcm_left_o<=sat24(trunc_sym({{16{sum_left[31]}},sum_left},MIX_SHIFT)<<<8);
                        pcm_right_o<=sat24(trunc_sym({{16{sum_right[31]}},sum_right},MIX_SHIFT)<<<8);
                        pcm_valid_o<=1; frame_count_o<=frame_count_o+1'b1;
                    end
                    frame_running<=0; work_epoch_valid<=0; state<=IDLE;
                    end
                end
                F_BODY: state<=F_BODY_WAIT;
                F_BODY_WAIT: if(body_done || !frame_current) begin
                    if(body_done) begin sum_left<=body_left;sum_right<=body_right;end
                    state<=F_PUBLISH_INPUT;
                end
                F_PUBLISH_INPUT: begin
                    if(ENABLE_KS_LIMITER && frame_current) begin
                        lim_acc_l<=(model_state_o==M_FADING || model_state_o==M_APPLYING) ? 32'(trunc_sym($signed(sum_left)*$signed({1'b0,model_fade_left}),0)/480) : sum_left;
                        lim_acc_r<=(model_state_o==M_FADING || model_state_o==M_APPLYING) ? 32'(trunc_sym($signed(sum_right)*$signed({1'b0,model_fade_left}),0)/480) : sum_right;
                        state<=F_LIM_ABS;
                    end else begin
                        if(frame_current) begin
                            pcm_left_o<=sat24(trunc_sym((model_state_o==M_FADING || model_state_o==M_APPLYING) ? ($signed(sum_left)*$signed({1'b0,model_fade_left}))/480 : {{16{sum_left[31]}},sum_left},MIX_SHIFT)<<<8);
                            pcm_right_o<=sat24(trunc_sym((model_state_o==M_FADING || model_state_o==M_APPLYING) ? ($signed(sum_right)*$signed({1'b0,model_fade_left}))/480 : {{16{sum_right[31]}},sum_right},MIX_SHIFT)<<<8);
                            pcm_valid_o<=1;frame_count_o<=frame_count_o+1'b1;
                        end
                        frame_running<=0;work_epoch_valid<=0;state<=IDLE;
                    end
                end
                F_LIM_ABS: begin
                    lim_abs_l<=lim_acc_l[31] ? (~lim_acc_l+32'd1) : lim_acc_l;
                    lim_abs_r<=lim_acc_r[31] ? (~lim_acc_r+32'd1) : lim_acc_r;
                    state<=F_LIM_LEVEL;
                end
                F_LIM_LEVEL: begin
                    lim_level<=(lim_abs_l>lim_abs_r ? lim_abs_l : lim_abs_r)>>1;
                    lim_wait<=0; state<=F_LIM_WAIT;
                end
                F_LIM_WAIT: begin
                    // The shared detector/target/decoder has five registers.
                    // Hold level and envelope through its complete latency.
                    if (lim_wait==4) state<=F_LIM_GAIN;
                    else lim_wait<=lim_wait+1'b1;
                end
                F_LIM_GAIN: begin
                    lim_candidate_q<=lim_gain_q_next; lim_gain<=lim_gain_lin;
                    state<=F_LIM_MUL;
                end
                F_LIM_MUL: begin
                    lim_product_l<=32'(lim_mul_l>>>16);
                    lim_product_r<=32'(lim_mul_r>>>16);
                    state<=F_LIM_PUBLISH;
                end
                F_LIM_PUBLISH: begin
                    // Audio, valid, frame counter and envelope commit together.
                    // A new tick invalidates an old frame even on this edge.
                    if (frame_current && working_epoch == frame_epoch) begin
                        pcm_left_o<=sat24({{16{lim_product_l[31]}},lim_product_l}<<<6);
                        pcm_right_o<=sat24({{16{lim_product_r[31]}},lim_product_r}<<<6);
                        lim_gain_q<=lim_candidate_q;
                        pcm_valid_o<=1; frame_count_o<=frame_count_o+1'b1;
                    end
                    frame_running<=0; work_epoch_valid<=0; state<=IDLE;
                end
                B_RANDOM: begin
                    if (bg_epoch_valid) begin
                        bg_next_rng<=xorshift32(bg_rng);
                        bg_raw<=16'($signed({1'b0,xorshift32(bg_rng) >> 16})-32768);
                    end
                    state<=B_MUL;
                end
                B_MUL: begin bg_product<=(shaped ? excitation_sample : bg_raw)*$signed({1'b0,bg_amplitude}); state<=B_WRITE; end
                B_WRITE: begin
                    for (i=0;i<VOICES;i=i+1) begin
                        if (bg_epoch_valid && (!ENABLE_KS_TIMBRES || bg_transaction_current) && bg_onehot[i]) begin
                            rng[i]<=bg_next_rng; init_sum[i]<=32'(bg_sum+$signed(bg_scaled));
                            if(shaped) begin init_u[i]<=bg_u+bg_step;noise_prev[i]<=color_sample;end
                            if (bg_cursor+1'b1 == bg_length) init_phase[i]<=I_DIV;
                            else init_cursor[i]<=bg_cursor+1'b1;
                        end
                    end
                    state<=IDLE;
                end
                B_READ: state<=B_CAPTURE;
                B_CAPTURE: begin
                    bg_read<=$signed(ram_read_data);
                    bg_dc_data<=sat16($signed({{32{ram_read_data[15]}},ram_read_data}) - $signed({{32{bg_mean[15]}},bg_mean}));
                    state<=B_DC_WRITE;
                end
                B_DC_WRITE: begin
                    for (i=0;i<VOICES;i=i+1) begin
                        if (bg_epoch_valid && (!ENABLE_KS_TIMBRES || bg_transaction_current) && bg_onehot[i]) begin
                            if (bg_cursor+1'b1 == bg_length) begin init_phase[i]<=I_DONE; done_generation[i]<=bg_generation; end
                            else init_cursor[i]<=bg_cursor+1'b1;
                        end
                    end
                    state<=IDLE;
                end
                default: state<=IDLE;
            endcase
            // Control is applied only at the sample boundary, after all old
            // background/frame assignments, so cancellation always wins.
            if (sample_tick_i) begin
                work_epoch_valid<=0;
                if (frame_pending || frame_running) deadline_count_o<=deadline_count_o+1'b1;
                frame_epoch<=frame_epoch+1'b1; frame_pending<=1;
                for (i=0;i<VOICES;i=i+1) begin
                    if (init_phase[i] == I_DONE && mode_o && done_generation[i] == generation[i]) begin
                        active[i]<=1; init_phase[i]<=I_IDLE; started[i]<=note_sequence[i];
                    end
                end
                if (cmd_pending) begin
                    if (cmd_op == PANIC || cmd_op == MODE ||
                        ((cmd_op == ON || cmd_op == OFF || cmd_op == KILL) && cmd_voice == bg_voice)) bg_epoch_valid<=0;
                    cmd_pending<=0;applied_sequence_o<=cmd_sequence;
                    case (cmd_op)
                        MODE, PANIC: begin
                            if (ENABLE_KS_LIMITER) begin
                                lim_gain_q<=0;
                                pcm_left_o<=0; pcm_right_o<=0;
                            end
                            for (i=0;i<VOICES;i=i+1) begin
                                active[i]<=0; init_phase[i]<=I_IDLE; pending_note[i]<=0;
                                steal_left[i]<=0; release_left[i]<=0; started[i]<=0; generation[i]<=generation[i]+1'b1;
                            end
                            if (cmd_op == MODE) begin mode_o<=cmd_mode; mode_sequence_o<=cmd_sequence; end
                        end
                        KILL: begin
                            generation[cmd_voice]<=generation[cmd_voice]+1'b1;
                            active[cmd_voice]<=0;init_phase[cmd_voice]<=I_IDLE;pending_note[cmd_voice]<=0;
                            started[cmd_voice]<=0;steal_left[cmd_voice]<=0;release_left[cmd_voice]<=0;
                            gain_left[cmd_voice]<=0;gain_right[cmd_voice]<=0;
                        end
                        OFF: begin
                            if(ENABLE_KS_TIMBRES && (new_model || cmd_identity!=0) && pending_note[cmd_voice] && cmd_identity==pending_identity[cmd_voice]) begin
                                pending_note[cmd_voice]<=0;
                                // Keep the old pluck's steal fade independent.
                            end else if(!ENABLE_KS_TIMBRES || (!new_model && cmd_identity==0) || cmd_identity==note_identity[cmd_voice]) begin
                                generation[cmd_voice]<=generation[cmd_voice]+1'b1;
                                init_phase[cmd_voice]<=I_IDLE;started[cmd_voice]<=0;
                                if(!ENABLE_KS_TIMBRES || (!new_model && cmd_identity==0)) begin pending_note[cmd_voice]<=0;steal_left[cmd_voice]<=0;end
                                if(active[cmd_voice] && (!ENABLE_KS_TIMBRES || (!new_model && cmd_identity==0) || !pending_note[cmd_voice])) begin
                                    release_left[cmd_voice]<=RELEASE_COUNT_BITS'(new_model ? model_words[15*32+:18] : 18'd960);
                                    release_gain[cmd_voice]<=65536;release_remainder[cmd_voice]<=0;
                                end else if(!active[cmd_voice]) release_left[cmd_voice]<=0;
                            end
                        end
                        ON: begin
                            if (mode_o) begin
                                generation[cmd_voice]<=generation[cmd_voice]+1'b1;
                                if (active[cmd_voice]) begin
                                    pending_note[cmd_voice]<=1; steal_left[cmd_voice]<=128;
                                    // A new pluck owns the old voice's fade;
                                    // a nearly finished release must not stop
                                    // feedback before the 128-sample steal ends.
                                    release_left[cmd_voice]<=0; release_gain[cmd_voice]<=65536; release_remainder[cmd_voice]<=0;
                                    pending_length[cmd_voice]<=cmd_length; pending_tuning[cmd_voice]<=cmd_tuning;
                                    pending_loss[cmd_voice]<=cmd_loss; pending_amplitude[cmd_voice]<=cmd_amplitude;
                                    pending_left[cmd_voice]<=cmd_left; pending_right[cmd_voice]<=cmd_right;
                                    pending_seed[cmd_voice]<=cmd_seed == 0 ? 1 : cmd_seed; pending_sequence[cmd_voice]<=cmd_sequence;
                                    pending_identity[cmd_voice]<=cmd_identity;
                                end else begin
                                    active[cmd_voice]<=0; started[cmd_voice]<=0; pending_note[cmd_voice]<=0; steal_left[cmd_voice]<=0;
                                    ring_length[cmd_voice]<=cmd_length; tuning[cmd_voice]<=cmd_tuning; loss[cmd_voice]<=cmd_loss;
                                    amplitude[cmd_voice]<=cmd_amplitude; gain_left[cmd_voice]<=cmd_left; gain_right[cmd_voice]<=cmd_right;
                                    rng[cmd_voice]<=cmd_seed == 0 ? 1 : cmd_seed; note_sequence[cmd_voice]<=cmd_sequence;
                                    note_identity[cmd_voice]<=cmd_identity;init_u[cmd_voice]<=0;noise_prev[cmd_voice]<=0;disp_x_prev[cmd_voice]<=0;disp_y_prev[cmd_voice]<=0;
                                    init_phase[cmd_voice]<=shaped ? I_SETUP : I_FILL; init_cursor[cmd_voice]<=0; init_sum[cmd_voice]<=0; cursor[cmd_voice]<=0;
                                    prev_read[cmd_voice]<=0; x_prev[cmd_voice]<=0; a_prev[cmd_voice]<=0; fade_step[cmd_voice]<=1;
                                    release_left[cmd_voice]<=0; release_gain[cmd_voice]<=65536; release_remainder[cmd_voice]<=0;
                                end
                            end
                        end
                        GAIN: begin
                            if(!ENABLE_KS_TIMBRES || (!new_model && cmd_identity==0) || cmd_identity==note_identity[cmd_voice]) begin gain_left[cmd_voice]<=cmd_left;gain_right[cmd_voice]<=cmd_right;end
                            if(pending_note[cmd_voice] && (!ENABLE_KS_TIMBRES || (!new_model && cmd_identity==0) || cmd_identity==pending_identity[cmd_voice])) begin pending_left[cmd_voice]<=cmd_left;pending_right[cmd_voice]<=cmd_right;end
                        end
                        LOSS: begin
                            if(!ENABLE_KS_TIMBRES || (!new_model && cmd_identity==0) || cmd_identity==note_identity[cmd_voice]) loss[cmd_voice]<=cmd_loss;
                            if(pending_note[cmd_voice] && (!ENABLE_KS_TIMBRES || (!new_model && cmd_identity==0) || cmd_identity==pending_identity[cmd_voice])) pending_loss[cmd_voice]<=cmd_loss;
                        end
                        default: ;
                    endcase
                end
                if(ENABLE_KS_TIMBRES) begin
                    if(model_state_o==M_FADING) begin
                        if(model_fade_left<=1) begin model_fade_left<=0;model_state_o<=M_APPLYING;end
                        else model_fade_left<=model_fade_left-1'b1;
                    end
                    if(model_apply_tick) begin
                        model_words<=pending_model_words;current_model_generation<=pending_model_words[3*32+:32];
                        model_state_o<=M_FIRST;state<=IDLE;frame_running<=0;bg_epoch_valid<=0;cmd_pending<=0;
                        lim_gain_q<=0;pcm_left_o<=0;pcm_right_o<=0;sum_left<=0;sum_right<=0;
                        for(i=0;i<VOICES;i=i+1) begin
                            active[i]<=0;init_phase[i]<=I_IDLE;pending_note[i]<=0;started[i]<=0;
                            generation[i]<=generation[i]+1'b1;release_left[i]<=0;steal_left[i]<=0;
                            prev_read[i]<=0;x_prev[i]<=0;a_prev[i]<=0;disp_x_prev[i]<=0;disp_y_prev[i]<=0;
                            note_identity[i]<=0;pending_identity[i]<=0;
                        end
                    end
                    if(safety_command || incoming_safety) begin
                        canceled_model_generation<=incoming_safety ? command_model_generation_i : cmd_model;
                        if(model_busy) begin
                            model_completed_generation_o<=pending_model_words[3*32+:32];model_result_o<=1;
                            model_words<=previous_model_words;current_model_generation<=previous_model_generation;
                            model_state_o<=M_IDLE;model_fade_left<=0;
                        end else if(model_valid_i && model_ready_o) begin
                            model_completed_generation_o<=model_words_i[3*32+:32];model_result_o<=1;model_state_o<=M_IDLE;
                        end
                        // An incoming safety command also revokes this boundary;
                        // its ordinary command acknowledgement still uses the queue.
                        state<=IDLE;frame_running<=0;bg_epoch_valid<=0;
                        pcm_left_o<=0;pcm_right_o<=0;lim_gain_q<=0;
                        for(i=0;i<VOICES;i=i+1) begin
                            active[i]<=0;init_phase[i]<=I_IDLE;pending_note[i]<=0;started[i]<=0;
                            generation[i]<=generation[i]+1'b1;release_left[i]<=0;steal_left[i]<=0;
                            disp_x_prev[i]<=0;disp_y_prev[i]<=0;
                        end
                    end
                end
            end
        end
    end
    // slot_i documents the shared synth schedule; sample_tick owns boundaries.
    wire unused_slot = ^slot_i;
endmodule
`default_nettype wire
