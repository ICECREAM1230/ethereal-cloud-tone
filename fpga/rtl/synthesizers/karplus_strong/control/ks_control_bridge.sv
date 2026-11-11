// Copyright (c) 2026 ICECREAM1230
// Atomic KS command mailbox and readback. All addresses are WORD addresses.
// Copyright (c) 2026 project contributors. License: MIT.
`default_nettype none
module ks_control_bridge #(
    parameter bit ENABLE_MIDI=0,
    parameter bit ENABLE_KS_TIMBRES=0,
    parameter bit REGISTERED_EVENT_INPUT=0,
    parameter bit ENABLE_LOCAL_SELECT=0,
    parameter bit LOCAL_MIDI_BOOT=0
) (
    input wire sysclk, rst_n, sclk,
    input wire write_enable,
    input wire [7:0] write_addr,
    input wire [31:0] write_data,
    input wire [7:0] read_addr,
    output logic [31:0] read_data,
    output wire command_valid,
    input wire command_ready,
    output wire [2:0] command_op,
    output wire [3:0] command_voice,
    output wire command_mode,
    output wire [14:0] command_length,
    output wire signed [15:0] command_tuning,
    output wire [15:0] command_loss, command_amplitude,
    output wire [15:0] command_gain_left, command_gain_right,
    output wire [31:0] command_seed, command_sequence,
    input wire mode,
    input wire [31:0] mode_sequence, accepted_sequence,
    input wire [511:0] started_sequences,
    input wire [15:0] active_mask, init_mask, release_mask,
    input wire [31:0] frame_count, deadline_count, core_reject_count,
    input wire midi_enable,
    input midi_pkg::midi_event_t midi_event,
    input wire midi_event_valid, output wire midi_event_ready,
    input wire midi_capture_pending,
    input wire [3:0] midi_flush_fence,
    output wire midi_event_drop,
    input wire [3:0] midi_flush,
    input wire perf_write_enable,input wire [15:0] perf_write_addr,
    input wire [31:0] perf_write_data,output wire [31:0] midi_template_status,
    input wire local_select_valid_i,local_select_bind_i,
    input wire [31:0] local_select_id_i,
    input wire local_switch_busy_i,
    output wire local_select_ready_o,
    output logic local_select_done_o,
    output logic [31:0] local_select_done_id_o,local_select_result_o,
    output wire [31:0] command_model_generation,command_template_generation,command_note_identity,
    output wire model_valid_o,input wire model_ready_i,output wire [2047:0] model_words_o,
    input wire [31:0] model_completed_generation_i,model_result_i,model_applied_generation_i,model_applied_crc_i,
    input wire [1:0] model_active_id_i,input wire [3:0] model_state_i
);
    // shadow may change during busy; the mailbox itself remains stable until ACK.
    logic [31:0] shadow [0:5];
    logic [31:0] mailbox [0:6];
    logic request_toggle, acknowledge_toggle;
    (* async_reg = "true" *) logic ack_meta, ack_sync;
    (* async_reg = "true" *) logic request_meta, request_sync;
    logic [31:0] sender_reject, sender_reject_gray;
    (* async_reg = "true" *) logic [31:0] reject_meta, reject_sync;
    logic [31:0] reject_binary, sender_reject_fabric, total_reject;
    logic reject_sticky, deadline_sticky;
    logic [31:0] payload [0:6];
    logic [31:0] metadata_shadow[0:2],metadata_mailbox[0:2],metadata_payload[0:2];
    logic mailbox_extended,payload_extended;
    wire [31:0] local_model,local_template,local_identity;
    wire bank_busy,bank_cancel;
    wire [31:0] bank_image_crc,bank_requested,bank_requested_template,bank_completed,bank_result;
    wire sender_busy = request_toggle != ack_sync;
    logic spi_valid;
    wire spi_ready;
    wire local_valid,local_ready,local_hard_mute;
    wire [2:0] local_op;wire [3:0] local_voice;
    wire [14:0] local_length;wire signed [15:0] local_tuning;
    wire [15:0] local_loss,local_amplitude,local_left,local_right;wire [31:0] local_seed;
    logic local_panic;
    logic [31:0] local_sequence,last_spi_accepted,last_local_accepted;
    logic select_pending,select_issued,select_bind;
    logic [31:0] select_id,select_sequence;
    logic [2:0] switch_tail;
    logic [31:0] local_spi_reject;
    wire template_available;
    assign local_select_ready_o=ENABLE_LOCAL_SELECT&&!select_pending;
    wire select_accept=local_select_valid_i&&local_select_ready_o;
    // Retain the fence while the two-stage request synchronizer drains. A
    // request captured at the final busy edge cannot arrive after local done.
    wire local_fence=ENABLE_LOCAL_SELECT&&(local_switch_busy_i||select_pending||select_accept||(|switch_tail));
    wire select_command=ENABLE_LOCAL_SELECT&&select_pending&&!select_issued;
    wire spi_issue=spi_valid&&!local_fence;
    wire spi_barrier=spi_issue&&(payload[0][2:0]==3'd3||payload[0][2:0]==3'd4);
    // Only submission waits for the raw event's registered payload. A direct
    // legacy input leaves this optional port unused and statically ignored.
    wire raw_pending=(REGISTERED_EVENT_INPUT&&ENABLE_MIDI)?midi_capture_pending:1'b0;
    wire flush_fence=(REGISTERED_EVENT_INPUT&&ENABLE_MIDI)?(|midi_flush_fence):1'b0;
    assign midi_event_drop=!midi_enable||!template_available||spi_barrier||local_fence;
    generate if(ENABLE_MIDI&&ENABLE_KS_TIMBRES)begin:g_timbre_midi
      ks_timbre_performance #(.LOCAL_MIDI_BOOT(LOCAL_MIDI_BOOT)) performance(
        .clk(sysclk),.rst_n(rst_n),.sclk(sclk),.enable(midi_enable&&!local_fence),.clear(spi_barrier||select_accept),
        .event_i(midi_event),.event_valid(midi_event_valid),.event_ready(midi_event_ready),.flush(midi_flush),
        .voice_idle(~(active_mask|init_mask|release_mask)),
        .perf_write_enable(perf_write_enable),.perf_write_addr(perf_write_addr),.perf_write_data(perf_write_data),
        .template_status(midi_template_status),.template_available(template_available),.command_valid(local_valid),.command_ready(local_ready),
        .command_op(local_op),.command_voice(local_voice),.command_length(local_length),.command_tuning(local_tuning),
        .command_loss(local_loss),.command_amplitude(local_amplitude),.command_gain_left(local_left),.command_gain_right(local_right),
        .command_seed(local_seed),.hard_mute(local_hard_mute),
        .command_model_generation(local_model),.command_template_generation(local_template),.command_note_identity(local_identity),
        .bank_busy(bank_busy),.cancel_panic(bank_cancel),.image_crc(bank_image_crc),.request_generation(bank_requested),
        .requested_template_generation(bank_requested_template),.completed_template(bank_completed),.result(bank_result),
        .model_valid(model_valid_o),.model_ready(model_ready_i),.model_words(model_words_o),
        .model_completed_generation(model_completed_generation_i),.model_result(model_result_i),
        .model_applied_generation(model_applied_generation_i),.model_active_id(model_active_id_i));
    end else if(ENABLE_MIDI)begin:g_midi
      midi_ks_performance #(.LOCAL_MIDI_BOOT(LOCAL_MIDI_BOOT)) performance(
        .clk(sysclk),.rst_n(rst_n),.sclk(sclk),.enable(midi_enable&&!local_fence),.clear(spi_barrier||select_accept),
        .event_i(midi_event),.event_valid(midi_event_valid),.event_ready(midi_event_ready),.flush(midi_flush),
        .voice_idle(~(active_mask|init_mask|release_mask)),
        .perf_write_enable(perf_write_enable),.perf_write_addr(perf_write_addr),.perf_write_data(perf_write_data),
        .template_status(midi_template_status),.template_available(template_available),.command_valid(local_valid),.command_ready(local_ready),
        .command_op(local_op),.command_voice(local_voice),.command_length(local_length),.command_tuning(local_tuning),
        .command_loss(local_loss),.command_amplitude(local_amplitude),.command_gain_left(local_left),.command_gain_right(local_right),
        .command_seed(local_seed),.hard_mute(local_hard_mute));
    end else begin:g_legacy
      assign midi_event_ready=1'b1;assign midi_template_status=0;assign template_available=0;
      assign local_valid=0;assign local_hard_mute=0;assign local_op=0;assign local_voice=0;
      assign local_length=0;assign local_tuning=0;assign local_loss=0;assign local_amplitude=0;
      assign local_left=0;assign local_right=0;assign local_seed=0;
    end endgenerate
    generate if(!(ENABLE_MIDI&&ENABLE_KS_TIMBRES))begin:g_no_timbre
      assign local_model=0;assign local_template=0;assign local_identity=0;
      assign bank_busy=0;assign bank_cancel=0;assign bank_image_crc=0;assign bank_requested=0;
      assign bank_requested_template=0;assign bank_completed=0;assign bank_result=0;
      assign model_valid_o=0;assign model_words_o=0;
    end endgenerate
    assign local_ready=command_ready&&!spi_valid&&!local_panic&&!select_command&&!local_fence&&!raw_pending&&!flush_fence;
    assign spi_ready=command_ready&&!local_panic&&!select_command&&!local_fence;
    always_ff @(posedge sysclk or negedge rst_n)begin
      if(!rst_n)begin
        local_panic<=0;local_sequence<=32'h80000001;last_spi_accepted<=0;last_local_accepted<=0;
        select_pending<=0;select_issued<=0;select_bind<=0;select_id<=0;select_sequence<=0;
        local_select_done_o<=0;local_select_done_id_o<=0;local_select_result_o<=0;switch_tail<=0;
      end
      else begin
        switch_tail<={switch_tail[1:0],ENABLE_LOCAL_SELECT&&(local_switch_busy_i||select_pending||select_accept)};
        local_select_done_o<=0;
        if(select_accept)begin
          select_pending<=1;select_issued<=0;select_bind<=local_select_bind_i;select_id<=local_select_id_i;
        end
        if(select_command&&command_ready)begin select_issued<=1;select_sequence<=local_sequence;end
        // The core updates mode_sequence only when MODE really commits on its
        // sample tick. Command acceptance (accepted_sequence) is insufficient.
        if(select_pending&&select_issued&&mode_sequence==select_sequence&&mode==select_bind)begin
          select_pending<=0;local_select_done_o<=1;local_select_done_id_o<=select_id;local_select_result_o<=0;
        end
        if(accepted_sequence[31])last_local_accepted<=accepted_sequence;
        else last_spi_accepted<=accepted_sequence;
        if(select_command&&command_ready)local_panic<=0;
        else if(local_hard_mute||bank_cancel)local_panic<=1;
        else if(local_panic&&command_ready)local_panic<=0;
        if(command_valid&&command_ready&&(select_command||local_panic||!spi_issue))
          local_sequence<=local_sequence==32'hffffffff ? 32'h80000001:local_sequence+1;
      end
    end


    always_ff @(posedge sclk or negedge rst_n) begin
        if (!rst_n) begin
            ack_meta <= 1'b0;
            ack_sync <= 1'b0;
        end else begin
            ack_meta <= acknowledge_toggle;
            ack_sync <= ack_meta;
        end
    end

    // Capture at the final data bit itself: another SCLK edge is not guaranteed.
    always_ff @(posedge sclk or negedge rst_n) begin
        if (!rst_n) begin
            request_toggle <= 1'b0;
            sender_reject <= 32'd0;
            sender_reject_gray <= 32'd0;
            for (int j=0; j<6; j=j+1) shadow[j] <= 32'd0;
            for (int j=0; j<7; j=j+1) mailbox[j] <= 32'd0;
            for (int j=0;j<3;j=j+1)begin metadata_shadow[j]<=0;metadata_mailbox[j]<=0;end
            mailbox_extended<=0;
        end else if (write_enable) begin
            if (write_addr >= 8'h10 && write_addr <= 8'h15)
                shadow[write_addr-8'h10] <= write_data;
            else if(ENABLE_KS_TIMBRES&&write_addr>=8'h17&&write_addr<=8'h19)metadata_shadow[write_addr-8'h17]<=write_data;
            else if (write_addr == 8'h16 || (ENABLE_KS_TIMBRES&&write_addr==8'h1a)) begin
                if (!sender_busy && write_data != 32'd0 && (!ENABLE_LOCAL_SELECT||!write_data[31])
                    && (shadow[0] & 32'hFFFEF0F8) == 32'd0
                    && shadow[1][31:15] == 17'd0
                    && shadow[3][31:16] == 16'd0) begin
                    for (int j=0; j<6; j=j+1) mailbox[j] <= shadow[j];
                    mailbox[6] <= write_data;
                    mailbox_extended<=write_addr==8'h1a;
                    for(int j=0;j<3;j=j+1)metadata_mailbox[j]<=metadata_shadow[j];
                    request_toggle <= ~request_toggle;
                end else begin
                    sender_reject <= sender_reject + 32'd1;
                    sender_reject_gray <= ((sender_reject+32'd1) >> 1)
                                          ^ (sender_reject+32'd1);
                end
            end
        end
    end

    always_comb begin
        reject_binary[31] = reject_sync[31];
        for (int j=30; j>=0; j=j-1)
            reject_binary[j] = reject_binary[j+1] ^ reject_sync[j];
    end

    always_ff @(posedge sysclk or negedge rst_n) begin
        if (!rst_n) begin
            request_meta <= 1'b0; request_sync <= 1'b0;
            reject_meta <= 32'd0; reject_sync <= 32'd0;
            sender_reject_fabric <= 32'd0;
            total_reject <= 32'd0;
            local_spi_reject <= 32'd0;
            reject_sticky <= 1'b0; deadline_sticky <= 1'b0;
            acknowledge_toggle <= 1'b0;
            spi_valid <= 1'b0;
            for (int j=0; j<7; j=j+1) payload[j] <= 32'd0;
            payload_extended<=0;for(int j=0;j<3;j=j+1)metadata_payload[j]<=0;
        end else begin
            request_meta <= request_toggle;
            request_sync <= request_meta;
            reject_meta <= sender_reject_gray;
            reject_sync <= reject_meta;
            // Registered decode/count keeps the readback path shallow.
            sender_reject_fabric <= reject_binary;
            total_reject <= sender_reject_fabric + core_reject_count + local_spi_reject;
            if (sender_reject_fabric != 0 || core_reject_count != 0 || local_spi_reject != 0)
                reject_sticky <= 1'b1;
            if (deadline_count != 0) deadline_sticky <= 1'b1;
            if (!spi_valid && request_sync != acknowledge_toggle) begin
                if(local_fence || (ENABLE_KS_TIMBRES&&!mailbox_extended&&model_active_id_i!=0&&(mailbox[0][2:0]==1||mailbox[0][2:0]==6)))begin
                    acknowledge_toggle<=request_sync;local_spi_reject<=local_spi_reject+1'b1;
                end else begin
                    for (int j=0; j<7; j=j+1) payload[j] <= mailbox[j];
                    payload_extended<=mailbox_extended;
                    metadata_payload[0]<=mailbox_extended?metadata_mailbox[0]:model_applied_generation_i;
                    metadata_payload[1]<=mailbox_extended?metadata_mailbox[1]:midi_template_status;
                    metadata_payload[2]<=mailbox_extended?metadata_mailbox[2]:32'd0;
                    spi_valid <= 1'b1;
                end
            end else if(spi_valid&&local_fence)begin
                spi_valid<=0;acknowledge_toggle<=request_sync;local_spi_reject<=local_spi_reject+1'b1;
            end else if (spi_valid && spi_ready) begin
                spi_valid <= 1'b0;
                acknowledge_toggle <= request_sync;
            end
        end
    end
    assign command_valid=select_command||local_panic||spi_issue||(local_valid&&!local_fence&&!raw_pending&&!flush_fence);
    wire use_local=ENABLE_MIDI&&!spi_issue&&!local_panic&&!select_command;
    assign command_op=select_command?3'd4:local_panic?3'd3:use_local?local_op:payload[0][2:0];
    assign command_voice=use_local?local_voice:payload[0][11:8];
    assign command_mode=select_command?select_bind:payload[0][16];
    assign command_length=use_local?local_length:payload[1][14:0];
    assign command_tuning=use_local?local_tuning:$signed(payload[2][15:0]);
    assign command_loss=use_local?local_loss:payload[2][31:16];
    assign command_amplitude=use_local?local_amplitude:payload[3][15:0];
    assign command_gain_left=use_local?local_left:payload[4][15:0];
    assign command_gain_right=use_local?local_right:payload[4][31:16];
    assign command_seed=use_local?local_seed:payload[5];
    assign command_sequence=(select_command||use_local||local_panic)?local_sequence:payload[6];

    // PANIC/MODE carries the in-flight model token even if its separate CDC
    // arrives later. The core fences that token and cannot resurrect it.
    wire safety_output=command_op==3'd3||command_op==3'd4;
    wire [31:0] selected_model=(safety_output&&bank_busy)?bank_requested:use_local?local_model:metadata_payload[0];
    logic safety_target_held;
    logic [31:0] safety_target;
    // Capture the cancellation target before source-side backpressure can
    // outlast a bank transition. An old untagged SPI note also retains the
    // generation captured at mailbox receipt, never a later active model.
    always_ff @(posedge sysclk or negedge rst_n)begin
      if(!rst_n)begin safety_target_held<=0;safety_target<=0;end
      else if(!command_valid||!safety_output||command_ready)safety_target_held<=0;
      else if(!safety_target_held)begin safety_target_held<=1;safety_target<=selected_model;end
    end
    assign command_model_generation=ENABLE_KS_TIMBRES?
        ((safety_output&&safety_target_held)?safety_target:selected_model):32'd0;
    assign command_template_generation=ENABLE_KS_TIMBRES?(use_local?local_template:metadata_payload[1]):32'd0;
    assign command_note_identity=ENABLE_KS_TIMBRES?(use_local?local_identity:metadata_payload[2]):32'd0;

    logic [31:0] active_version,active_factory,pending_version,pending_factory,pending_generation;
    logic [31:0] snapshot_counter;
    wire [359:0] snapshot_key={model_state_i,bank_busy,3'd0,bank_requested,model_applied_generation_i,
        30'd0,model_active_id_i,active_version,active_factory,model_applied_crc_i,bank_image_crc,
        midi_template_status,bank_completed,bank_result,bank_requested_template};
    logic [359:0] snapshot_previous;
    always_ff @(posedge sysclk or negedge rst_n)begin
      if(!rst_n)begin active_version<=1;active_factory<=1;pending_version<=1;pending_factory<=1;
        pending_generation<=0;snapshot_counter<=0;snapshot_previous<=0;end
      else begin
        if(model_valid_o&&model_ready_i)begin
          pending_generation<=model_words_o[3*32+:32];pending_version<=model_words_o[32+:32]>>16;pending_factory<=model_words_o[4*32+:32];
        end
        if(model_completed_generation_i==pending_generation&&model_applied_generation_i==pending_generation&&model_result_i==0)begin
          active_version<=pending_version;active_factory<=pending_factory;
        end
        if(snapshot_key!=snapshot_previous)begin snapshot_previous<=snapshot_key;snapshot_counter<=snapshot_counter+1'b1;end
      end
    end
    // Snapshot only the synth-domain command contribution to status busy.
    // The synchronized SPI request/ACK term keeps its original qualification.
    logic command_busy_readback_q;
    always_ff @(posedge sysclk or negedge rst_n) begin
        if (!rst_n) command_busy_readback_q <= 1'b0;
        else command_busy_readback_q <= command_valid;
    end

    always_comb begin
        read_data = 32'd0;
        case (read_addr)
            8'h00: read_data = 32'h4B533031;
            8'h01: read_data = 32'h00020010;
            8'h02: read_data = {16'd0, release_mask[3:0], init_mask[3:0], active_mask[3:0],
                deadline_sticky, reject_sticky,
                (request_sync != acknowledge_toggle) || command_busy_readback_q, mode};
            8'h03: read_data = (ENABLE_MIDI||ENABLE_LOCAL_SELECT)?last_spi_accepted:accepted_sequence;
            8'h04: read_data = total_reject;
            8'h05: read_data = mode_sequence;
            8'h06: read_data = frame_count;
            8'h07: read_data = deadline_count;
            8'h08: read_data = {16'd0, active_mask};
            8'h09: read_data = {16'd0, init_mask};
            8'h0A: read_data = {16'd0, release_mask};
            8'h0B: read_data = 32'd4096;
            8'h20: read_data = started_sequences[31:0];
            8'h21: read_data = started_sequences[63:32];
            8'h22: read_data = started_sequences[95:64];
            8'h23: read_data = started_sequences[127:96];
            8'h24: if(ENABLE_MIDI||ENABLE_LOCAL_SELECT)read_data=last_local_accepted;
            8'h50: if(ENABLE_KS_TIMBRES)read_data=32'h4b535431;
            8'h51: if(ENABLE_KS_TIMBRES)read_data=32'h00010000;
            8'h52: if(ENABLE_KS_TIMBRES)read_data=32'h3f;
            8'h53: if(ENABLE_KS_TIMBRES)read_data=snapshot_counter;
            8'h54: if(ENABLE_KS_TIMBRES)read_data={23'd0,bank_busy,4'd0,model_state_i};
            8'h55: if(ENABLE_KS_TIMBRES)read_data=bank_requested;
            8'h56: if(ENABLE_KS_TIMBRES)read_data=model_applied_generation_i;
            8'h57: if(ENABLE_KS_TIMBRES)read_data={30'd0,model_active_id_i};
            8'h58: if(ENABLE_KS_TIMBRES)read_data=active_version;
            8'h59: if(ENABLE_KS_TIMBRES)read_data=active_factory;
            8'h5a: if(ENABLE_KS_TIMBRES)read_data=model_applied_crc_i;
            8'h5b: if(ENABLE_KS_TIMBRES)read_data=bank_image_crc;
            8'h5c: if(ENABLE_KS_TIMBRES)read_data=midi_template_status;
            8'h5d: if(ENABLE_KS_TIMBRES)read_data=bank_completed;
            8'h5e: if(ENABLE_KS_TIMBRES)read_data=bank_result;
            8'h5f: if(ENABLE_KS_TIMBRES)read_data=bank_requested_template;
            default: begin
                // Full started bank is disjoint from legacy 0524 local ACK.
                if (read_addr >= 8'h40 && read_addr <= 8'h4F)
                    read_data = started_sequences[(read_addr-8'h40)*32 +: 32];
                // Readback shadow remains private to the SPI clock domain.
            end
        endcase
    end
endmodule
`default_nettype wire
