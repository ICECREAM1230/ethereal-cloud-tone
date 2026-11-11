`timescale 1ns/1ps
`default_nettype none
// Diagnostic observer only. All clocks are supplied by the active design.
// Timestamps are generated only in clk_i; reported boundaries include the
// event synchronizer delay (normally 60..80 ns at 50 MHz), not UART time.
module midi_pcm_latency_event #(
    parameter integer WIDTH=1
)(
    input wire src_clk_i,src_rst_ni,src_enable_i,src_valid_i,
    input wire [WIDTH-1:0] src_data_i,
    output wire src_ready_o,
    output logic src_loss_o,
    input wire dst_clk_i,dst_rst_ni,
    output logic dst_valid_o,
    output logic [WIDTH-1:0] dst_data_o
);
    wire link_rst_n=src_rst_ni && dst_rst_ni;
    // Cancel both phases on either reset, then release only on local edges.
    (* ASYNC_REG="TRUE" *) logic source_reset_meta,source_reset_sync,dest_reset_meta,dest_reset_sync;
    always_ff @(posedge src_clk_i or negedge link_rst_n)begin
        if(!link_rst_n)begin source_reset_meta<=0;source_reset_sync<=0;end
        else begin source_reset_meta<=1;source_reset_sync<=source_reset_meta;end
    end
    always_ff @(posedge dst_clk_i or negedge link_rst_n)begin
        if(!link_rst_n)begin dest_reset_meta<=0;dest_reset_sync<=0;end
        else begin dest_reset_meta<=1;dest_reset_sync<=dest_reset_meta;end
    end
    logic req,ack,seen;
    logic [WIDTH-1:0] held_data;
    (* ASYNC_REG="TRUE" *) logic ack_meta,ack_sync,req_meta,req_sync;
    assign src_ready_o=req==ack_sync;
    always_ff @(posedge src_clk_i or negedge source_reset_sync) begin
        if(!source_reset_sync)begin req<=0;held_data<=0;ack_meta<=0;ack_sync<=0;src_loss_o<=0;end
        else begin
            ack_meta<=ack;ack_sync<=ack_meta;
            if(!src_enable_i)src_loss_o<=0;
            else if(src_valid_i)begin
                if(src_ready_o)begin held_data<=src_data_i;req<=~req;end
                else src_loss_o<=1;
            end
        end
    end
    always_ff @(posedge dst_clk_i or negedge dest_reset_sync)begin
        if(!dest_reset_sync)begin req_meta<=0;req_sync<=0;seen<=0;ack<=0;dst_valid_o<=0;dst_data_o<=0;end
        else begin
            req_meta<=req;req_sync<=req_meta;dst_valid_o<=0;
            if(req_sync!=seen)begin
                // The payload has been held for both request synchronizers.
                dst_data_o<=held_data;dst_valid_o<=1;seen<=req_sync;ack<=req_sync;
            end
        end
    end
endmodule

// A single event per arm. The enable crosses as a level before MIDI arrives.
module midi_pcm_latency_once #(
    parameter integer WIDTH=1
)(
    input wire src_clk_i,src_rst_ni,arm_i,event_i,
    input wire [WIDTH-1:0] payload_i,
    input wire dst_clk_i,dst_rst_ni,
    output wire valid_o,
    output wire [WIDTH-1:0] data_o,
    output wire loss_o
);
    (* ASYNC_REG="TRUE" *) logic arm_meta,arm_sync;
    logic sent;
    wire fire=arm_sync && !sent && event_i;
    always_ff @(posedge src_clk_i or negedge src_rst_ni)begin
        if(!src_rst_ni)begin arm_meta<=0;arm_sync<=0;sent<=0;end
        else begin
            arm_meta<=arm_i;arm_sync<=arm_meta;
            if(!arm_sync)sent<=0;
            else if(fire)sent<=1;
        end
    end
    midi_pcm_latency_event #(.WIDTH(WIDTH)) u_event(
        .src_clk_i,.src_rst_ni,.src_enable_i(arm_sync),.src_valid_i(fire),.src_data_i(payload_i),
        .src_ready_o(),.src_loss_o(loss_o),.dst_clk_i,.dst_rst_ni,.dst_valid_o(valid_o),.dst_data_o(data_o));
endmodule

module midi_pcm_latency_probe #(
    parameter integer CLK_HZ=50_000_000,
    parameter integer TIMEOUT_CYCLES=50_000_000,
    parameter integer QUIET_FRAMES=256,
    parameter integer HOLD_CYCLES=100_000_000,
    parameter integer THRESHOLD=1
)(
    input wire clk_i,rst_ni,
    input wire raw_clk_i,raw_rst_ni,raw_accept_i,
    input wire [1:0] raw_source_i,
    input wire [7:0] raw_data_i,
    input wire usb_clk_i,usb_rst_ni,usb_accept_i,
    input wire [31:0] usb_packet_i,
    input wire control_clk_i,control_rst_ni,
    input wire [2:0] flush_i,
    input wire [1:0] selected_engine_i,
    input wire cpu_clk_i,cpu_rst_ni,cpu_submit_i,
    input wire [1:0] cpu_engine_i,
    input wire cpu_ack_i,
    input wire [1:0] cpu_ack_engine_i,
    input wire [2:0] native_clk_i,native_rst_ni,dsp_valid_i,
    input wire [71:0] dsp_left_i,dsp_right_i,
    input wire audio_clk_i,audio_rst_ni,serial_load_i,
    input wire signed [23:0] serial_left_i,serial_right_i,
    input wire [1:0] serial_engine_i,
    input wire wire_end_i,
    output logic [511:0] result_o,
    output logic result_valid_o,
    output wire armed_o
);
`ifndef SYNTHESIS
    initial begin
        if(CLK_HZ<1 || TIMEOUT_CYCLES<1 || TIMEOUT_CYCLES>=32'h80000000 ||
           QUIET_FRAMES<1 || HOLD_CYCLES<1 || THRESHOLD<1 || THRESHOLD>32767)
            $fatal(1,"Invalid MIDI PCM latency parameters");
    end
`endif
    localparam logic signed [16:0] LIMIT=17'(THRESHOLD);
    function automatic logic above(input logic signed [23:0] l,input logic signed [23:0] r);
        logic signed [16:0] a,b;
        begin
            a={l[23],l[23:8]};b={r[23],r[23:8]};
            above=a>LIMIT || a< -LIMIT || b>LIMIT || b< -LIMIT;
        end
    endfunction
    localparam logic [1:0] WAIT_QUIET=0,ARMED=1,MEASURE=2,HOLD=3;
    logic [1:0] state;
    logic [31:0] tick,hold_count,sequence_r;
    logic [5:0] settle_count;
    wire arm=state==ARMED || state==MEASURE;
    assign armed_o=state==ARMED;

    // Route and flush use an ordinary held mailbox from their owning domain.
    // A changed route during a measurement is an invalid precondition.
    logic [2:0] flush_toggle,flush_previous;
    always_ff @(posedge control_clk_i or negedge control_rst_ni)begin
        if(!control_rst_ni)begin flush_toggle<=0;flush_previous<=0;end
        else begin flush_toggle<=flush_toggle^(flush_i & ~flush_previous);flush_previous<=flush_i;end
    end
    (* ASYNC_REG="TRUE" *) logic [2:0] flush_meta,flush_sync;
    logic [2:0] flush_seen;
    wire [2:0] flush_event=flush_sync^flush_seen;
    logic [1:0] route_held;
    logic route_req,route_ack;
    (* ASYNC_REG="TRUE" *) logic route_ack_meta,route_ack_sync,route_meta,route_sync;
    logic route_seen;
    logic [1:0] route_sys,target_engine;
    always_ff @(posedge control_clk_i or negedge control_rst_ni)begin
        if(!control_rst_ni)begin route_held<=0;route_req<=0;route_ack_meta<=0;route_ack_sync<=0;end
        else begin
            route_ack_meta<=route_ack;route_ack_sync<=route_ack_meta;
            if(route_req==route_ack_sync && route_held!=selected_engine_i)begin
                route_held<=selected_engine_i;route_req<=~route_req;
            end
        end
    end
    always_ff @(posedge clk_i or negedge rst_ni)begin
        if(!rst_ni)begin
            flush_meta<=0;flush_sync<=0;flush_seen<=0;route_meta<=0;route_sync<=0;route_seen<=0;route_ack<=0;route_sys<=0;
        end else begin
            flush_meta<=flush_toggle;flush_sync<=flush_meta;flush_seen<=flush_sync;
            route_meta<=route_req;route_sync<=route_meta;
            if(route_sync!=route_seen)begin route_sys<=route_held;route_seen<=route_sync;route_ack<=route_sync;end
        end
    end

    // RAW is a small observer of channel NoteOn syntax, not a MIDI owner.
    // Realtime bytes preserve context, other statuses replace/cancel it.
    (* ASYNC_REG="TRUE" *) logic raw_arm_meta,raw_arm_sync;
    (* ASYNC_REG="TRUE" *) logic [1:0] raw_flush_meta,raw_flush_sync;
    logic [1:0] raw_flush_seen,have_first,candidate,status_pending;
    logic [7:0] running[0:1];
    logic [6:0] note[0:1];
    wire [1:0] raw_first_fire,raw_note_fire;
    wire [31:0] raw_note_payload[0:1];
    wire [1:0] raw_first_valid,raw_note_valid;
    wire [31:0] raw_note_meta[0:1];
    wire [1:0] raw_first_loss,raw_note_loss;
    always_ff @(posedge raw_clk_i or negedge raw_rst_ni)begin
        if(!raw_rst_ni)begin
            raw_arm_meta<=0;raw_arm_sync<=0;raw_flush_meta<=0;raw_flush_sync<=0;raw_flush_seen<=0;
            have_first<=0;candidate<=0;status_pending<=0;
            for(integer n=0;n<2;n=n+1)begin running[n]<=0;note[n]<=0;end
        end else begin
            raw_arm_meta<=arm;raw_arm_sync<=raw_arm_meta;
            raw_flush_meta<=flush_toggle[2:1];raw_flush_sync<=raw_flush_meta;raw_flush_seen<=raw_flush_sync;
            for(integer n=0;n<2;n=n+1)begin
                if(!raw_arm_sync)candidate[n]<=0;
                if(raw_flush_sync[n]!=raw_flush_seen[n])begin
                    running[n]<=0;have_first[n]<=0;candidate[n]<=0;status_pending[n]<=0;
                end else if(raw_accept_i && raw_source_i==2'(n+1) && raw_data_i<8'hf8)begin
                    // Parse byte phase continuously, including outside arm.
                    // A message beginning before arm remains unqualified;
                    // its velocity must never become a new running note.
                    if(raw_data_i[7])begin
                        running[n]<=raw_data_i<8'hf0 ? raw_data_i : 8'd0;
                        have_first[n]<=0;
                        status_pending[n]<=raw_data_i[7:4]==4'h9;
                        candidate[n]<=raw_arm_sync && raw_data_i[7:4]==4'h9;
                    end else if(running[n][7:4]==4'h9)begin
                        if(!have_first[n])begin
                            note[n]<=raw_data_i[6:0];have_first[n]<=1;
                            candidate[n]<=raw_arm_sync && (status_pending[n] ? candidate[n] : 1'b1);
                        end else begin
                            have_first[n]<=0;candidate[n]<=0;status_pending[n]<=0;
                        end
                    end
                end
            end
        end
    end
    for(genvar n=0;n<2;n=n+1)begin:g_raw
        wire matching=raw_accept_i && raw_source_i==2'(n+1) && raw_arm_sync &&
            raw_flush_sync[n]==raw_flush_seen[n];
        assign raw_first_fire[n]=matching &&
            ((raw_data_i[7:4]==4'h9) || (!raw_data_i[7] && running[n][7:4]==4'h9 && !have_first[n] && !status_pending[n]));
        assign raw_note_fire[n]=matching && !raw_data_i[7] && running[n][7:4]==4'h9 &&
            have_first[n] && candidate[n] && raw_data_i[6:0]!=0;
        assign raw_note_payload[n]={10'd0,raw_data_i[6:0],note[n],running[n][3:0],2'd0,2'(n+1)};
        midi_pcm_latency_event u_first(
            .src_clk_i(raw_clk_i),.src_rst_ni(raw_rst_ni),.src_enable_i(raw_arm_sync),
            .src_valid_i(raw_first_fire[n]),.src_data_i(1'b0),.src_ready_o(),.src_loss_o(raw_first_loss[n]),
            .dst_clk_i(clk_i),.dst_rst_ni(rst_ni),.dst_valid_o(raw_first_valid[n]),.dst_data_o());
        midi_pcm_latency_event #(.WIDTH(32)) u_note(
            .src_clk_i(raw_clk_i),.src_rst_ni(raw_rst_ni),.src_enable_i(raw_arm_sync),
            .src_valid_i(raw_note_fire[n]),.src_data_i(raw_note_payload[n]),.src_ready_o(),.src_loss_o(raw_note_loss[n]),
            .dst_clk_i(clk_i),.dst_rst_ni(rst_ni),.dst_valid_o(raw_note_valid[n]),.dst_data_o(raw_note_meta[n]));
    end

    // The project's USB packet ABI is {velocity,note,status,cable/CIN}.
    // Source zero owns cable zero. A valid packet already contains NoteOn.
    (* ASYNC_REG="TRUE" *) logic usb_arm_meta,usb_arm_sync;
    wire usb_note_fire=usb_arm_sync && usb_accept_i && usb_packet_i[7:0]==8'h09 &&
        usb_packet_i[15:12]==4'h9 && !usb_packet_i[23] && !usb_packet_i[31] && usb_packet_i[30:24]!=0;
    wire usb_first_valid,usb_note_valid,usb_first_loss,usb_note_loss;
    wire [31:0] usb_note_meta;
    always_ff @(posedge usb_clk_i or negedge usb_rst_ni)begin
        if(!usb_rst_ni)begin usb_arm_meta<=0;usb_arm_sync<=0;end
        else begin usb_arm_meta<=arm;usb_arm_sync<=usb_arm_meta;end
    end
    midi_pcm_latency_event u_usb_first(
        .src_clk_i(usb_clk_i),.src_rst_ni(usb_rst_ni),.src_enable_i(usb_arm_sync),
        .src_valid_i(usb_note_fire),.src_data_i(1'b0),.src_ready_o(),.src_loss_o(usb_first_loss),
        .dst_clk_i(clk_i),.dst_rst_ni(rst_ni),.dst_valid_o(usb_first_valid),.dst_data_o());
    midi_pcm_latency_event #(.WIDTH(32)) u_usb_note(
        .src_clk_i(usb_clk_i),.src_rst_ni(usb_rst_ni),.src_enable_i(usb_arm_sync),
        .src_valid_i(usb_note_fire),.src_data_i({10'd0,usb_packet_i[30:24],usb_packet_i[22:16],usb_packet_i[11:8],4'd0}),
        .src_ready_o(),.src_loss_o(usb_note_loss),.dst_clk_i(clk_i),.dst_rst_ni(rst_ni),
        .dst_valid_o(usb_note_valid),.dst_data_o(usb_note_meta));

    wire cpu_submit_valid,cpu_ack_valid,cpu_submit_loss,cpu_ack_loss;
    wire [1:0] cpu_submit_engine,cpu_ack_engine;
    midi_pcm_latency_once #(.WIDTH(2)) u_cpu_submit(
        .src_clk_i(cpu_clk_i),.src_rst_ni(cpu_rst_ni),.arm_i(arm),.event_i(cpu_submit_i),.payload_i(cpu_engine_i),
        .dst_clk_i(clk_i),.dst_rst_ni(rst_ni),.valid_o(cpu_submit_valid),.data_o(cpu_submit_engine),.loss_o(cpu_submit_loss));
    midi_pcm_latency_once #(.WIDTH(2)) u_cpu_ack(
        .src_clk_i(cpu_clk_i),.src_rst_ni(cpu_rst_ni),.arm_i(arm),.event_i(cpu_ack_i),.payload_i(cpu_ack_engine_i),
        .dst_clk_i(clk_i),.dst_rst_ni(rst_ni),.valid_o(cpu_ack_valid),.data_o(cpu_ack_engine),.loss_o(cpu_ack_loss));
    wire [2:0] dsp_event,dsp_loss;
    for(genvar n=0;n<3;n=n+1)begin:g_dsp
        midi_pcm_latency_once u_first_pcm(
            .src_clk_i(native_clk_i[n]),.src_rst_ni(native_rst_ni[n]),.arm_i(arm),
            .event_i(dsp_valid_i[n] && above($signed(dsp_left_i[n*24+:24]),$signed(dsp_right_i[n*24+:24]))),.payload_i(1'b0),
            .dst_clk_i(clk_i),.dst_rst_ni(rst_ni),.valid_o(dsp_event[n]),.data_o(),.loss_o(dsp_loss[n]));
    end

    localparam integer QW=QUIET_FRAMES>1 ? $clog2(QUIET_FRAMES+1) : 1;
    logic [QW-1:0] quiet_count;
    logic quiet_audio;
    (* ASYNC_REG="TRUE" *) logic quiet_meta,quiet_sync;
    (* ASYNC_REG="TRUE" *) logic audio_arm_meta,audio_arm_sync;
    logic load_sent,end_sent,pending_end;
    wire load_fire=audio_arm_sync && !load_sent && serial_load_i && above(serial_left_i,serial_right_i);
    // wire_end on a load edge can belong to the preceding frame: require
    // pending_end from an earlier audio edge, never its next-state value.
    wire end_fire=audio_arm_sync && pending_end && !end_sent && wire_end_i;
    wire serial_valid,wire_valid,serial_loss,wire_loss;
    wire [1:0] serial_engine;
    always_ff @(posedge audio_clk_i or negedge audio_rst_ni)begin
        if(!audio_rst_ni)begin
            quiet_count<=0;quiet_audio<=0;audio_arm_meta<=0;audio_arm_sync<=0;
            load_sent<=0;end_sent<=0;pending_end<=0;
        end else begin
            audio_arm_meta<=arm;audio_arm_sync<=audio_arm_meta;
            if(serial_load_i)begin
                if(above(serial_left_i,serial_right_i))begin quiet_count<=0;quiet_audio<=0;end
                else if(quiet_count<QW'(QUIET_FRAMES))begin
                    quiet_count<=quiet_count+1'b1;
                    if(quiet_count==QW'(QUIET_FRAMES-1))quiet_audio<=1;
                end
            end
            if(!audio_arm_sync)begin load_sent<=0;end_sent<=0;pending_end<=0;end
            else begin
                if(load_fire)begin load_sent<=1;pending_end<=1;end
                if(end_fire)begin end_sent<=1;pending_end<=0;end
            end
        end
    end
    midi_pcm_latency_event #(.WIDTH(2)) u_serial_load(
        .src_clk_i(audio_clk_i),.src_rst_ni(audio_rst_ni),.src_enable_i(audio_arm_sync),.src_valid_i(load_fire),
        .src_data_i(serial_engine_i),.src_ready_o(),.src_loss_o(serial_loss),.dst_clk_i(clk_i),.dst_rst_ni(rst_ni),
        .dst_valid_o(serial_valid),.dst_data_o(serial_engine));
    midi_pcm_latency_event u_wire_end(
        .src_clk_i(audio_clk_i),.src_rst_ni(audio_rst_ni),.src_enable_i(audio_arm_sync),.src_valid_i(end_fire),
        .src_data_i(1'b0),.src_ready_o(),.src_loss_o(wire_loss),.dst_clk_i(clk_i),.dst_rst_ni(rst_ni),
        .dst_valid_o(wire_valid),.dst_data_o());
    always_ff @(posedge clk_i or negedge rst_ni)begin
        if(!rst_ni)begin quiet_meta<=0;quiet_sync<=0;end
        else begin quiet_meta<=quiet_audio;quiet_sync<=quiet_meta;end
    end
    wire [12:0] source_loss={raw_first_loss,raw_note_loss,usb_first_loss,usb_note_loss,
        cpu_submit_loss,cpu_ack_loss,dsp_loss,serial_loss,wire_loss};
    (* ASYNC_REG="TRUE" *) logic [12:0] loss_meta,loss_sync;
    always_ff @(posedge clk_i or negedge rst_ni)begin
        if(!rst_ni)begin loss_meta<=0;loss_sync<=0;end
        else begin loss_meta<=source_loss;loss_sync<=loss_meta;end
    end
    // Sticky loss from a stopped, unrelated source may survive a disarm.
    // Attribute it only after this measurement's source is established.
    function automatic logic input_loss(input logic [12:0] losses,input logic [1:0] source);
        begin
            case(source)
                0:input_loss=losses[8] || losses[7];
                1:input_loss=losses[11] || losses[9];
                2:input_loss=losses[12] || losses[10];
                default:input_loss=1;
            endcase
        end
    endfunction
    wire common_loss=loss_sync[6] || loss_sync[5] || loss_sync[1] || loss_sync[0] ||
        (target_engine==0 ? loss_sync[2] : target_engine==1 ? loss_sync[3] : loss_sync[4]);
    wire [2:0] first_event={raw_first_valid[1],raw_first_valid[0],usb_first_valid};
    wire [2:0] note_event={raw_note_valid[1],raw_note_valid[0],usb_note_valid};
    wire any_note=|note_event;
    wire [1:0] note_source=note_event[0] ? 2'd0 : note_event[1] ? 2'd1 : 2'd2;
    wire [31:0] note_metadata=note_event[0] ? usb_note_meta : note_event[1] ? raw_note_meta[0] : raw_note_meta[1];
    logic [2:0] first_seen;
    logic [31:0] first_tick[0:2];
    (* ASYNC_REG="TRUE" *) logic [7:0] alive_meta,alive_sync;
    wire [7:0] domain_alive={control_rst_ni,raw_rst_ni,usb_rst_ni,cpu_rst_ni,audio_rst_ni,native_rst_ni};
    always_ff @(posedge clk_i or negedge rst_ni)begin
        if(!rst_ni)begin alive_meta<=0;alive_sync<=0;end
        else begin alive_meta<=domain_alive;alive_sync<=alive_meta;end
    end
    logic [31:0] work_flags,work_meta;
    // A stopped, unused SPI clock must not prevent a USB measurement.
    // Control, CPU, selected DSP and audio are common; source health is
    // qualified only after the selected NoteOn establishes its source.
    wire common_alive=alive_sync[7] && alive_sync[4] && alive_sync[3] &&
        target_engine<3 && alive_sync[{1'b0,target_engine}];
    wire input_alive=work_meta[1:0]==0 ? alive_sync[5] : alive_sync[6];
    logic [31:0] first_time,note_time,submit_time,ack_time,dsp_time,serial_time,end_time;
    logic finish_pending;
    always_ff @(posedge clk_i or negedge rst_ni)begin
        if(!rst_ni)begin
            tick<=0;state<=WAIT_QUIET;hold_count<=0;sequence_r<=0;target_engine<=0;settle_count<=0;
            first_seen<=0;work_flags<=1;work_meta<=0;first_time<=0;note_time<=0;
            submit_time<=0;ack_time<=0;dsp_time<=0;serial_time<=0;end_time<=0;
            finish_pending<=0;result_o<=0;result_valid_o<=0;
            for(integer n=0;n<3;n=n+1)first_tick[n]<=0;
        end else begin
            tick<=tick+1'b1;
            if(arm)begin
                for(integer n=0;n<3;n=n+1)begin
                    if(first_event[n])begin first_seen[n]<=1;first_tick[n]<=tick;end
                    if(flush_event[n])first_seen[n]<=0;
                end
                if(cpu_submit_valid && cpu_submit_engine==target_engine && !work_flags[6])begin work_flags[6]<=1;submit_time<=tick;end
                if(cpu_ack_valid && cpu_ack_engine==target_engine && !work_flags[7])begin work_flags[7]<=1;ack_time<=tick;end
                if(target_engine<3 && dsp_event[target_engine] && !work_flags[8])begin work_flags[8]<=1;dsp_time<=tick;end
                if(serial_valid && serial_engine==target_engine && !work_flags[9])begin work_flags[9]<=1;serial_time<=tick;end
                if(wire_valid && work_flags[9] && !work_flags[10])begin work_flags[10]<=1;end_time<=tick;end
                if(state==MEASURE && (common_loss || input_loss(loss_sync,work_meta[1:0])))work_flags[11]<=1;
            end
            case(state)
                WAIT_QUIET:begin
                    finish_pending<=0;
                    if(!quiet_sync || route_sys!=target_engine || |flush_event || !common_alive)begin settle_count<=0;target_engine<=route_sys;end
                    else if(settle_count<32)settle_count<=settle_count+1'b1;
                    if(quiet_sync && route_sys<3 && route_sys==target_engine && settle_count==32 && !(|flush_event) && common_alive)begin
                        state<=ARMED;target_engine<=route_sys;first_seen<=0;work_flags<=1;work_meta<=0;
                        first_time<=0;note_time<=0;submit_time<=0;ack_time<=0;dsp_time<=0;serial_time<=0;end_time<=0;
                    end
                end
                ARMED:begin
                    if(any_note)begin
                        state<=MEASURE;work_meta<=(note_metadata & 32'hfffffff3)|{28'd0,target_engine,2'b0};
                        work_flags[4]<=first_seen[note_source] || first_event[note_source];work_flags[5]<=1;
                        first_time<=first_event[note_source] ? tick : first_tick[note_source];note_time<=tick;
                        if(!(first_seen[note_source] || first_event[note_source]) || common_loss || input_loss(loss_sync,note_source))work_flags[11]<=1;
                        if((note_event & (note_event-3'd1))!=0)work_flags[3]<=1;
                        // A first endpoint/command already seen while waiting
                        // for a complete NoteOn cannot be attributed to it.
                        if(|work_flags[10:6] || cpu_submit_valid || cpu_ack_valid ||
                           (target_engine<3 && dsp_event[target_engine]) || serial_valid || wire_valid)begin
                            work_flags[3]<=1;finish_pending<=1;
                        end
                    end else if(!quiet_sync || route_sys!=target_engine || |flush_event || !common_alive)begin state<=WAIT_QUIET;settle_count<=0;end
                end
                MEASURE:begin
                    if(!common_alive || !input_alive)begin work_flags[3]<=1;work_flags[11]<=1;finish_pending<=1;end
                    if(|flush_event || route_sys!=target_engine || any_note || (serial_valid && serial_engine!=target_engine))begin work_flags[3]<=1;finish_pending<=1;end
                    if(work_flags[10])finish_pending<=1;
                    if((tick-first_time)>=32'(TIMEOUT_CYCLES))begin work_flags[2]<=1;finish_pending<=1;end
                    if(finish_pending)begin
                        result_o<={32'h4c415431,sequence_r+32'd1,(work_flags|32'd2),work_meta,
                            first_time,note_time,submit_time,ack_time,dsp_time,serial_time,end_time,32'd0,
                            32'(CLK_HZ),32'(TIMEOUT_CYCLES),32'(THRESHOLD),32'd0};
                        result_valid_o<=1;sequence_r<=sequence_r+1'b1;state<=HOLD;hold_count<=0;
                    end
                end
                HOLD:begin
                    if(hold_count==32'(HOLD_CYCLES-1))begin state<=WAIT_QUIET;settle_count<=0;end
                    else hold_count<=hold_count+1'b1;
                end
                default:state<=WAIT_QUIET;
            endcase
        end
    end
endmodule
`default_nettype wire
