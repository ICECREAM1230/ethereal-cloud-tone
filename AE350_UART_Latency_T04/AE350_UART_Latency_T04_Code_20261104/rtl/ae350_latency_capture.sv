`timescale 1ns/1ps
`default_nettype none
// USB/control48 observations only. AE350 owns the 20-row table, arithmetic
// and UART formatting. This block has one active row and one held result.
// T0 is the actual complete accepted cable-zero NoteOn edge. T1/T2/T3 are
// observations of coherent source events, including synchronizer latency.
module ae350_latency_capture #(
    parameter integer CLK_HZ=48_000_000,
    parameter integer TIMEOUT_CYCLES=48_000_000,
    parameter integer QUIET_FRAMES=256,
    parameter integer LIMIT=20
)(
    input wire clk_i,rst_ni,usb_accept_i,other_midi_i,
    input wire [31:0] usb_packet_i,
    input wire [1:0] selected_engine_i,
    input wire [2:0] flush_i,
    input wire cpu_clk_i,cpu_rst_ni,cpu_submit_i,cpu_ack_i,
    input wire [1:0] cpu_engine_i,cpu_ack_engine_i,
    input wire [68:0] cpu_submit_identity_i,cpu_ack_identity_i,
    input wire audio_clk_i,audio_rst_ni,serial_load_i,
    input wire signed [23:0] serial_left_i,serial_right_i,
    input wire [1:0] serial_engine_i,
    input wire [7:0] serial_quality_i,
    output wire [575:0] result_o,
    output wire result_valid_o,
    input wire result_ready_i,
    output wire loss_o
);
    localparam integer QW=QUIET_FRAMES>1 ? $clog2(QUIET_FRAMES+1) : 1;
    logic [QW-1:0] quiet_count;
    wire nonzero=(serial_left_i!=24'sd0 || serial_right_i!=24'sd0);
    // pcm_request_audio: bit0=matched playback pair, bit3=test tone. Other
    // live status bits vary legitimately (normal current producer is0x37).
    // Match the existing PCM measurement contract, not one literal pattern.
    wire pair_valid=serial_quality_i[0] && !serial_quality_i[3];
    always_ff @(posedge audio_clk_i or negedge audio_rst_ni)begin
        if(!audio_rst_ni)quiet_count<=0;
        else if(serial_load_i)begin
            if(nonzero || !pair_valid)quiet_count<=0;
            else if(quiet_count<QW'(QUIET_FRAMES))quiet_count<=quiet_count+1'b1;
        end
    end

    wire begin_valid,ack_valid,audio_valid;
    wire begin_loss,ack_loss,audio_loss;
    wire [70:0] begin_data,ack_data;
    wire [58:0] audio_data;
    midi_pcm_latency_event #(.WIDTH(71)) u_begin(
        .src_clk_i(cpu_clk_i),.src_rst_ni(cpu_rst_ni),.src_enable_i(1'b1),
        .src_valid_i(cpu_submit_i),.src_data_i({cpu_submit_identity_i,cpu_engine_i}),
        .src_ready_o(),.src_loss_o(begin_loss),.dst_clk_i(clk_i),.dst_rst_ni(rst_ni),
        .dst_valid_o(begin_valid),.dst_data_o(begin_data));
    midi_pcm_latency_event #(.WIDTH(71)) u_ack(
        .src_clk_i(cpu_clk_i),.src_rst_ni(cpu_rst_ni),.src_enable_i(1'b1),
        .src_valid_i(cpu_ack_i),.src_data_i({cpu_ack_identity_i,cpu_ack_engine_i}),
        .src_ready_o(),.src_loss_o(ack_loss),.dst_clk_i(clk_i),.dst_rst_ni(rst_ni),
        .dst_valid_o(ack_valid),.dst_data_o(ack_data));
    // {qualified_zero,quality,engine,R24,L24}; every real playback load is
    // observed, so ±1 in either full PCM24 channel triggers the endpoint.
    midi_pcm_latency_event #(.WIDTH(59)) u_audio(
        .src_clk_i(audio_clk_i),.src_rst_ni(audio_rst_ni),.src_enable_i(1'b1),
        .src_valid_i(serial_load_i),
        .src_data_i({!nonzero && pair_valid && quiet_count>=QW'(QUIET_FRAMES-1),
            serial_quality_i,serial_engine_i,serial_right_i,serial_left_i}),
        .src_ready_o(),.src_loss_o(audio_loss),.dst_clk_i(clk_i),.dst_rst_ni(rst_ni),
        .dst_valid_o(audio_valid),.dst_data_o(audio_data));

    wire note_on=usb_accept_i && usb_packet_i[7:0]==8'h09 &&
        usb_packet_i[15:12]==4'h9 && !usb_packet_i[23] &&
        !usb_packet_i[31] && usb_packet_i[30:24]!=0;
    // Match production ingress: an all-zero USB word is transfer padding.
    wire interference=other_midi_i || (usb_accept_i && usb_packet_i!=32'd0 && usb_packet_i[7:4]==0 &&
        usb_packet_i[15:8]<8'hf8 && !note_on);
    (* ASYNC_REG="TRUE" *) logic [2:0] loss_meta,loss_sync;
    (* ASYNC_REG="TRUE" *) logic [1:0] alive_meta,alive_sync;
    (* ASYNC_REG="TRUE" *) logic result_loss_meta,result_loss_sync;
    logic [63:0] tick;
    logic [4:0] seen_notes;
    logic [575:0] row,row_d;
    logic active,active_d,sticky_loss,sticky_loss_d;
    logic quiet;
    logic [1:0] quiet_engine,old_engine;
    logic [2:0] old_flush;
    wire route_event=selected_engine_i!=old_engine || |(flush_i & ~old_flush);
    wire source_ready;
    wire publish=active && row[100]; // flags bit4 is word3 bit4.
    held_bus_cdc #(.WIDTH(576)) u_result(
        .src_clk_i(clk_i),.dst_clk_i(cpu_clk_i),.rst_n_i(rst_ni && cpu_rst_ni),
        .src_data_i(row),.src_valid_i(publish),.src_ready_o(source_ready),
        .dst_data_o(result_o),.dst_valid_o(result_valid_o),.dst_ready_i(result_ready_i));
    always_ff @(posedge cpu_clk_i or negedge cpu_rst_ni)begin
        if(!cpu_rst_ni)begin result_loss_meta<=0;result_loss_sync<=0;end
        else begin result_loss_meta<=sticky_loss;result_loss_sync<=result_loss_meta;end
    end
    assign loss_o=result_loss_sync;

    logic [31:0] flags;
    logic [68:0] first_key;
    function automatic logic later_revision(input logic [68:0] key,input logic [68:0] original_key);
        later_revision=key[68:37]==original_key[68:37] &&
            key[4:0]==original_key[4:0] && key[36:5]>original_key[36:5];
    endfunction
    always_comb begin
        row_d=row;active_d=active;sticky_loss_d=sticky_loss;
        flags=row[96+:32];first_key={row[416+:32],row[384+:32],row[448+:5]};
        if((seen_notes<5'(LIMIT) || (active && !flags[4])) && |loss_sync)sticky_loss_d=1;
        // A published result lives in the mailbox until CPU POP. Only this
        // private active slot can be reused; the mailbox payload never changes.
        if(publish && source_ready)active_d=0;
        if(note_on && seen_notes<5'(LIMIT))begin
            if(active_d)begin
                sticky_loss_d=1;
                // A frozen row is immutable even while waiting to publish.
                if(!flags[4])begin flags[4]=1;flags[6]=1;flags[8]=1;flags[9]=1;end
            end else begin
                row_d=0;active_d=1;flags=32'h1;
                row_d[0+:32]=32'h4c543034;
                row_d[32+:32]=32'(seen_notes)+32'd1;
                row_d[64+:32]={12'd0,usb_packet_i[30:24],usb_packet_i[22:16],usb_packet_i[11:8],selected_engine_i};
                row_d[128+:64]=tick;
                row_d[480+:32]=32'(CLK_HZ);
                first_key=0;
                if(!quiet || quiet_engine!=selected_engine_i)begin flags[6]=1;flags[7]=1;end
                if(sticky_loss_d)begin flags[6]=1;flags[9]=1;end
                if(route_event || selected_engine_i>2 || !(&alive_sync))begin
                    flags[4]=1;flags[6]=1;flags[10]=1;
                end
                if(audio_valid && (audio_data[47:0]!=0))begin
                    // A simultaneous observed onset has ambiguous attribution.
                    flags[6]=1;flags[7]=1;
                end
            end
        end
        // Out-of-range notes cannot allocate, but a 21st note arriving before
        // row20's onset still destroys that active attribution. Completed
        // rows stay immutable and later playing remains outside the batch.
        if(note_on && seen_notes>=5'(LIMIT) && active_d && !flags[4])begin
            sticky_loss_d=1;flags[4]=1;flags[6]=1;flags[8]=1;flags[9]=1;
        end
        if(active_d && !flags[4])begin
            if(interference || route_event || !(&alive_sync))begin
                flags[4]=1;flags[6]=1;flags[10]=1;
            end
            if(|loss_sync)begin flags[4]=1;flags[6]=1;flags[9]=1;end
            if(!flags[4] && begin_valid && begin_data[1:0]==row_d[65:64])begin
                if(!flags[1])begin
                    flags[1]=1;row_d[192+:64]=tick;first_key=begin_data[70:2];
                    row_d[384+:32]=begin_data[38:7];
                    row_d[416+:32]=begin_data[70:39];
                    row_d[448+:32]={27'd0,begin_data[6:2]};
                end else if(begin_data[70:2]!=first_key &&
                    !(flags[2] && later_revision(begin_data[70:2],first_key)))begin
                    flags[4]=1;flags[6]=1;flags[11]=1;
                end
            end
            if(!flags[4] && ack_valid && ack_data[1:0]==row_d[65:64])begin
                if(flags[1] && ack_data[70:2]==first_key)begin
                    if(!flags[2])begin flags[2]=1;row_d[256+:64]=tick;end
                end else if(!(flags[2] && later_revision(ack_data[70:2],first_key)))begin
                    flags[4]=1;flags[6]=1;flags[11]=1;
                end
            end
            if(!flags[4] && audio_valid && audio_data[47:0]!=0 && !flags[3])begin
                flags[3]=1;row_d[320+:64]=tick;
                row_d[512+:32]={audio_data[23:0],8'd0};
                row_d[544+:32]={audio_data[47:24],8'd0};
                // An onset before this note's BEGIN cannot be attributed to
                // its command. Keep raw T3, but no later BEGIN/ACK may turn
                // that unrelated sample into a successful measurement. A
                // BEGIN on this same observation tick was processed above.
                if(!flags[1])begin flags[4]=1;flags[6]=1;flags[11]=1;end
                if(audio_data[49:48]!=row_d[65:64] || !audio_data[50] || audio_data[53])begin
                    flags[4]=1;flags[6]=1;flags[12]=1;
                end
            end
            if(&flags[3:0])flags[4]=1;
            if(!flags[4] && (tick-row_d[128+:64])>=64'(TIMEOUT_CYCLES))begin
                flags[4]=1;flags[5]=1;flags[6]=1;
            end
        end
        row_d[96+:32]=flags;
    end
    always_ff @(posedge clk_i or negedge rst_ni)begin
        if(!rst_ni)begin
            tick<=0;seen_notes<=0;row<=0;active<=0;sticky_loss<=0;
            quiet<=0;quiet_engine<=0;old_engine<=0;old_flush<=0;
            loss_meta<=0;loss_sync<=0;alive_meta<=0;alive_sync<=0;
        end else begin
            tick<=tick+1'b1;row<=row_d;active<=active_d;sticky_loss<=sticky_loss_d;
            loss_meta<={audio_loss,ack_loss,begin_loss};loss_sync<=loss_meta;
            alive_meta<={audio_rst_ni,cpu_rst_ni};alive_sync<=alive_meta;
            old_engine<=selected_engine_i;old_flush<=flush_i;
            if(audio_valid)begin quiet<=audio_data[58] && audio_data[47:0]==0;quiet_engine<=audio_data[49:48];end
            if(route_event || !(&alive_sync))quiet<=0;
            if(note_on && seen_notes<5'(LIMIT))seen_notes<=seen_notes+1'b1;
        end
    end
`ifndef SYNTHESIS
    initial begin
        if(CLK_HZ<1 || TIMEOUT_CYCLES<1 || QUIET_FRAMES<1 || LIMIT<1 || LIMIT>20)
            $fatal(1,"Invalid AE350 latency capture parameters");
    end
`endif
endmodule
`default_nettype wire
