`default_nettype none
// Three same-tag source taps, two selectors, one total-mix effect worker.
// A missing source contributes silence; no previous frame is replayed.
module pcm_three_output_stage #(
    parameter bit ENABLE_SUBTRACTIVE=1,ENABLE_KS=1,ENABLE_FM=1,
    parameter bit ENABLE_REVERB=1,ENABLE_OUTPUT_TILT=0,
    parameter bit REVERB_COMPACT=0,
    parameter integer OUTPUT_TILT_SHIFT=3
)(
    input wire clk_i,rst_ni,
    input wire [69:0] controls_i,input wire controls_valid_i,
    input wire [3:0] output_routes_i,
    input wire [2:0] native_alive_i,channel_error_i,
    input wire [2:0] response_valid_i,
    input wire [111:0] sub_response_i,ks_response_i,fm_response_i,
    output wire [63:0] request_tag_o,
    output wire frame_accept_o,frame_latched_o,
    output wire hp_bck_o,hp_ws_o,hp_data_o,pcm_bck_o,pcm_ws_o,pcm_data_o,pcm_b_data_o,
    output logic signed [23:0] tap_left_o,tap_right_o,
    output logic tap_valid_o,output wire [2:0] tap_source_o,
    output logic [31:0] tap_revision_o,output logic [63:0] tap_tag_o,
    output wire busy_o,fm_active_o,
    output wire [127:0] synth_stats_o,fm_stats_o,audio_stats_o
);
    localparam logic [2:0] ENABLED={ENABLE_FM,ENABLE_KS,ENABLE_SUBTRACTIVE};
    logic [31:0] sequence_r;
    logic [1:0] frames;
    logic control_seen,muted,test_tone,bypass_tilt;
    logic [31:0] revision;
    logic [15:0] wet,damp;
    (* ASYNC_REG="TRUE" *)logic [2:0] alive_meta,alive_sync;
    wire [111:0] incoming[0:2];
    assign incoming[0]=sub_response_i;assign incoming[1]=ks_response_i;assign incoming[2]=fm_response_i;
    logic [111:0] raw_packet[0:2][0:1];
    logic [1:0] raw_valid[0:2];
    logic [47:0] frozen[0:2][0:1];
    logic [63:0] frozen_tag[0:1];logic [1:0] frozen_valid;
    logic [111:0] processed[0:1];logic [1:0] processed_valid;
    wire [31:0] mix_sequence=sequence_r-32'd2,play_sequence=sequence_r-32'd3;
    wire [63:0] mix_tag={32'd1,mix_sequence},play_tag={32'd1,play_sequence};
    wire mix_slot=mix_sequence[0],play_slot=play_sequence[0];
    wire play_found=frames==3 && frozen_valid[play_slot] && frozen_tag[play_slot]==play_tag;
    wire total_found=frames==3 && processed_valid[play_slot] && processed[play_slot][111:48]==play_tag;
    logic signed [23:0] next_left[0:2],next_right[0:2];
    integer e,s;
    always_comb begin
        for(integer k=0;k<3;k=k+1)begin
            next_left[k]=0;next_right[k]=0;
            if(ENABLED[k] && alive_sync[k] && raw_valid[k][mix_slot] && raw_packet[k][mix_slot][111:48]==mix_tag)begin
                next_left[k]=$signed(raw_packet[k][mix_slot][47:24]);
                next_right[k]=$signed(raw_packet[k][mix_slot][23:0]);
            end
        end
    end
    wire signed [25:0] sum_left={{2{next_left[0][23]}},next_left[0]}+
        {{2{next_left[1][23]}},next_left[1]}+{{2{next_left[2][23]}},next_left[2]};
    wire signed [25:0] sum_right={{2{next_right[0][23]}},next_right[0]}+
        {{2{next_right[1][23]}},next_right[1]}+{{2{next_right[2][23]}},next_right[2]};
    // Raw sum: no new limiter or normalization. Low24 preserves unit gain.
    wire [47:0] total_pair=total_found?processed[play_slot][47:0]:48'd0;
    function automatic [47:0] select_pair(input [1:0] which);
        case(which)
            0:select_pair=play_found?frozen[0][play_slot]:48'd0;
            1:select_pair=play_found?frozen[1][play_slot]:48'd0;
            2:select_pair=play_found?frozen[2][play_slot]:48'd0;
            default:select_pair=total_pair;
        endcase
    endfunction
    wire [47:0] hp_pair=control_seen && !muted?select_pair(output_routes_i[1:0]):48'd0;
    wire [47:0] a_pair=control_seen && !muted?select_pair(output_routes_i[3:2]):48'd0;
    wire [47:0] b_pair=control_seen && !muted?total_pair:48'd0;
    pt8211_tx #(.BCK_HALF_CYCLES(4)) u_tx(
        .clk_i,.rst_n_i(rst_ni),.left_i(hp_pair[47:32]),.right_i(hp_pair[23:8]),
        .bck_o(hp_bck_o),.ws_o(hp_ws_o),.data_o(hp_data_o),.i2s_ws_o(),
        .frame_accept_o(frame_accept_o),.frame_latched_o(frame_latched_o));
    // PT remains the sole request-frame authority. Both I2S masters use the
    // same audio clock/reset and 4*64=256 cycles/frame as PT's 8*32. Their
    // explicit reset phase accepts that very stereo pair on the PT edge.
    wire pcm_b_frame_accept,pcm_b_frame_latched;
    i2s_tx #(.BITS(24),.BCLK_DIV(4),.BCLK_PER_WS(64)) u_a_tx(
        .sysclk(clk_i),.rst_n(rst_ni),.sample_tick(frame_accept_o),
        .data_left(a_pair[47:24]),.data_right(a_pair[23:0]),
        .i2s_bclk(pcm_bck_o),.i2s_lrclk(pcm_ws_o),.sd(pcm_data_o),
        .frame_accept_o(),.frame_latched_o());
    i2s_tx #(.BITS(24),.BCLK_DIV(4),.BCLK_PER_WS(64)) u_b_tx(
        .sysclk(clk_i),.rst_n(rst_ni),.sample_tick(frame_accept_o),
        .data_left(b_pair[47:24]),.data_right(b_pair[23:0]),
        .i2s_bclk(),.i2s_lrclk(),.sd(pcm_b_data_o),
        .frame_accept_o(pcm_b_frame_accept),.frame_latched_o(pcm_b_frame_latched));
    // This is an explicit checked timing contract, not an additional native
    // request source. Tap publication below waits for PCM-B's real capture.
    // synthesis translate_off
`ifndef SYNTHESIS
    always @(posedge clk_i)if(rst_ni)
        assert(pcm_b_frame_accept == frame_accept_o)
            else $fatal(1,"PT/I2S frame acceptance mismatch");
`endif
    // synthesis translate_on
    // Preserve the original scheduler's nonzero initial epoch; native cores
    // use zero as the cold/unpublished tag.
    assign request_tag_o={32'd1,sequence_r};
    assign tap_source_o=3'd3; // Actual tap is total mix, independent of solo selectors.
    assign fm_active_o=control_seen && ENABLE_FM && alive_sync[2];
    wire effect_online,worker_ready,worker_valid,worker_late,worker_clip;
    wire [111:0] worker_result;
    logic [111:0] worker_input;
    logic worker_input_valid,filter_tick;
    logic [2:0] filter_state;
    logic signed [23:0] filter_left,filter_right;
    wire signed [23:0] filtered_left,filtered_right;
    logic [63:0] filter_tag;
    output_tilt #(.ENABLE(ENABLE_OUTPUT_TILT),.SHIFT(OUTPUT_TILT_SHIFT)) u_tilt_l(
        .clk(clk_i),.rst_n(rst_ni),.tick(filter_tick),.in(filter_left),.out(filtered_left));
    output_tilt #(.ENABLE(ENABLE_OUTPUT_TILT),.SHIFT(OUTPUT_TILT_SHIFT)) u_tilt_r(
        .clk(clk_i),.rst_n(rst_ni),.tick(filter_tick),.in(filter_right),.out(filtered_right));
    pcm_frame_effect #(.ENABLE_REVERB(ENABLE_REVERB),.REVERB_COMPACT(REVERB_COMPACT)) u_worker(
        .clk_i,.rst_n_i(rst_ni),.data_i(worker_input),.valid_i(worker_input_valid),
        .ready_o(worker_ready),.data_o(worker_result),.valid_o(worker_valid),.ready_i(1'b1),
        .wet_q15_i(wet),.damp_q15_i(damp),.online_o(effect_online),.late_o(worker_late),
        .clipped_o(worker_clip),.stats_o(audio_stats_o));
    assign busy_o=!control_seen || frames!=3 || !effect_online;
    logic [31:0] received[0:2],missing[0:2],played;
    assign synth_stats_o={received[0],received[1],played,missing[0]+missing[1]};
    assign fm_stats_o={received[2],received[2],played,missing[2]};
    logic [47:0] accepted_pair;logic [63:0] accepted_tag;
    logic accepted_valid;logic [31:0] accepted_revision;
    logic [23:0] tone_phase;
    wire signed [17:0] tone_q216;
    osc_core u_tone(.phase_next(tone_phase),.duty(24'sd0),.wave(2'd3),.sample_out(tone_q216));
    wire signed [23:0] tone_sample={tone_q216[15:0],8'd0};
    always_ff @(posedge clk_i or negedge rst_ni)begin
        if(!rst_ni)begin
            sequence_r<=0;frames<=0;control_seen<=0;muted<=1;test_tone<=0;bypass_tilt<=0;
            wet<=0;damp<=0;revision<=0;alive_meta<=0;alive_sync<=0;
            frozen_valid<=0;processed_valid<=0;worker_input<=0;worker_input_valid<=0;
            filter_tick<=0;filter_state<=0;filter_left<=0;filter_right<=0;filter_tag<=0;
            tap_left_o<=0;tap_right_o<=0;tap_valid_o<=0;tap_revision_o<=0;tap_tag_o<=0;
            accepted_pair<=0;accepted_tag<=0;accepted_valid<=0;accepted_revision<=0;tone_phase<=0;played<=0;
            for(e=0;e<3;e=e+1)begin
                raw_valid[e]<=0;received[e]<=0;missing[e]<=0;
                for(s=0;s<2;s=s+1)begin raw_packet[e][s]<=0;frozen[e][s]<=0;end
            end
            for(s=0;s<2;s=s+1)begin frozen_tag[s]<=0;processed[s]<=0;end
        end else begin
            alive_meta<=native_alive_i;alive_sync<=alive_meta;
            tap_valid_o<=0;filter_tick<=0;
            if(controls_valid_i)begin
                control_seen<=1;muted<=controls_i[66];test_tone<=controls_i[65];bypass_tilt<=controls_i[64];
                wet<=controls_i[63:48];damp<=controls_i[47:32];revision<=controls_i[31:0];
            end
            if(frame_accept_o)begin
                sequence_r<=sequence_r+1'b1;tone_phase<=tone_phase+24'd262144;
                if(frames!=3)frames<=frames+1'b1;
                accepted_pair<=b_pair;accepted_tag<=play_tag;accepted_revision<=revision;
                accepted_valid<=control_seen && frames==3 && total_found;
                if(total_found)played<=played+1'b1;
                if(frames==3)begin frozen_valid[play_slot]<=0;processed_valid[play_slot]<=0;end
                if(frames>=2)begin
                    frozen_tag[mix_slot]<=mix_tag;frozen_valid[mix_slot]<=1;
                    for(e=0;e<3;e=e+1)begin
                        frozen[e][mix_slot]<=test_tone?{tone_sample,tone_sample}:{next_left[e],next_right[e]};
                        raw_valid[e][mix_slot]<=0;
                        if(ENABLED[e] && alive_sync[e] && (!raw_valid[e][mix_slot] || raw_packet[e][mix_slot][111:48]!=mix_tag))
                            missing[e]<=missing[e]+1'b1;
                    end
                    if(filter_state==0 && effect_online)begin
                        filter_left<=test_tone?tone_sample:sum_left[23:0];
                        filter_right<=test_tone?tone_sample:sum_right[23:0];
                        filter_tag<=mix_tag;filter_tick<=1;filter_state<=1;
                    end
                end
            end
            if(pcm_b_frame_latched)begin
                tap_left_o<=$signed(accepted_pair[47:24]);tap_right_o<=$signed(accepted_pair[23:0]);
                tap_tag_o<=accepted_tag;tap_revision_o<=accepted_revision;tap_valid_o<=accepted_valid;
            end
            for(e=0;e<3;e=e+1)begin
                if(response_valid_i[e] && incoming[e][111:80]==1 &&
                   (sequence_r-incoming[e][79:48])>=1 && (sequence_r-incoming[e][79:48])<=2)begin
                    raw_packet[e][incoming[e][48]]<=incoming[e];raw_valid[e][incoming[e][48]]<=1;
                    received[e]<=received[e]+1'b1;
                end
            end
            case(filter_state)
                1:filter_state<=2;
                2:begin
                    worker_input<={filter_tag,bypass_tilt?filter_left:filtered_left,bypass_tilt?filter_right:filtered_right};
                    worker_input_valid<=1;filter_state<=3;
                end
                3:if(worker_input_valid && worker_ready)begin worker_input_valid<=0;filter_state<=0;end
                default:begin end
            endcase
            if(worker_valid && worker_result[111:80]==1 &&
               (sequence_r-worker_result[79:48])>=1 && (sequence_r-worker_result[79:48])<=3)begin
                processed[worker_result[48]]<=worker_result;processed_valid[worker_result[48]]<=1;
            end
        end
    end
    // Sticky transport errors remain visible through source error statistics.
    wire unused_status=&{1'b0,channel_error_i,worker_late,worker_clip,sum_left[25:24],sum_right[25:24]};
endmodule
`default_nettype wire
