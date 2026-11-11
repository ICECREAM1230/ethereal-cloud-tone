`default_nettype none
module control_lab_top #(
    parameter bit ENABLE_AE350_CONTROL=1'b1,
    parameter bit REVERB_COMPACT=1'b1, // 1: two combs/channel + two allpasses; 0: frozen trim full algorithm
    parameter bit ENABLE_HDMI=1'b1,
    parameter bit ENABLE_FFT=1'b1,
    parameter bit DEFAULT_DISPLAY_FFT=1'b1,
    // 综合前开关：只关闭 PC PCM 采集回传，保留本地音频和 USB-MIDI 输入。
    parameter bit ENABLE_CH569_PCM_CAPTURE=1'b0,
    // 可选 USB-MIDI 状态灯；关闭不影响枚举、输入或音源控制。
    parameter bit ENABLE_MIDI_STATUS_LED=1'b1,
    // Optional AE35 UART, including PCM/HSPI diagnosis; idle high when off.
    parameter bit ENABLE_CH569_UART_DIAG=1'b0,
    parameter bit ENABLE_SENSOR_I2C=1'b1,
    parameter bit ENABLE_AE350_FDC=ENABLE_AE350_CONTROL && ENABLE_SENSOR_I2C
) (
    input wire sys_clk,
    input wire sys_rst_n,
    input wire display_key_n,
    input wire ESP_SPI_SCLK,ESP_SPI_CS_N,ESP_SPI_MOSI,
    output wire ESP_SPI_MISO,
    input wire [3:0] ESP_PMOD1_PASSIVE,
    inout wire USB_MIDI_DP,USB_MIDI_DM,
    output wire HP_BCK,HP_WS,HP_DIN,PCM_BCK,PCM_WS,PCM_DIN,PA_EN,
    output wire PCM_B_BCK,PCM_B_WS,PCM_B_DIN,
    output wire PCM_MCK,PCM_B_MCK,
    output wire HSPI_HTCLK,HSPI_HTREQ,HSPI_HTVLD,
    input wire HSPI_HTRDY,
    inout wire [15:0] HSPI_HD,
    output wire MIDI_DEBUG_TX,
    output wire MIDI_STATUS_LED,
    output wire tmds_clk_p,tmds_clk_n,
    output wire [2:0] tmds_data_p,tmds_data_n
    ,inout wire FDC_SCL,FDC_SDA,
    output wire FDC_ADDR,FDC_SD,
    input wire FDC_INT_N
);
    // Five dedicated sensor pads follow the confirmed H4 wiring.
    // Disabling the feature releases I2C and holds SD high; pads stay bound.
    generate if(ENABLE_AE350_FDC != (ENABLE_AE350_CONTROL && ENABLE_SENSOR_I2C)) begin:g_invalid_fdc_switch
        FDC_ENABLE_PARAMETER_MISMATCH invalid_configuration();
    end
    if(ENABLE_SENSOR_I2C && !ENABLE_AE350_CONTROL) begin:g_fdc_requires_cpu
        FDC_CONTROL_LAB_REQUIRES_AE350_OWNER invalid_configuration();
    end endgenerate
    wire core_clk,dtcm_clk,ahb_clk,apb_clk,rtc_clk,core_lock;
    Lab_Core_PLL u_core_pll(.clkin(sys_clk),.init_clk(sys_clk),.clkout0(dtcm_clk),
        .clkout1(core_clk),.clkout2(ahb_clk),.clkout3(apb_clk),.clkout4(rtc_clk),.lock(core_lock));
    reg [15:0] powerup_count=0;
    // AA13 is the user's active-low system-reset button, as in the main
    // project. N12 remains the separate device RECONFIG input. Drive GSR
    // directly from AA13: startup counters, PLL lock and CPU state must not
    // feed GSRI, because GSR also resets their fabric registers.
    GSR GSR(.GSRI(sys_rst_n));
    always @(posedge sys_clk or negedge sys_rst_n)
        if(!sys_rst_n)powerup_count<=0;
        else if(!(&powerup_count))powerup_count<=powerup_count+1'b1;
    wire powerup_ready=sys_rst_n && (&powerup_count) && core_lock;
    wire [31:0] paddr,pwdata,prdata;
    wire [3:0] pstrb;
    wire psel,penable,pwrite,pready,pslverr,pclk,preset_n;
    wire menu_sel,control_sel,slave_enable,slave_write;
    wire [31:0] slave_addr,slave_wdata;
    wire [3:0] slave_strb;
    wire owner_reset_n,control_apb_reset_n;
    wire [31:0] menu_rdata,control_rdata;
    wire menu_ready,menu_error,control_ready,control_error;
    wire [31:0] native_control_rdata,fdc_rdata,fdc_cpu_status,fdc_status_spi;
    wire [127:0] fdc_cpu_telemetry,fdc_telemetry_spi;
    wire native_control_ready,native_control_error,fdc_ready,fdc_error;
    // Decode the captured request after the existing request/response pipeline.
    // Both SETUP and ACCESS are exclusive; no live CPU address reaches a slave.
    wire fdc_page=slave_addr[31:7]==25'h1f00202; // F8010100..F801017F
    wire native_control_sel=control_sel && !fdc_page;
    wire fdc_sel=control_sel && fdc_page;
    assign control_rdata=fdc_page?fdc_rdata:native_control_rdata;
    assign control_ready=fdc_page?fdc_ready:native_control_ready;
    assign control_error=fdc_page?fdc_error:native_control_error;
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
    // lab_debug_monitor defines its upper22 status bits as zero. Extend only
    // that unused UART field; preserve all low10 boot/trap flags and words.
    wire [21:0] ch569_diagnostics;
    generate if(ENABLE_CH569_UART_DIAG) begin:g_ch569_uart_diag
    lab_debug_uart u_debug_uart(.clk_i(sys_clk),.rst_ni(powerup_ready),.status_i({ch569_diagnostics,debug_status[9:0]}),
        .boot_stage_i(debug_boot),.apb_count_i(debug_count),.last_addr_i(debug_addr),
        .last_wdata_i(debug_wdata),.last_rdata_i(debug_rdata),.trap_cause_i(debug_cause),
        .trap_pc_i(debug_pc),.trap_value_i(debug_value),.tx_o(MIDI_DEBUG_TX));
    end else begin:g_no_ch569_uart_diag
        assign MIDI_DEBUG_TX=1'b1;
    end endgenerate
    // J16 retains the original USB MIDI keyboard indicator. AE350 boot and
    // APB diagnostics remain available on the independent UART above.
    lab_apb u_apb(.pclk_i(pclk),.preset_ni(preset_n),.paddr_i(slave_addr),.pwdata_i(slave_wdata),.pstrb_i(slave_strb),
        .psel_i(menu_sel),.penable_i(slave_enable),.pwrite_i(slave_write),.prdata_o(menu_rdata),.pready_o(menu_ready),.pslverr_o(menu_error),
        // Keep the C publication ABI and its four-phase ACK alive without
        // a video clock. Text writes remain legal; no character RAM read
        // port is consumed, so synthesis can remove its unused storage.
        // The obsolete engine switches are released. Preserve the shared
        // three-counter APB ABI as zero; AA13 remains the system reset.
        .keys_ni(3'b111),.pixel_clk_i(pclk),.pixel_rst_ni(preset_n),.vblank_i(1'b1),
        .text_addr_i(10'd0),.text_data_o(),.display_valid_o());
    // The synth display owns the existing differential pads. Its disabled
    // branch retains the same static electrical behavior when HDMI is off.

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
    wire [415:0] template_status_pclk,template_status_spi;
    wire sub_patch1_supported_pclk;
    (* ASYNC_REG="TRUE" *) logic sub_patch1_cap_meta_spi,sub_patch1_cap_sync_spi;
    // Only a stable software capability latch crosses this domain. Status
    // ACK words retain the existing coherent 256-bit held-bus channel.
    always_ff @(posedge ESP_SPI_SCLK or negedge owner_reset_n)begin
        if(!owner_reset_n)begin sub_patch1_cap_meta_spi<=0;sub_patch1_cap_sync_spi<=0;end
        else begin sub_patch1_cap_meta_spi<=sub_patch1_supported_pclk;sub_patch1_cap_sync_spi<=sub_patch1_cap_meta_spi;end
    end
    ae350_template_ingress u_template_ingress(
        .wclk_i(ESP_SPI_SCLK),.wrst_ni(owner_reset_n),.wvalid_i(template_write),.wready_o(template_write_ready),
        .waddr_i(template_write_addr),.wdata_i(template_write_data),.wfree_o(template_free_spi),.woverflow_o(template_overflow),
        .rclk_i(pclk),.rrst_ni(owner_reset_n),.rvalid_o(template_valid),.rready_i(template_pop),
        .raddr_o(template_addr),.rdata_o(template_data),.rfree_o(template_free_pclk),.roverflow_o(template_overflow_pclk));
    held_bus_cdc #(.WIDTH(416)) u_template_status_cdc(
        .src_clk_i(pclk),.dst_clk_i(ESP_SPI_SCLK),.rst_n_i(owner_reset_n),
        .src_data_i(template_status_pclk),.src_valid_i(1'b1),.src_ready_o(),
        .dst_data_o(template_status_spi),.dst_valid_o(),.dst_ready_i(1'b1));
    control_link_apb #(.RX_DEPTH(32),.TX_DEPTH(4),.FB_DEPTH(16)) u_control_apb(
        .pclk_i(pclk),.preset_ni(control_apb_reset_n),.paddr_i(slave_addr),.pwdata_i(slave_wdata),.pstrb_i(slave_strb),
        .psel_i(native_control_sel),.penable_i(slave_enable),.pwrite_i(slave_write),
        .prdata_o(native_control_rdata),.pready_o(native_control_ready),.pslverr_o(native_control_error),
        .rx_valid_i(rx_valid),.rx_ready_o(rx_ready),.rx_source_i(rx_source),.rx_data_i(rx_data),.rx_tick_i(rx_tick),
        .flush_valid_i(flush_valid),.flush_ready_o(flush_ready),.flush_mask_i(flush_mask),.flush_tick_i(flush_tick),
        .rx_overflow_o(),.rx_recovering_o(),
        .cmd_valid_o(cmd_valid),.cmd_ready_i(cmd_ready),.cmd_data_o(cmd_data),.cmd_sequence_o(cmd_sequence),
        .fb_valid_i(fb_valid),.fb_ready_o(fb_ready),.fb_engine_i(feedback[1:0]),.fb_voice_i(feedback[6:2]),
        .fb_generation_i(feedback[38:7]),
        .backend_status_i({23'd0,backend_snapshot[3],5'd0,backend_snapshot[2:0]}),
        .backend_fault_i(backend_snapshot[35:4]),.cmd_result_i(cmd_result),
        .flush_done_valid_o(done50),.flush_done_mask_o(done_mask50),.flush_done_sequence_o(done_seq50),
        .route_valid_o(route_valid),.route_ready_i(route_ready),.route_o(route),
        .template_valid_i(template_valid),.template_ready_o(template_pop),.template_addr_i(template_addr),.template_data_i(template_data),
        .template_free_i(template_free_pclk),.template_overflow_i(template_overflow_pclk),.template_status_o(template_status_pclk),.sub_patch1_supported_o(sub_patch1_supported_pclk));
    // The mailbox CTRL return is diagnostic only. PCM sensor gain/liveness
    // is owned by real native publication, never by a second DSP writer here.
    fdc_ae350_subsystem #(.ENABLE(ENABLE_AE350_FDC),.CLK_HZ(50_000_000)) u_fdc (
        .sys_clk_i(sys_clk),.pclk_i(pclk),.rst_ni(powerup_ready && owner_reset_n),
        .paddr_i(slave_addr),.pwdata_i(slave_wdata),.pstrb_i(slave_strb),
        .psel_i(fdc_sel),.penable_i(slave_enable),.pwrite_i(slave_write),
        .prdata_o(fdc_rdata),.pready_o(fdc_ready),.pslverr_o(fdc_error),
        .fdc_scl_io(FDC_SCL),.fdc_sda_io(FDC_SDA),.fdc_addr_o(FDC_ADDR),.fdc_sd_o(FDC_SD),.fdc_int_ni(FDC_INT_N),
        .ctrl_valid_o(),.ctrl_alive_o(),.ctrl_active_o(),.ctrl_timed_out_o(),
        .ctrl_epoch_o(),.ctrl_sequence_o(),.ctrl_pitch_o(),.ctrl_gain_o(),.cpu_status_o(fdc_cpu_status),.cpu_telemetry_o(fdc_cpu_telemetry));
    // Independent status publication preserves all eight native/template ACKs.
    generate if(ENABLE_AE350_FDC)begin:g_fdc_status
        held_bus_cdc #(.WIDTH(32)) u_publish(
            .src_clk_i(pclk),.dst_clk_i(ESP_SPI_SCLK),.rst_n_i(owner_reset_n),
            .src_data_i(fdc_cpu_status),.src_valid_i(1'b1),.src_ready_o(),
            .dst_data_o(fdc_status_spi),.dst_valid_o(),.dst_ready_i(1'b1));
    end else begin:g_no_fdc_status
        assign fdc_status_spi=0;
    end endgenerate
    generate if(ENABLE_AE350_FDC)begin:g_fdc_telemetry
        held_bus_cdc #(.WIDTH(128)) u_publish(
            .src_clk_i(pclk),.dst_clk_i(ESP_SPI_SCLK),.rst_n_i(owner_reset_n),
            .src_data_i(fdc_cpu_telemetry),.src_valid_i(1'b1),.src_ready_o(),
            .dst_data_o(fdc_telemetry_spi),.dst_valid_o(),.dst_ready_i(1'b1));
    end else begin:g_no_fdc_telemetry
        assign fdc_telemetry_spi=0;
    end endgenerate
    // synthesis translate_off
    always @(posedge pclk)if(preset_n)
        assert(!(native_control_sel && fdc_sel))else $fatal(1,"FDC/native APB simultaneous select");
    // synthesis translate_on
    // PCM A and B share frame clocks; each carries an independent stereo stream.
    // Four-wire I2S uses the same audio-domain serializer and its 256fs MCK.
    // Both DAC SCK inputs must be disconnected from any ground strap.
    assign PCM_B_BCK = PCM_BCK;
    assign PCM_B_WS = PCM_WS;
    assign PCM_B_MCK = PCM_MCK;
    synth_system #(.ENABLE_AE350_CONTROL(ENABLE_AE350_CONTROL),
        .ENABLE_CH569_PCM_CAPTURE(ENABLE_CH569_PCM_CAPTURE),
        .ENABLE_CH569_UART_DIAG(ENABLE_CH569_UART_DIAG),
        .ENABLE_AE350_FDC(ENABLE_AE350_FDC),
        .ENABLE_MIDI_STATUS_LED(ENABLE_MIDI_STATUS_LED),.ENABLE_MIDI_DEBUG_UART(1'b0),
        .ENABLE_SUBTRACTIVE(1'b1),.ENABLE_KS(1'b1),.ENABLE_FM(1'b1),
        .ENABLE_ADDITIVE(1'b0),.ENABLE_FLUTE(1'b0),.ENABLE_HDMI(ENABLE_HDMI),.ENABLE_FFT(ENABLE_FFT),
        .DEFAULT_DISPLAY_FFT(DEFAULT_DISPLAY_FFT),
        .ENABLE_SENSOR_I2C(ENABLE_SENSOR_I2C),.ENABLE_REVERB(1'b1),.REVERB_COMPACT(REVERB_COMPACT),.REVERB_WET_Q15(16'd0),.REVERB_DAMP_Q15(16'd0),
        .ENABLE_OUTPUT_TILT(1'b0),.ENABLE_KS_TIMBRES(1'b0)) u_system(
        .sys_clk(sys_clk),.sys_rst_n(powerup_ready),.Key_in({1'b1,display_key_n}),
        .ESP_SPI_SCLK(ESP_SPI_SCLK),.ESP_SPI_CS_N(ESP_SPI_CS_N),.ESP_SPI_MOSI(ESP_SPI_MOSI),.ESP_SPI_MISO(ESP_SPI_MISO),
        .ESP_PMOD1_PASSIVE(ESP_PMOD1_PASSIVE),.USB_MIDI_DP(USB_MIDI_DP),.USB_MIDI_DM(USB_MIDI_DM),
        .MIDI_STATUS_LED(MIDI_STATUS_LED),.MIDI_DEBUG_TX(),.FM_DIAG_LED(),
        .HP_BCK(HP_BCK),.HP_WS(HP_WS),.HP_DIN(HP_DIN),.PCM_BCK(PCM_BCK),.PCM_WS(PCM_WS),.PCM_DIN(PCM_DIN),.PCM_B_DIN(PCM_B_DIN),.PCM_MCK(PCM_MCK),.PA_EN(PA_EN),
        .HSPI_HTCLK(HSPI_HTCLK),.HSPI_HTREQ(HSPI_HTREQ),.HSPI_HTRDY(HSPI_HTRDY),.HSPI_HTVLD(HSPI_HTVLD),.HSPI_HD(HSPI_HD),
        .ch569_diag_o(ch569_diagnostics),
        .tmds_clk_p(tmds_clk_p),.tmds_clk_n(tmds_clk_n),.tmds_data_p(tmds_data_p),.tmds_data_n(tmds_data_n),
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
        .ae350_sub_patch1_supported_i(sub_patch1_cap_sync_spi),.ae350_template_status_i(template_status_spi),.ae350_fdc_status_i(fdc_status_spi),.ae350_fdc_telemetry_i(fdc_telemetry_spi),.ae350_template_free_i(template_free_spi),
        .ae350_template_overflow_i(template_overflow),.ae350_template_write_o(template_write),
        .ae350_template_addr_o(template_write_addr),.ae350_template_data_o(template_write_data));
endmodule
`default_nettype wire
