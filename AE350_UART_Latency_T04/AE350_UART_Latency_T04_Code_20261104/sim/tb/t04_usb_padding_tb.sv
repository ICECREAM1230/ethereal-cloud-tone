`timescale 1ns/1ps
`default_nettype none
// Real transfer bank -> packet CDC -> production ingress. The observer taps
// the very same accepted source word, before CDC, as in midi_host_frontend.
// CPU BEGIN/ACK and playback loads are directed events, not CPU/DSP models.
module t04_usb_padding_tb;
    logic clk=0,cpu_clk=0,audio_clk=0,rst_n=0;
    always #10.417 clk=~clk;
    always #10 cpu_clk=~cpu_clk;
    always #20.343 audio_clk=~audio_clk;
    logic bank_input=0,allow_padding=1;
    logic [6:0] bank_length=0;
    logic [511:0] bank_payload=0;
    wire bank_ready,packet_valid,packet_ready,format_error;
    wire [31:0] packet;
    wire cdc_ready,cdc_valid,cdc_flush,ingress_ready;
    wire [31:0] cdc_packet;
    // The optional source backpressure controls when word2 of one transfer is
    // accepted; it never changes the bank payload or invents an accepted tap.
    assign packet_ready=cdc_ready && (packet!=0 || allow_padding);
    wire source_accept=packet_valid && packet_ready;
    midi_packet_queue u_bank(.clk,.rst(!rst_n),.flush(1'b0),
        .in_valid(bank_input),.in_ready(bank_ready),.in_len(bank_length),
        .in_payload(bank_payload),.packet_data(packet),.packet_valid,
        .packet_ready,.format_error);
    midi_packet_cdc u_packet_cdc(.src_clk(clk),.dst_clk(clk),.rst_n,
        .src_data(packet),.src_valid(packet_valid && (packet!=0 || allow_padding)),
        .src_ready(cdc_ready),.src_flush(1'b0),.dst_data(cdc_packet),
        .dst_valid(cdc_valid),.dst_ready(ingress_ready),.dst_flush(cdc_flush));
    wire byte_valid,raw_ready,flush_valid,local_flush;
    wire [1:0] byte_source;
    wire [7:0] byte_data;
    wire [63:0] byte_tick,flush_tick;
    wire [3:0] flush_mask,blocked;
    wire [31:0] accepted,packet_errors,padding;
    logic done=0;
    logic [3:0] done_mask=0;
    // The harness acknowledges the startup boundary; no host/CPU command is
    // synthesized by this completion shim. Later flushes remain observable.
    always @(posedge clk)begin
        done<=rst_n && flush_valid;
        done_mask<=flush_mask;
    end
    ae350_ingress_frontend u_ingress(.clk_i(clk),.rst_ni(rst_n),
        .usb_packet_data_i(cdc_packet),.usb_packet_valid_i(cdc_valid),
        .usb_packet_ready_o(ingress_ready),.usb_session_flush_i(cdc_flush),
        .usb_flush_busy_i(local_flush),.usb_local_flush_o(local_flush),
        .raw_source_i(2'd1),.raw_data_i(8'd0),.raw_valid_i(1'b0),
        .raw_ready_o(raw_ready),.raw_enabled_i(1'b0),.mailbox_flush_i(4'd0),
        .flush_done_valid_i(done),.flush_done_mask_i(done_mask),.flush_done_sequence_i(32'd1),
        .byte_valid_o(byte_valid),.byte_ready_i(1'b1),.byte_source_o(byte_source),
        .byte_data_o(byte_data),.byte_tick_o(byte_tick),
        .flush_valid_o(flush_valid),.flush_ready_i(1'b1),.flush_mask_o(flush_mask),
        .flush_tick_o(flush_tick),.blocked_sources_o(blocked),
        .accepted_count_o(accepted),.packet_error_count_o(packet_errors),.padding_count_o(padding));
    logic [1:0] engine=0;
    logic cpu_submit=0,cpu_ack=0,serial_load=0,result_ready=0;
    logic [68:0] identity={32'd7,32'd11,5'd2};
    logic signed [23:0] left=0,right=0;
    wire [575:0] result;
    wire result_valid,loss;
    ae350_latency_capture #(.QUIET_FRAMES(4),.TIMEOUT_CYCLES(4000)) u_capture(
        .clk_i(clk),.rst_ni(rst_n),.usb_accept_i(source_accept),.usb_packet_i(packet),
        .other_midi_i(byte_valid && byte_source!=0 && byte_data<8'hf8),
        .selected_engine_i(engine),.flush_i(flush_valid ? flush_mask[2:0] : 3'd0),
        .cpu_clk_i(cpu_clk),.cpu_rst_ni(rst_n),.cpu_submit_i(cpu_submit),.cpu_ack_i(cpu_ack),
        .cpu_engine_i(engine),.cpu_ack_engine_i(engine),
        .cpu_submit_identity_i(identity),.cpu_ack_identity_i(identity),
        .audio_clk_i(audio_clk),.audio_rst_ni(rst_n),.serial_load_i(serial_load),
        .serial_left_i(left),.serial_right_i(right),.serial_engine_i(engine),.serial_quality_i(8'h37),
        .result_o(result),.result_valid_o(result_valid),.result_ready_i(result_ready),.loss_o(loss));
    integer checks=0,scenarios=0,normal_rows=0,invalid_rows=0,fd;
    integer source_words=0,source_zeros=0,byte_count=0,flush_count=0;
    integer base_flush=0;
    logic [7:0] bytes_seen[0:15];
    logic [63:0] ref_tick=0,t0=0;
    always @(posedge clk)begin
        if(!rst_n)begin
            source_words<=0;source_zeros<=0;byte_count<=0;flush_count<=0;
            ref_tick<=0;t0<=0;
        end else begin
            ref_tick<=ref_tick+1'b1;
            if(source_accept)begin
                source_words<=source_words+1;
                if(packet==0)source_zeros<=source_zeros+1;
                if(packet==32'h403c9009)t0<=ref_tick;
                $fwrite(fd,"WORD scenario=%0d tick=%0d packet=%08x\n",scenarios,ref_tick,packet);
            end
            if(byte_valid)begin
                if(byte_count<16)bytes_seen[byte_count]<=byte_data;
                byte_count<=byte_count+1;
                $fwrite(fd,"BYTE scenario=%0d tick=%0d source=%0d value=%02x\n",scenarios,ref_tick,byte_source,byte_data);
            end
            if(flush_valid)flush_count<=flush_count+1;
        end
    end
    task automatic check(input logic good,input string message);
        checks++;
        if(!good)$fatal(1,"USB_PADDING_ASSERT scenario=%0d check=%0d %s",scenarios,checks,message);
    endtask
    task automatic settle(input integer n);
        repeat(n)@(posedge clk);
        #0.001;
    endtask
    task automatic reset_trial(input integer e);
        @(negedge clk);rst_n=0;engine=2'(e);bank_input=0;bank_payload=0;bank_length=0;
        allow_padding=1;cpu_submit=0;cpu_ack=0;serial_load=0;result_ready=0;left=0;right=0;
        settle(5);@(negedge clk);rst_n=1;settle(100);
        check(blocked==0 && !flush_valid && !local_flush,"startup boundary completed");
        base_flush=flush_count;
    endtask
    task automatic pair(input logic signed [23:0] l,input logic signed [23:0] r);
        @(negedge audio_clk);left=l;right=r;serial_load=1;
        @(negedge audio_clk);serial_load=0;settle(12);
    endtask
    task automatic quiet_audio;
        repeat(5)pair(24'sd0,24'sd0);
        check(u_capture.quiet,"qualified zero playback before trial");
    endtask
    task automatic transfer(input logic [31:0] first,input integer count);
        integer guard;
        guard=0;
        while(!bank_ready && guard<1000)begin settle(1);guard++;end
        check(bank_ready,"transfer bank ready");
        @(negedge clk);bank_payload=0;bank_payload[31:0]=first;
        bank_length=7'(count*4);bank_input=1;
        @(negedge clk);bank_input=0;
    endtask
    task automatic wait_words(input integer count);
        integer guard;
        guard=0;
        while(source_words<count && guard<1500)begin settle(1);guard++;end
        check(source_words==count,"actual transfer bank word handshakes");
    endtask
    task automatic begin_event;
        @(negedge cpu_clk);cpu_submit=1;
        @(negedge cpu_clk);cpu_submit=0;settle(14);
    endtask
    task automatic ack_event;
        @(negedge cpu_clk);cpu_ack=1;
        @(negedge cpu_clk);cpu_ack=0;settle(14);
    endtask
    task automatic result_row(output logic [575:0] row);
        integer guard;
        guard=0;
        while(!result_valid && guard<4500)begin settle(1);guard++;end
        check(result_valid,"result mailbox becomes valid");row=result;
        $fwrite(fd,"ROW scenario=%0d ",scenarios);
        for(integer w=0;w<18;w++)$fwrite(fd,"%08x%s",row[w*32+:32],w==17?"\n":",");
    endtask
    task automatic check_production(input integer expected_bytes,input integer expected_padding);
        settle(60);
        check(!format_error && packet_errors==0,"bank format and production ingress have no error");
        check(flush_count==base_flush && blocked==0,"padding/valid MIDI cause no source flush");
        check(byte_count==expected_bytes && accepted==32'(expected_bytes),"production emits only musical MIDI bytes");
        check(padding==32'(expected_padding) && source_zeros==expected_padding,"padding reaches and is consumed by production ingress");
        check(bytes_seen[0]==8'h90 && bytes_seen[1]==60 && bytes_seen[2]==64,"NoteOn byte order preserved");
        $display("PRODUCTION_OK scenario=%0d bytes=%0d padding=%0d flush_delta=%0d",scenarios,byte_count,padding,flush_count-base_flush);
    endtask
    initial begin:run
        logic [575:0] row;
        logic [31:0] bad_packet;
        fd=$fopen("usb_words_bytes_rows.log","w");
        check(fd!=0,"raw transfer/byte/row evidence opens");
        for(integer e=0;e<3;e++)for(integer stage=0;stage<3;stage++)begin
            scenarios++;reset_trial(e);quiet_audio();
            allow_padding=stage==0;
            transfer(32'h403c9009,2);wait_words(1);
            if(stage==0)begin
                wait_words(2);check_production(3,1);
                check(!result_valid && !u_capture.row[100],"padding before BEGIN must not freeze observer");
            end
            begin_event();
            if(stage==1)begin
                @(negedge clk);allow_padding=1;wait_words(2);check_production(3,1);
                check(!result_valid && !u_capture.row[100],"padding between BEGIN and ACK must not freeze observer");
            end
            ack_event();
            if(stage==2)begin
                @(negedge clk);allow_padding=1;wait_words(2);check_production(3,1);
                check(!result_valid && !u_capture.row[100],"padding after ACK must not freeze observer");
            end
            pair(24'sd1,24'sd0);result_row(row);
            check(row[96+:32]==32'h1f && !loss,"all four timestamps valid after padding");
            check(row[128+:64]==t0,"T0 equals actual source-word acceptance tick");
            check(row[192+:64]>=t0 && row[256+:64]>=row[192+:64] && row[320+:64]>=row[256+:64],"directed T0 T1 T2 T3 event order");
            check(row[384+:32]==11 && row[416+:32]==7 && row[448+:32]==2,"command identity survives padding");
            check(row[512+:32]==32'h100 && row[544+:32]==0,"full PCM24 plus-one endpoint retained");
            normal_rows++;
        end
        for(integer e=0;e<3;e++)for(integer kind=0;kind<5;kind++)begin
            scenarios++;reset_trial(e);quiet_audio();
            transfer(32'h403c9009,1);wait_words(1);settle(60);
            case(kind)
                0:bad_packet=32'h003c8008; // NoteOff
                1:bad_packet=32'h4001b00b; // CC
                2:bad_packet=32'h203ca00a; // polyphonic aftertouch
                3:bad_packet=32'h4000e00e; // pitch bend
                default:bad_packet=32'h003c9009; // velocity-zero NoteOn
            endcase
            transfer(bad_packet,1);wait_words(2);check_production(6,0);result_row(row);
            check(row[96+:32]==32'h451 && !loss,"nonzero interfering MIDI still freezes invalid T0-only row");
            check(row[192+:192]==0,"interference never invents T1 T2 T3");
            invalid_rows++;
        end
        $fclose(fd);
        $display("PASS T04_USB_PADDING checks=%0d scenarios=%0d normal_rows=%0d invalid_rows=%0d",checks,scenarios,normal_rows,invalid_rows);
        $finish;
    end
    initial begin #5000000;$fatal(1,"USB padding fixture global timeout");end
endmodule
`default_nettype wire
