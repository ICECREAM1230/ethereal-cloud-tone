`default_nettype none
module control_lab_top #(parameter bit ENABLE_AE350_CONTROL=1'b1,
    parameter bit ENABLE_MIDI_PCM_LATENCY_TEST=1'b0,
    parameter bit ENABLE_MIDI_NOTE20_TEST=1'b0,
    parameter integer DEBUG_UART_BAUD=115200) (
    input wire sys_clk,
    input wire [2:0] keys_n,
    input wire ESP_SPI_SCLK,ESP_SPI_CS_N,ESP_SPI_MOSI,
    output wire ESP_SPI_MISO,
    input wire [3:0] ESP_PMOD1_PASSIVE,
    inout wire USB_MIDI_DP,USB_MIDI_DM,
    output wire HP_BCK,HP_WS,HP_DIN,PCM_BCK,PCM_WS,PCM_DIN,PA_EN,
    output wire MIDI_DEBUG_TX,
    output wire MIDI_STATUS_LED,
    output wire tmds_clk_p,tmds_clk_n,
    output wire [2:0] tmds_data_p,tmds_data_n
);
    wire core_clk,dtcm_clk,ahb_clk,apb_clk,rtc_clk,core_lock;
    Lab_Core_PLL u_core_pll(.clkin(sys_clk),.init_clk(sys_clk),.clkout0(dtcm_clk),
        .clkout1(core_clk),.clkout2(ahb_clk),.clkout3(apb_clk),.clkout4(rtc_clk),.lock(core_lock));
    reg [15:0] powerup_count=0;
    // Configuration provides the device power-up GSR. Keep its fabric input
    // deasserted: feeding a GSR-reset counter back to GSRI would prevent that
    // counter from ever releasing reset on silicon. Local startup/PLL/owner
    // resets below handle this lab's three keys without borrowing a reset pad.
    GSR GSR(.GSRI(1'b1));
    always @(posedge sys_clk)if(!(&powerup_count))powerup_count<=powerup_count+1'b1;
    wire powerup_ready=(&powerup_count)&&core_lock;
    wire [31:0] paddr,pwdata,prdata;
    wire [3:0] pstrb;
    wire psel,penable,pwrite,pready,pslverr,pclk,preset_n;
    wire menu_sel,control_sel,slave_enable,slave_write;
    wire [31:0] slave_addr,slave_wdata;
    wire [3:0] slave_strb;
    wire owner_reset_n,control_apb_reset_n;
    wire [31:0] menu_rdata,control_rdata;
    wire menu_ready,menu_error,control_ready,control_error;
    wire [31:0] link_rdata,timing_rdata;
    wire link_ready,link_error,timing_ready,timing_error;
    wire timing_page=slave_addr[15:8]==8'h01;
    assign control_rdata=timing_page ? timing_rdata : link_rdata;
    assign control_ready=timing_page ? timing_ready : link_ready;
    assign control_error=timing_page ? timing_error : link_error;
    // Request and response registers cut both the address/error decode and
    // the control-session combinational readback into the hard CPU APB port.
    lab_apb_pipeline u_apb_pipeline(
        .pclk_i(pclk),.preset_ni(preset_n),.paddr_i(paddr),.pwdata_i(pwdata),.pstrb_i(pstrb),
        .psel_i(psel),.penable_i(penable),.pwrite_i(pwrite),
        .prdata_o(prdata),.pready_o(pready),.pslverr_o(pslverr),
        .slave_addr_o(slave_addr),.slave_wdata_o(slave_wdata),.slave_strb_o(slave_strb),
        .slave_write_o(slave_write),.slave_enable_o(slave_enable),
        .menu_sel_o(menu_sel),.control_sel_o(control_sel),.control_available_i(control_apb_reset_n),
        .menu_rdata_i(menu_rdata),.control_rdata_i(control_rdata),
        .menu_ready_i(menu_ready),.menu_error_i(menu_error),
        .control_ready_i(control_ready),.control_error_i(control_error));
    RiscV_AE350_SOC_Top u_cpu(
        .TCK_IN(1'b0),.TMS_IN(1'b1),.TRST_IN(1'b1),.TDI_IN(1'b0),.TDO_OUT(),.TDO_OE(),
        .CORE_CLK(core_clk),.DDR_CLK(dtcm_clk),.AHB_CLK(ahb_clk),.APB_CLK(apb_clk),.RTC_CLK(rtc_clk),
        .POR_RSTN(powerup_ready),.HW_RSTN(powerup_ready),
        .APB_PADDR(paddr),.APB_PWDATA(pwdata),.APB_PSTRB(pstrb),.APB_PPROT(),
        .APB_PSEL(psel),.APB_PENABLE(penable),.APB_PWRITE(pwrite),
        .APB_PRDATA(prdata),.APB_PREADY(pready),.APB_PSLVERR(pslverr),
        .APB_PCLK(pclk),.APB_PRSTN(preset_n));
    wire [31:0] debug_status,debug_boot,debug_count,debug_addr,debug_wdata,debug_rdata;
    wire [31:0] debug_cause,debug_pc,debug_value;
    wire [31:0] debug_display_ack;
    wire [4:0] note20_read_index,note20_allocated,note20_frozen;
    wire [1023:0] note20_read_data;
    wire note20_read_valid,note20_done,note20_loss,note20_session_n,legacy_debug_tx;
    wire [511:0] latency_result;
    wire latency_result_valid,latency_cpu_submit,latency_session_n;
    wire latency_note_submit;
    wire [575:0] cpu_timing_result;
    wire cpu_timing_valid,cpu_timing_ready,cpu_timing_loss;
    wire [1:0] latency_cpu_engine,latency_cpu_commit_engine;
    wire latency_cpu_commit;
    wire [68:0] latency_cpu_submit_identity,latency_cpu_commit_identity;
    wire observed_valid,observed_write,observed_error;
    wire [31:0] observed_addr,observed_wdata,observed_rdata;
    lab_apb_observer u_debug_apb_stage(.pclk_i(pclk),.rst_ni(powerup_ready),
        .paddr_i(slave_addr),.pwdata_i(slave_wdata),.prdata_i(prdata),
        .psel_i(psel),.penable_i(penable),.pwrite_i(slave_write),.pready_i(pready),.pslverr_i(pslverr),
        .valid_o(observed_valid),.write_o(observed_write),.error_o(observed_error),
        .addr_o(observed_addr),.wdata_o(observed_wdata),.rdata_o(observed_rdata));
    lab_debug_monitor u_debug_monitor(.clk_i(sys_clk),.rst_ni(powerup_ready),.apb_clk_i(pclk),
        // In this headless build status bit 1 means the publication service
        // is ready in PCLK, rather than reporting a video PLL lock.
        .core_lock_i(core_lock),.video_lock_i(preset_n),.preset_ni(preset_n),
        .paddr_i(observed_addr),.pwdata_i(observed_wdata),.prdata_i(observed_rdata),
        .psel_i(observed_valid),.penable_i(1'b1),.pwrite_i(observed_write),.pready_i(1'b1),.pslverr_i(observed_error),
        .status_o(debug_status),.boot_stage_o(debug_boot),.apb_count_o(debug_count),
        .last_addr_o(debug_addr),.last_wdata_o(debug_wdata),.last_rdata_o(debug_rdata),
        .trap_cause_o(debug_cause),.trap_pc_o(debug_pc),.trap_value_o(debug_value),.display_ack_o(debug_display_ack));
    lab_debug_uart #(.ENABLE_LATENCY(ENABLE_MIDI_PCM_LATENCY_TEST),.BAUD(DEBUG_UART_BAUD)) u_debug_uart(.clk_i(sys_clk),.rst_ni(powerup_ready),.status_i(debug_status),
        .boot_stage_i(debug_boot),.apb_count_i(debug_count),.last_addr_i(debug_addr),
        .last_wdata_i(debug_wdata),.last_rdata_i(debug_rdata),.trap_cause_i(debug_cause),
        .trap_pc_i(debug_pc),.trap_value_i(debug_value),
        .latency_session_ni(latency_session_n),.latency_valid_i(latency_result_valid),.latency_result_i(latency_result),.tx_o(legacy_debug_tx));
    generate if(ENABLE_MIDI_NOTE20_TEST)begin:g_cpu_timing_uart
        wire uart_reset_n;
        jt51_reset_sync u_uart_reset(.clk_i(sys_clk),.rst_n_i(control_apb_reset_n),.rst_n_o(uart_reset_n));
        ae350_latency_apb #(.BAUD(DEBUG_UART_BAUD)) u_apb(
            .pclk_i(pclk),.preset_ni(control_apb_reset_n),.paddr_i(slave_addr),.pwdata_i(slave_wdata),.pstrb_i(slave_strb),
            .psel_i(control_sel && timing_page),.penable_i(slave_enable),.pwrite_i(slave_write),
            .prdata_o(timing_rdata),.pready_o(timing_ready),.pslverr_o(timing_error),
            .result_i(cpu_timing_result),.result_valid_i(cpu_timing_valid),.result_ready_o(cpu_timing_ready),.loss_i(cpu_timing_loss),
            .uart_clk_i(sys_clk),.uart_rst_ni(uart_reset_n),.tx_o(MIDI_DEBUG_TX));
        assign note20_read_index=5'd0;
    end else begin:g_normal_uart
        assign note20_read_index=5'd0;assign MIDI_DEBUG_TX=legacy_debug_tx;
        // The common firmware probes this extension once. ID0 means disabled;
        // it must not trap just because the synthesis test switch is off.
        assign timing_rdata=32'd0;assign timing_ready=1'b1;
        assign timing_error=control_sel && timing_page && slave_enable &&
            (slave_write || slave_addr[7:0]!=0);
        assign cpu_timing_ready=1'b0;
    end endgenerate
    // J16 retains the original USB MIDI keyboard indicator. AE350 boot and
    // APB diagnostics remain available on the independent UART above.
    lab_apb u_apb(.pclk_i(pclk),.preset_ni(preset_n),.paddr_i(slave_addr),.pwdata_i(slave_wdata),.pstrb_i(slave_strb),
        .psel_i(menu_sel),.penable_i(slave_enable),.pwrite_i(slave_write),.prdata_o(menu_rdata),.pready_o(menu_ready),.pslverr_o(menu_error),
        // Keep the C publication ABI and its four-phase ACK alive without
        // a video clock. Text writes remain legal; no character RAM read
        // port is consumed, so synthesis can remove its unused storage.
        .keys_ni(keys_n),.pixel_clk_i(pclk),.pixel_rst_ni(preset_n),.vblank_i(1'b1),
        .text_addr_i(10'd0),.text_data_o(),.display_valid_o());
    // Video PLL, renderer, encoders and OSER10 serializers are disabled.
    // Retain the existing differential pads and electrical configuration,
    // holding them static rather than emitting an HDMI clock/data stream.
    ELVDS_OBUF u_hdmi_idle_clock(.I(1'b0),.O(tmds_clk_p),.OB(tmds_clk_n));
    for(genvar lane=0;lane<3;lane=lane+1)begin:g_hdmi_idle
        ELVDS_OBUF u_buffer(.I(1'b0),.O(tmds_data_p[lane]),.OB(tmds_data_n[lane]));
    end

    wire control_clk,control_rst_n;
    assign owner_reset_n=preset_n && control_rst_n;
    // Assertion still cancels the coordinated owner immediately. The APB
    // peripheral's release is local to PCLK; the CPU response bank above is
    // reset only by CPU PRESET and always holds a registered bus response.
    jt51_reset_sync u_control_apb_reset(.clk_i(pclk),.rst_n_i(owner_reset_n),.rst_n_o(control_apb_reset_n));
    wire byte_valid48,byte_ready48,flush_valid48,flush_ready48;
    wire [1:0] byte_source48;
    wire [7:0] byte_data48;
    wire [63:0] byte_tick48,flush_tick48;
    wire [3:0] flush_mask48;
    wire done48,done50;
    wire [3:0] done_mask48,done_mask50;
    wire [31:0] done_seq48,done_seq50;
    wire rx_valid,rx_ready,flush_valid,flush_ready;
    wire [1:0] rx_source;
    wire [7:0] rx_data;
    wire [63:0] rx_tick,flush_tick;
    wire [3:0] flush_mask;
    wire cmd_valid,cmd_ready,cmd48_valid,cmd48_ready;
    wire [159:0] cmd_data;
    wire [31:0] cmd_sequence,cmd_result,cmd48_result;
    wire [191:0] cmd48;
    wire fb48_valid,fb48_ready,fb_valid,fb_ready;
    wire [1:0] fb48_engine;
    wire [4:0] fb48_voice;
    wire [31:0] fb48_generation;
    wire [38:0] feedback;
    wire [2:0] backend_ready;
    wire [31:0] backend_fault;
    wire [35:0] backend_snapshot;
    wire route_valid,route_ready,route48_valid;
    wire [1:0] route,route48_data;
    logic [1:0] active_route48;
    always_ff @(posedge control_clk or negedge owner_reset_n)
        if(!owner_reset_n)active_route48<=0;
        else if(route48_valid)active_route48<=route48_data;
    ae350_ingress_cdc u_ingress_cdc(
        .src_clk_i(control_clk),.dst_clk_i(pclk),.link_rst_ni(owner_reset_n),
        .src_byte_valid_i(byte_valid48),.src_byte_ready_o(byte_ready48),.src_byte_source_i(byte_source48),
        .src_byte_data_i(byte_data48),.src_byte_tick_i(byte_tick48),
        .src_flush_valid_i(flush_valid48),.src_flush_ready_o(flush_ready48),
        .src_flush_mask_i(flush_mask48),.src_flush_tick_i(flush_tick48),
        .dst_byte_valid_o(rx_valid),.dst_byte_ready_i(rx_ready),.dst_byte_source_o(rx_source),
        .dst_byte_data_o(rx_data),.dst_byte_tick_o(rx_tick),
        .dst_flush_valid_o(flush_valid),.dst_flush_ready_i(flush_ready),
        .dst_flush_mask_o(flush_mask),.dst_flush_tick_o(flush_tick),
        .dst_flush_done_valid_i(done50),.dst_flush_done_mask_i(done_mask50),.dst_flush_done_sequence_i(done_seq50),
        .src_flush_done_valid_o(done48),.src_flush_done_mask_o(done_mask48),.src_flush_done_sequence_o(done_seq48));
    lab_transaction_cdc u_command_cdc(
        .src_clk_i(pclk),.dst_clk_i(control_clk),.rst_ni(owner_reset_n),
        .src_data_i({cmd_sequence,cmd_data}),.src_valid_i(cmd_valid),.src_ready_o(cmd_ready),.src_response_o(cmd_result),
        .dst_data_o(cmd48),.dst_valid_o(cmd48_valid),.dst_ready_i(cmd48_ready),.dst_response_i(cmd48_result));
    held_bus_cdc #(.WIDTH(39)) u_feedback_cdc(
        .src_clk_i(control_clk),.dst_clk_i(pclk),.rst_n_i(owner_reset_n),
        .src_data_i({fb48_generation,fb48_voice,fb48_engine}),.src_valid_i(fb48_valid),.src_ready_o(fb48_ready),
        .dst_data_o(feedback),.dst_valid_o(fb_valid),.dst_ready_i(fb_ready));
    held_bus_cdc #(.WIDTH(36)) u_backend_status_cdc(
        .src_clk_i(control_clk),.dst_clk_i(pclk),.rst_n_i(owner_reset_n),
        .src_data_i({backend_fault,1'b1,backend_ready}),.src_valid_i(1'b1),.src_ready_o(),
        .dst_data_o(backend_snapshot),.dst_valid_o(),.dst_ready_i(1'b1));
    lab_transaction_cdc #(.WIDTH(2),.RESPONSE_WIDTH(1)) u_route_cdc(
        .src_clk_i(pclk),.dst_clk_i(control_clk),.rst_ni(owner_reset_n),
        .src_data_i(route),.src_valid_i(route_valid),.src_ready_o(route_ready),.src_response_o(),
        .dst_data_o(route48_data),.dst_valid_o(route48_valid),.dst_ready_i(1'b1),.dst_response_i(1'b0));
    wire template_write,template_write_ready,template_valid,template_pop;
    wire template_overflow,template_overflow_pclk;
    wire [15:0] template_write_addr,template_addr,template_free_spi,template_free_pclk;
    wire [31:0] template_write_data,template_data;
    wire [255:0] template_status_pclk,template_status_spi;
    ae350_template_ingress u_template_ingress(
        .wclk_i(ESP_SPI_SCLK),.wrst_ni(owner_reset_n),.wvalid_i(template_write),.wready_o(template_write_ready),
        .waddr_i(template_write_addr),.wdata_i(template_write_data),.wfree_o(template_free_spi),.woverflow_o(template_overflow),
        .rclk_i(pclk),.rrst_ni(owner_reset_n),.rvalid_o(template_valid),.rready_i(template_pop),
        .raddr_o(template_addr),.rdata_o(template_data),.rfree_o(template_free_pclk),.roverflow_o(template_overflow_pclk));
    held_bus_cdc #(.WIDTH(256)) u_template_status_cdc(
        .src_clk_i(pclk),.dst_clk_i(ESP_SPI_SCLK),.rst_n_i(owner_reset_n),
        .src_data_i(template_status_pclk),.src_valid_i(1'b1),.src_ready_o(),
        .dst_data_o(template_status_spi),.dst_valid_o(),.dst_ready_i(1'b1));
    control_link_apb #(.ENABLE_MIDI_PCM_LATENCY_TEST(ENABLE_MIDI_PCM_LATENCY_TEST || ENABLE_MIDI_NOTE20_TEST),.RX_DEPTH(32),.TX_DEPTH(4),.FB_DEPTH(16)) u_control_apb(
        .pclk_i(pclk),.preset_ni(control_apb_reset_n),.paddr_i(slave_addr),.pwdata_i(slave_wdata),.pstrb_i(slave_strb),
        .psel_i(control_sel && !timing_page),.penable_i(slave_enable),.pwrite_i(slave_write),
        .prdata_o(link_rdata),.pready_o(link_ready),.pslverr_o(link_error),
        .rx_valid_i(rx_valid),.rx_ready_o(rx_ready),.rx_source_i(rx_source),.rx_data_i(rx_data),.rx_tick_i(rx_tick),
        .flush_valid_i(flush_valid),.flush_ready_o(flush_ready),.flush_mask_i(flush_mask),.flush_tick_i(flush_tick),
        .rx_overflow_o(),.rx_recovering_o(),
        .latency_submit_o(latency_cpu_submit),.latency_engine_o(latency_cpu_engine),
        .latency_note_submit_o(latency_note_submit),
        .latency_commit_o(latency_cpu_commit),.latency_commit_engine_o(latency_cpu_commit_engine),
        .latency_submit_identity_o(latency_cpu_submit_identity),.latency_commit_identity_o(latency_cpu_commit_identity),
        .cmd_valid_o(cmd_valid),.cmd_ready_i(cmd_ready),.cmd_data_o(cmd_data),.cmd_sequence_o(cmd_sequence),
        .fb_valid_i(fb_valid),.fb_ready_o(fb_ready),.fb_engine_i(feedback[1:0]),.fb_voice_i(feedback[6:2]),
        .fb_generation_i(feedback[38:7]),
        .backend_status_i({23'd0,backend_snapshot[3],5'd0,backend_snapshot[2:0]}),
        .backend_fault_i(backend_snapshot[35:4]),.cmd_result_i(cmd_result),
        .flush_done_valid_o(done50),.flush_done_mask_o(done_mask50),.flush_done_sequence_o(done_seq50),
        .route_valid_o(route_valid),.route_ready_i(route_ready),.route_o(route),
        .template_valid_i(template_valid),.template_ready_o(template_pop),.template_addr_i(template_addr),.template_data_i(template_data),
        .template_free_i(template_free_pclk),.template_overflow_i(template_overflow_pclk),.template_status_o(template_status_pclk));
    synth_system #(.ENABLE_MIDI_PCM_LATENCY_TEST(ENABLE_MIDI_PCM_LATENCY_TEST),
        .ENABLE_MIDI_NOTE20_TEST(ENABLE_MIDI_NOTE20_TEST),
        .ENABLE_AE350_CONTROL(ENABLE_AE350_CONTROL),
        .ENABLE_MIDI_STATUS_LED(1'b1),.ENABLE_MIDI_DEBUG_UART(1'b0),
        .ENABLE_SUBTRACTIVE(1'b1),.ENABLE_KS(1'b1),.ENABLE_FM(1'b1),
        .ENABLE_ADDITIVE(1'b0),.ENABLE_FLUTE(1'b0),.ENABLE_HDMI(1'b0),.ENABLE_FFT(1'b0),
        .ENABLE_SENSOR_I2C(1'b0),.ENABLE_REVERB(1'b1),.REVERB_WET_Q15(16'd0),.REVERB_DAMP_Q15(16'd0),
        .ENABLE_OUTPUT_TILT(1'b0),.ENABLE_KS_TIMBRES(1'b0)) u_system(
        .sys_clk(sys_clk),.sys_rst_n(powerup_ready),.Key_in(2'b11),
        .latency_cpu_clk_i(pclk),.latency_cpu_rst_ni(control_apb_reset_n),
        .latency_cpu_submit_i(latency_cpu_submit),.latency_cpu_engine_i(latency_cpu_engine),
        .latency_note_submit_i(latency_note_submit),
        .latency_cpu_commit_i(latency_cpu_commit),.latency_cpu_commit_engine_i(latency_cpu_commit_engine),
        .latency_cpu_submit_identity_i(latency_cpu_submit_identity),.latency_cpu_commit_identity_i(latency_cpu_commit_identity),
        .latency_cpu_ack_identity_i({cmd_data[159:128],cmd_data[63:32],cmd_data[12:8]}),
        .latency_cpu_ack_i(cmd_valid && cmd_ready && cmd_data[23:16]==8'h13 && cmd_result==0),
        .latency_cpu_ack_engine_i(cmd_data[1:0]),
        .latency_result_o(latency_result),.latency_result_valid_o(latency_result_valid),.latency_session_no(latency_session_n),
        .note20_read_index_i(note20_read_index),.note20_read_data_o(note20_read_data),.note20_read_valid_o(note20_read_valid),
        .note20_allocated_o(note20_allocated),.note20_frozen_o(note20_frozen),.note20_done_o(note20_done),
        .note20_loss_o(note20_loss),.note20_session_no(note20_session_n),
        .cpu_timing_result_o(cpu_timing_result),.cpu_timing_valid_o(cpu_timing_valid),
        .cpu_timing_ready_i(cpu_timing_ready),.cpu_timing_loss_o(cpu_timing_loss),
        .ESP_SPI_SCLK(ESP_SPI_SCLK),.ESP_SPI_CS_N(ESP_SPI_CS_N),.ESP_SPI_MOSI(ESP_SPI_MOSI),.ESP_SPI_MISO(ESP_SPI_MISO),
        .ESP_PMOD1_PASSIVE(ESP_PMOD1_PASSIVE),.USB_MIDI_DP(USB_MIDI_DP),.USB_MIDI_DM(USB_MIDI_DM),
        .MIDI_STATUS_LED(MIDI_STATUS_LED),.MIDI_DEBUG_TX(),.FM_DIAG_LED(),
        .HP_BCK(HP_BCK),.HP_WS(HP_WS),.HP_DIN(HP_DIN),.PCM_BCK(PCM_BCK),.PCM_WS(PCM_WS),.PCM_DIN(PCM_DIN),.PA_EN(PA_EN),
        .tmds_clk_p(),.tmds_clk_n(),.tmds_data_p(),.tmds_data_n(),
        .ae350_owner_rst_ni(owner_reset_n),.ae350_control_clk_o(control_clk),.ae350_control_rst_no(control_rst_n),
        .ae350_byte_valid_o(byte_valid48),.ae350_byte_ready_i(byte_ready48),.ae350_byte_source_o(byte_source48),
        .ae350_byte_data_o(byte_data48),.ae350_byte_tick_o(byte_tick48),
        .ae350_flush_valid_o(flush_valid48),.ae350_flush_ready_i(flush_ready48),
        .ae350_flush_mask_o(flush_mask48),.ae350_flush_tick_o(flush_tick48),
        .ae350_flush_done_valid_i(done48),.ae350_flush_done_mask_i(done_mask48),.ae350_flush_done_sequence_i(done_seq48),
        .ae350_cmd_valid_i(cmd48_valid),.ae350_cmd_ready_o(cmd48_ready),.ae350_cmd_data_i(cmd48[159:0]),
        .ae350_cmd_sequence_i(cmd48[191:160]),.ae350_cmd_result_o(cmd48_result),
        .ae350_fb_valid_o(fb48_valid),.ae350_fb_ready_i(fb48_ready),.ae350_fb_engine_o(fb48_engine),
        .ae350_fb_voice_o(fb48_voice),.ae350_fb_generation_o(fb48_generation),
        .ae350_ready_o(backend_ready),.ae350_fault_o(backend_fault),.ae350_route_i(active_route48),
        .ae350_template_status_i(template_status_spi),.ae350_template_free_i(template_free_spi),
        .ae350_template_overflow_i(template_overflow),.ae350_template_write_o(template_write),
        .ae350_template_addr_o(template_write_addr),.ae350_template_data_o(template_write_data));
endmodule
`default_nettype wire
