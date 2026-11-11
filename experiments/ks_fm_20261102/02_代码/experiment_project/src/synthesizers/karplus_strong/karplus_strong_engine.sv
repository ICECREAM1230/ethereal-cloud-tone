`default_nettype none
module karplus_strong_engine #(
    parameter bit ENABLE_AE350_VOICE_OWNER=1'b0,
    parameter bit ENABLE_MIDI=0,ENABLE_LOCAL_ENGINE_CONTROL=0,AUTONOMOUS_USB_MIDI=0,
    parameter bit ENABLE_KS_LIMITER=1'b1,
    parameter bit ENABLE_KS_TIMBRES=1'b0,
    parameter bit KS_ISLAND_CLOCK=1'b0,
    parameter bit SPLIT_AUDIO_CLOCK=1'b0,
    parameter bit CANCEL_ON_CONTROL_FAULT=1'b0,
    parameter bit PCM_REQUEST_DRIVEN=1'b0
) (

    input wire ae350_cmd_valid_i,output wire ae350_cmd_ready_o,
    output wire [1:0] ae350_cmd_result_o,
    input wire [159:0] ae350_cmd_data_i,input wire [31:0] ae350_cmd_sequence_i,
    output wire ae350_fb_valid_o,input wire ae350_fb_ready_i,
    output wire [4:0] ae350_fb_voice_o,output wire [31:0] ae350_fb_generation_o,
    output wire ae350_ready_o,
    input wire sysclk,rst_n,sclk,
    input wire ks_native_clk,ks_clock_locked_i,
    input wire [9:0] slot,
    input wire local_select_valid,local_select_bind,
    input wire [31:0] local_select_id,
    input wire local_switch_mute,
    output wire local_select_ready,local_select_done,
    output wire [31:0] local_select_done_id,local_select_result,
    input wire ks_midi_enable,ks_midi_valid,midi_event_pending,
    input midi_pkg::midi_event_t ks_midi_event,
    output wire ks_midi_drop,ks_midi_ready,
    input wire [3:0] midi_consumer_flush,midi_submit_fence,
    input wire perf_write_enable,
    input wire [15:0] perf_write_addr,
    input wire [31:0] perf_write_data,
    output wire [31:0] ks_template_status,
    input wire ks_write_enable,
    input wire [7:0] ks_write_addr,ks_read_addr,
    input wire [31:0] ks_write_data,
    output wire [31:0] ks_read_data,
    output wire ks_mode,ks_pcm_valid,
    output wire signed [23:0] ks_pcm_left,ks_pcm_right,
    output wire [15:0] ks_active_mask,
    input wire audio_clk_i,audio_rst_n_i,
    input wire [31:0] transaction_fault_i,
    // All request/response signals below belong to core_clk (48 MHz in
    // production), not the compatibility audio-output clock domain.
    input wire pcm_request_valid_i,
    input wire [63:0] pcm_request_tag_i,
    output wire pcm_request_ready_o,
    output wire pcm_response_valid_o,
    output wire [63:0] pcm_response_tag_o,
    output wire signed [23:0] pcm_response_left_o, pcm_response_right_o
);
    wire [31:0] legacy_template_status;
    wire ks_command_valid, ks_command_ready;
    wire [2:0] ks_command_op;
    wire [3:0] ks_command_voice;
    wire ks_command_mode;
    wire [14:0] ks_command_length;
    wire signed [15:0] ks_command_tuning;
    wire [15:0] ks_command_loss, ks_command_amplitude;
    wire [15:0] ks_command_gain_left, ks_command_gain_right;
    wire [31:0] ks_command_seed, ks_command_sequence;
    wire [31:0] ks_mode_sequence, ks_accepted_sequence;
    wire [511:0] ks_started_sequences;
    wire [15:0] ks_init_mask, ks_release_mask;
    wire [31:0] ks_frame_count, ks_deadline_count, ks_reject_count;

    // The 48 MHz source is the free-running USB PLL, not a recovered USB
    // clock. Bus reset/enumeration are deliberately absent from this reset.
    // A raw audio/control reset or native lock loss cancels every CDC end,
    // including an endpoint whose clock has stopped. Release is local.
    wire shared_rst_n = rst_n && (!KS_ISLAND_CLOCK || ks_clock_locked_i) &&
                        (!SPLIT_AUDIO_CLOCK || audio_rst_n_i) &&
                        (!CANCEL_ON_CONTROL_FAULT || transaction_fault_i==0);
    wire control_rst_n, core_rst_n, audio_rst_n;
    wire core_clk = KS_ISLAND_CLOCK ? ks_native_clk : sysclk;
    wire audio_clk = SPLIT_AUDIO_CLOCK ? audio_clk_i : sysclk;
    generate if (KS_ISLAND_CLOCK || SPLIT_AUDIO_CLOCK) begin : g_clock_reset
        jt51_reset_sync u_control_reset (.clk_i(sysclk), .rst_n_i(shared_rst_n), .rst_n_o(control_rst_n));
        jt51_reset_sync u_core_reset (.clk_i(core_clk), .rst_n_i(shared_rst_n), .rst_n_o(core_rst_n));
    end else begin : g_same_clock_reset
        assign control_rst_n = rst_n;
        assign core_rst_n = rst_n;
    end endgenerate
    generate if (SPLIT_AUDIO_CLOCK) begin : g_audio_reset
        jt51_reset_sync u_reset (.clk_i(audio_clk), .rst_n_i(shared_rst_n), .rst_n_o(audio_rst_n));
    end else begin : g_shared_audio_reset
        assign audio_rst_n = control_rst_n;
    end endgenerate

    wire [31:0] ks_command_model, ks_command_template, ks_command_identity;
    wire model_valid_src,model_ready_src,model_valid_core,model_ready_core;
    wire [2047:0] model_words_src,model_words_core;
    wire [31:0] model_completed,model_result,model_applied,model_crc;
    wire [1:0] model_id;
    wire [3:0] model_state;
    wire control_select_ready;
    // A reset owner must not advertise a handshake which it cannot retain.
    assign local_select_ready = control_select_ready && (!(KS_ISLAND_CLOCK || SPLIT_AUDIO_CLOCK) || control_rst_n);
    ks_control_bridge #(.ENABLE_MIDI(ENABLE_MIDI && !ENABLE_AE350_VOICE_OWNER),.REGISTERED_EVENT_INPUT(ENABLE_MIDI),
        .ENABLE_LOCAL_SELECT(ENABLE_LOCAL_ENGINE_CONTROL && !ENABLE_AE350_VOICE_OWNER),.LOCAL_MIDI_BOOT(AUTONOMOUS_USB_MIDI),.ENABLE_KS_TIMBRES(ENABLE_KS_TIMBRES)) u_control (
        .local_select_valid_i(local_select_valid),.local_select_bind_i(local_select_bind),
        .local_select_id_i(local_select_id),.local_switch_busy_i(local_switch_mute),
        .local_select_ready_o(control_select_ready),.local_select_done_o(local_select_done),
        .local_select_done_id_o(local_select_done_id),.local_select_result_o(local_select_result),
        .midi_enable(ks_midi_enable),.midi_event(ks_midi_event),.midi_event_valid(ks_midi_valid),
        .midi_capture_pending(midi_event_pending && ks_midi_enable),.midi_event_drop(ks_midi_drop),
        .midi_event_ready(ks_midi_ready),.midi_flush(midi_consumer_flush),.midi_flush_fence(midi_submit_fence),
        .perf_write_enable,.perf_write_addr,.perf_write_data,.midi_template_status(legacy_template_status),
        .sysclk(sysclk), .rst_n(control_rst_n), .sclk(sclk),
        .write_enable(ks_write_enable && !ENABLE_AE350_VOICE_OWNER), .write_addr(ks_write_addr),
        .write_data(ks_write_data), .read_addr(ks_read_addr),
        .read_data(ks_read_data),
        .command_valid(ks_command_valid), .command_ready(ks_command_ready),
        .command_op(ks_command_op), .command_voice(ks_command_voice),
        .command_mode(ks_command_mode), .command_length(ks_command_length),
        .command_tuning(ks_command_tuning), .command_loss(ks_command_loss),
        .command_amplitude(ks_command_amplitude),
        .command_gain_left(ks_command_gain_left), .command_gain_right(ks_command_gain_right),
        .command_seed(ks_command_seed), .command_sequence(ks_command_sequence),
        .mode(ks_mode), .mode_sequence(ks_mode_sequence),
        .accepted_sequence(ks_accepted_sequence), .started_sequences(ks_started_sequences),
        .active_mask(ks_active_mask), .init_mask(ks_init_mask), .release_mask(ks_release_mask),
        .frame_count(ks_frame_count), .deadline_count(ks_deadline_count),
        .core_reject_count(ks_reject_count),
        .command_model_generation(ks_command_model),.command_template_generation(ks_command_template),
        .command_note_identity(ks_command_identity),.model_valid_o(model_valid_src),.model_ready_i(model_ready_src),
        .model_words_o(model_words_src),.model_completed_generation_i(model_completed),.model_result_i(model_result),
        .model_applied_generation_i(model_applied),.model_applied_crc_i(model_crc),
        .model_active_id_i(model_id),.model_state_i(model_state)
    );

    // Predict the drum's next slot-zero value in a dedicated KS register.
    // Reset and wrap match the original combinational tick on the same cycle;
    // this separates the long KS initialization enable route from other users.
    (* syn_preserve = 1, syn_keep = 1 *) logic ks_sample_tick;
    always_ff @(posedge audio_clk or negedge audio_rst_n) begin
        if (!audio_rst_n) ks_sample_tick <= !(KS_ISLAND_CLOCK || SPLIT_AUDIO_CLOCK);
        else ks_sample_tick <= (slot == synth_pkg::DRUM_CYCLES - 1);
    end

    wire [166:0] command_base = {ks_command_op, ks_command_voice, ks_command_mode,
        ks_command_length, ks_command_tuning, ks_command_loss, ks_command_amplitude,
        ks_command_gain_left, ks_command_gain_right, ks_command_seed, ks_command_sequence};
    localparam integer COMMAND_BITS=ENABLE_KS_TIMBRES?263:167;
    localparam integer STATUS_BITS=ENABLE_KS_TIMBRES?855:721;
    localparam integer PCM_BITS=ENABLE_KS_TIMBRES?112:48;
    wire [COMMAND_BITS-1:0] command_src, command_core;
    assign command_src=ENABLE_KS_TIMBRES?{ks_command_model,ks_command_template,ks_command_identity,command_base}:263'(command_base);
    wire command_valid_core, command_ready_core, sample_tick_core, legacy_sample_tick_core;
    wire [STATUS_BITS-1:0] status_core,status_src;
    wire [PCM_BITS-1:0] pcm_core,pcm_src;
    wire [31:0] core_model,core_template,core_identity;
    wire pcm_valid_core;
    wire [2:0] core_op;
    wire [3:0] core_voice;
    wire core_mode;
    wire [14:0] core_length;
    wire signed [15:0] core_tuning;
    wire [15:0] core_loss, core_amplitude, core_left, core_right;
    wire [31:0] core_seed, core_sequence;
    assign {core_op, core_voice, core_mode, core_length, core_tuning, core_loss,
        core_amplitude, core_left, core_right, core_seed, core_sequence} = command_core[166:0];
    assign {core_model,core_template,core_identity}=ENABLE_KS_TIMBRES?96'(command_core>>167):96'd0;
    assign {ks_mode, ks_mode_sequence, ks_accepted_sequence, ks_started_sequences,
        ks_active_mask, ks_init_mask, ks_release_mask, ks_frame_count,
        ks_deadline_count, ks_reject_count} = status_src[720:0];
    assign {model_completed,model_result,model_applied,model_crc,model_id,model_state} =
        ENABLE_KS_TIMBRES?134'(status_src>>721):134'd0;
    assign {ks_pcm_left, ks_pcm_right} = pcm_src[47:0];
    // Metadata shares exactly the same pending register and CDC handshake as PCM.
    wire [31:0] pcm_model_generation_src,pcm_frame_src;
    assign {pcm_frame_src,pcm_model_generation_src}=ENABLE_KS_TIMBRES?64'(pcm_src>>48):64'd0;

    generate if (KS_ISLAND_CLOCK || SPLIT_AUDIO_CLOCK) begin : g_clock_bridge
        // Every frame request follows the original drum timebase. A local
        // 48MHz/500 divider would drift from the actual 96008.043Hz DAC rate.
        wire [31:0] sample_overrun, pcm_overrun;
        ks_clock_bridge #(.SPLIT_AUDIO_CLOCK(SPLIT_AUDIO_CLOCK),.AUDIO_BRIDGES(!PCM_REQUEST_DRIVEN),.COMMAND_WIDTH(COMMAND_BITS),.STATUS_WIDTH(STATUS_BITS),.PCM_WIDTH(PCM_BITS)) u_cdc (
            .src_clk_i(sysclk), .ks_clk_i(core_clk), .rst_n_i(shared_rst_n),
            .audio_clk_i(audio_clk), .audio_rst_n_i(audio_rst_n_i),
            .sample_tick_i(PCM_REQUEST_DRIVEN ? 1'b0 : ks_sample_tick), .sample_tick_o(legacy_sample_tick_core),
            .command_i(command_src), .command_valid_i(ks_command_valid),
            .command_ready_o(ks_command_ready), .command_o(command_core),
            .command_valid_o(command_valid_core), .command_ready_i(command_ready_core),
            .status_i(status_core), .status_o(status_src),
            .pcm_i(pcm_core), .pcm_valid_i(PCM_REQUEST_DRIVEN ? 1'b0 : pcm_valid_core),
            .pcm_o(pcm_src), .pcm_valid_o(ks_pcm_valid),
            .sample_overrun_o(sample_overrun), .pcm_overrun_o(pcm_overrun)
        );
    if(ENABLE_KS_TIMBRES)begin:g_model_cdc
            held_bus_cdc #(.WIDTH(2048),.PRELOAD_WHEN_READY(1'b1)) u_model_cdc(
                .src_clk_i(sysclk),.dst_clk_i(core_clk),.rst_n_i(shared_rst_n),
                .src_data_i(model_words_src),.src_valid_i(model_valid_src),.src_ready_o(model_ready_src),
                .dst_data_o(model_words_core),.dst_valid_o(model_valid_core),.dst_ready_i(model_ready_core));
        end else begin:g_no_model_cdc
            assign model_words_core=0;assign model_valid_core=0;assign model_ready_src=0;
        end
    end else begin : g_same_clock
        assign model_words_core=model_words_src;assign model_valid_core=model_valid_src;assign model_ready_src=model_ready_core;
        assign command_core = command_src;
        assign command_valid_core = ks_command_valid;
        assign ks_command_ready = command_ready_core;
        assign legacy_sample_tick_core = ks_sample_tick;
        assign status_src = status_core;
        assign pcm_src = pcm_core;
        assign ks_pcm_valid = pcm_valid_core;
    end endgenerate

    generate if (PCM_REQUEST_DRIVEN) begin : g_pcm_request
        logic owned;
        logic [63:0] tag, response_tag;
        logic response_valid;
        logic signed [23:0] response_left, response_right;
        wire accept=pcm_request_valid_i && pcm_request_ready_o;
        // Periodic sample requests are the core's original cancellation
        // boundary. A safety command can cancel a frame without publication;
        // complete that old identity with silence before owning the next one.
        assign pcm_request_ready_o=core_rst_n;
        assign sample_tick_core=accept;
        always_ff @(posedge core_clk or negedge core_rst_n) begin
            if (!core_rst_n) begin
                owned<=0;tag<=0;response_tag<=0;response_valid<=0;
                response_left<=0;response_right<=0;
            end else begin
                response_valid<=0;
                if (owned && pcm_valid_core) begin
                    response_tag<=tag;response_left<=$signed(pcm_core[47:24]);
                    response_right<=$signed(pcm_core[23:0]);response_valid<=1;owned<=0;
                end
                if (accept) begin
                    if (owned && !pcm_valid_core) begin
                        response_tag<=tag;response_left<=0;response_right<=0;response_valid<=1;
                    end
                    tag<=pcm_request_tag_i;owned<=1;
                end
            end
        end
        assign pcm_response_valid_o=response_valid;
        assign pcm_response_tag_o=response_tag;
        assign pcm_response_left_o=response_left;
        assign pcm_response_right_o=response_right;
    end else begin : g_pcm_legacy
        assign sample_tick_core=legacy_sample_tick_core;
        assign pcm_request_ready_o=0;
        assign pcm_response_valid_o=0;
        assign pcm_response_tag_o=0;
        assign pcm_response_left_o=0;
        assign pcm_response_right_o=0;
    end endgenerate

    // Sixteen 4096-sample rings share the existing official delay RAM.
    // Keep the original audio timebase and complete control/status snapshots.
    wire [31:0] core_completed,core_result,core_applied,core_crc,core_pcm_model,core_pcm_frame;
    wire [1:0] core_id;wire [3:0] core_state;
    generate if(ENABLE_KS_TIMBRES)begin:g_model_status
        assign status_core[854:721]={core_completed,core_result,core_applied,core_crc,core_id,core_state};
        assign pcm_core[111:48]={core_pcm_frame,core_pcm_model};
    end endgenerate
    wire ext_core_valid;wire[2:0] ext_op;wire[3:0] ext_voice;
    wire[14:0] ext_length;wire signed[15:0] ext_tuning;wire[15:0] ext_loss,ext_amp,ext_left,ext_right;
    wire[31:0] ext_seed,ext_sequence,physical_applied_sequence;
    wire physical_command_ready;
    assign command_ready_core=physical_command_ready && !ENABLE_AE350_VOICE_OWNER;
    generate if(ENABLE_AE350_VOICE_OWNER)begin:g_ae350
        ae350_voice_backend_ks #(.LOCAL_MIDI_BOOT(AUTONOMOUS_USB_MIDI)) u_executor(
            .clk(core_clk),.rst_n(core_rst_n),.sclk(sclk),
            .cmd_valid_i(ae350_cmd_valid_i),.cmd_ready_o(ae350_cmd_ready_o),.cmd_result_o(ae350_cmd_result_o),.cmd_data_i(ae350_cmd_data_i),.ready_o(ae350_ready_o),
            .fb_valid_o(ae350_fb_valid_o),.fb_ready_i(ae350_fb_ready_i),.fb_voice_o(ae350_fb_voice_o),.fb_generation_o(ae350_fb_generation_o),
            .voice_idle(~(status_core[143:128] | status_core[127:112] | status_core[111:96])),.core_mode(status_core[720]),
            .applied_sequence_i(physical_applied_sequence),.perf_write_enable(perf_write_enable),.perf_write_addr(perf_write_addr),.perf_write_data(perf_write_data),
            .template_status(ks_template_status),.template_available(),.command_valid(ext_core_valid),.command_ready(physical_command_ready),
            .command_op(ext_op),.command_voice(ext_voice),.command_length(ext_length),.command_tuning(ext_tuning),
            .command_loss(ext_loss),.command_amplitude(ext_amp),.command_gain_left(ext_left),.command_gain_right(ext_right),
            .command_seed(ext_seed),.command_sequence(ext_sequence));
    end else begin:g_no_ae350
        assign ks_template_status=legacy_template_status;
        assign ae350_cmd_result_o=0;assign ae350_cmd_ready_o=0;assign ae350_fb_valid_o=0;assign ae350_fb_voice_o=0;assign ae350_fb_generation_o=0;assign ae350_ready_o=0;
        assign ext_core_valid=0;assign ext_op=0;assign ext_voice=0;assign ext_length=0;assign ext_tuning=0;
        assign ext_loss=0;assign ext_amp=0;assign ext_left=0;assign ext_right=0;assign ext_seed=0;assign ext_sequence=0;
    end endgenerate
    physical_model_top #(.ENABLE_AE350_VOICE_OWNER(ENABLE_AE350_VOICE_OWNER),.ENABLE_KS_LIMITER(ENABLE_KS_LIMITER),.ENABLE_KS_TIMBRES(ENABLE_KS_TIMBRES)) u_core (
        .clk_i(core_clk), .rst_n_i(core_rst_n), .sample_tick_i(sample_tick_core), .slot_i(10'd0),
        .command_valid_i((ENABLE_AE350_VOICE_OWNER ? ext_core_valid : command_valid_core)), .command_ready_o(physical_command_ready),
        .command_op_i((ENABLE_AE350_VOICE_OWNER ? ext_op : core_op)), .voice_i((ENABLE_AE350_VOICE_OWNER ? ext_voice : core_voice)), .mode_i((ENABLE_AE350_VOICE_OWNER ? 1'b1 : core_mode)),
        .length_i((ENABLE_AE350_VOICE_OWNER ? ext_length : core_length)), .tuning_i((ENABLE_AE350_VOICE_OWNER ? ext_tuning : core_tuning)), .loss_i((ENABLE_AE350_VOICE_OWNER ? ext_loss : core_loss)),
        .amplitude_i((ENABLE_AE350_VOICE_OWNER ? ext_amp : core_amplitude)), .gain_left_i((ENABLE_AE350_VOICE_OWNER ? ext_left : core_left)), .gain_right_i((ENABLE_AE350_VOICE_OWNER ? ext_right : core_right)),
        .seed_i((ENABLE_AE350_VOICE_OWNER ? ext_seed : core_seed)), .sequence_i((ENABLE_AE350_VOICE_OWNER ? ext_sequence : core_sequence)),
        .model_valid_i(model_valid_core),.model_ready_o(model_ready_core),.model_words_i(model_words_core),
        .command_model_generation_i(core_model),.command_template_generation_i(core_template),.command_note_identity_i(core_identity),
        .model_completed_generation_o(core_completed),.model_result_o(core_result),
        .model_applied_generation_o(core_applied),.model_applied_crc_o(core_crc),
        .model_active_id_o(core_id),.model_state_o(core_state),
        .pcm_model_generation_o(core_pcm_model),.pcm_frame_o(core_pcm_frame),
        .pcm_left_o(pcm_core[47:24]), .pcm_right_o(pcm_core[23:0]), .pcm_valid_o(pcm_valid_core),
        .mode_o(status_core[720]), .mode_sequence_o(status_core[719:688]),
        .applied_sequence_o(physical_applied_sequence),.accepted_sequence_o(status_core[687:656]), .started_sequences_o(status_core[655:144]),
        .active_mask_o(status_core[143:128]), .init_mask_o(status_core[127:112]),
        .release_mask_o(status_core[111:96]), .frame_count_o(status_core[95:64]),
        .deadline_count_o(status_core[63:32]), .reject_count_o(status_core[31:0])
    );

endmodule
`default_nettype wire
