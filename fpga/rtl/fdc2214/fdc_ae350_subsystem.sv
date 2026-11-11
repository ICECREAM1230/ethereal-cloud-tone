// SPDX-License-Identifier: MIT
`timescale 1ns/1ps
`default_nettype none
// Optional FDC-only acquisition + processor mailbox. This has no native voice
// writer: control outputs are diagnostic contracts until the CPU/native owner
// explicitly binds and applies them. Existing PCM and MIDI owners are separate.
module fdc_ae350_subsystem #(
    parameter bit ENABLE = 1'b0,
    parameter [31:0] APB_BASE = 32'hf8010100,
    parameter integer CLK_HZ = 50_000_000,
    parameter integer I2C_HZ = 12_500,
    parameter integer POWERUP_CYCLES = CLK_HZ / 500,
    parameter integer RESET_HOLD_CYCLES = CLK_HZ / 500,
    parameter integer CONFIG_DELAY_CYCLES = (CLK_HZ / 1000) * 60,
    parameter integer POLL_CYCLES = CLK_HZ / 10_000,
    parameter integer TRANSFER_TIMEOUT_CYCLES = CLK_HZ / 50,
    parameter integer DATA_TIMEOUT_CYCLES = CLK_HZ / 10,
    parameter integer WATCHDOG_US = 500_000,
    parameter logic [6:0] I2C_ADDR = 7'h2a,
    parameter logic [15:0] RCOUNT_CH0 = 16'h1000,
    parameter logic [15:0] RCOUNT_CH1 = 16'h1000,
    parameter logic [15:0] SETTLECOUNT_CH0 = 16'h0100,
    parameter logic [15:0] SETTLECOUNT_CH1 = 16'h0100,
    parameter logic [15:0] CLOCK_DIVIDERS_CH0 = 16'h2001,
    parameter logic [15:0] CLOCK_DIVIDERS_CH1 = 16'h2001,
    parameter logic [4:0] IDRIVE_CH0 = 5'd15,
    parameter logic [4:0] IDRIVE_CH1 = 5'd15,
    parameter bit USE_EXTERNAL_CLOCK = 1'b1,
    parameter bit LOW_POWER_ACTIVATION = 1'b0
) (
    input wire sys_clk_i, pclk_i, rst_ni,
    input wire [31:0] paddr_i, pwdata_i,
    input wire [3:0] pstrb_i,
    input wire psel_i, penable_i, pwrite_i,
    output wire [31:0] prdata_o,
    output wire pready_o, pslverr_o,
    inout wire fdc_scl_io, fdc_sda_io,
    output wire fdc_addr_o, fdc_sd_o,
    input wire fdc_int_ni,
    output wire ctrl_valid_o, ctrl_alive_o, ctrl_active_o, ctrl_timed_out_o,
    output wire [31:0] ctrl_epoch_o, ctrl_sequence_o,
    output wire [13:0] ctrl_pitch_o,
    output wire [16:0] ctrl_gain_o,
    output wire [31:0] cpu_status_o,
    output wire [127:0] cpu_telemetry_o
);
    wire acquisition_reset_n;
    generate if (ENABLE) begin : g_acquisition_reset
        jt51_reset_sync u_release(.clk_i(sys_clk_i),.rst_n_i(rst_ni),.rst_n_o(acquisition_reset_n));
    end else begin : g_no_acquisition_reset
        assign acquisition_reset_n = rst_ni;
    end endgenerate
    wire restart, frame_complete, transport_error, configured, fault;
    wire [27:0] raw0, raw1;
    wire [1:0] flags0, flags1;
    wire [15:0] sensor_status;
    wire [3:0] fault_code;
    // Mailbox restart is registered in SYS. The acquisition device consumes
    // it on the following SYS edge; suppress any old-session completion/error
    // still visible on that edge instead of publishing it under the new epoch.
    wire publish_frame = acquisition_reset_n && !restart && frame_complete;
    wire publish_transport_error = acquisition_reset_n && !restart && transport_error;
    fdc_ae350_acquisition #(
        .ENABLE(ENABLE),.CLK_HZ(CLK_HZ),.I2C_HZ(I2C_HZ),
        .POWERUP_CYCLES(POWERUP_CYCLES),.RESET_HOLD_CYCLES(RESET_HOLD_CYCLES),
        .CONFIG_DELAY_CYCLES(CONFIG_DELAY_CYCLES),.POLL_CYCLES(POLL_CYCLES),
        .TRANSFER_TIMEOUT_CYCLES(TRANSFER_TIMEOUT_CYCLES),.DATA_TIMEOUT_CYCLES(DATA_TIMEOUT_CYCLES),
        .I2C_ADDR(I2C_ADDR),.RCOUNT_CH0(RCOUNT_CH0),.RCOUNT_CH1(RCOUNT_CH1),
        .SETTLECOUNT_CH0(SETTLECOUNT_CH0),.SETTLECOUNT_CH1(SETTLECOUNT_CH1),
        .CLOCK_DIVIDERS_CH0(CLOCK_DIVIDERS_CH0),.CLOCK_DIVIDERS_CH1(CLOCK_DIVIDERS_CH1),
        .IDRIVE_CH0(IDRIVE_CH0),.IDRIVE_CH1(IDRIVE_CH1),
        .USE_EXTERNAL_CLOCK(USE_EXTERNAL_CLOCK),.LOW_POWER_ACTIVATION(LOW_POWER_ACTIVATION)
    ) u_acquisition (
        .clk_i(sys_clk_i),.rst_ni(acquisition_reset_n),.restart_i(restart),
        .scl_io(fdc_scl_io),.sda_io(fdc_sda_io),.fdc_addr_o(fdc_addr_o),.fdc_sd_o(fdc_sd_o),.fdc_int_ni(fdc_int_ni),
        .fdc_int_active_o(),.scl_drive_low_o(),.sda_drive_low_o(),
        .configured_o(configured),.fault_o(fault),.fault_code_o(fault_code),
        .raw_ch0_o(raw0),.raw_ch1_o(raw1),.flags_ch0_o(flags0),.flags_ch1_o(flags1),.status_o(sensor_status),
        .frame_complete_o(frame_complete),.transport_error_o(transport_error),.transport_error_code_o(),
        .sample_valid_o(),.sample_error_o(),.sample_count_o());
    fdc_ae350_apb #(.ENABLE(ENABLE),.BASE(APB_BASE),.CLK_HZ(CLK_HZ),.WATCHDOG_US(WATCHDOG_US)) u_mailbox (
        .sys_clk_i(sys_clk_i),.pclk_i(pclk_i),.rst_ni(rst_ni),
        .paddr_i(paddr_i),.pwdata_i(pwdata_i),.pstrb_i(pstrb_i),.psel_i(psel_i),.penable_i(penable_i),.pwrite_i(pwrite_i),
        .prdata_o(prdata_o),.pready_o(pready_o),.pslverr_o(pslverr_o),
        .frame_complete_i(publish_frame),.raw0_i(raw0),.raw1_i(raw1),.flags0_i(flags0),.flags1_i(flags1),.status_i(sensor_status),
        .configured_i(configured),.fault_i(fault),.fault_code_i(fault_code),.transport_error_i(publish_transport_error),
        .restart_o(restart),.ctrl_valid_o(ctrl_valid_o),.ctrl_alive_o(ctrl_alive_o),.ctrl_active_o(ctrl_active_o),.ctrl_timed_out_o(ctrl_timed_out_o),
        .ctrl_epoch_o(ctrl_epoch_o),.ctrl_sequence_o(ctrl_sequence_o),.ctrl_pitch_o(ctrl_pitch_o),.ctrl_gain_o(ctrl_gain_o),.cpu_status_o(cpu_status_o),.cpu_telemetry_o(cpu_telemetry_o));
endmodule
`default_nettype wire
