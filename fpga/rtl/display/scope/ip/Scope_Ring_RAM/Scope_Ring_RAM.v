//Copyright (C)2014-2026 Gowin Semiconductor Corporation.
//All rights reserved.
//File Title: IP file
//Tool Version: V1.9.12.04
//IP Version: 1.0
//Part Number: GW5AST-LV138PG484AC1/I0
//Device: GW5AST-138
//Device Version: C
//Created Time: Mon Oct  5 09:48:25 2026

module Scope_Ring_RAM (dout, clka, cea, clkb, ceb, oce, reset, ada, din, adb);

output [63:0] dout;
input clka;
input cea;
input clkb;
input ceb;
input oce;
input reset;
input [12:0] ada;
input [63:0] din;
input [12:0] adb;

wire [26:0] sdpx9b_inst_0_dout_w;
wire [8:0] sdpx9b_inst_0_dout;
wire [26:0] sdpx9b_inst_1_dout_w;
wire [8:0] sdpx9b_inst_1_dout;
wire [26:0] sdpx9b_inst_2_dout_w;
wire [8:0] sdpx9b_inst_2_dout;
wire [26:0] sdpx9b_inst_3_dout_w;
wire [8:0] sdpx9b_inst_3_dout;
wire [26:0] sdpx9b_inst_4_dout_w;
wire [17:9] sdpx9b_inst_4_dout;
wire [26:0] sdpx9b_inst_5_dout_w;
wire [17:9] sdpx9b_inst_5_dout;
wire [26:0] sdpx9b_inst_6_dout_w;
wire [17:9] sdpx9b_inst_6_dout;
wire [26:0] sdpx9b_inst_7_dout_w;
wire [17:9] sdpx9b_inst_7_dout;
wire [26:0] sdpx9b_inst_8_dout_w;
wire [26:18] sdpx9b_inst_8_dout;
wire [26:0] sdpx9b_inst_9_dout_w;
wire [26:18] sdpx9b_inst_9_dout;
wire [26:0] sdpx9b_inst_10_dout_w;
wire [26:18] sdpx9b_inst_10_dout;
wire [26:0] sdpx9b_inst_11_dout_w;
wire [26:18] sdpx9b_inst_11_dout;
wire [26:0] sdpx9b_inst_12_dout_w;
wire [35:27] sdpx9b_inst_12_dout;
wire [26:0] sdpx9b_inst_13_dout_w;
wire [35:27] sdpx9b_inst_13_dout;
wire [26:0] sdpx9b_inst_14_dout_w;
wire [35:27] sdpx9b_inst_14_dout;
wire [26:0] sdpx9b_inst_15_dout_w;
wire [35:27] sdpx9b_inst_15_dout;
wire [26:0] sdpx9b_inst_16_dout_w;
wire [44:36] sdpx9b_inst_16_dout;
wire [26:0] sdpx9b_inst_17_dout_w;
wire [44:36] sdpx9b_inst_17_dout;
wire [26:0] sdpx9b_inst_18_dout_w;
wire [44:36] sdpx9b_inst_18_dout;
wire [26:0] sdpx9b_inst_19_dout_w;
wire [44:36] sdpx9b_inst_19_dout;
wire [26:0] sdpx9b_inst_20_dout_w;
wire [53:45] sdpx9b_inst_20_dout;
wire [26:0] sdpx9b_inst_21_dout_w;
wire [53:45] sdpx9b_inst_21_dout;
wire [26:0] sdpx9b_inst_22_dout_w;
wire [53:45] sdpx9b_inst_22_dout;
wire [26:0] sdpx9b_inst_23_dout_w;
wire [53:45] sdpx9b_inst_23_dout;
wire [29:0] sdpb_inst_24_dout_w;
wire [29:0] sdpb_inst_25_dout_w;
wire [29:0] sdpb_inst_26_dout_w;
wire [29:0] sdpb_inst_27_dout_w;
wire [29:0] sdpb_inst_28_dout_w;
wire dff_q_0;
wire dff_q_1;
wire mux_o_0;
wire mux_o_1;
wire mux_o_3;
wire mux_o_4;
wire mux_o_6;
wire mux_o_7;
wire mux_o_9;
wire mux_o_10;
wire mux_o_12;
wire mux_o_13;
wire mux_o_15;
wire mux_o_16;
wire mux_o_18;
wire mux_o_19;
wire mux_o_21;
wire mux_o_22;
wire mux_o_24;
wire mux_o_25;
wire mux_o_27;
wire mux_o_28;
wire mux_o_30;
wire mux_o_31;
wire mux_o_33;
wire mux_o_34;
wire mux_o_36;
wire mux_o_37;
wire mux_o_39;
wire mux_o_40;
wire mux_o_42;
wire mux_o_43;
wire mux_o_45;
wire mux_o_46;
wire mux_o_48;
wire mux_o_49;
wire mux_o_51;
wire mux_o_52;
wire mux_o_54;
wire mux_o_55;
wire mux_o_57;
wire mux_o_58;
wire mux_o_60;
wire mux_o_61;
wire mux_o_63;
wire mux_o_64;
wire mux_o_66;
wire mux_o_67;
wire mux_o_69;
wire mux_o_70;
wire mux_o_72;
wire mux_o_73;
wire mux_o_75;
wire mux_o_76;
wire mux_o_78;
wire mux_o_79;
wire mux_o_81;
wire mux_o_82;
wire mux_o_84;
wire mux_o_85;
wire mux_o_87;
wire mux_o_88;
wire mux_o_90;
wire mux_o_91;
wire mux_o_93;
wire mux_o_94;
wire mux_o_96;
wire mux_o_97;
wire mux_o_99;
wire mux_o_100;
wire mux_o_102;
wire mux_o_103;
wire mux_o_105;
wire mux_o_106;
wire mux_o_108;
wire mux_o_109;
wire mux_o_111;
wire mux_o_112;
wire mux_o_114;
wire mux_o_115;
wire mux_o_117;
wire mux_o_118;
wire mux_o_120;
wire mux_o_121;
wire mux_o_123;
wire mux_o_124;
wire mux_o_126;
wire mux_o_127;
wire mux_o_129;
wire mux_o_130;
wire mux_o_132;
wire mux_o_133;
wire mux_o_135;
wire mux_o_136;
wire mux_o_138;
wire mux_o_139;
wire mux_o_141;
wire mux_o_142;
wire mux_o_144;
wire mux_o_145;
wire mux_o_147;
wire mux_o_148;
wire mux_o_150;
wire mux_o_151;
wire mux_o_153;
wire mux_o_154;
wire mux_o_156;
wire mux_o_157;
wire mux_o_159;
wire mux_o_160;
wire gw_gnd;

assign gw_gnd = 1'b0;

SDPX9B sdpx9b_inst_0 (
    .DO({sdpx9b_inst_0_dout_w[26:0],sdpx9b_inst_0_dout[8:0]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[8:0]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_0.READ_MODE = 1'b0;
defparam sdpx9b_inst_0.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_0.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_0.BLK_SEL_0 = 3'b000;
defparam sdpx9b_inst_0.BLK_SEL_1 = 3'b000;
defparam sdpx9b_inst_0.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_1 (
    .DO({sdpx9b_inst_1_dout_w[26:0],sdpx9b_inst_1_dout[8:0]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[8:0]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_1.READ_MODE = 1'b0;
defparam sdpx9b_inst_1.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_1.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_1.BLK_SEL_0 = 3'b001;
defparam sdpx9b_inst_1.BLK_SEL_1 = 3'b001;
defparam sdpx9b_inst_1.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_2 (
    .DO({sdpx9b_inst_2_dout_w[26:0],sdpx9b_inst_2_dout[8:0]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[8:0]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_2.READ_MODE = 1'b0;
defparam sdpx9b_inst_2.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_2.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_2.BLK_SEL_0 = 3'b010;
defparam sdpx9b_inst_2.BLK_SEL_1 = 3'b010;
defparam sdpx9b_inst_2.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_3 (
    .DO({sdpx9b_inst_3_dout_w[26:0],sdpx9b_inst_3_dout[8:0]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[8:0]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_3.READ_MODE = 1'b0;
defparam sdpx9b_inst_3.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_3.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_3.BLK_SEL_0 = 3'b011;
defparam sdpx9b_inst_3.BLK_SEL_1 = 3'b011;
defparam sdpx9b_inst_3.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_4 (
    .DO({sdpx9b_inst_4_dout_w[26:0],sdpx9b_inst_4_dout[17:9]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[17:9]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_4.READ_MODE = 1'b0;
defparam sdpx9b_inst_4.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_4.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_4.BLK_SEL_0 = 3'b000;
defparam sdpx9b_inst_4.BLK_SEL_1 = 3'b000;
defparam sdpx9b_inst_4.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_5 (
    .DO({sdpx9b_inst_5_dout_w[26:0],sdpx9b_inst_5_dout[17:9]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[17:9]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_5.READ_MODE = 1'b0;
defparam sdpx9b_inst_5.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_5.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_5.BLK_SEL_0 = 3'b001;
defparam sdpx9b_inst_5.BLK_SEL_1 = 3'b001;
defparam sdpx9b_inst_5.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_6 (
    .DO({sdpx9b_inst_6_dout_w[26:0],sdpx9b_inst_6_dout[17:9]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[17:9]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_6.READ_MODE = 1'b0;
defparam sdpx9b_inst_6.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_6.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_6.BLK_SEL_0 = 3'b010;
defparam sdpx9b_inst_6.BLK_SEL_1 = 3'b010;
defparam sdpx9b_inst_6.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_7 (
    .DO({sdpx9b_inst_7_dout_w[26:0],sdpx9b_inst_7_dout[17:9]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[17:9]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_7.READ_MODE = 1'b0;
defparam sdpx9b_inst_7.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_7.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_7.BLK_SEL_0 = 3'b011;
defparam sdpx9b_inst_7.BLK_SEL_1 = 3'b011;
defparam sdpx9b_inst_7.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_8 (
    .DO({sdpx9b_inst_8_dout_w[26:0],sdpx9b_inst_8_dout[26:18]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[26:18]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_8.READ_MODE = 1'b0;
defparam sdpx9b_inst_8.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_8.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_8.BLK_SEL_0 = 3'b000;
defparam sdpx9b_inst_8.BLK_SEL_1 = 3'b000;
defparam sdpx9b_inst_8.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_9 (
    .DO({sdpx9b_inst_9_dout_w[26:0],sdpx9b_inst_9_dout[26:18]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[26:18]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_9.READ_MODE = 1'b0;
defparam sdpx9b_inst_9.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_9.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_9.BLK_SEL_0 = 3'b001;
defparam sdpx9b_inst_9.BLK_SEL_1 = 3'b001;
defparam sdpx9b_inst_9.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_10 (
    .DO({sdpx9b_inst_10_dout_w[26:0],sdpx9b_inst_10_dout[26:18]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[26:18]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_10.READ_MODE = 1'b0;
defparam sdpx9b_inst_10.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_10.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_10.BLK_SEL_0 = 3'b010;
defparam sdpx9b_inst_10.BLK_SEL_1 = 3'b010;
defparam sdpx9b_inst_10.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_11 (
    .DO({sdpx9b_inst_11_dout_w[26:0],sdpx9b_inst_11_dout[26:18]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[26:18]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_11.READ_MODE = 1'b0;
defparam sdpx9b_inst_11.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_11.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_11.BLK_SEL_0 = 3'b011;
defparam sdpx9b_inst_11.BLK_SEL_1 = 3'b011;
defparam sdpx9b_inst_11.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_12 (
    .DO({sdpx9b_inst_12_dout_w[26:0],sdpx9b_inst_12_dout[35:27]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[35:27]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_12.READ_MODE = 1'b0;
defparam sdpx9b_inst_12.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_12.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_12.BLK_SEL_0 = 3'b000;
defparam sdpx9b_inst_12.BLK_SEL_1 = 3'b000;
defparam sdpx9b_inst_12.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_13 (
    .DO({sdpx9b_inst_13_dout_w[26:0],sdpx9b_inst_13_dout[35:27]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[35:27]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_13.READ_MODE = 1'b0;
defparam sdpx9b_inst_13.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_13.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_13.BLK_SEL_0 = 3'b001;
defparam sdpx9b_inst_13.BLK_SEL_1 = 3'b001;
defparam sdpx9b_inst_13.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_14 (
    .DO({sdpx9b_inst_14_dout_w[26:0],sdpx9b_inst_14_dout[35:27]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[35:27]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_14.READ_MODE = 1'b0;
defparam sdpx9b_inst_14.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_14.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_14.BLK_SEL_0 = 3'b010;
defparam sdpx9b_inst_14.BLK_SEL_1 = 3'b010;
defparam sdpx9b_inst_14.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_15 (
    .DO({sdpx9b_inst_15_dout_w[26:0],sdpx9b_inst_15_dout[35:27]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[35:27]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_15.READ_MODE = 1'b0;
defparam sdpx9b_inst_15.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_15.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_15.BLK_SEL_0 = 3'b011;
defparam sdpx9b_inst_15.BLK_SEL_1 = 3'b011;
defparam sdpx9b_inst_15.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_16 (
    .DO({sdpx9b_inst_16_dout_w[26:0],sdpx9b_inst_16_dout[44:36]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[44:36]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_16.READ_MODE = 1'b0;
defparam sdpx9b_inst_16.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_16.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_16.BLK_SEL_0 = 3'b000;
defparam sdpx9b_inst_16.BLK_SEL_1 = 3'b000;
defparam sdpx9b_inst_16.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_17 (
    .DO({sdpx9b_inst_17_dout_w[26:0],sdpx9b_inst_17_dout[44:36]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[44:36]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_17.READ_MODE = 1'b0;
defparam sdpx9b_inst_17.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_17.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_17.BLK_SEL_0 = 3'b001;
defparam sdpx9b_inst_17.BLK_SEL_1 = 3'b001;
defparam sdpx9b_inst_17.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_18 (
    .DO({sdpx9b_inst_18_dout_w[26:0],sdpx9b_inst_18_dout[44:36]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[44:36]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_18.READ_MODE = 1'b0;
defparam sdpx9b_inst_18.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_18.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_18.BLK_SEL_0 = 3'b010;
defparam sdpx9b_inst_18.BLK_SEL_1 = 3'b010;
defparam sdpx9b_inst_18.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_19 (
    .DO({sdpx9b_inst_19_dout_w[26:0],sdpx9b_inst_19_dout[44:36]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[44:36]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_19.READ_MODE = 1'b0;
defparam sdpx9b_inst_19.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_19.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_19.BLK_SEL_0 = 3'b011;
defparam sdpx9b_inst_19.BLK_SEL_1 = 3'b011;
defparam sdpx9b_inst_19.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_20 (
    .DO({sdpx9b_inst_20_dout_w[26:0],sdpx9b_inst_20_dout[53:45]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[53:45]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_20.READ_MODE = 1'b0;
defparam sdpx9b_inst_20.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_20.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_20.BLK_SEL_0 = 3'b000;
defparam sdpx9b_inst_20.BLK_SEL_1 = 3'b000;
defparam sdpx9b_inst_20.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_21 (
    .DO({sdpx9b_inst_21_dout_w[26:0],sdpx9b_inst_21_dout[53:45]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[53:45]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_21.READ_MODE = 1'b0;
defparam sdpx9b_inst_21.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_21.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_21.BLK_SEL_0 = 3'b001;
defparam sdpx9b_inst_21.BLK_SEL_1 = 3'b001;
defparam sdpx9b_inst_21.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_22 (
    .DO({sdpx9b_inst_22_dout_w[26:0],sdpx9b_inst_22_dout[53:45]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[53:45]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_22.READ_MODE = 1'b0;
defparam sdpx9b_inst_22.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_22.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_22.BLK_SEL_0 = 3'b010;
defparam sdpx9b_inst_22.BLK_SEL_1 = 3'b010;
defparam sdpx9b_inst_22.RESET_MODE = "SYNC";

SDPX9B sdpx9b_inst_23 (
    .DO({sdpx9b_inst_23_dout_w[26:0],sdpx9b_inst_23_dout[53:45]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,ada[12],ada[11]}),
    .BLKSELB({gw_gnd,adb[12],adb[11]}),
    .ADA({ada[10:0],gw_gnd,gw_gnd,gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[53:45]}),
    .ADB({adb[10:0],gw_gnd,gw_gnd,gw_gnd})
);

defparam sdpx9b_inst_23.READ_MODE = 1'b0;
defparam sdpx9b_inst_23.BIT_WIDTH_0 = 9;
defparam sdpx9b_inst_23.BIT_WIDTH_1 = 9;
defparam sdpx9b_inst_23.BLK_SEL_0 = 3'b011;
defparam sdpx9b_inst_23.BLK_SEL_1 = 3'b011;
defparam sdpx9b_inst_23.RESET_MODE = "SYNC";

SDPB sdpb_inst_24 (
    .DO({sdpb_inst_24_dout_w[29:0],dout[55:54]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,gw_gnd,gw_gnd}),
    .BLKSELB({gw_gnd,gw_gnd,gw_gnd}),
    .ADA({ada[12:0],gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[55:54]}),
    .ADB({adb[12:0],gw_gnd})
);

defparam sdpb_inst_24.READ_MODE = 1'b0;
defparam sdpb_inst_24.BIT_WIDTH_0 = 2;
defparam sdpb_inst_24.BIT_WIDTH_1 = 2;
defparam sdpb_inst_24.BLK_SEL_0 = 3'b000;
defparam sdpb_inst_24.BLK_SEL_1 = 3'b000;
defparam sdpb_inst_24.RESET_MODE = "SYNC";

SDPB sdpb_inst_25 (
    .DO({sdpb_inst_25_dout_w[29:0],dout[57:56]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,gw_gnd,gw_gnd}),
    .BLKSELB({gw_gnd,gw_gnd,gw_gnd}),
    .ADA({ada[12:0],gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[57:56]}),
    .ADB({adb[12:0],gw_gnd})
);

defparam sdpb_inst_25.READ_MODE = 1'b0;
defparam sdpb_inst_25.BIT_WIDTH_0 = 2;
defparam sdpb_inst_25.BIT_WIDTH_1 = 2;
defparam sdpb_inst_25.BLK_SEL_0 = 3'b000;
defparam sdpb_inst_25.BLK_SEL_1 = 3'b000;
defparam sdpb_inst_25.RESET_MODE = "SYNC";

SDPB sdpb_inst_26 (
    .DO({sdpb_inst_26_dout_w[29:0],dout[59:58]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,gw_gnd,gw_gnd}),
    .BLKSELB({gw_gnd,gw_gnd,gw_gnd}),
    .ADA({ada[12:0],gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[59:58]}),
    .ADB({adb[12:0],gw_gnd})
);

defparam sdpb_inst_26.READ_MODE = 1'b0;
defparam sdpb_inst_26.BIT_WIDTH_0 = 2;
defparam sdpb_inst_26.BIT_WIDTH_1 = 2;
defparam sdpb_inst_26.BLK_SEL_0 = 3'b000;
defparam sdpb_inst_26.BLK_SEL_1 = 3'b000;
defparam sdpb_inst_26.RESET_MODE = "SYNC";

SDPB sdpb_inst_27 (
    .DO({sdpb_inst_27_dout_w[29:0],dout[61:60]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,gw_gnd,gw_gnd}),
    .BLKSELB({gw_gnd,gw_gnd,gw_gnd}),
    .ADA({ada[12:0],gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[61:60]}),
    .ADB({adb[12:0],gw_gnd})
);

defparam sdpb_inst_27.READ_MODE = 1'b0;
defparam sdpb_inst_27.BIT_WIDTH_0 = 2;
defparam sdpb_inst_27.BIT_WIDTH_1 = 2;
defparam sdpb_inst_27.BLK_SEL_0 = 3'b000;
defparam sdpb_inst_27.BLK_SEL_1 = 3'b000;
defparam sdpb_inst_27.RESET_MODE = "SYNC";

SDPB sdpb_inst_28 (
    .DO({sdpb_inst_28_dout_w[29:0],dout[63:62]}),
    .CLKA(clka),
    .CEA(cea),
    .CLKB(clkb),
    .CEB(ceb),
    .OCE(oce),
    .RESET(reset),
    .BLKSELA({gw_gnd,gw_gnd,gw_gnd}),
    .BLKSELB({gw_gnd,gw_gnd,gw_gnd}),
    .ADA({ada[12:0],gw_gnd}),
    .DI({gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,gw_gnd,din[63:62]}),
    .ADB({adb[12:0],gw_gnd})
);

defparam sdpb_inst_28.READ_MODE = 1'b0;
defparam sdpb_inst_28.BIT_WIDTH_0 = 2;
defparam sdpb_inst_28.BIT_WIDTH_1 = 2;
defparam sdpb_inst_28.BLK_SEL_0 = 3'b000;
defparam sdpb_inst_28.BLK_SEL_1 = 3'b000;
defparam sdpb_inst_28.RESET_MODE = "SYNC";

DFFRE dff_inst_0 (
  .Q(dff_q_0),
  .D(adb[12]),
  .CLK(clkb),
  .CE(ceb),
  .RESET(gw_gnd)
);
DFFRE dff_inst_1 (
  .Q(dff_q_1),
  .D(adb[11]),
  .CLK(clkb),
  .CE(ceb),
  .RESET(gw_gnd)
);
MUX2 mux_inst_0 (
  .O(mux_o_0),
  .I0(sdpx9b_inst_0_dout[0]),
  .I1(sdpx9b_inst_1_dout[0]),
  .S0(dff_q_1)
);
MUX2 mux_inst_1 (
  .O(mux_o_1),
  .I0(sdpx9b_inst_2_dout[0]),
  .I1(sdpx9b_inst_3_dout[0]),
  .S0(dff_q_1)
);
MUX2 mux_inst_2 (
  .O(dout[0]),
  .I0(mux_o_0),
  .I1(mux_o_1),
  .S0(dff_q_0)
);
MUX2 mux_inst_3 (
  .O(mux_o_3),
  .I0(sdpx9b_inst_0_dout[1]),
  .I1(sdpx9b_inst_1_dout[1]),
  .S0(dff_q_1)
);
MUX2 mux_inst_4 (
  .O(mux_o_4),
  .I0(sdpx9b_inst_2_dout[1]),
  .I1(sdpx9b_inst_3_dout[1]),
  .S0(dff_q_1)
);
MUX2 mux_inst_5 (
  .O(dout[1]),
  .I0(mux_o_3),
  .I1(mux_o_4),
  .S0(dff_q_0)
);
MUX2 mux_inst_6 (
  .O(mux_o_6),
  .I0(sdpx9b_inst_0_dout[2]),
  .I1(sdpx9b_inst_1_dout[2]),
  .S0(dff_q_1)
);
MUX2 mux_inst_7 (
  .O(mux_o_7),
  .I0(sdpx9b_inst_2_dout[2]),
  .I1(sdpx9b_inst_3_dout[2]),
  .S0(dff_q_1)
);
MUX2 mux_inst_8 (
  .O(dout[2]),
  .I0(mux_o_6),
  .I1(mux_o_7),
  .S0(dff_q_0)
);
MUX2 mux_inst_9 (
  .O(mux_o_9),
  .I0(sdpx9b_inst_0_dout[3]),
  .I1(sdpx9b_inst_1_dout[3]),
  .S0(dff_q_1)
);
MUX2 mux_inst_10 (
  .O(mux_o_10),
  .I0(sdpx9b_inst_2_dout[3]),
  .I1(sdpx9b_inst_3_dout[3]),
  .S0(dff_q_1)
);
MUX2 mux_inst_11 (
  .O(dout[3]),
  .I0(mux_o_9),
  .I1(mux_o_10),
  .S0(dff_q_0)
);
MUX2 mux_inst_12 (
  .O(mux_o_12),
  .I0(sdpx9b_inst_0_dout[4]),
  .I1(sdpx9b_inst_1_dout[4]),
  .S0(dff_q_1)
);
MUX2 mux_inst_13 (
  .O(mux_o_13),
  .I0(sdpx9b_inst_2_dout[4]),
  .I1(sdpx9b_inst_3_dout[4]),
  .S0(dff_q_1)
);
MUX2 mux_inst_14 (
  .O(dout[4]),
  .I0(mux_o_12),
  .I1(mux_o_13),
  .S0(dff_q_0)
);
MUX2 mux_inst_15 (
  .O(mux_o_15),
  .I0(sdpx9b_inst_0_dout[5]),
  .I1(sdpx9b_inst_1_dout[5]),
  .S0(dff_q_1)
);
MUX2 mux_inst_16 (
  .O(mux_o_16),
  .I0(sdpx9b_inst_2_dout[5]),
  .I1(sdpx9b_inst_3_dout[5]),
  .S0(dff_q_1)
);
MUX2 mux_inst_17 (
  .O(dout[5]),
  .I0(mux_o_15),
  .I1(mux_o_16),
  .S0(dff_q_0)
);
MUX2 mux_inst_18 (
  .O(mux_o_18),
  .I0(sdpx9b_inst_0_dout[6]),
  .I1(sdpx9b_inst_1_dout[6]),
  .S0(dff_q_1)
);
MUX2 mux_inst_19 (
  .O(mux_o_19),
  .I0(sdpx9b_inst_2_dout[6]),
  .I1(sdpx9b_inst_3_dout[6]),
  .S0(dff_q_1)
);
MUX2 mux_inst_20 (
  .O(dout[6]),
  .I0(mux_o_18),
  .I1(mux_o_19),
  .S0(dff_q_0)
);
MUX2 mux_inst_21 (
  .O(mux_o_21),
  .I0(sdpx9b_inst_0_dout[7]),
  .I1(sdpx9b_inst_1_dout[7]),
  .S0(dff_q_1)
);
MUX2 mux_inst_22 (
  .O(mux_o_22),
  .I0(sdpx9b_inst_2_dout[7]),
  .I1(sdpx9b_inst_3_dout[7]),
  .S0(dff_q_1)
);
MUX2 mux_inst_23 (
  .O(dout[7]),
  .I0(mux_o_21),
  .I1(mux_o_22),
  .S0(dff_q_0)
);
MUX2 mux_inst_24 (
  .O(mux_o_24),
  .I0(sdpx9b_inst_0_dout[8]),
  .I1(sdpx9b_inst_1_dout[8]),
  .S0(dff_q_1)
);
MUX2 mux_inst_25 (
  .O(mux_o_25),
  .I0(sdpx9b_inst_2_dout[8]),
  .I1(sdpx9b_inst_3_dout[8]),
  .S0(dff_q_1)
);
MUX2 mux_inst_26 (
  .O(dout[8]),
  .I0(mux_o_24),
  .I1(mux_o_25),
  .S0(dff_q_0)
);
MUX2 mux_inst_27 (
  .O(mux_o_27),
  .I0(sdpx9b_inst_4_dout[9]),
  .I1(sdpx9b_inst_5_dout[9]),
  .S0(dff_q_1)
);
MUX2 mux_inst_28 (
  .O(mux_o_28),
  .I0(sdpx9b_inst_6_dout[9]),
  .I1(sdpx9b_inst_7_dout[9]),
  .S0(dff_q_1)
);
MUX2 mux_inst_29 (
  .O(dout[9]),
  .I0(mux_o_27),
  .I1(mux_o_28),
  .S0(dff_q_0)
);
MUX2 mux_inst_30 (
  .O(mux_o_30),
  .I0(sdpx9b_inst_4_dout[10]),
  .I1(sdpx9b_inst_5_dout[10]),
  .S0(dff_q_1)
);
MUX2 mux_inst_31 (
  .O(mux_o_31),
  .I0(sdpx9b_inst_6_dout[10]),
  .I1(sdpx9b_inst_7_dout[10]),
  .S0(dff_q_1)
);
MUX2 mux_inst_32 (
  .O(dout[10]),
  .I0(mux_o_30),
  .I1(mux_o_31),
  .S0(dff_q_0)
);
MUX2 mux_inst_33 (
  .O(mux_o_33),
  .I0(sdpx9b_inst_4_dout[11]),
  .I1(sdpx9b_inst_5_dout[11]),
  .S0(dff_q_1)
);
MUX2 mux_inst_34 (
  .O(mux_o_34),
  .I0(sdpx9b_inst_6_dout[11]),
  .I1(sdpx9b_inst_7_dout[11]),
  .S0(dff_q_1)
);
MUX2 mux_inst_35 (
  .O(dout[11]),
  .I0(mux_o_33),
  .I1(mux_o_34),
  .S0(dff_q_0)
);
MUX2 mux_inst_36 (
  .O(mux_o_36),
  .I0(sdpx9b_inst_4_dout[12]),
  .I1(sdpx9b_inst_5_dout[12]),
  .S0(dff_q_1)
);
MUX2 mux_inst_37 (
  .O(mux_o_37),
  .I0(sdpx9b_inst_6_dout[12]),
  .I1(sdpx9b_inst_7_dout[12]),
  .S0(dff_q_1)
);
MUX2 mux_inst_38 (
  .O(dout[12]),
  .I0(mux_o_36),
  .I1(mux_o_37),
  .S0(dff_q_0)
);
MUX2 mux_inst_39 (
  .O(mux_o_39),
  .I0(sdpx9b_inst_4_dout[13]),
  .I1(sdpx9b_inst_5_dout[13]),
  .S0(dff_q_1)
);
MUX2 mux_inst_40 (
  .O(mux_o_40),
  .I0(sdpx9b_inst_6_dout[13]),
  .I1(sdpx9b_inst_7_dout[13]),
  .S0(dff_q_1)
);
MUX2 mux_inst_41 (
  .O(dout[13]),
  .I0(mux_o_39),
  .I1(mux_o_40),
  .S0(dff_q_0)
);
MUX2 mux_inst_42 (
  .O(mux_o_42),
  .I0(sdpx9b_inst_4_dout[14]),
  .I1(sdpx9b_inst_5_dout[14]),
  .S0(dff_q_1)
);
MUX2 mux_inst_43 (
  .O(mux_o_43),
  .I0(sdpx9b_inst_6_dout[14]),
  .I1(sdpx9b_inst_7_dout[14]),
  .S0(dff_q_1)
);
MUX2 mux_inst_44 (
  .O(dout[14]),
  .I0(mux_o_42),
  .I1(mux_o_43),
  .S0(dff_q_0)
);
MUX2 mux_inst_45 (
  .O(mux_o_45),
  .I0(sdpx9b_inst_4_dout[15]),
  .I1(sdpx9b_inst_5_dout[15]),
  .S0(dff_q_1)
);
MUX2 mux_inst_46 (
  .O(mux_o_46),
  .I0(sdpx9b_inst_6_dout[15]),
  .I1(sdpx9b_inst_7_dout[15]),
  .S0(dff_q_1)
);
MUX2 mux_inst_47 (
  .O(dout[15]),
  .I0(mux_o_45),
  .I1(mux_o_46),
  .S0(dff_q_0)
);
MUX2 mux_inst_48 (
  .O(mux_o_48),
  .I0(sdpx9b_inst_4_dout[16]),
  .I1(sdpx9b_inst_5_dout[16]),
  .S0(dff_q_1)
);
MUX2 mux_inst_49 (
  .O(mux_o_49),
  .I0(sdpx9b_inst_6_dout[16]),
  .I1(sdpx9b_inst_7_dout[16]),
  .S0(dff_q_1)
);
MUX2 mux_inst_50 (
  .O(dout[16]),
  .I0(mux_o_48),
  .I1(mux_o_49),
  .S0(dff_q_0)
);
MUX2 mux_inst_51 (
  .O(mux_o_51),
  .I0(sdpx9b_inst_4_dout[17]),
  .I1(sdpx9b_inst_5_dout[17]),
  .S0(dff_q_1)
);
MUX2 mux_inst_52 (
  .O(mux_o_52),
  .I0(sdpx9b_inst_6_dout[17]),
  .I1(sdpx9b_inst_7_dout[17]),
  .S0(dff_q_1)
);
MUX2 mux_inst_53 (
  .O(dout[17]),
  .I0(mux_o_51),
  .I1(mux_o_52),
  .S0(dff_q_0)
);
MUX2 mux_inst_54 (
  .O(mux_o_54),
  .I0(sdpx9b_inst_8_dout[18]),
  .I1(sdpx9b_inst_9_dout[18]),
  .S0(dff_q_1)
);
MUX2 mux_inst_55 (
  .O(mux_o_55),
  .I0(sdpx9b_inst_10_dout[18]),
  .I1(sdpx9b_inst_11_dout[18]),
  .S0(dff_q_1)
);
MUX2 mux_inst_56 (
  .O(dout[18]),
  .I0(mux_o_54),
  .I1(mux_o_55),
  .S0(dff_q_0)
);
MUX2 mux_inst_57 (
  .O(mux_o_57),
  .I0(sdpx9b_inst_8_dout[19]),
  .I1(sdpx9b_inst_9_dout[19]),
  .S0(dff_q_1)
);
MUX2 mux_inst_58 (
  .O(mux_o_58),
  .I0(sdpx9b_inst_10_dout[19]),
  .I1(sdpx9b_inst_11_dout[19]),
  .S0(dff_q_1)
);
MUX2 mux_inst_59 (
  .O(dout[19]),
  .I0(mux_o_57),
  .I1(mux_o_58),
  .S0(dff_q_0)
);
MUX2 mux_inst_60 (
  .O(mux_o_60),
  .I0(sdpx9b_inst_8_dout[20]),
  .I1(sdpx9b_inst_9_dout[20]),
  .S0(dff_q_1)
);
MUX2 mux_inst_61 (
  .O(mux_o_61),
  .I0(sdpx9b_inst_10_dout[20]),
  .I1(sdpx9b_inst_11_dout[20]),
  .S0(dff_q_1)
);
MUX2 mux_inst_62 (
  .O(dout[20]),
  .I0(mux_o_60),
  .I1(mux_o_61),
  .S0(dff_q_0)
);
MUX2 mux_inst_63 (
  .O(mux_o_63),
  .I0(sdpx9b_inst_8_dout[21]),
  .I1(sdpx9b_inst_9_dout[21]),
  .S0(dff_q_1)
);
MUX2 mux_inst_64 (
  .O(mux_o_64),
  .I0(sdpx9b_inst_10_dout[21]),
  .I1(sdpx9b_inst_11_dout[21]),
  .S0(dff_q_1)
);
MUX2 mux_inst_65 (
  .O(dout[21]),
  .I0(mux_o_63),
  .I1(mux_o_64),
  .S0(dff_q_0)
);
MUX2 mux_inst_66 (
  .O(mux_o_66),
  .I0(sdpx9b_inst_8_dout[22]),
  .I1(sdpx9b_inst_9_dout[22]),
  .S0(dff_q_1)
);
MUX2 mux_inst_67 (
  .O(mux_o_67),
  .I0(sdpx9b_inst_10_dout[22]),
  .I1(sdpx9b_inst_11_dout[22]),
  .S0(dff_q_1)
);
MUX2 mux_inst_68 (
  .O(dout[22]),
  .I0(mux_o_66),
  .I1(mux_o_67),
  .S0(dff_q_0)
);
MUX2 mux_inst_69 (
  .O(mux_o_69),
  .I0(sdpx9b_inst_8_dout[23]),
  .I1(sdpx9b_inst_9_dout[23]),
  .S0(dff_q_1)
);
MUX2 mux_inst_70 (
  .O(mux_o_70),
  .I0(sdpx9b_inst_10_dout[23]),
  .I1(sdpx9b_inst_11_dout[23]),
  .S0(dff_q_1)
);
MUX2 mux_inst_71 (
  .O(dout[23]),
  .I0(mux_o_69),
  .I1(mux_o_70),
  .S0(dff_q_0)
);
MUX2 mux_inst_72 (
  .O(mux_o_72),
  .I0(sdpx9b_inst_8_dout[24]),
  .I1(sdpx9b_inst_9_dout[24]),
  .S0(dff_q_1)
);
MUX2 mux_inst_73 (
  .O(mux_o_73),
  .I0(sdpx9b_inst_10_dout[24]),
  .I1(sdpx9b_inst_11_dout[24]),
  .S0(dff_q_1)
);
MUX2 mux_inst_74 (
  .O(dout[24]),
  .I0(mux_o_72),
  .I1(mux_o_73),
  .S0(dff_q_0)
);
MUX2 mux_inst_75 (
  .O(mux_o_75),
  .I0(sdpx9b_inst_8_dout[25]),
  .I1(sdpx9b_inst_9_dout[25]),
  .S0(dff_q_1)
);
MUX2 mux_inst_76 (
  .O(mux_o_76),
  .I0(sdpx9b_inst_10_dout[25]),
  .I1(sdpx9b_inst_11_dout[25]),
  .S0(dff_q_1)
);
MUX2 mux_inst_77 (
  .O(dout[25]),
  .I0(mux_o_75),
  .I1(mux_o_76),
  .S0(dff_q_0)
);
MUX2 mux_inst_78 (
  .O(mux_o_78),
  .I0(sdpx9b_inst_8_dout[26]),
  .I1(sdpx9b_inst_9_dout[26]),
  .S0(dff_q_1)
);
MUX2 mux_inst_79 (
  .O(mux_o_79),
  .I0(sdpx9b_inst_10_dout[26]),
  .I1(sdpx9b_inst_11_dout[26]),
  .S0(dff_q_1)
);
MUX2 mux_inst_80 (
  .O(dout[26]),
  .I0(mux_o_78),
  .I1(mux_o_79),
  .S0(dff_q_0)
);
MUX2 mux_inst_81 (
  .O(mux_o_81),
  .I0(sdpx9b_inst_12_dout[27]),
  .I1(sdpx9b_inst_13_dout[27]),
  .S0(dff_q_1)
);
MUX2 mux_inst_82 (
  .O(mux_o_82),
  .I0(sdpx9b_inst_14_dout[27]),
  .I1(sdpx9b_inst_15_dout[27]),
  .S0(dff_q_1)
);
MUX2 mux_inst_83 (
  .O(dout[27]),
  .I0(mux_o_81),
  .I1(mux_o_82),
  .S0(dff_q_0)
);
MUX2 mux_inst_84 (
  .O(mux_o_84),
  .I0(sdpx9b_inst_12_dout[28]),
  .I1(sdpx9b_inst_13_dout[28]),
  .S0(dff_q_1)
);
MUX2 mux_inst_85 (
  .O(mux_o_85),
  .I0(sdpx9b_inst_14_dout[28]),
  .I1(sdpx9b_inst_15_dout[28]),
  .S0(dff_q_1)
);
MUX2 mux_inst_86 (
  .O(dout[28]),
  .I0(mux_o_84),
  .I1(mux_o_85),
  .S0(dff_q_0)
);
MUX2 mux_inst_87 (
  .O(mux_o_87),
  .I0(sdpx9b_inst_12_dout[29]),
  .I1(sdpx9b_inst_13_dout[29]),
  .S0(dff_q_1)
);
MUX2 mux_inst_88 (
  .O(mux_o_88),
  .I0(sdpx9b_inst_14_dout[29]),
  .I1(sdpx9b_inst_15_dout[29]),
  .S0(dff_q_1)
);
MUX2 mux_inst_89 (
  .O(dout[29]),
  .I0(mux_o_87),
  .I1(mux_o_88),
  .S0(dff_q_0)
);
MUX2 mux_inst_90 (
  .O(mux_o_90),
  .I0(sdpx9b_inst_12_dout[30]),
  .I1(sdpx9b_inst_13_dout[30]),
  .S0(dff_q_1)
);
MUX2 mux_inst_91 (
  .O(mux_o_91),
  .I0(sdpx9b_inst_14_dout[30]),
  .I1(sdpx9b_inst_15_dout[30]),
  .S0(dff_q_1)
);
MUX2 mux_inst_92 (
  .O(dout[30]),
  .I0(mux_o_90),
  .I1(mux_o_91),
  .S0(dff_q_0)
);
MUX2 mux_inst_93 (
  .O(mux_o_93),
  .I0(sdpx9b_inst_12_dout[31]),
  .I1(sdpx9b_inst_13_dout[31]),
  .S0(dff_q_1)
);
MUX2 mux_inst_94 (
  .O(mux_o_94),
  .I0(sdpx9b_inst_14_dout[31]),
  .I1(sdpx9b_inst_15_dout[31]),
  .S0(dff_q_1)
);
MUX2 mux_inst_95 (
  .O(dout[31]),
  .I0(mux_o_93),
  .I1(mux_o_94),
  .S0(dff_q_0)
);
MUX2 mux_inst_96 (
  .O(mux_o_96),
  .I0(sdpx9b_inst_12_dout[32]),
  .I1(sdpx9b_inst_13_dout[32]),
  .S0(dff_q_1)
);
MUX2 mux_inst_97 (
  .O(mux_o_97),
  .I0(sdpx9b_inst_14_dout[32]),
  .I1(sdpx9b_inst_15_dout[32]),
  .S0(dff_q_1)
);
MUX2 mux_inst_98 (
  .O(dout[32]),
  .I0(mux_o_96),
  .I1(mux_o_97),
  .S0(dff_q_0)
);
MUX2 mux_inst_99 (
  .O(mux_o_99),
  .I0(sdpx9b_inst_12_dout[33]),
  .I1(sdpx9b_inst_13_dout[33]),
  .S0(dff_q_1)
);
MUX2 mux_inst_100 (
  .O(mux_o_100),
  .I0(sdpx9b_inst_14_dout[33]),
  .I1(sdpx9b_inst_15_dout[33]),
  .S0(dff_q_1)
);
MUX2 mux_inst_101 (
  .O(dout[33]),
  .I0(mux_o_99),
  .I1(mux_o_100),
  .S0(dff_q_0)
);
MUX2 mux_inst_102 (
  .O(mux_o_102),
  .I0(sdpx9b_inst_12_dout[34]),
  .I1(sdpx9b_inst_13_dout[34]),
  .S0(dff_q_1)
);
MUX2 mux_inst_103 (
  .O(mux_o_103),
  .I0(sdpx9b_inst_14_dout[34]),
  .I1(sdpx9b_inst_15_dout[34]),
  .S0(dff_q_1)
);
MUX2 mux_inst_104 (
  .O(dout[34]),
  .I0(mux_o_102),
  .I1(mux_o_103),
  .S0(dff_q_0)
);
MUX2 mux_inst_105 (
  .O(mux_o_105),
  .I0(sdpx9b_inst_12_dout[35]),
  .I1(sdpx9b_inst_13_dout[35]),
  .S0(dff_q_1)
);
MUX2 mux_inst_106 (
  .O(mux_o_106),
  .I0(sdpx9b_inst_14_dout[35]),
  .I1(sdpx9b_inst_15_dout[35]),
  .S0(dff_q_1)
);
MUX2 mux_inst_107 (
  .O(dout[35]),
  .I0(mux_o_105),
  .I1(mux_o_106),
  .S0(dff_q_0)
);
MUX2 mux_inst_108 (
  .O(mux_o_108),
  .I0(sdpx9b_inst_16_dout[36]),
  .I1(sdpx9b_inst_17_dout[36]),
  .S0(dff_q_1)
);
MUX2 mux_inst_109 (
  .O(mux_o_109),
  .I0(sdpx9b_inst_18_dout[36]),
  .I1(sdpx9b_inst_19_dout[36]),
  .S0(dff_q_1)
);
MUX2 mux_inst_110 (
  .O(dout[36]),
  .I0(mux_o_108),
  .I1(mux_o_109),
  .S0(dff_q_0)
);
MUX2 mux_inst_111 (
  .O(mux_o_111),
  .I0(sdpx9b_inst_16_dout[37]),
  .I1(sdpx9b_inst_17_dout[37]),
  .S0(dff_q_1)
);
MUX2 mux_inst_112 (
  .O(mux_o_112),
  .I0(sdpx9b_inst_18_dout[37]),
  .I1(sdpx9b_inst_19_dout[37]),
  .S0(dff_q_1)
);
MUX2 mux_inst_113 (
  .O(dout[37]),
  .I0(mux_o_111),
  .I1(mux_o_112),
  .S0(dff_q_0)
);
MUX2 mux_inst_114 (
  .O(mux_o_114),
  .I0(sdpx9b_inst_16_dout[38]),
  .I1(sdpx9b_inst_17_dout[38]),
  .S0(dff_q_1)
);
MUX2 mux_inst_115 (
  .O(mux_o_115),
  .I0(sdpx9b_inst_18_dout[38]),
  .I1(sdpx9b_inst_19_dout[38]),
  .S0(dff_q_1)
);
MUX2 mux_inst_116 (
  .O(dout[38]),
  .I0(mux_o_114),
  .I1(mux_o_115),
  .S0(dff_q_0)
);
MUX2 mux_inst_117 (
  .O(mux_o_117),
  .I0(sdpx9b_inst_16_dout[39]),
  .I1(sdpx9b_inst_17_dout[39]),
  .S0(dff_q_1)
);
MUX2 mux_inst_118 (
  .O(mux_o_118),
  .I0(sdpx9b_inst_18_dout[39]),
  .I1(sdpx9b_inst_19_dout[39]),
  .S0(dff_q_1)
);
MUX2 mux_inst_119 (
  .O(dout[39]),
  .I0(mux_o_117),
  .I1(mux_o_118),
  .S0(dff_q_0)
);
MUX2 mux_inst_120 (
  .O(mux_o_120),
  .I0(sdpx9b_inst_16_dout[40]),
  .I1(sdpx9b_inst_17_dout[40]),
  .S0(dff_q_1)
);
MUX2 mux_inst_121 (
  .O(mux_o_121),
  .I0(sdpx9b_inst_18_dout[40]),
  .I1(sdpx9b_inst_19_dout[40]),
  .S0(dff_q_1)
);
MUX2 mux_inst_122 (
  .O(dout[40]),
  .I0(mux_o_120),
  .I1(mux_o_121),
  .S0(dff_q_0)
);
MUX2 mux_inst_123 (
  .O(mux_o_123),
  .I0(sdpx9b_inst_16_dout[41]),
  .I1(sdpx9b_inst_17_dout[41]),
  .S0(dff_q_1)
);
MUX2 mux_inst_124 (
  .O(mux_o_124),
  .I0(sdpx9b_inst_18_dout[41]),
  .I1(sdpx9b_inst_19_dout[41]),
  .S0(dff_q_1)
);
MUX2 mux_inst_125 (
  .O(dout[41]),
  .I0(mux_o_123),
  .I1(mux_o_124),
  .S0(dff_q_0)
);
MUX2 mux_inst_126 (
  .O(mux_o_126),
  .I0(sdpx9b_inst_16_dout[42]),
  .I1(sdpx9b_inst_17_dout[42]),
  .S0(dff_q_1)
);
MUX2 mux_inst_127 (
  .O(mux_o_127),
  .I0(sdpx9b_inst_18_dout[42]),
  .I1(sdpx9b_inst_19_dout[42]),
  .S0(dff_q_1)
);
MUX2 mux_inst_128 (
  .O(dout[42]),
  .I0(mux_o_126),
  .I1(mux_o_127),
  .S0(dff_q_0)
);
MUX2 mux_inst_129 (
  .O(mux_o_129),
  .I0(sdpx9b_inst_16_dout[43]),
  .I1(sdpx9b_inst_17_dout[43]),
  .S0(dff_q_1)
);
MUX2 mux_inst_130 (
  .O(mux_o_130),
  .I0(sdpx9b_inst_18_dout[43]),
  .I1(sdpx9b_inst_19_dout[43]),
  .S0(dff_q_1)
);
MUX2 mux_inst_131 (
  .O(dout[43]),
  .I0(mux_o_129),
  .I1(mux_o_130),
  .S0(dff_q_0)
);
MUX2 mux_inst_132 (
  .O(mux_o_132),
  .I0(sdpx9b_inst_16_dout[44]),
  .I1(sdpx9b_inst_17_dout[44]),
  .S0(dff_q_1)
);
MUX2 mux_inst_133 (
  .O(mux_o_133),
  .I0(sdpx9b_inst_18_dout[44]),
  .I1(sdpx9b_inst_19_dout[44]),
  .S0(dff_q_1)
);
MUX2 mux_inst_134 (
  .O(dout[44]),
  .I0(mux_o_132),
  .I1(mux_o_133),
  .S0(dff_q_0)
);
MUX2 mux_inst_135 (
  .O(mux_o_135),
  .I0(sdpx9b_inst_20_dout[45]),
  .I1(sdpx9b_inst_21_dout[45]),
  .S0(dff_q_1)
);
MUX2 mux_inst_136 (
  .O(mux_o_136),
  .I0(sdpx9b_inst_22_dout[45]),
  .I1(sdpx9b_inst_23_dout[45]),
  .S0(dff_q_1)
);
MUX2 mux_inst_137 (
  .O(dout[45]),
  .I0(mux_o_135),
  .I1(mux_o_136),
  .S0(dff_q_0)
);
MUX2 mux_inst_138 (
  .O(mux_o_138),
  .I0(sdpx9b_inst_20_dout[46]),
  .I1(sdpx9b_inst_21_dout[46]),
  .S0(dff_q_1)
);
MUX2 mux_inst_139 (
  .O(mux_o_139),
  .I0(sdpx9b_inst_22_dout[46]),
  .I1(sdpx9b_inst_23_dout[46]),
  .S0(dff_q_1)
);
MUX2 mux_inst_140 (
  .O(dout[46]),
  .I0(mux_o_138),
  .I1(mux_o_139),
  .S0(dff_q_0)
);
MUX2 mux_inst_141 (
  .O(mux_o_141),
  .I0(sdpx9b_inst_20_dout[47]),
  .I1(sdpx9b_inst_21_dout[47]),
  .S0(dff_q_1)
);
MUX2 mux_inst_142 (
  .O(mux_o_142),
  .I0(sdpx9b_inst_22_dout[47]),
  .I1(sdpx9b_inst_23_dout[47]),
  .S0(dff_q_1)
);
MUX2 mux_inst_143 (
  .O(dout[47]),
  .I0(mux_o_141),
  .I1(mux_o_142),
  .S0(dff_q_0)
);
MUX2 mux_inst_144 (
  .O(mux_o_144),
  .I0(sdpx9b_inst_20_dout[48]),
  .I1(sdpx9b_inst_21_dout[48]),
  .S0(dff_q_1)
);
MUX2 mux_inst_145 (
  .O(mux_o_145),
  .I0(sdpx9b_inst_22_dout[48]),
  .I1(sdpx9b_inst_23_dout[48]),
  .S0(dff_q_1)
);
MUX2 mux_inst_146 (
  .O(dout[48]),
  .I0(mux_o_144),
  .I1(mux_o_145),
  .S0(dff_q_0)
);
MUX2 mux_inst_147 (
  .O(mux_o_147),
  .I0(sdpx9b_inst_20_dout[49]),
  .I1(sdpx9b_inst_21_dout[49]),
  .S0(dff_q_1)
);
MUX2 mux_inst_148 (
  .O(mux_o_148),
  .I0(sdpx9b_inst_22_dout[49]),
  .I1(sdpx9b_inst_23_dout[49]),
  .S0(dff_q_1)
);
MUX2 mux_inst_149 (
  .O(dout[49]),
  .I0(mux_o_147),
  .I1(mux_o_148),
  .S0(dff_q_0)
);
MUX2 mux_inst_150 (
  .O(mux_o_150),
  .I0(sdpx9b_inst_20_dout[50]),
  .I1(sdpx9b_inst_21_dout[50]),
  .S0(dff_q_1)
);
MUX2 mux_inst_151 (
  .O(mux_o_151),
  .I0(sdpx9b_inst_22_dout[50]),
  .I1(sdpx9b_inst_23_dout[50]),
  .S0(dff_q_1)
);
MUX2 mux_inst_152 (
  .O(dout[50]),
  .I0(mux_o_150),
  .I1(mux_o_151),
  .S0(dff_q_0)
);
MUX2 mux_inst_153 (
  .O(mux_o_153),
  .I0(sdpx9b_inst_20_dout[51]),
  .I1(sdpx9b_inst_21_dout[51]),
  .S0(dff_q_1)
);
MUX2 mux_inst_154 (
  .O(mux_o_154),
  .I0(sdpx9b_inst_22_dout[51]),
  .I1(sdpx9b_inst_23_dout[51]),
  .S0(dff_q_1)
);
MUX2 mux_inst_155 (
  .O(dout[51]),
  .I0(mux_o_153),
  .I1(mux_o_154),
  .S0(dff_q_0)
);
MUX2 mux_inst_156 (
  .O(mux_o_156),
  .I0(sdpx9b_inst_20_dout[52]),
  .I1(sdpx9b_inst_21_dout[52]),
  .S0(dff_q_1)
);
MUX2 mux_inst_157 (
  .O(mux_o_157),
  .I0(sdpx9b_inst_22_dout[52]),
  .I1(sdpx9b_inst_23_dout[52]),
  .S0(dff_q_1)
);
MUX2 mux_inst_158 (
  .O(dout[52]),
  .I0(mux_o_156),
  .I1(mux_o_157),
  .S0(dff_q_0)
);
MUX2 mux_inst_159 (
  .O(mux_o_159),
  .I0(sdpx9b_inst_20_dout[53]),
  .I1(sdpx9b_inst_21_dout[53]),
  .S0(dff_q_1)
);
MUX2 mux_inst_160 (
  .O(mux_o_160),
  .I0(sdpx9b_inst_22_dout[53]),
  .I1(sdpx9b_inst_23_dout[53]),
  .S0(dff_q_1)
);
MUX2 mux_inst_161 (
  .O(dout[53]),
  .I0(mux_o_159),
  .I1(mux_o_160),
  .S0(dff_q_0)
);
endmodule //Scope_Ring_RAM
