//Copyright (C)2014-2026 Gowin Semiconductor Corporation.
//All rights reserved.
//File Title: Template file for instantiation
//Tool Version: V1.9.12.04(89079)
//IP Version: 1.2
//Device: GW5AST-138
//Device Version: C
//Part Number: GW5AST-LV138PG484AC1/I0
//Device-Package: GW5AST-138C-PBGA484A
//Created Time: Wed Oct 28 13:53:01 2026

//Change the instance name and port connections to the signal names
//--------Copy here to design--------

	RiscV_AE350_SOC_Top your_instance_name(
		.TCK_IN(TCK_IN), //input TCK_IN
		.TMS_IN(TMS_IN), //input TMS_IN
		.TRST_IN(TRST_IN), //input TRST_IN
		.TDI_IN(TDI_IN), //input TDI_IN
		.TDO_OUT(TDO_OUT), //output TDO_OUT
		.TDO_OE(TDO_OE), //output TDO_OE
		.APB_PRDATA(APB_PRDATA), //input [31:0] APB_PRDATA
		.APB_PREADY(APB_PREADY), //input APB_PREADY
		.APB_PSLVERR(APB_PSLVERR), //input APB_PSLVERR
		.APB_PADDR(APB_PADDR), //output [31:0] APB_PADDR
		.APB_PENABLE(APB_PENABLE), //output APB_PENABLE
		.APB_PSEL(APB_PSEL), //output APB_PSEL
		.APB_PWDATA(APB_PWDATA), //output [31:0] APB_PWDATA
		.APB_PWRITE(APB_PWRITE), //output APB_PWRITE
		.APB_PPROT(APB_PPROT), //output [2:0] APB_PPROT
		.APB_PSTRB(APB_PSTRB), //output [3:0] APB_PSTRB
		.APB_PCLK(APB_PCLK), //output APB_PCLK
		.APB_PRSTN(APB_PRSTN), //output APB_PRSTN
		.CORE_CLK(CORE_CLK), //input CORE_CLK
		.DDR_CLK(DDR_CLK), //input DDR_CLK
		.AHB_CLK(AHB_CLK), //input AHB_CLK
		.APB_CLK(APB_CLK), //input APB_CLK
		.RTC_CLK(RTC_CLK), //input RTC_CLK
		.POR_RSTN(POR_RSTN), //input POR_RSTN
		.HW_RSTN(HW_RSTN) //input HW_RSTN
	);

//--------Copy end-------------------
