//Copyright (C)2014-2026 Gowin Semiconductor Corporation.
//All rights reserved.
//File Title: Template file for instantiation
//Tool Version: V1.9.12.04
//IP Version: 1.0
//Part Number: GW5AST-LV138PG484AC1/I0
//Device: GW5AST-138
//Device Version: C
//Created Time: Tue Nov  3 04:24:29 2026

//Change the instance name and port connections to the signal names
//--------Copy here to design--------

    schroeder_delay_ram_compact your_instance_name(
        .dout(dout), //output [31:0] dout
        .clka(clka), //input clka
        .cea(cea), //input cea
        .clkb(clkb), //input clkb
        .ceb(ceb), //input ceb
        .oce(oce), //input oce
        .reset(reset), //input reset
        .ada(ada), //input [13:0] ada
        .din(din), //input [31:0] din
        .adb(adb) //input [13:0] adb
    );

//--------Copy end-------------------
