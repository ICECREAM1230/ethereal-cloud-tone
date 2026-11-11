`timescale 1ns/1ps
`default_nettype none
module ae350_latency_capture_tb;
    logic clk=0,cpu_clk=0,audio_clk=0;
    always #10.417 clk=~clk;
    always #10 cpu_clk=~cpu_clk;
    always #20.343 audio_clk=~audio_clk;
    logic rst_n=0,cpu_rst_n=0,audio_rst_n=0,usb_accept=0,other_midi=0;
    logic [31:0] usb_packet=0;
    logic [1:0] engine=0,cpu_engine=0,ack_engine=0,serial_engine=0;
    logic [2:0] flush=0;
    logic cpu_submit=0,cpu_ack=0,serial_load=0,result_ready=0;
    logic [68:0] begin_key=0,ack_key=0;
    logic signed [23:0] left=0,right=0;
    logic [7:0] quality=8'h37;
    wire [575:0] result;
    wire result_valid,loss;
    ae350_latency_capture #(.QUIET_FRAMES(4),.TIMEOUT_CYCLES(800)) dut(
        .clk_i(clk),.rst_ni(rst_n),.usb_accept_i(usb_accept),.other_midi_i(other_midi),
        .usb_packet_i(usb_packet),.selected_engine_i(engine),.flush_i(flush),
        .cpu_clk_i(cpu_clk),.cpu_rst_ni(cpu_rst_n),.cpu_submit_i(cpu_submit),.cpu_ack_i(cpu_ack),
        .cpu_engine_i(cpu_engine),.cpu_ack_engine_i(ack_engine),
        .cpu_submit_identity_i(begin_key),.cpu_ack_identity_i(ack_key),
        .audio_clk_i(audio_clk),.audio_rst_ni(audio_rst_n),.serial_load_i(serial_load),
        .serial_left_i(left),.serial_right_i(right),.serial_engine_i(serial_engine),.serial_quality_i(quality),
        .result_o(result),.result_valid_o(result_valid),.result_ready_i(result_ready),.loss_o(loss));
    integer checks=0,scenarios=0,rows=0,events=0;
    integer fd;
    logic [63:0] reference_tick=0;
    realtime reference_base=0,begin_source=0,ack_source=0,audio_source=0;
    logic reference_started=0;
    realtime min_delay=1.0e9,max_delay=0;
    always @(posedge clk or negedge rst_n)begin
        if(!rst_n)begin reference_tick<=0;reference_started<=0;reference_base=0;end
        else begin
            if(!reference_started)begin reference_base=$realtime;reference_started<=1;end
            reference_tick<=reference_tick+1'b1;
        end
    end
    task automatic check(input logic good,input string message);
        checks++;
        if(!good)$fatal(1,"check %0d scenario %0d: %s",checks,scenarios,message);
    endtask
    task automatic settle(input integer n);
        repeat(n)@(posedge clk);
        #0.001;
    endtask
    task automatic reset_trial(input logic [1:0] e);
        rst_n=0;cpu_rst_n=0;audio_rst_n=0;usb_accept=0;other_midi=0;cpu_submit=0;cpu_ack=0;
        serial_load=0;result_ready=0;flush=0;left=0;right=0;quality=8'h37;
        engine=e;cpu_engine=e;ack_engine=e;serial_engine=e;
        settle(4);
        @(negedge clk);rst_n=1;cpu_rst_n=1;audio_rst_n=1;
        settle(15);
    endtask
    task automatic pair(input logic signed [23:0] l,input logic signed [23:0] r);
        @(negedge audio_clk);left=l;right=r;serial_load=1;
        @(posedge audio_clk);audio_source=$realtime;
        @(negedge audio_clk);serial_load=0;
        settle(12);
    endtask
    task automatic quiet_audio;
        repeat(5)pair(24'sd0,24'sd0);
        check(dut.quiet,"qualified exact zero audio");
    endtask
    task automatic note_on(input integer note,input integer velocity,output logic [63:0] stamp);
        @(negedge clk);usb_packet={1'b0,7'(velocity),1'b0,7'(note),8'h92,8'h09};usb_accept=1;
        @(posedge clk);stamp=reference_tick;
        @(negedge clk);usb_accept=0;
        settle(2);
    endtask
    task automatic submit_begin(input logic [68:0] key);
        @(negedge cpu_clk);begin_key=key;cpu_submit=1;
        @(posedge cpu_clk);begin_source=$realtime;
        @(negedge cpu_clk);cpu_submit=0;
        settle(12);
    endtask
    task automatic commit_ack(input logic [68:0] key);
        @(negedge cpu_clk);ack_key=key;cpu_ack=1;
        @(posedge cpu_clk);ack_source=$realtime;
        @(negedge cpu_clk);cpu_ack=0;
        settle(12);
    endtask
    task automatic await_result(output logic [575:0] row);
        integer waited;
        waited=0;
        while(!result_valid && waited<950)begin settle(1);waited++;end
        check(result_valid,"result arrives before guard timeout");row=result;
    endtask
    task automatic consume(input logic expect_empty=1);
        @(negedge cpu_clk);result_ready=1;
        @(negedge cpu_clk);result_ready=0;
        settle(16);if(expect_empty)check(!result_valid,"CPU POP releases mailbox");
    endtask
    task automatic delay_check(input logic [63:0] stamp,input realtime source,input string what);
        realtime observed,delay_ns;
        observed=reference_base+real'(stamp)*20.834;
        delay_ns=observed-source;
        check(delay_ns>=62.500 && delay_ns<=83.338,what);
        if(delay_ns<min_delay)min_delay=delay_ns;
        if(delay_ns>max_delay)max_delay=delay_ns;
        events++;
    endtask
    task automatic store_row(input logic [575:0] row);
        for(integer w=0;w<18;w++)$fwrite(fd,"%08x%s",row[w*32+:32],w==17?"\n":",");
    endtask
    initial begin:run
        logic [63:0] t0;
        logic [68:0] key;
        logic [575:0] row,held;
        realtime bsrc,asrc,psrc;
        logic [63:0] first_begin,first_ack;
        fd=$fopen("capture_records.csv","w");
        check(fd!=0,"raw record log opens");
        for(integer e=0;e<3;e++)begin
            reset_trial(2'(e));
            for(integer i=0;i<20;i++)begin
                scenarios++;
                quiet_audio();
                key={32'(i+1),32'(i+11),5'(i%(e==0?32:e==1?16:8))};
                note_on(60+i%5,64,t0);
                submit_begin(key);bsrc=begin_source;
                first_begin=dut.row[192+:64];
                if(i==0)begin
                    // Exact-key duplicate cannot replace the first successful submission.
                    submit_begin(key);check(dut.row[192+:64]==first_begin,"duplicate BEGIN does not overwrite");
                end
                if(i==1)begin
                    pair(-24'sd1,24'sd0);psrc=audio_source;
                    commit_ack(key);asrc=ack_source;
                end else begin
                    commit_ack(key);asrc=ack_source;first_ack=dut.row[256+:64];
                    if(i==2)begin
                        submit_begin({key[68:37],key[36:5]+32'd1,key[4:0]});
                        commit_ack({key[68:37],key[36:5]+32'd1,key[4:0]});
                        check(dut.row[192+:64]==first_begin && dut.row[256+:64]==first_ack,"legal SUB later revision keeps first group");
                    end
                    if(i%2==0)pair(24'sd0,24'sd1);else pair(24'sd1,24'sd0);
                    psrc=audio_source;
                end
                await_result(row);rows++;
                check(row[0+:32]==32'h4c543034,"signature");
                check(row[32+:32]==32'(i+1),"first20 original ID");
                check(row[64+:32]=={12'd0,7'd64,7'(60+i%5),4'd2,2'(e)},"MIDI/engine metadata");
                check(row[96+:32]==32'h1f,"four presence flags plus frozen; no error");
                check(row[128+:64]==t0,"T0 is exact accepted USB control edge");
                delay_check(row[192+:64],bsrc,"T1 independent source phase bound");
                delay_check(row[256+:64],asrc,"T2 independent source phase bound");
                delay_check(row[320+:64],psrc,"T3 independent source phase bound");
                check({row[416+:32],row[384+:32],row[448+:5]}==key,"first group identity exact");
                check(row[480+:32]==48_000_000,"48MHz timestamps do not use CPU or sys50 rate");
                if(i==1)begin
                    check(row[512+:32]==32'hffffff00 && row[544+:32]==0,"negative one fullPCM24 triggers");
                    check($signed(row[320+:64]-row[256+:64])<0,"T3 before T2 retained");
                end else check((row[512+:32]==32'h100 && row[544+:32]==0) ||
                    (row[544+:32]==32'h100 && row[512+:32]==0),"single-channel positive one triggers");
                held=row;settle(20);check(result===held && result_valid,"snapshot held while CPU waits");
                pair(24'sd200,24'sd300);check(result===held,"later sample never replaces first PCM");
                check(!loss,"normal slow trial has no loss");store_row(row);consume();
            end
            note_on(70,64,t0);settle(80);
            check(!result_valid && dut.seen_notes==20 && !loss,"21st NoteOn ignored without overflow");
        end

        // Zero velocity and malformed cable packets never occupy first20.
        scenarios++;reset_trial(0);quiet_audio();
        note_on(60,0,t0);check(dut.seen_notes==0,"velocity zero not a trial");
        @(negedge clk);usb_accept=1;usb_packet=32'h403c9019;
        @(negedge clk);usb_accept=0;settle(6);check(dut.seen_notes==0,"other cable ignored");

        // Identity conflicts, missing stages and source/quality faults.
        for(integer fault=0;fault<12;fault++)begin
            scenarios++;reset_trial(0);quiet_audio();key={32'd7,32'd5,5'd2};
            note_on(60,64,t0);submit_begin(key);
            case(fault)
                0:commit_ack({32'd8,32'd5,5'd2});
                1:commit_ack({32'd7,32'd5,5'd3});
                2:submit_begin({32'd7,32'd4,5'd2});
                3:submit_begin({32'd7,32'd6,5'd2}); // newer before first ACK rejected.
                4:begin engine=1;settle(4);end
                5:begin flush=1;settle(4);end
                6:begin @(negedge clk);other_midi=1;@(negedge clk);other_midi=0;settle(4);end
                7:begin audio_rst_n=0;settle(5);audio_rst_n=1;settle(15);end
                8:begin quality=8'h2d;pair(24'sd1,24'sd0);end
                9:begin serial_engine=1;pair(24'sd1,24'sd0);end
                10:begin
                    @(negedge clk);usb_packet=32'h003c8008;usb_accept=1;
                    @(negedge clk);usb_accept=0;settle(4);
                end
                11:begin cpu_rst_n=0;settle(5);cpu_rst_n=1;settle(15);end
                default:begin end
            endcase
            await_result(row);
            check(row[100] && row[102],"fault freezes invalid record");
            if(fault<4)check(row[107],"wrong command identity preserved as fault");
            else if(fault==8 || fault==9)check(row[108] && row[320+:64]!=0,"bad quality or wrong audio route keeps raw T3 and fault");
            else check(row[106],"route/flush/RAW/USB/reset cancels attribution");
            check(row[128+:64]==t0,"fault keeps original T0");store_row(row);consume();
        end
        scenarios++;reset_trial(0);quiet_audio();note_on(60,64,t0);submit_begin({32'd7,32'd5,5'd2});
        pair(24'sd1,24'sd0);await_result(row);
        check(row[101] && row[102] && row[99] && !row[98],"late missing ACK times out with first T3 and no fabricated T2");consume();

        for(integer q=0;q<3;q++)begin
            scenarios++;reset_trial(0);quality=q==0?8'h25:q==1?8'h35:8'h37;
            quiet_audio();note_on(60,64,t0);key={32'd1,32'd1,5'd0};
            submit_begin(key);commit_ack(key);pair(24'sd1,24'sd0);await_result(row);
            check(row[96+:32]==32'h1f,"valid playback quality tolerates nonreserved status-bit variations");consume();
        end

        scenarios++;reset_trial(0);quiet_audio();note_on(60,64,t0);pair(24'sd1,24'sd0);
        submit_begin({32'd1,32'd1,5'd0});commit_ack({32'd1,32'd1,5'd0});
        await_result(held);check(held[99] && !held[97] && held[100] && held[102] && held[107] &&
            held[512+:32]==32'h100,"preBEGIN PCM preserves raw T3 but freezes invalid causal attribution");
        submit_begin({32'd1,32'd1,5'd0});commit_ack({32'd1,32'd1,5'd0});
        check(result===held,"later BEGIN ACK cannot legitimize earlier unrelated PCM");consume();

        scenarios++;reset_trial(0);quiet_audio();note_on(60,64,t0);key={32'd1,32'd1,5'd0};
        cpu_engine=1;ack_engine=1;submit_begin(key);commit_ack(key);
        check(dut.row[99:96]==4'h1,"unrelated engine BEGIN and ACK do not populate target stages");
        cpu_engine=0;ack_engine=0;submit_begin(key);commit_ack(key);pair(24'sd1,24'sd0);await_result(row);
        check(row[96+:32]==32'h1f,"matching target succeeds after unrelated engine events");consume();

        scenarios++;reset_trial(0);quiet_audio();note_on(60,64,t0);key={32'd1,32'd1,5'd0};
        @(negedge cpu_clk);begin_key=key;cpu_submit=1;
        repeat(2)@(posedge cpu_clk);
        @(negedge cpu_clk);cpu_submit=0;settle(16);await_result(row);
        check(row[105] && row[102] && loss,"source mailbox overflow sticky-fails active trial");consume();

        scenarios++;reset_trial(0);note_on(60,64,t0);submit_begin({32'd1,32'd1,5'd0});
        commit_ack({32'd1,32'd1,5'd0});pair(24'sd1,24'sd0);await_result(row);
        check(row[103] && row[102] && row[99:96]==4'hf,"not-quiet trial retains all four times but invalid");consume();

        scenarios++;reset_trial(0);quiet_audio();note_on(60,64,t0);submit_begin({32'd1,32'd1,5'd0});
        note_on(61,64,t0);await_result(row);
        check(row[32+:32]==1 && row[104] && row[105] && row[102] && loss,"overlap drops ID2 visibly, never silently replaces row1");consume();
        quiet_audio();note_on(62,64,t0);submit_begin({32'd2,32'd2,5'd1});commit_ack({32'd2,32'd2,5'd1});
        pair(24'sd1,24'sd0);await_result(row);
        check(row[32+:32]==3 && row[105] && row[102],"later source ID exposes missing second slot and sticky loss");consume();

        scenarios++;reset_trial(0);quiet_audio();
        for(integer i=0;i<19;i++)begin
            note_on(60,64,t0);
            @(negedge clk);other_midi=1;@(negedge clk);other_midi=0;
            await_result(row);consume();
        end
        quiet_audio();note_on(60,64,t0);note_on(61,64,t0);await_result(row);
        check(row[32+:32]==20 && row[104] && row[102] && dut.seen_notes==20,
            "21st during active20 invalidates attribution without allocating or overwriting");consume();

        // One queued result plus one active row, never twenty parallel rows.
        scenarios++;reset_trial(0);quiet_audio();note_on(60,64,t0);submit_begin({32'd1,32'd1,5'd0});
        commit_ack({32'd1,32'd1,5'd0});pair(24'sd1,24'sd0);await_result(held);
        quiet_audio();note_on(61,64,t0);submit_begin({32'd2,32'd2,5'd1});
        commit_ack({32'd2,32'd2,5'd1});pair(24'sd1,24'sd0);settle(10);
        check(result===held && result_valid,"pending row2 cannot corrupt unread row1");
        consume(0);await_result(row);check(row[32+:32]==2 && row[96+:32]==32'h1f,"second private row publishes after mailbox release");consume();

        // Reset clears a pending old result and all batch IDs, no stale phase.
        scenarios++;reset_trial(0);quiet_audio();note_on(60,64,t0);submit_begin({32'd1,32'd1,5'd0});
        commit_ack({32'd1,32'd1,5'd0});pair(24'sd1,24'sd0);await_result(held);
        reset_trial(1);check(!result_valid && dut.seen_notes==0 && !loss,"coordinated reset cancels old pending result");
        quiet_audio();note_on(61,64,t0);submit_begin({32'd2,32'd2,5'd1});commit_ack({32'd2,32'd2,5'd1});
        pair(24'sd0,-24'sd1);await_result(row);check(row[32+:32]==1 && row[96+:32]==32'h1f && row[65:64]==1,"fresh batch after reset has no stale packet");consume();

        // Presence, rather than nonzero timestamp value, defines a captured T0.
        scenarios++;rst_n=0;cpu_rst_n=0;audio_rst_n=0;settle(4);
        @(negedge clk);usb_packet=32'h403c9009;usb_accept=1;rst_n=1;cpu_rst_n=1;audio_rst_n=1;
        @(negedge clk);usb_accept=0;await_result(row);
        check(row[96] && row[128+:64]==0 && row[100],"tick zero T0 is present and retained even in reset-boundary invalid row");consume();
        $fclose(fd);
        $display("PASS AE350 latency capture checks=%0d scenarios=%0d normal_rows=%0d independent_events=%0d min_delay_ns=%0.3f max_delay_ns=%0.3f",checks,scenarios,rows,events,min_delay,max_delay);
        $finish;
    end
    initial begin #10000000;$fatal(1,"global test guard");end
endmodule
`default_nettype wire
