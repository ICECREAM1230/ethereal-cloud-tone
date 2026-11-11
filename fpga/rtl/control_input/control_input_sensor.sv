`default_nettype none
// Sensor branch of the shared input layer. Acquisition/decoding: sys_clk_i
// (50MHz); public sensor controls: core_clk_i. Group 0=FDC, 1=dual ToF.
module control_input_sensor #(
    parameter bit ENABLE = 1'b0,
    parameter integer CLK_HZ = 50_000_000,
    parameter integer I2C_HZ = 100_000,
    parameter integer STALE_CYCLES = CLK_HZ/10
) (
    input wire sys_clk_i, core_clk_i, rst_ni, cfg_clk_i, cfg_cs_ni,
    input wire cfg_write_i,
    input wire [15:0] cfg_addr_i,
    input wire [31:0] cfg_data_i,
    input wire read_toggle_i,
    input wire [15:0] read_addr_i,
    output wire read_selected_o,
    output wire [31:0] read_data_o,
    output logic controls_valid_o, active_o, source_o,
    output logic [13:0] pitch_code_o,
    output logic signed [17:0] pitch_delta_o,
    output logic [16:0] expression_gain_o
`ifdef SYNTH_SENSOR_I2C_PORTS
    ,inout wire scl_io, sda_io,
    output wire fdc_addr_o, fdc_sd_o,
    input wire fdc_int_ni,
    inout wire tof1_scl_io, tof1_sda_io, tof1_xshut_io,
    input wire tof1_int_ni,
    inout wire tof2_scl_io, tof2_sda_io, tof2_xshut_io,
    input wire tof2_int_ni
`endif
);
    generate if (ENABLE) begin : g_sensor
`ifdef SYNTH_SENSOR_I2C_PORTS
        wire sensor_rst_n, modulation_rst_n;
        jt51_reset_sync u_reset(.clk_i(sys_clk_i),.rst_n_i(rst_ni),.rst_n_o(sensor_rst_n));
        jt51_reset_sync u_core_reset(.clk_i(core_clk_i),.rst_n_i(rst_ni),.rst_n_o(modulation_rst_n));
        wire [2:0] configured, fault;
        wire [2:0][3:0] unused_fault_code;
        wire [27:0] fdc0, fdc1;
        wire fdc_valid, fdc_error, unused_fdc_int;
        wire [1:0] unused_fdc_flags0, unused_fdc_flags1;
        wire [15:0] unused_fdc_status;
        wire [31:0] unused_fdc_count;
        wire [1:0][15:0] distance;
        wire [1:0][7:0] unused_range_status;
        wire [1:0] tof_valid, tof_error, unused_tof_int;
        wire [1:0][31:0] unused_tof_count;
        wire unused_bus_active;
        wire [1:0] unused_bus_select;
        sensor_i2c_top #(.CLK_HZ(CLK_HZ),.I2C_HZ(I2C_HZ)) u_acquisition (
            .clk_i(sys_clk_i),.rst_ni(sensor_rst_n),.restart_i(3'b000),
            .scl_io,.sda_io,.fdc_addr_o,.fdc_sd_o,.fdc_int_ni,
            .fdc_int_active_o(unused_fdc_int),
            .tof1_scl_io,.tof1_sda_io,.tof1_xshut_io,.tof1_int_ni,
            .tof2_scl_io,.tof2_sda_io,.tof2_xshut_io,.tof2_int_ni,
            .tof_int_active_o(unused_tof_int),.configured_o(configured),.fault_o(fault),
            .fault_code_o(unused_fault_code),.fdc_raw_ch0_o(fdc0),.fdc_raw_ch1_o(fdc1),
            .fdc_flags_ch0_o(unused_fdc_flags0),.fdc_flags_ch1_o(unused_fdc_flags1),
            .fdc_status_o(unused_fdc_status),.fdc_sample_valid_o(fdc_valid),.fdc_sample_error_o(fdc_error),
            .fdc_sample_count_o(unused_fdc_count),.tof_distance_mm_o(distance),
            .tof_range_status_o(unused_range_status),.tof_sample_valid_o(tof_valid),
            .tof_sample_error_o(tof_error),.tof_sample_count_o(unused_tof_count),
            .bus_active_o(unused_bus_active),.bus_select_o(unused_bus_select)
        );
        wire enable, selected_source, clear, cfg_valid, cfg_ready, cfg_bank;
        wire [27:0] pitch_far, pitch_near, volume_far, volume_near;
        wire [13:0] pitch_min, pitch_max, reference_pitch;
        wire [1:0] calibrated, applied, cfg_error;
        wire target_valid, target_active, target_source, stale, ready, hard_fault;
        wire signed [17:0] delta;
        wire [16:0] gain;
        wire [13:0] target_pitch;
        wire [16:0] unused_u_pitch, unused_u_volume;
        wire sample_error, overrun, expired;
        sensor_control_registers u_registers (
            .cfg_clk_i,.clk_i(sys_clk_i),.rst_ni,.cfg_cs_ni,
            .write_i(cfg_write_i),.addr_i(cfg_addr_i),.data_i(cfg_data_i),
            .read_toggle_i,.read_addr_i,.read_selected_o,.read_data_o,
            .enable_o(enable),.source_o(selected_source),.clear_o(clear),
            .cfg_valid_o(cfg_valid),.cfg_ready_i(cfg_ready),.cfg_source_o(cfg_bank),
            .pitch_far_o(pitch_far),.pitch_near_o(pitch_near),.volume_far_o(volume_far),.volume_near_o(volume_near),
            .pitch_min_o(pitch_min),.pitch_max_o(pitch_max),.reference_o(reference_pitch),
            .cfg_applied_i(applied),.cfg_error_i(cfg_error),.calibrated_i(calibrated),
            .active_i(target_active),.stale_i(stale),.ready_i(ready),.fault_i(hard_fault),
            .delta_i(delta),.gain_i(gain),.fdc0_i(fdc0),.fdc1_i(fdc1),.distance_i(distance),
            .configured_i(configured),.device_fault_i(fault),.sample_error_i(sample_error),.overrun_i(overrun),.expired_i(expired)
        );
        sensor_control_decoder #(.CLK_HZ(CLK_HZ),.STALE_TIMEOUT_CYCLES(STALE_CYCLES),
            .FRAME_TIMEOUT_CYCLES(STALE_CYCLES)) u_decoder (
            .clk_i(sys_clk_i),.rst_ni(sensor_rst_n),.clear_i(clear),.enable_i(enable),.source_select_i(selected_source),
            .cfg_valid_i(cfg_valid),.cfg_ready_o(cfg_ready),.cfg_source_i(cfg_bank),
            .cfg_pitch_far_i(pitch_far),.cfg_pitch_near_i(pitch_near),.cfg_volume_far_i(volume_far),.cfg_volume_near_i(volume_near),
            .cfg_pitch_min_i(pitch_min),.cfg_pitch_max_i(pitch_max),.cfg_reference_pitch_i(reference_pitch),
            .calibrated_o(calibrated),.cfg_applied_o(applied),.cfg_error_o(cfg_error),
            .fdc_ready_i(configured[0]),.fdc_fault_i(fault[0]),.fdc_sample_valid_i(fdc_valid),.fdc_sample_error_i(fdc_error),
            .fdc_raw_ch0_i(fdc0),.fdc_raw_ch1_i(fdc1),.tof_ready_i(configured[2:1]),.tof_fault_i(fault[2:1]),
            .tof_sample_valid_i(tof_valid),.tof_sample_error_i(tof_error),.tof_distance_mm_i(distance),
            .controls_valid_o(target_valid),.modulation_active_o(target_active),.source_o(target_source),
            .source_ready_o(ready),.source_fault_o(hard_fault),.stale_o(stale),
            .pitch_code_o(target_pitch),.pitch_delta_o(delta),.expression_gain_q16_o(gain),
            .normalized_pitch_o(unused_u_pitch),.normalized_volume_o(unused_u_volume),
            .sample_error_o(sample_error),.overrun_o(overrun),.expired_o(expired)
        );
        // Latest-value coalescing in front of an immutable CDC slot. Targets
        // may be replaced while busy; an in-flight bundle is never modified.
        logic [50:0] latest;
        logic pending;
        wire transfer_ready, received_valid;
        wire [50:0] received;
        always_ff @(posedge sys_clk_i or negedge sensor_rst_n) begin
            if (!sensor_rst_n) begin latest<={2'b00,14'd0,18'd0,17'd65536}; pending<=1; end
            else begin
                if (pending && transfer_ready) pending<=0;
                if (target_valid) begin latest<={target_active,target_source,target_pitch,delta,gain}; pending<=1; end
            end
        end
        held_bus_cdc #(.WIDTH(51)) u_modulation_cdc (
            .src_clk_i(sys_clk_i),.dst_clk_i(core_clk_i),.rst_n_i(rst_ni),
            .src_data_i(latest),.src_valid_i(pending),.src_ready_o(transfer_ready),
            .dst_data_o(received),.dst_valid_o(received_valid),.dst_ready_i(1'b1)
        );
        always_ff @(posedge core_clk_i or negedge modulation_rst_n) begin
            if (!modulation_rst_n) begin controls_valid_o<=0; active_o<=0; source_o<=0; pitch_code_o<=0; pitch_delta_o<=0; expression_gain_o<=65536; end
            else begin
                controls_valid_o<=received_valid;
                if (received_valid) {active_o,source_o,pitch_code_o,pitch_delta_o,expression_gain_o}<=received;
            end
        end
`else
        SENSOR_I2C_REQUIRES_SYNTH_SENSOR_I2C_PORTS_AND_CONFIRMED_CST invalid_sensor_pin_configuration ();
`endif
    end else begin : g_disabled
        // Stable capability/read response even when the entire sensor branch
        // is removed at elaboration. No automatically allocated package pins.
        assign controls_valid_o=0;
        assign active_o=0; assign source_o=0; assign pitch_code_o=0; assign pitch_delta_o=0; assign expression_gain_o=17'd65536;
        logic read_meta, read_sync, read_previous, read_pending, selected;
        logic [31:0] result;
        assign read_selected_o=selected; assign read_data_o=result;
        always_ff @(posedge core_clk_i or negedge rst_ni) begin
            if (!rst_ni) begin read_meta<=0; read_sync<=0; read_previous<=0; read_pending<=0; selected<=0; result<=0; end
            else begin
                read_meta<=read_toggle_i; read_sync<=read_meta; read_previous<=read_sync; read_pending<=read_sync!=read_previous;
                if (read_pending) begin
                    selected<=read_addr_i[15:5]==11'h03a;
                    result<=read_addr_i==16'h0740 ? 32'h534e5331 : read_addr_i==16'h0754 ? 32'h00010000 : 32'd0;
                end
            end
        end
`ifdef SYNTH_SENSOR_I2C_PORTS
        assign scl_io=1'bz; assign sda_io=1'bz; assign fdc_addr_o=0; assign fdc_sd_o=1;
        assign tof1_scl_io=1'bz; assign tof1_sda_io=1'bz; assign tof1_xshut_io=0;
        assign tof2_scl_io=1'bz; assign tof2_sda_io=1'bz; assign tof2_xshut_io=0;
`endif
    end endgenerate
endmodule
`default_nettype wire
