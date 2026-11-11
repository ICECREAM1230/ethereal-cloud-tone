`timescale 1ns/1ps
`default_nettype none
module ae350_latency_apb_tb;
    localparam time BIT_NS=5000ns;
    logic pclk=0,uart_clk=0,capture_clk=0;
    always #13ns pclk=~pclk;
    always #10ns uart_clk=~uart_clk;
    always #10.417ns capture_clk=~capture_clk;
    logic preset_n=0,uart_reset_n=0;
    logic [31:0] addr=0,wdata=0;
    logic [3:0] strb=0;
    logic sel=0,enable=0,write_request=0,loss=0;
    wire [31:0] rdata;
    wire ready,error,tx,pop,result_valid,capture_ready;
    wire [575:0] result_data;
    logic [575:0] capture_data=0;
    logic capture_valid=0;
    integer checks=0,pops=0,decoded=0;
    logic [7:0] received[0:15];
    bit monitor_enabled=1;
    always @(posedge pclk)if(pop)pops=pops+1;
    held_bus_cdc #(.WIDTH(576)) u_capture_result(
        .src_clk_i(capture_clk),.dst_clk_i(pclk),.rst_n_i(preset_n),
        .src_data_i(capture_data),.src_valid_i(capture_valid),.src_ready_o(capture_ready),
        .dst_data_o(result_data),.dst_valid_o(result_valid),.dst_ready_i(pop));
    ae350_latency_apb dut(
        .pclk_i(pclk),.preset_ni(preset_n),.paddr_i(addr),.pwdata_i(wdata),.pstrb_i(strb),
        .psel_i(sel),.penable_i(enable),.pwrite_i(write_request),.prdata_o(rdata),
        .pready_o(ready),.pslverr_o(error),.result_i(result_data),.result_valid_i(result_valid),
        .result_ready_o(pop),.loss_i(loss),.uart_clk_i(uart_clk),.uart_rst_ni(uart_reset_n),.tx_o(tx));

    task automatic check(input bit condition,input string label_text);
        checks++;
        if(!condition)$fatal(1,"check %0d: %s",checks,label_text);
    endtask
    task automatic transfer(input bit wr,input logic [31:0] address_value,input logic [31:0] data_value,
                            input logic [3:0] strobes,input bit expected_error,output logic [31:0] got);
        @(negedge pclk);
        sel=1;enable=0;write_request=wr;addr=address_value;wdata=data_value;strb=strobes;
        repeat(3)begin @(posedge pclk);#1ns;check(!pop,"SETUP never POPs");end
        @(negedge pclk);enable=1;
        #1ns;got=rdata;check(ready,"APB is bounded ready");check(error==expected_error,"APB error matches access");
        if(expected_error)check(!pop,"illegal access has no POP side effect");
        @(posedge pclk);#1ns;
        @(negedge pclk);sel=0;enable=0;write_request=0;strb=0;
    endtask
    task automatic rd(input logic [31:0] address_value,input logic [31:0] expected);
        logic [31:0] got;
        transfer(0,address_value,0,4'hf,0,got);check(got==expected,"read data");
    endtask
    task automatic wr(input logic [31:0] address_value,input logic [31:0] value);
        logic [31:0] ignored;
        transfer(1,address_value,value,4'hf,0,ignored);
    endtask
    task automatic bad(input bit wr_access,input logic [31:0] address_value,input logic [31:0] value,input logic [3:0] strobes);
        logic [31:0] ignored;
        transfer(wr_access,address_value,value,strobes,1,ignored);
    endtask
    task automatic wait_tx_ready;
        logic [31:0] status;
        integer attempts;
        attempts=0;status=0;
        while((status&2)==0)begin
            transfer(0,32'hf8010104,0,4'hf,0,status);attempts++;
            if(attempts>1000)$fatal(1,"UART ready timeout");
        end
    endtask
    task automatic publish(input integer seed);
        while(!capture_ready)@(negedge capture_clk);
        @(negedge capture_clk);
        for(integer w=0;w<18;w++)capture_data[w*32+:32]=32'(seed+w);
        capture_valid=1;
        @(negedge capture_clk);capture_valid=0;
        while(!result_valid)@(negedge pclk);
    endtask
    // Decode the physical 8N1 waveform at the production 50MHz / 200k baud.
    initial begin:uart_monitor
        logic [7:0] value;
        forever begin
            @(negedge tx);
            if(monitor_enabled)begin
                #(BIT_NS/2);check(tx==0,"UART start midpoint");
                for(integer b=0;b<8;b++)begin #BIT_NS;value[b]=tx;end
                #BIT_NS;check(tx==1,"UART stop midpoint");
                if(decoded<16)received[decoded]=value;
                decoded++;
            end
        end
    end
    initial begin:test
        repeat(6)@(negedge pclk);
        preset_n=1;uart_reset_n=1;
        repeat(8)@(negedge pclk);
        check(tx==1,"UART reset idles high");
        rd(32'hf8010100,32'h4c543034);rd(32'hf8010110,48_000_000);rd(32'hf8010104,2);
        bad(0,32'hf8010000,0,4'hf);bad(0,32'hf8010101,0,4'hf);
        bad(0,32'hf8010114,0,4'hf);bad(0,32'hf8010108,0,4'hf);
        bad(0,32'hf801010c,0,4'hf);bad(0,32'hf8010120,0,4'hf);
        bad(1,32'hf8010108,1,4'hf);bad(1,32'hf8010100,1,4'hf);
        bad(1,32'hf801010c,32'h44,4'h1);bad(1,32'hf801010c,32'h100,4'hf);
        publish(32'h10203040);
        for(integer w=0;w<18;w++)rd(32'hf8010120+32'(w*4),32'h10203040+32'(w));
        // Result source changing while its mailbox waits must not change CPU words.
        capture_data='1;
        repeat(20)@(negedge pclk);
        for(integer w=0;w<18;w++)rd(32'hf8010120+32'(w*4),32'h10203040+32'(w));
        bad(0,32'hf8010168,0,4'hf);bad(1,32'hf8010120,1,4'hf);
        bad(1,32'hf8010108,2,4'hf);bad(1,32'hf8010108,1,4'h7);
        check(pops==0,"bad and SETUP accesses leave result held");
        loss=1;rd(32'hf8010104,7);loss=0;
        wr(32'hf8010108,1);check(pops==1,"one successful POP");
        rd(32'hf8010104,2);bad(0,32'hf8010120,0,4'hf);bad(1,32'hf8010108,1,4'hf);
        publish(32'h50607080);rd(32'hf8010164,32'h50607091);wr(32'hf8010108,1);check(pops==2,"second row retires once");

        wait_tx_ready;wr(32'hf801010c,0);
        wait_tx_ready;wr(32'hf801010c,32'h55);
        bad(1,32'hf801010c,32'haa,4'hf);
        wait_tx_ready;wr(32'hf801010c,32'haa);
        wait_tx_ready;wr(32'hf801010c,32'hff);
        wait_tx_ready;wr(32'hf801010c,32'h0d);
        wait_tx_ready;wr(32'hf801010c,32'h0a);
        wait_tx_ready;wr(32'hf801010c,32'h80);
        wait_tx_ready;wr(32'hf801010c,32'h01);
        wait(decoded==8);#(BIT_NS*2);
        check(received[0]==8'h00 && received[1]==8'h55 && received[2]==8'haa && received[3]==8'hff &&
              received[4]==8'h0d && received[5]==8'h0a && received[6]==8'h80 && received[7]==8'h01,"all UART raw bytes preserved");
        check(decoded==8,"rejected busy/invalid writes emitted no bytes");

        // A UART reset cancels an in-flight and pending byte, not the result slot.
        monitor_enabled=0;
        publish(32'h11223344);wait_tx_ready;wr(32'hf801010c,32'h02);
        wait_tx_ready;wr(32'hf801010c,32'h03);
        uart_reset_n=0;
        repeat(8)@(negedge uart_clk);
        check(tx==1,"independent UART reset cancels transmission");
        bad(1,32'hf801010c,32'h04,4'hf);
        rd(32'hf8010120,32'h11223344);check(result_valid,"UART reset preserves captured row");
        uart_reset_n=1;repeat(12)@(negedge pclk);
        check(tx==1,"UART reset does not replay stale byte");
        monitor_enabled=1;wait_tx_ready;wr(32'hf801010c,32'h66);
        wait(decoded==9);#(BIT_NS*2);check(received[8]==8'h66,"UART resumes with new byte");
        preset_n=0;repeat(8)@(negedge pclk);check(!pop && tx==1,"CPU reset has no POP and idles TX");
        preset_n=1;repeat(12)@(negedge pclk);
        rd(32'hf8010104,2);bad(0,32'hf8010120,0,4'hf);
        $display("PASS ae350_latency_apb: %0d checks, %0d physical UART bytes, 50MHz/200k/div250, 18-word immutable snapshots, APB errors and reset",checks,decoded);
        $finish;
    end
    initial begin #2ms;$fatal(1,"test watchdog");end
endmodule
`default_nettype wire
