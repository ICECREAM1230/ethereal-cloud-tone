`default_nettype none
module fm_engine #(
    parameter bit ENABLE_AE350_VOICE_OWNER=1'b0,
    parameter bit ENABLE_MIDI=0,ENABLE_LOCAL_ENGINE_CONTROL=0,FM_SHARED_CLOCK=0,FM_NATIVE96=0,FM_ISLAND_CLOCK=0,
    parameter bit CONTROL_RESET_RELEASE=1'b0,
    parameter bit PCM_REQUEST_DRIVEN=1'b0
) (
    input wire ae350_cmd_valid_i,output wire ae350_cmd_ready_o,
    output wire [1:0] ae350_cmd_result_o,
    input wire [159:0] ae350_cmd_data_i,input wire [31:0] ae350_cmd_sequence_i,
    output wire ae350_fb_valid_o,input wire ae350_fb_ready_i,
    output wire [4:0] ae350_fb_voice_o,output wire [31:0] ae350_fb_generation_o,
    output wire ae350_ready_o,output wire ae350_fault_o,
    input wire sysclk,rst_n,sclk,fm_native_clk,fm_rst_n,sample_tick,
    input wire [31:0] engine_fault,
    input wire local_switch_mute,local_select_valid,local_select_bind,
    input wire [31:0] local_select_id,
    output wire local_select_ready,local_select_done,
    output wire [31:0] local_select_done_id,local_select_result,
    input wire fm_midi_enable,fm_mode_cancel,fm_midi_valid,midi_event_pending,effective_test_tone,
    input wire [2:0] effective_engine,
    input midi_pkg::midi_event_t fm_midi_event,
    input wire [3:0] midi_submit_fence,midi_consumer_flush,
    output wire fm_midi_ready,fm_cdc_delivered,fm_cdc_drained,
    output wire fm_events_quiesced_o,
    output wire fm_event_admission_ready_o,
    output wire fm_event_link_reset_o,
    output midi_pkg::midi_event_t fm_return_event,
    input wire perf_write_enable,
    input wire [15:0] perf_write_addr,
    input wire [31:0] perf_write_data,
    input wire fm_write_enable,
    input wire [7:0] fm_write_addr,fm_read_addr,
    input wire [31:0] fm_write_data,
    output wire [31:0] fm_read_data,
    output wire fm_selected,fm_mute,fm_binding,fm_loss_mute,fm_midi_drop,
    output wire signed [23:0] fm_aligned_left,fm_aligned_right,fm_pcm_left_o,fm_pcm_right_o,
    output wire fm_route_o,fm_path_rst_n_o,fm_pcm_valid_o,reverb_fm_rst_n_o,reverb_fm_mute_o,
    output wire [7:0] fm_diag_o,
    input wire pcm_request_valid_i,
    output wire pcm_request_ready_o,
    input wire [63:0] pcm_request_tag_i,
    output wire pcm_response_valid_o,
    output wire [63:0] pcm_response_tag_o,
    output wire signed [23:0] pcm_response_left_o,pcm_response_right_o
);

`ifndef SYNTHESIS
        initial if (PCM_REQUEST_DRIVEN && (!FM_NATIVE96 || !FM_ISLAND_CLOCK))
            $fatal(1,"FM request PCM requires FM_NATIVE96=1 and FM_ISLAND_CLOCK=1");
`endif

    generate if(ENABLE_AE350_VOICE_OWNER)begin:g_prepared
        wire host_clk=FM_ISLAND_CLOCK?fm_native_clk:sysclk;
        wire path_reset_n,initialized;
        wire control_reset_n;
        // Initialized is native status data; release its observer in sysclk.
        // Keep immediate assertion independent of either clock's progress.
        jt51_reset_sync u_control_reset(.clk_i(sysclk),.rst_n_i(rst_n),.rst_n_o(control_reset_n));
        (* ASYNC_REG="TRUE" *) reg [1:0] initialized_sync;
        always @(posedge sysclk or negedge control_reset_n)
            if(!control_reset_n)initialized_sync<=0;else initialized_sync<={initialized_sync[0],initialized};
        jt51_reset_sync u_reset(.clk_i(host_clk),.rst_n_i(rst_n&&fm_rst_n&&engine_fault==0),.rst_n_o(path_reset_n));
        jt51_prepared_audio_backend u_backend(.clk_i(host_clk),.rst_n_i(path_reset_n),
            .cmd_valid_i(ae350_cmd_valid_i),.cmd_ready_o(ae350_cmd_ready_o),.cmd_result_o(ae350_cmd_result_o),.cmd_data_i(ae350_cmd_data_i),
            .ready_o(ae350_ready_o),.initialized_o(initialized),.fault_o(ae350_fault_o),
            .fb_valid_o(ae350_fb_valid_o),.fb_ready_i(ae350_fb_ready_i),.fb_voice_o(ae350_fb_voice_o),.fb_generation_o(ae350_fb_generation_o),
            .pcm_request_valid_i,.pcm_request_ready_o,.pcm_request_tag_i,.pcm_response_valid_o,.pcm_response_tag_o,
            .pcm_response_left_o,.pcm_response_right_o);
        assign fm_midi_ready=1'b1;assign fm_cdc_delivered=0;assign fm_cdc_drained=0;
        assign fm_events_quiesced_o=1;assign fm_event_admission_ready_o=1;assign fm_event_link_reset_o=!path_reset_n;assign fm_return_event='0;
        assign local_select_ready=1;assign local_select_done=local_select_valid;assign local_select_done_id=local_select_id;assign local_select_result=0;
        assign fm_read_data=0;assign fm_selected=1;assign fm_binding=1;assign fm_mute=!initialized_sync[1];
        assign fm_loss_mute=!path_reset_n;assign fm_midi_drop=0;
        assign fm_aligned_left=0;assign fm_aligned_right=0;
        assign fm_pcm_left_o=pcm_response_left_o;assign fm_pcm_right_o=pcm_response_right_o;assign fm_pcm_valid_o=pcm_response_valid_o;
        assign fm_route_o=effective_engine==2&&!effective_test_tone;assign fm_path_rst_n_o=path_reset_n;
        assign reverb_fm_rst_n_o=path_reset_n;assign reverb_fm_mute_o=!initialized||ae350_fault_o;
        assign fm_diag_o={4'd0,initialized,ae350_ready_o,1'b1,path_reset_n};
    end else begin:g_legacy
        assign ae350_cmd_result_o=0;
        wire command_valid, command_ready, command_done;
        wire [31:0] command_result;
        fm_contract_pkg::fm_command_t command;
        wire panic_valid, panic_ready, panic_done;
        wire backend_ready, backend_fault, audio_flush, audio_enable;
        wire selection_applied, selection_next;
        wire [31:0] fifo_data;
        wire fifo_empty, fifo_read;
        wire [8:0] fifo_count;
        wire signed [23:0] resampled_left, resampled_right;
        wire resampled_valid, resampled_primed;
        wire [31:0] resampler_underflows;
        reg [3:0] diagnostic_seen_r;

        // All FM owners and PCM processing share the native clock in the
        // production island. Legacy benches retain their original boundary.
        wire host_clk = FM_ISLAND_CLOCK ? fm_native_clk : sysclk;
        wire host_rst_n;
        if(ENABLE_LOCAL_ENGINE_CONTROL)begin:g_local_fault_reset
            // A timed-out selection may still be held by the command CDC or
            // backend when the native clock stops without deasserting LOCK.
            // Cancel the whole FM transaction on fault; resuming the clock or
            // releasing local ownership must never replay that old SELECT.
            jt51_reset_sync u_reset (.clk_i(host_clk),
                .rst_n_i(rst_n && fm_rst_n && engine_fault==0),.rst_n_o(host_rst_n));
        end else begin:g_legacy_host_reset
            assign host_rst_n=rst_n && fm_rst_n;
        end
        assign fm_path_rst_n_o=host_rst_n;
        wire selected_native, mute_native, drop_native, loss_native;
        wire local_valid_native,local_bind_native,local_ready_native,local_done_native,local_lock_native;
        wire [31:0] local_id_native,local_done_id_native,local_result_native;
        // Only a confirmed reset of both command/event endpoints cancels an
        // epoch. A stopped native clock with reset high is not cancellation.
        (* ASYNC_REG="TRUE" *) logic [1:0] link_reset_sync;
        always_ff @(posedge sysclk or negedge rst_n)
            if(!rst_n)link_reset_sync<=2'b11;
            else link_reset_sync<={link_reset_sync[0],!host_rst_n};
        assign fm_event_link_reset_o=link_reset_sync[1];
        wire reset_deselect_hold;
        if(FM_ISLAND_CLOCK && ENABLE_LOCAL_ENGINE_CONTROL)begin:g_local_select_cdc
            (* ASYNC_REG="TRUE" *) logic [1:0] lock_sync;
            always_ff @(posedge host_clk or negedge host_rst_n)
                if(!host_rst_n)lock_sync<=0;else lock_sync<={lock_sync[0],local_switch_mute};
            assign local_lock_native=lock_sync[1];
            wire normal_ready,normal_done,normal_valid;
            wire [31:0] normal_done_id,normal_result;
            local_select_cdc u_select (
                .src_clk_i(sysclk),.dst_clk_i(host_clk),.rst_n_i(host_rst_n),
                .src_valid_i(normal_valid),.src_bind_i(local_select_bind),.src_id_i(local_select_id),
                .src_ready_o(normal_ready),.src_done_o(normal_done),
                .src_done_id_o(normal_done_id),.src_result_o(normal_result),
                .dst_valid_o(local_valid_native),.dst_bind_o(local_bind_native),.dst_id_o(local_id_native),
                .dst_ready_i(local_ready_native),.dst_done_i(local_done_native),
                .dst_done_id_i(local_done_id_native),.dst_result_i(local_result_native));
            if(CONTROL_RESET_RELEASE)begin:g_reset_release
                logic pending,done;
                logic [31:0] pending_id,done_id;
                wire accept=local_select_valid && local_select_ready && link_reset_sync[1];
                assign normal_valid=local_select_valid && !pending && !link_reset_sync[1];
                assign local_select_ready=!pending && (link_reset_sync[1] ? !local_select_bind : normal_ready);
                assign local_select_done=done || normal_done;
                assign local_select_done_id=done ? done_id : normal_done_id;
                assign local_select_result=done ? 32'd0 : normal_result;
                assign reset_deselect_hold=pending || accept;
                // Reset already cancelled the actual FM backend. Apply an
                // explicit DESELECT to the sticky guard, then acknowledge
                // the original ID only after its source-domain feedback is 0.
                always_ff @(posedge sysclk or negedge rst_n)begin
                    if(!rst_n)begin pending<=0;done<=0;pending_id<=0;done_id<=0;end
                    else begin
                        done<=0;
                        if(accept)begin pending<=1;pending_id<=local_select_id;end
                        else if(pending && !fm_binding)begin
                            pending<=0;done<=1;done_id<=pending_id;
                        end
                    end
                end
            end else begin:g_legacy_release
                assign normal_valid=local_select_valid;
                assign local_select_ready=normal_ready;assign local_select_done=normal_done;
                assign local_select_done_id=normal_done_id;assign local_select_result=normal_result;
                assign reset_deselect_hold=1'b0;
            end
        end else begin:g_direct_local_select
            assign reset_deselect_hold=1'b0;
            assign local_valid_native=local_select_valid;assign local_bind_native=local_select_bind;
            assign local_id_native=local_select_id;assign local_lock_native=local_switch_mute;
            assign local_select_ready=local_ready_native;assign local_select_done=local_done_native;
            assign local_select_done_id=local_done_id_native;assign local_select_result=local_result_native;
        end
        // Observe actual handshakes in the native host domain. Flags never
        // gate a command, a response or any musical reset/enable. Request
        // mode observes native tagged response and final publication; legacy
        // mode retains the FIFO-read and SRC-output milestones.
        always @(posedge host_clk or negedge host_rst_n) begin
            if (!host_rst_n) diagnostic_seen_r <= 4'b0000;
            else if (!selected_native) diagnostic_seen_r <= 4'b0000;
            else begin
                // READY也用于丢弃事件；只把原生owner实际接纳的NoteOn记为已到达。
                if (owner_enable && owner_valid && owner_ready && !drop_native &&
                    !owner_flush[owner_event.source[1:0]] && owner_event.status[7:4] == 4'h9 &&
                    owner_event.data2 != 0) diagnostic_seen_r[0] <= 1'b1;
                if (diagnostic_seen_r[0] && command_done &&
                    command_result == fm_contract_pkg::FM_RESULT_OK &&
                    command.opcode_id == fm_contract_pkg::FM_SPI_OPCODE_NOTE_ON)
                    diagnostic_seen_r[1] <= 1'b1;
                if (PCM_REQUEST_DRIVEN) begin
                    if (diagnostic_seen_r[1] && request_response_valid &&
                        ((request_left != 0) || (request_right != 0)))
                        diagnostic_seen_r[2] <= 1'b1;
                    if (diagnostic_seen_r[2] && pcm_response_valid_o &&
                        ((pcm_response_left_o != 0) || (pcm_response_right_o != 0)))
                        diagnostic_seen_r[3] <= 1'b1;
                end else begin
                    if (diagnostic_seen_r[1] && fifo_read && !fifo_empty && fifo_data != 0)
                        diagnostic_seen_r[2] <= 1'b1;
                    if (diagnostic_seen_r[2] && resampled_valid &&
                        ((resampled_left != 0) || (resampled_right != 0)))
                        diagnostic_seen_r[3] <= 1'b1;
                end
            end
        end
        assign fm_diag_o = {diagnostic_seen_r,
            (!mute_native && !loss_native && !local_lock_native),
            backend_ready, selected_native, host_rst_n};
        wire owner_enable, owner_valid, owner_ready, owner_pending;
        wire [3:0] owner_flush, owner_fence;
        midi_pkg::midi_event_t owner_event;
        if (FM_ISLAND_CLOCK && ENABLE_MIDI) begin : g_event_cdc
            midi_event_cdc u_events (
                .src_clk_i(sysclk), .dst_clk_i(host_clk), .rst_n_i(host_rst_n),
                .src_enable_i(fm_midi_enable), .src_cancel_i(fm_mode_cancel),
                .src_flush_i(midi_submit_fence), .src_event_i(fm_midi_event),
                .src_valid_i(fm_midi_valid), .src_ready_o(fm_midi_ready),
                .src_delivered_o(fm_cdc_delivered), .src_drained_o(fm_cdc_drained),
                .src_quiesced_o(fm_events_quiesced_o),
                .src_control_ready_o(fm_event_admission_ready_o),
                .src_return_event_o(fm_return_event), .dst_enable_o(owner_enable),
                .dst_event_o(owner_event), .dst_valid_o(owner_valid),
                .dst_ready_i(owner_ready), .dst_drop_i(drop_native),
                .dst_flush_o(owner_flush), .dst_pending_o(owner_pending)
            );
            assign owner_fence=owner_flush;
        end else begin : g_direct_events
            assign owner_enable=fm_midi_enable;
            assign owner_event=fm_midi_event;
            assign owner_valid=fm_midi_valid;
            assign fm_midi_ready=owner_ready;
            assign owner_pending=midi_event_pending && fm_midi_enable;
            assign owner_flush=midi_consumer_flush;
            assign owner_fence=midi_submit_fence;
            assign fm_cdc_delivered=1'b0;
            assign fm_cdc_drained=1'b0;
            assign fm_events_quiesced_o=1'b1;
            assign fm_event_admission_ready_o=1'b1;
            assign fm_return_event='0;
        end
        fm_control_bridge #(.ENABLE_AE350_VOICE_OWNER(ENABLE_AE350_VOICE_OWNER),.ENABLE_MIDI(ENABLE_MIDI && !ENABLE_AE350_VOICE_OWNER),.REGISTERED_EVENT_INPUT(ENABLE_MIDI),
            .ENABLE_LOCAL_SELECT(ENABLE_LOCAL_ENGINE_CONTROL || ENABLE_AE350_VOICE_OWNER),
            .ASYNC_READBACK(FM_ISLAND_CLOCK),
            .SOURCE_RATE_NUMERATOR(PCM_REQUEST_DRIVEN ? 32'd91015625 : FM_NATIVE96 ? 32'd11328125 : 32'd4921875),
            .SOURCE_RATE_DENOMINATOR(PCM_REQUEST_DRIVEN ? 32'd948 : FM_NATIVE96 ? 32'd118 : 32'd88),
            .OUTPUT_RATE_NUMERATOR(PCM_REQUEST_DRIVEN ? 32'd91015625 : FM_ISLAND_CLOCK ? 32'd11328125 : 32'd91015625),
            .OUTPUT_RATE_DENOMINATOR(PCM_REQUEST_DRIVEN ? 32'd948 : FM_ISLAND_CLOCK ? 32'd118 : 32'd948)) u_control (
            .local_select_valid_i(ENABLE_AE350_VOICE_OWNER ? !ae_boot_sent : local_valid_native),.local_select_bind_i(ENABLE_AE350_VOICE_OWNER ? 1'b1 : local_bind_native),
            .local_select_id_i(ENABLE_AE350_VOICE_OWNER ? 32'd1 : local_id_native),.local_switch_busy_i(local_lock_native),
            .local_select_ready_o(local_ready_native),.local_select_done_o(local_done_native),
            .local_select_done_id_o(local_done_id_native),.local_select_result_o(local_result_native),
            .midi_enable_i(owner_enable),.midi_event_i(owner_event),
            .midi_event_valid_i(owner_valid),.midi_event_ready_o(owner_ready),
            .midi_capture_pending_i(owner_pending),.midi_event_drop_o(drop_native),
            .midi_flush_i(owner_flush),.midi_flush_fence_i(owner_fence),.perf_write_enable_i(perf_write_enable),
            .perf_write_addr_i(perf_write_addr),.perf_write_data_i(perf_write_data),
            .clk_i(host_clk), .rst_n_i(host_rst_n), .spi_clk_i(sclk),.read_clk_i(sysclk),
            .write_enable_i(fm_write_enable), .write_addr_i(fm_write_addr),
            .write_data_i(fm_write_data), .read_addr_i(fm_read_addr),
            .read_data_o(fm_read_data), .command_valid_o(command_valid),
            .command_ready_i(command_ready), .command_o(command),
            .command_done_i(command_done), .command_result_i(command_result),
            .panic_valid_o(panic_valid), .panic_ready_i(panic_ready),
            .panic_done_i(panic_done), .backend_ready_i(backend_ready),
            .backend_fault_i(backend_fault), .selected_o(selected_native), .mute_o(mute_native),
            .selection_applied_o(selection_applied), .selection_next_o(selection_next),
            .expression_applied_data_o()
        );
        // Retain the selected binding across FM-local reset/clock loss. Only
        // global reset or a completed explicit SELECT can release this mute.
        wire guard_rst_n;
        if (FM_ISLAND_CLOCK) begin : g_guard_reset
            jt51_reset_sync u_reset (.clk_i(host_clk),.rst_n_i(rst_n && !reset_deselect_hold),.rst_n_o(guard_rst_n));
        end else begin : g_direct_guard_reset
            assign guard_rst_n=rst_n;
        end
        logic was_selected, loss_mute;
        always_ff @(posedge host_clk or negedge guard_rst_n) begin
            if (!guard_rst_n) begin was_selected <= 0; loss_mute <= 0; end
            else begin
                if (fm_rst_n) was_selected <= selection_next;
                if (!fm_rst_n && was_selected) loss_mute <= 1;
                else if (fm_rst_n && selection_applied) loss_mute <= 0;
            end
        end
        assign loss_native = loss_mute || (!fm_rst_n && was_selected);
        if (FM_ISLAND_CLOCK) begin : g_route_cdc
            // Only second stages drive synth-domain routing and event slots.
            // The combined route remains FM (silent) after a local fault.
            (* syn_preserve = 1, ASYNC_REG = "TRUE" *) reg [4:0] route_meta,route_sync;
            always_ff @(posedge sysclk or negedge rst_n) begin
                if (!rst_n) begin route_meta<=5'b00110;route_sync<=5'b00110;end
                else begin
                    route_meta<={selected_native || loss_native,loss_native,drop_native,mute_native,selected_native};
                    route_sync<=route_meta;
                end
            end
            assign {fm_loss_mute,fm_midi_drop,fm_mute,fm_selected}=route_sync[3:0];
            assign fm_binding=route_sync[4];
            // The existing diagnostic tone belongs to the common-rate chain.
            // Switching its DAC route never changes the native FM reader.
            assign fm_route_o=effective_engine==2 && !effective_test_tone;
        end else begin : g_direct_route
            assign fm_binding=selected_native;
            assign fm_selected=selected_native;
            assign fm_mute=mute_native;
            assign fm_midi_drop=drop_native;
            assign fm_loss_mute=loss_native;
            assign fm_route_o=1'b0;
        end
        reg ae_boot_sent,ae_boot_done;
        always @(posedge host_clk or negedge host_rst_n)begin
            if(!host_rst_n)begin ae_boot_sent<=0;ae_boot_done<=0;end
            else begin
                if(ENABLE_AE350_VOICE_OWNER && !ae_boot_sent && local_ready_native)ae_boot_sent<=1;
                if(ENABLE_AE350_VOICE_OWNER && ae_boot_sent && local_done_native && local_result_native==0)ae_boot_done<=1;
            end
        end
        wire ae_command_valid,ae_command_ready,backend_command_ready,backend_command_done;
        wire[31:0] backend_command_result;
        fm_contract_pkg::fm_command_t ae_command,backend_command;
        wire[7:0] ae_mute_mask,ae_idle_mask;wire ae_fault;
        assign ae350_fault_o=ae_fault || backend_fault;
        reg ae_inflight;
        wire backend_command_valid=!ae_inflight && (command_valid || ae_command_valid);
        assign backend_command=command_valid ? command : ae_command;
        assign command_ready=backend_command_ready && !ae_inflight;
        assign command_done=backend_command_done && !ae_inflight;
        assign command_result=backend_command_result;
        assign ae_command_ready=backend_command_ready && !ae_inflight && !command_valid;
        always @(posedge host_clk or negedge host_rst_n)begin
            if(!host_rst_n)ae_inflight<=0;
            else begin
                if(backend_command_done)ae_inflight<=0;
                if(ae_command_valid && ae_command_ready)ae_inflight<=1;
            end
        end
        assign ae_command_valid=0;assign ae_command='0;assign ae_mute_mask=0;assign ae_fault=0;
        assign ae350_cmd_ready_o=0;assign ae350_ready_o=0;assign ae350_fb_valid_o=0;assign ae350_fb_voice_o=0;assign ae350_fb_generation_o=0;
        wire signed [23:0] request_left,request_right;
        wire request_response_valid;
        jt51_audio_backend #(.ENABLE_AE350_VOICE_OWNER(ENABLE_AE350_VOICE_OWNER),.SHARED_CLOCK(FM_SHARED_CLOCK),.NATIVE96(FM_NATIVE96),.PCM_REQUEST_DRIVEN(PCM_REQUEST_DRIVEN)) u_backend (
            .clk_i(host_clk), .native_clk_i(fm_native_clk), .rst_n_i(host_rst_n),
            .ae350_mute_i(ae_mute_mask),.ae350_idle_o(ae_idle_mask),
            .command_valid_i(backend_command_valid), .command_i(backend_command),
            .command_ready_o(backend_command_ready), .command_done_o(backend_command_done),
            .command_result_o(backend_command_result), .panic_valid_i(panic_valid),
            .panic_ready_o(panic_ready), .panic_done_o(panic_done),
            .ready_o(backend_ready), .fault_o(backend_fault),
            .audio_flush_o(audio_flush), .audio_enable_o(audio_enable),
            .fifo_read_i(fifo_read), .fifo_data_o(fifo_data),
            .fifo_empty_o(fifo_empty), .fifo_count_o(fifo_count),
            .pcm_request_valid_i(pcm_request_valid_i),.pcm_request_ready_o(pcm_request_ready_o),
            .pcm_request_tag_i(pcm_request_tag_i),.pcm_response_valid_o(request_response_valid),
            .pcm_response_tag_o(pcm_response_tag_o),.pcm_response_left_o(request_left),.pcm_response_right_o(request_right)
        );
        assign pcm_response_valid_o=PCM_REQUEST_DRIVEN && request_response_valid;
        assign pcm_response_left_o=(!selected_native || mute_native || loss_native || local_lock_native) ? 24'sd0 : request_left;
        assign pcm_response_right_o=(!selected_native || mute_native || loss_native || local_lock_native) ? 24'sd0 : request_right;
        wire frame_tick;
        wire reader_rst_n = FM_ISLAND_CLOCK ? host_rst_n : rst_n;
        if (PCM_REQUEST_DRIVEN) begin : g_request_frame
            assign frame_tick=pcm_request_valid_i && pcm_request_ready_o;
        end else if (FM_ISLAND_CLOCK) begin : g_frame
            drum #(.CYCLES(128),.NUM_LANES(1),.CELLDIV(2)) u_drum (
                .clk(host_clk),.rst_n(reader_rst_n),.sample_tick(frame_tick),
                .lane_enter(),.cell_tick(),.sample_tick48(),.cell_tick48(),.slot()
            );
        end else begin : g_direct_frame
            assign frame_tick=sample_tick;
        end
        if(PCM_REQUEST_DRIVEN)begin:g_request_result
            assign fifo_read=1'b0;
            assign resampled_left=request_left;assign resampled_right=request_right;
            assign resampled_valid=request_response_valid;assign resampled_primed=backend_ready;
            assign resampler_underflows=32'd0;
        end else begin:g_resampler
        fm_audio_resampler #(
            .SOURCE_RATE_NUM(FM_ISLAND_CLOCK ? 1 : FM_NATIVE96 ? 13746 : 14931),
            .SOURCE_RATE_DEN(FM_ISLAND_CLOCK ? 1 : FM_NATIVE96 ? 13747 : 25630)
        ) u_resampler (
            .clk_i(host_clk), .rst_n_i(reader_rst_n), .enable_i(audio_enable),
            .flush_i(audio_flush), .frame_tick_i(frame_tick),
            .fifo_data_i(fifo_data), .fifo_empty_i(fifo_empty), .fifo_read_o(fifo_read),
            .left_o(resampled_left), .right_o(resampled_right),
            .valid_o(resampled_valid), .primed_o(resampled_primed),
            .underflow_count_o(resampler_underflows)
        );
        end
        logic signed [23:0] pending_left, pending_right, aligned_left, aligned_right;
        logic frame_seen;
        always_ff @(posedge host_clk or negedge reader_rst_n) begin
            if (!reader_rst_n) begin
                pending_left <= 0; pending_right <= 0;
                aligned_left <= 0; aligned_right <= 0; frame_seen <= 0;
            end else if (audio_flush || !audio_enable || mute_native) begin
                pending_left <= 0; pending_right <= 0;
                aligned_left <= 0; aligned_right <= 0; frame_seen <= 0;
            end else if (frame_tick) begin
                aligned_left <= resampled_valid ? resampled_left :
                                frame_seen ? pending_left : 24'sd0;
                aligned_right <= resampled_valid ? resampled_right :
                                 frame_seen ? pending_right : 24'sd0;
                frame_seen <= 0;
            end else if (resampled_valid) begin
                pending_left <= resampled_left; pending_right <= resampled_right;
                frame_seen <= 1;
            end
        end
        if (FM_ISLAND_CLOCK) begin : g_native_output
            wire filter_rst_n=host_rst_n && !loss_native && !(selected_native && mute_native) && !local_lock_native;
            assign fm_pcm_valid_o=frame_tick;
            assign reverb_fm_rst_n_o=filter_rst_n;
            assign reverb_fm_mute_o=!selected_native || mute_native || loss_native || local_lock_native;
            assign fm_pcm_left_o=aligned_left;
            assign fm_pcm_right_o=aligned_right;
            // No FM PCM crosses into the common-rate audio chain.
            assign fm_aligned_left=24'sd0;
            assign fm_aligned_right=24'sd0;
        end else begin : g_legacy_output
            assign fm_pcm_valid_o=1'b0;
            assign reverb_fm_rst_n_o=1'b0;
            assign reverb_fm_mute_o=1'b1;
            assign fm_aligned_left=aligned_left;
            assign fm_aligned_right=aligned_right;
            assign fm_pcm_left_o=24'sd0;
            assign fm_pcm_right_o=24'sd0;
        end

    end endgenerate
endmodule
`default_nettype wire
