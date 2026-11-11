//Copyright (C)2014-2026 Gowin Semiconductor Corporation.
//All rights reserved.
//File Title: IP file
//Tool Version: V1.9.12.04
//IP Version: 1.0
//Part Number: GW5AST-LV138PG484AC1/I0
//Device: GW5AST-138
//Device Version: C
//Created Time: Mon Oct  5 09:48:25 2026

module Scope_Feature_RAM (dout, clka, cea, clkb, ceb, oce, reset, ada, din, adb);

output [35:0] dout;
input clka;
input cea;
input clkb;
input ceb;
input oce;
input reset;
input [9:0] ada;
input [35:0] din;
input [9:0] adb;

wire [17:0] sdpx9b_inst_0_dout_w;
wire [17:0] sdpx9b_inst_1_dout_w;
wire gw_vcc;
wire gw_gnd;

assign gw_vcc = 1'b1;
assign gw_gnd = 1'b0;

SDPX9B sdpx9b_inst_0 (
    .DO({sdpx9b_inst_0_dout_w[17:0],dout[17:0]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,gw_gnd,gw_gnd}),
    .BLKSELB({gw_gnd,gw_gnd,gw_gnd}),
    .ADA({ada[9:0],gw_gnd,gw_gnd,gw_vcc,gw_vcc}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[17:0]}),
    .ADB({adb[9:0],gw_gnd,gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_0.READ_MODE = 1'b0;
defparam sdpx9b_inst_0.BIT_WIDTH_0 = 18;
defparam sdpx9b_inst_0.BIT_WIDTH_1 = 18;
defparam sdpx9b_inst_0.BLK_SEL_0 = 3'b000;
defparam sdpx9b_inst_0.BLK_SEL_1 = 3'b000;
defparam sdpx9b_inst_0.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_1 (
    .DO({sdpx9b_inst_1_dout_w[17:0],dout[35:18]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,gw_gnd,gw_gnd}),
    .BLKSELB({gw_gnd,gw_gnd,gw_gnd}),
    .ADA({ada[9:0],gw_gnd,gw_gnd,gw_vcc,gw_vcc}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[35:18]}),
    .ADB({adb[9:0],gw_gnd,gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_1.READ_MODE = 1'b0;
defparam sdpx9b_inst_1.BIT_WIDTH_0 = 18;
defparam sdpx9b_inst_1.BIT_WIDTH_1 = 18;
defparam sdpx9b_inst_1.BLK_SEL_0 = 3'b000;
defparam sdpx9b_inst_1.BLK_SEL_1 = 3'b000;
defparam sdpx9b_inst_1.RESET_MODE = "SYNC";

endmodule //Scope_Feature_RAM
