`default_nettype none
module lab_top (
    input wire sys_clk,
    input wire [2:0] keys_n,
    output wire MIDI_DEBUG_TX,
    output wire MIDI_STATUS_LED,
    output wire tmds_clk_p,tmds_clk_n,
    output wire [2:0] tmds_data_p,tmds_data_n
);
    wire core_clk,dtcm_clk,ahb_clk,apb_clk,rtc_clk,core_lock;
    Lab_Core_PLL u_core_pll(.clkin(sys_clk),.init_clk(sys_clk),.clkout0(dtcm_clk),
        .clkout1(core_clk),.clkout2(ahb_clk),.clkout3(apb_clk),.clkout4(rtc_clk),.lock(core_lock));
    reg [15:0] powerup_count=0;
    always @(posedge sys_clk)if(!(&powerup_count))powerup_count<=powerup_count+1'b1;
    wire powerup_ready=(&powerup_count)&&core_lock;
    wire pixel_clk,serial_clk,video_lock;
    Spectrum_Video_PLL u_video_pll(.clkin(sys_clk),.init_clk(sys_clk),
        .clkout0(pixel_clk),.clkout1(serial_clk),.clkout2(),.lock(video_lock));
    (* async_reg="true" *) reg [1:0] pixel_reset_sync;
    wire pixel_reset_request=!(powerup_ready&&video_lock);
    always @(posedge pixel_clk or posedge pixel_reset_request)
        if(pixel_reset_request)pixel_reset_sync<=0;
        else pixel_reset_sync<={pixel_reset_sync[0],1'b1};
    wire pixel_rst_n=pixel_reset_sync[1];
    wire [31:0] paddr,pwdata,prdata;
    wire [3:0] pstrb;
    wire psel,penable,pwrite,pready,pslverr,pclk,preset_n;
    RiscV_AE350_SOC_Top u_cpu(
        .TCK_IN(1'b0),.TMS_IN(1'b1),.TRST_IN(1'b1),.TDI_IN(1'b0),.TDO_OUT(),.TDO_OE(),
        .CORE_CLK(core_clk),.DDR_CLK(dtcm_clk),.AHB_CLK(ahb_clk),.APB_CLK(apb_clk),.RTC_CLK(rtc_clk),
        .POR_RSTN(powerup_ready),.HW_RSTN(powerup_ready),
        .APB_PADDR(paddr),.APB_PWDATA(pwdata),.APB_PSTRB(pstrb),.APB_PPROT(),
        .APB_PSEL(psel),.APB_PENABLE(penable),.APB_PWRITE(pwrite),
        .APB_PRDATA(prdata),.APB_PREADY(pready),.APB_PSLVERR(pslverr),
        .APB_PCLK(pclk),.APB_PRSTN(preset_n));
    wire [9:0] text_addr;wire [7:0] text_data;wire display_valid,vblank;
    wire [31:0] debug_status,debug_boot,debug_count,debug_addr,debug_wdata,debug_rdata;
    wire [31:0] debug_cause,debug_pc,debug_value;
    wire [31:0] debug_display_ack;
    lab_debug_monitor u_debug_monitor(.clk_i(sys_clk),.rst_ni(powerup_ready),.apb_clk_i(pclk),
        .core_lock_i(core_lock),.video_lock_i(video_lock),.preset_ni(preset_n),
        .paddr_i(paddr),.pwdata_i(pwdata),.prdata_i(prdata),.psel_i(psel),.penable_i(penable),
        .pwrite_i(pwrite),.pready_i(pready),.pslverr_i(pslverr),
        .status_o(debug_status),.boot_stage_o(debug_boot),.apb_count_o(debug_count),
        .last_addr_o(debug_addr),.last_wdata_o(debug_wdata),.last_rdata_o(debug_rdata),
        .trap_cause_o(debug_cause),.trap_pc_o(debug_pc),.trap_value_o(debug_value),.display_ack_o(debug_display_ack));
    lab_debug_uart u_debug_uart(.clk_i(sys_clk),.rst_ni(powerup_ready),.status_i(debug_status),
        .boot_stage_i(debug_boot),.apb_count_i(debug_count),.last_addr_i(debug_addr),
        .last_wdata_i(debug_wdata),.last_rdata_i(debug_rdata),.trap_cause_i(debug_cause),
        .trap_pc_i(debug_pc),.trap_value_i(debug_value),.tx_o(MIDI_DEBUG_TX));
    // Show waiting/failure even if the AE350 PLL never locks.
    lab_boot_led u_boot_led(.clk_i(sys_clk),.rst_ni(&powerup_count),
        .status_i(debug_status),.boot_stage_i(debug_boot),.display_ack_i(debug_display_ack),
        .led_o(MIDI_STATUS_LED));
    lab_apb u_apb(.pclk_i(pclk),.preset_ni(preset_n),.paddr_i(paddr),.pwdata_i(pwdata),.pstrb_i(pstrb),
        .psel_i(psel),.penable_i(penable),.pwrite_i(pwrite),.prdata_o(prdata),.pready_o(pready),.pslverr_o(pslverr),
        .keys_ni(keys_n),.pixel_clk_i(pixel_clk),.pixel_rst_ni(pixel_rst_n),.vblank_i(vblank),
        .text_addr_i(text_addr),.text_data_o(text_data),.display_valid_o(display_valid));
    wire [23:0] rgb;wire de,hs,vs;
    menu_video u_video(.clk_i(pixel_clk),.rst_ni(pixel_rst_n),.display_valid_i(display_valid),
        .text_addr_o(text_addr),.text_data_i(text_data),.vblank_o(vblank),.rgb_o(rgb),.de_o(de),.hs_o(hs),.vs_o(vs));
    spectrum_hdmi_tx u_hdmi(.pixel_clk_i(pixel_clk),.serial_clk_i(serial_clk),.reset_clk_i(pixel_clk),
        .rst_n_i(pixel_rst_n),.rgb_i(rgb),.de_i(de),.hs_i(hs),.vs_i(vs),
        .tmds_clk_p_o(tmds_clk_p),.tmds_clk_n_o(tmds_clk_n),.tmds_data_p_o(tmds_data_p),.tmds_data_n_o(tmds_data_n));
endmodule
`default_nettype wire
