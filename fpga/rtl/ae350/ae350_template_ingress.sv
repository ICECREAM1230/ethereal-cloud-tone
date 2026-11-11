// Copyright (c) 2026 ICECREAM1230
// SPDX-License-Identifier: MIT
`timescale 1ns/1ps
`default_nettype none
// Fixed 64-record SPI -> APB FIFO. The two reset inputs must describe the
// coordinated link session; either assertion cancels the entire FIFO.
// wvalid_i denotes an attempted SPI word write. A full attempt is rejected
// with sticky overflow. Only wfree_o authorizes further SPI writes; rfree_o
// is a delayed read-domain observation and can overestimate writer free space.
module ae350_template_ingress(
    input wire wclk_i,wrst_ni,wvalid_i,
    output wire wready_o,
    input wire [15:0] waddr_i,
    input wire [31:0] wdata_i,
    output wire [15:0] wfree_o,
    output reg woverflow_o,
    input wire rclk_i,rrst_ni,
    output wire rvalid_o,
    input wire rready_i,
    output wire [15:0] raddr_o,
    output wire [31:0] rdata_o,
    output wire [15:0] rfree_o,
    output wire roverflow_o
);
    wire link_rst_n=wrst_ni && rrst_ni;
    (* ASYNC_REG="TRUE" *)reg [1:0] wrelease,rrelease;
    always @(posedge wclk_i or negedge link_rst_n)
        if(!link_rst_n)wrelease<=0;else wrelease<={wrelease[0],1'b1};
    always @(posedge rclk_i or negedge link_rst_n)
        if(!link_rst_n)rrelease<=0;else rrelease<={rrelease[0],1'b1};

    reg [6:0] wbin,wgray,rbin,rgray;
    (* ASYNC_REG="TRUE" *)reg [6:0] rgray_meta,rgray_sync,wgray_meta,wgray_sync;
    (* ASYNC_REG="TRUE" *)reg overflow_meta,overflow_sync;
    function automatic [6:0] gray_to_binary(input [6:0] gray);
        reg [6:0] binary;
        begin
            binary[6]=gray[6];
            for(integer bit_index=5;bit_index>=0;bit_index=bit_index-1)
                binary[bit_index]=binary[bit_index+1]^gray[bit_index];
            gray_to_binary=binary;
        end
    endfunction
    wire [6:0] writer_used=wbin-gray_to_binary(rgray_sync);
    wire [6:0] reader_used=gray_to_binary(wgray_sync)-rbin;
    wire full=wgray=={~rgray_sync[6:5],rgray_sync[4:0]};
    wire empty=rgray==wgray_sync;
    assign wready_o=wrelease[1] && !full;
    wire write_fire=wvalid_i && wready_o;
    wire [6:0] next_wbin=wbin+7'd1,next_rbin=rbin+7'd1;
    assign wfree_o=wrelease[1] ? {9'd0,(7'd64-writer_used)} : 16'd0;
    assign rfree_o=rrelease[1] ? {9'd0,(7'd64-reader_used)} : 16'd0;
    assign roverflow_o=rrelease[1] && overflow_sync;

    reg head_valid,fetch_pending;
    reg [47:0] head;
    wire fetch=rrelease[1] && !head_valid && !fetch_pending && !empty;
    wire read_fire=rvalid_o && rready_i;
    wire [53:0] ram_word;
    wire [53:0] write_word={6'd0,waddr_i,wdata_i};
    // READ_MODE=0 in the original wrapper is synchronous bypass: request on
    // CLKB, then capture the RAM output on the following CLKB. Head ownership
    // remains in rbin until POP, so prefetch never releases write credits.
    for(genvar lane=0;lane<3;lane=lane+1)begin:g_ram
        sdp_ram_1024x18 u_ram(.dout(ram_word[lane*18+:18]),
            .clka(wclk_i),.cea(write_fire),.clkb(rclk_i),.ceb(fetch),
            .oce(1'b1),.reset(!rrelease[1]),.ada({4'd0,wbin[5:0]}),
            .din(write_word[lane*18+:18]),.adb({4'd0,rbin[5:0]}));
    end
    assign rvalid_o=rrelease[1] && head_valid;
    assign raddr_o=head[47:32];
    assign rdata_o=head[31:0];

    always @(posedge wclk_i or negedge link_rst_n)begin
        if(!link_rst_n)begin
            wbin<=0;wgray<=0;rgray_meta<=0;rgray_sync<=0;woverflow_o<=0;
        end else if(!wrelease[1])begin
            wbin<=0;wgray<=0;rgray_meta<=0;rgray_sync<=0;woverflow_o<=0;
        end else begin
            rgray_meta<=rgray;rgray_sync<=rgray_meta;
            if(write_fire)begin wbin<=next_wbin;wgray<=(next_wbin>>1)^next_wbin;end
            if(wvalid_i && full)woverflow_o<=1;
        end
    end
    always @(posedge rclk_i or negedge link_rst_n)begin
        if(!link_rst_n)begin
            rbin<=0;rgray<=0;wgray_meta<=0;wgray_sync<=0;
            overflow_meta<=0;overflow_sync<=0;head_valid<=0;fetch_pending<=0;head<=0;
        end else if(!rrelease[1])begin
            rbin<=0;rgray<=0;wgray_meta<=0;wgray_sync<=0;
            overflow_meta<=0;overflow_sync<=0;head_valid<=0;fetch_pending<=0;head<=0;
        end else begin
            wgray_meta<=wgray;wgray_sync<=wgray_meta;
            overflow_meta<=woverflow_o;overflow_sync<=overflow_meta;
            fetch_pending<=fetch;
            if(fetch_pending)begin head<=ram_word[47:0];head_valid<=1;end
            if(read_fire)begin
                head_valid<=0;rbin<=next_rbin;rgray<=(next_rbin>>1)^next_rbin;
            end
        end
    end
    wire unused_ram_padding=|ram_word[53:48];
endmodule
`default_nettype wire
