`default_nettype none
// Audio-domain owner of sample requests, effect scheduling and both serial
// formats. A source computes with its own clock; PCM never returns to it.
module pcm_request_audio #(
    parameter bit ENABLE_MIDI_PCM_LATENCY_TEST=1'b0,
    parameter bit ENABLE_SUBTRACTIVE=1'b1,ENABLE_KS=1'b1,ENABLE_FM=1'b1,
    parameter bit ENABLE_REVERB=1'b1,ENABLE_OUTPUT_TILT=1'b0,
    parameter integer OUTPUT_TILT_SHIFT=3
)(
    input wire global_rst_n_i,audio_clk_i,audio_rst_n_i,
    input wire sub_clk_i,sub_rst_n_i,ks_clk_i,ks_rst_n_i,fm_clk_i,fm_rst_n_i,
    input wire [2:0] source_i,
    input wire mute_i,test_tone_i,bypass_tilt_i,
    input wire [15:0] wet_q15_i,damp_q15_i,
    input wire [31:0] revision_i,
    output wire sub_request_valid_o,
    input wire sub_request_ready_i,
    output wire [63:0] sub_request_tag_o,
    input wire sub_response_valid_i,
    input wire [63:0] sub_response_tag_i,
    input wire signed [23:0] sub_left_i,sub_right_i,
    output wire ks_request_valid_o,
    input wire ks_request_ready_i,
    output wire [63:0] ks_request_tag_o,
    input wire ks_response_valid_i,
    input wire [63:0] ks_response_tag_i,
    input wire signed [23:0] ks_left_i,ks_right_i,
    output wire fm_request_valid_o,
    input wire fm_request_ready_i,
    output wire [63:0] fm_request_tag_o,
    input wire fm_response_valid_i,
    input wire [63:0] fm_response_tag_i,
    input wire signed [23:0] fm_left_i,fm_right_i,
    output wire hp_bck_o,hp_ws_o,hp_data_o,pcm_bck_o,pcm_ws_o,pcm_data_o,
    output wire frame_tick_o,
    output wire latency_load_o,latency_end_o,
    output wire signed [23:0] latency_left_o,latency_right_o,
    output wire [1:0] latency_engine_o,
    output wire [63:0] latency_tag_o,
    output wire [7:0] latency_quality_o,
    output logic signed [23:0] tap_left_o,tap_right_o,
    output logic tap_valid_o,
    output logic [2:0] tap_source_o,
    output logic [31:0] tap_revision_o,
    output logic [63:0] tap_tag_o,
    output wire busy_o,fm_active_o,
    output wire [127:0] synth_stats_o,fm_stats_o,audio_stats_o
);
    wire reset_request_n=global_rst_n_i && audio_rst_n_i;
    wire reset_n;
    // Board/control qualification may deassert on another clock. Only the
    // audio clock releases the scheduler, effect and physical serializer.
    jt51_reset_sync u_audio_reset(.clk_i(audio_clk_i),.rst_n_i(reset_request_n),.rst_n_o(reset_n));
    wire [69:0] controls;
    wire controls_valid;
    held_bus_cdc #(.WIDTH(70)) u_controls(
        .src_clk_i(sub_clk_i),.dst_clk_i(audio_clk_i),.rst_n_i(global_rst_n_i && sub_rst_n_i && audio_rst_n_i),
        .src_data_i({source_i,mute_i,test_tone_i,bypass_tilt_i,wet_q15_i,damp_q15_i,revision_i}),
        .src_valid_i(1'b1),.src_ready_o(),.dst_data_o(controls),.dst_valid_o(controls_valid),.dst_ready_i(1'b1));
    logic control_seen,muted,test_tone,bypass_tilt;
    logic [2:0] source;
    logic [31:0] revision,epoch,sequence_r;
    logic [15:0] wet,damp;
    wire control_change=controls_valid && (!control_seen ||
        controls[69:64]!={source,muted,test_tone,bypass_tilt} || controls[31:0]!=revision);
    (* ASYNC_REG="TRUE" *)logic [2:0] alive_meta,alive_sync;
    logic previous_alive;
    wire selected_alive=source==1 ? alive_sync[1] : source==2 ? alive_sync[2] : alive_sync[0];
    wire source_enabled=source==1 ? ENABLE_KS : source==2 ? ENABLE_FM : ENABLE_SUBTRACTIVE;
    wire fault_change=control_seen && previous_alive && !selected_alive;
    logic flush;
    wire effect_reset_n=reset_n && !flush;
    wire frame_accept,frame_latched,frame_complete;
    wire [63:0] request_tag={epoch,sequence_r};
    wire [111:0] sub_response,ks_response,fm_response;
    wire sub_valid,ks_valid,fm_valid;
    wire [2:0] channel_error;
    pcm_request_channel #(.ENABLE(ENABLE_SUBTRACTIVE)) u_sub_channel(
        .global_rst_n_i,.audio_clk_i,.audio_rst_n_i,.native_clk_i(sub_clk_i),.native_rst_n_i(sub_rst_n_i),
        .request_tick_i(frame_accept),.request_tag_i(request_tag),
        .request_valid_o(sub_request_valid_o),.request_ready_i(sub_request_ready_i),.request_tag_o(sub_request_tag_o),
        .response_valid_i(sub_response_valid_i),.response_tag_i(sub_response_tag_i),.left_i(sub_left_i),.right_i(sub_right_i),
        .response_valid_o(sub_valid),.response_o(sub_response),.error_o(channel_error[0]));
    pcm_request_channel #(.ENABLE(ENABLE_KS)) u_ks_channel(
        .global_rst_n_i,.audio_clk_i,.audio_rst_n_i,.native_clk_i(ks_clk_i),.native_rst_n_i(ks_rst_n_i),
        .request_tick_i(frame_accept),.request_tag_i(request_tag),
        .request_valid_o(ks_request_valid_o),.request_ready_i(ks_request_ready_i),.request_tag_o(ks_request_tag_o),
        .response_valid_i(ks_response_valid_i),.response_tag_i(ks_response_tag_i),.left_i(ks_left_i),.right_i(ks_right_i),
        .response_valid_o(ks_valid),.response_o(ks_response),.error_o(channel_error[1]));
    pcm_request_channel #(.ENABLE(ENABLE_FM)) u_fm_channel(
        .global_rst_n_i,.audio_clk_i,.audio_rst_n_i,.native_clk_i(fm_clk_i),.native_rst_n_i(fm_rst_n_i),
        .request_tick_i(frame_accept),.request_tag_i(request_tag),
        .request_valid_o(fm_request_valid_o),.request_ready_i(fm_request_ready_i),.request_tag_o(fm_request_tag_o),
        .response_valid_i(fm_response_valid_i),.response_tag_i(fm_response_tag_i),.left_i(fm_left_i),.right_i(fm_right_i),
        .response_valid_o(fm_valid),.response_o(fm_response),.error_o(channel_error[2]));
    wire incoming_valid=source==1 ? ks_valid : source==2 ? fm_valid : sub_valid;
    wire [111:0] incoming=source==1 ? ks_response : source==2 ? fm_response : sub_response;
    logic [1:0] input_valid,output_valid;
    logic [111:0] input_packet[0:1],output_packet[0:1];
    wire incoming_slot=incoming[48];
    wire [31:0] next_play_sequence=sequence_r-32'd2;
    wire incoming_not_expired=$signed(incoming[79:48]-next_play_sequence)>=0;
    wire incoming_current=incoming[111:80]==epoch && incoming_not_expired &&
        $signed(sequence_r-incoming[79:48])>0;
    wire choose_input=input_valid[0] && (!input_valid[1] ||
        $signed(input_packet[0][79:48]-input_packet[1][79:48])<=0) ? 1'b0 : 1'b1;
    logic [1:0] epoch_frames,ready_frames;
    wire eligible=control_seen && epoch_frames>=2;
    wire play_slot=next_play_sequence[0];
    wire [63:0] play_tag={epoch,next_play_sequence};
    wire play_found=eligible && output_valid[play_slot] && output_packet[play_slot][111:48]==play_tag;
    wire effect_online,worker_ready,worker_valid,worker_late,worker_clip;
    wire [111:0] worker_result;
    wire [127:0] worker_stats;
    wire output_ready=effect_online || test_tone;
    wire processing=control_seen && selected_alive && source_enabled && output_ready;
    wire audible=processing && !muted;
    wire signed [23:0] serial_left=audible && play_found ? $signed(output_packet[play_slot][47:24]) : 24'sd0;
    wire signed [23:0] serial_right=audible && play_found ? $signed(output_packet[play_slot][23:0]) : 24'sd0;
    assign latency_load_o=ENABLE_MIDI_PCM_LATENCY_TEST && frame_accept;
    assign latency_end_o=ENABLE_MIDI_PCM_LATENCY_TEST && frame_complete;
    assign latency_left_o=ENABLE_MIDI_PCM_LATENCY_TEST ? serial_left : 24'sd0;
    assign latency_right_o=ENABLE_MIDI_PCM_LATENCY_TEST ? serial_right : 24'sd0;
    assign latency_engine_o=ENABLE_MIDI_PCM_LATENCY_TEST ? source[1:0] : 2'd0;
    assign latency_tag_o=ENABLE_MIDI_PCM_LATENCY_TEST ? play_tag : 64'd0;
    assign latency_quality_o=ENABLE_MIDI_PCM_LATENCY_TEST ?
        {2'b0,processing,control_seen,test_tone,audible,eligible,play_found} : 8'd0;
    pt8211_tx #(.BCK_HALF_CYCLES(4)) u_tx(
        .clk_i(audio_clk_i),.rst_n_i(reset_n),.left_i(serial_left[23:8]),.right_i(serial_right[23:8]),
        .bck_o(hp_bck_o),.ws_o(hp_ws_o),.data_o(hp_data_o),.i2s_ws_o(pcm_ws_o),
        .frame_accept_o(frame_accept),.frame_latched_o(frame_latched),.frame_complete_o(frame_complete));
    assign pcm_bck_o=hp_bck_o;assign pcm_data_o=hp_data_o;
    assign frame_tick_o=frame_accept;
    logic signed [23:0] accepted_left,accepted_right;
    logic [63:0] accepted_tag;
    logic [2:0] accepted_source;
    logic [31:0] accepted_revision;
    logic accepted_valid,output_matched;
    assign busy_o=!control_seen || !selected_alive || !output_ready || !output_matched;
    assign fm_active_o=accepted_valid && accepted_source==2;
    logic [23:0] tone_phase;
    wire signed [17:0] tone_q216;
    osc_core u_tone(.phase_next(tone_phase),.duty(24'sd0),.wave(2'd3),.sample_out(tone_q216));
    wire signed [23:0] tone_sample={tone_q216[15:0],8'b0};
    logic tone_pending;
    logic [63:0] tone_tag;
    logic [1:0] filter_state;
    logic filter_tick,worker_input_valid;
    logic signed [23:0] filter_left,filter_right;
    wire signed [23:0] filtered_left,filtered_right;
    logic [63:0] filter_tag;
    logic [111:0] worker_input;
    output_tilt #(.ENABLE(ENABLE_OUTPUT_TILT),.SHIFT(OUTPUT_TILT_SHIFT)) u_tilt_l(
        .clk(audio_clk_i),.rst_n(effect_reset_n),.tick(filter_tick),.in(filter_left),.out(filtered_left));
    output_tilt #(.ENABLE(ENABLE_OUTPUT_TILT),.SHIFT(OUTPUT_TILT_SHIFT)) u_tilt_r(
        .clk(audio_clk_i),.rst_n(effect_reset_n),.tick(filter_tick),.in(filter_right),.out(filtered_right));
    pcm_frame_effect #(.ENABLE_REVERB(ENABLE_REVERB)) u_worker(
        .clk_i(audio_clk_i),.rst_n_i(effect_reset_n),.data_i(worker_input),.valid_i(worker_input_valid),
        .ready_o(worker_ready),.data_o(worker_result),.valid_o(worker_valid),.ready_i(1'b1),
        .wet_q15_i(wet),.damp_q15_i(damp),.online_o(effect_online),.late_o(worker_late),
        .clipped_o(worker_clip),.stats_o(worker_stats));
    logic [31:0] synth_received,synth_processed,synth_played,synth_errors;
    logic [31:0] fm_received,fm_processed,fm_played,fm_errors;
    logic [31:0] good_frames,late_frames;
    logic [2:0] observed_channel_error;
    assign synth_stats_o={synth_received,synth_processed,synth_played,synth_errors};
    assign fm_stats_o={fm_received,fm_processed,fm_played,fm_errors};
    // Retain the existing common-worker SPI diagnostic field layout.
    assign audio_stats_o=worker_stats;
    integer j;
    always_ff @(posedge audio_clk_i or negedge reset_n)begin
        if(!reset_n)begin
            control_seen<=0;source<=0;muted<=1;test_tone<=0;bypass_tilt<=0;revision<=0;wet<=0;damp<=0;
            epoch<=0;sequence_r<=0;epoch_frames<=0;ready_frames<=0;flush<=0;alive_meta<=0;alive_sync<=0;previous_alive<=0;
            input_valid<=0;output_valid<=0;filter_state<=0;filter_tick<=0;worker_input_valid<=0;
            filter_left<=0;filter_right<=0;filter_tag<=0;worker_input<=0;
            accepted_left<=0;accepted_right<=0;accepted_tag<=0;accepted_source<=0;accepted_revision<=0;accepted_valid<=0;
            tap_left_o<=0;tap_right_o<=0;tap_tag_o<=0;tap_source_o<=0;tap_revision_o<=0;tap_valid_o<=0;
            output_matched<=0;tone_phase<=0;tone_pending<=0;tone_tag<=0;
            synth_received<=0;synth_processed<=0;synth_played<=0;synth_errors<=0;
            fm_received<=0;fm_processed<=0;fm_played<=0;fm_errors<=0;
            good_frames<=0;late_frames<=0;observed_channel_error<=0;
            for(j=0;j<2;j=j+1)begin input_packet[j]<=0;output_packet[j]<=0;end
        end else begin
            alive_meta<={fm_rst_n_i,ks_rst_n_i,sub_rst_n_i};alive_sync<=alive_meta;previous_alive<=selected_alive;
            flush<=0;tap_valid_o<=0;
            if(controls_valid)begin wet<=controls[63:48];damp<=controls[47:32];end
            // The sample timeline and serial capture never pause for an effect
            // clear, source switch, mute, missing response or engine warmup.
            if(frame_accept)begin
                sequence_r<=sequence_r+32'd1;tone_phase<=tone_phase+24'd262144;
                accepted_left<=serial_left;accepted_right<=serial_right;
                accepted_tag<=play_tag;accepted_source<=source;accepted_revision<=revision;accepted_valid<=eligible && play_found;
            end
            if(frame_latched)begin
                tap_left_o<=accepted_left;tap_right_o<=accepted_right;tap_tag_o<=accepted_tag;
                tap_source_o<=accepted_source;tap_revision_o<=accepted_revision;tap_valid_o<=accepted_valid;
            end
            if(control_change || fault_change)begin
                if(control_change)begin
                    source<=controls[69:67];muted<=controls[66];test_tone<=controls[65];bypass_tilt<=controls[64];revision<=controls[31:0];control_seen<=1;
                end
                epoch<=epoch+32'd1;epoch_frames<=0;ready_frames<=0;flush<=1;output_matched<=0;
                input_valid<=0;output_valid<=0;filter_state<=0;filter_tick<=0;worker_input_valid<=0;tone_pending<=0;
            end else if(flush)begin
                input_valid<=0;output_valid<=0;filter_state<=0;filter_tick<=0;worker_input_valid<=0;tone_pending<=0;
            end else begin
                filter_tick<=0;
                if(frame_accept)begin
                    if(epoch_frames!=3)epoch_frames<=epoch_frames+1'b1;
                    // SCRUB and the first two source-paced fill intervals
                    // are expected silence, not missed processing deadlines.
                    if(processing)begin if(ready_frames!=3)ready_frames<=ready_frames+1'b1;end
                    else ready_frames<=0;
                    if(eligible && play_found && output_ready && selected_alive)begin
                        output_matched<=1;
                        if(source==2)fm_played<=fm_played+32'd1;else synth_played<=synth_played+32'd1;
                    end
                    if(eligible)output_valid[play_slot]<=0;
                    if(play_found)begin good_frames<=good_frames+32'd1;end
                    else if(eligible && processing)begin
                        output_matched<=0;
                        if(ready_frames>=2)begin
                        late_frames<=late_frames+32'd1;
                        if(source==2)fm_errors<=fm_errors+32'd1;else synth_errors<=synth_errors+32'd1;
                        end
                    end
                    if(test_tone && control_seen && !muted)begin tone_pending<=1;tone_tag<=request_tag;end
                end
                if(tone_pending)begin
                    output_packet[tone_tag[0]]<={tone_tag,tone_sample,tone_sample};output_valid[tone_tag[0]]<=1;tone_pending<=0;
                end
                if(incoming_valid && incoming_current && processing && !test_tone)begin
                    if(!input_valid[incoming_slot])begin
                        input_packet[incoming_slot]<={incoming[111:48],muted?48'd0:incoming[47:0]};input_valid[incoming_slot]<=1;
                        if(source==2)fm_received<=fm_received+32'd1;else synth_received<=synth_received+32'd1;
                    end else begin
                        if(source==2)fm_errors<=fm_errors+32'd1;else synth_errors<=synth_errors+32'd1;
                    end
                end
                case(filter_state)
                    0:if((|input_valid) && worker_ready && !worker_input_valid && !test_tone)begin
                        filter_tag<=input_packet[choose_input][111:48];
                        filter_left<=$signed(input_packet[choose_input][47:24]);filter_right<=$signed(input_packet[choose_input][23:0]);
                        input_valid[choose_input]<=0;filter_tick<=1;filter_state<=1;
                    end
                    1:filter_state<=2;
                    2:begin worker_input<={filter_tag,bypass_tilt?filter_left:filtered_left,bypass_tilt?filter_right:filtered_right};worker_input_valid<=1;filter_state<=3;end
                    3:if(worker_input_valid && worker_ready)begin
                        worker_input_valid<=0;filter_state<=0;
                        if(source==2)fm_processed<=fm_processed+32'd1;else synth_processed<=synth_processed+32'd1;
                    end
                endcase
                if(worker_valid && worker_result[111:80]==epoch && !test_tone &&
                    $signed(worker_result[79:48]-next_play_sequence)>=0)begin
                    if(!output_valid[worker_result[48]])begin
                        output_packet[worker_result[48]]<=worker_result;output_valid[worker_result[48]]<=1;
                    end else begin
                        if(source==2)fm_errors<=fm_errors+32'd1;else synth_errors<=synth_errors+32'd1;
                    end
                end
                if(|(channel_error & ~observed_channel_error))begin
                    observed_channel_error<=observed_channel_error | channel_error;
                    if(source==2)fm_errors<=fm_errors+32'd1;else synth_errors<=synth_errors+32'd1;
                end
            end
        end
    end
endmodule
`default_nettype wire
