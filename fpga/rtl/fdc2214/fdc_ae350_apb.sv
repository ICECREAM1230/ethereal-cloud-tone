// SPDX-License-Identifier: MIT
`timescale 1ns/1ps
`default_nettype none
// Optional FDC mailbox. The CPU reads an immutable raw snapshot; no sensor
// filtering/mapping or PCM generation lives here. Outputs are only a contract
// for the separately integrated sensor voice, never a global audio mute.
module fdc_ae350_apb #(
    parameter bit ENABLE = 1'b1,
    parameter [31:0] BASE = 32'hf8010100,
    parameter integer CLK_HZ = 50_000_000,
    parameter integer WATCHDOG_US = 500_000
) (
    input wire sys_clk_i, pclk_i, rst_ni,
    input wire [31:0] paddr_i, pwdata_i,
    input wire [3:0] pstrb_i,
    input wire psel_i, penable_i, pwrite_i,
    output logic [31:0] prdata_o,
    output wire pready_o,
    output logic pslverr_o,
    input wire frame_complete_i,
    input wire [27:0] raw0_i, raw1_i,
    input wire [1:0] flags0_i, flags1_i,
    input wire [15:0] status_i,
    input wire configured_i, fault_i,
    input wire [3:0] fault_code_i,
    input wire transport_error_i,
    output wire restart_o,
    output wire ctrl_valid_o, ctrl_alive_o, ctrl_active_o, ctrl_timed_out_o,
    output wire [31:0] ctrl_epoch_o, ctrl_sequence_o,
    output wire [13:0] ctrl_pitch_o,
    output wire [16:0] ctrl_gain_o,
    output wire [31:0] cpu_status_o,
    output wire [127:0] cpu_telemetry_o
);
    localparam integer US_CYCLES = CLK_HZ / 1_000_000;
    localparam integer UW = US_CYCLES > 1 ? $clog2(US_CYCLES) : 1;
    wire [31:0] offset = paddr_i - BASE;
    wire address_ok = paddr_i >= BASE && paddr_i < BASE + 32'h80 && paddr_i[1:0] == 0;
    wire access = psel_i && penable_i;
    (* ASYNC_REG = "TRUE" *) logic [1:0] pclk_release;
    always_ff @(posedge pclk_i or negedge rst_ni) begin
        if (!rst_ni) pclk_release <= 0;
        else pclk_release <= {pclk_release[0],1'b1};
    end
    wire pclk_reset_n = pclk_release[1];
    assign pready_o = pclk_reset_n;
    // Side effects occur at most once even if a caller erroneously holds an
    // already completed ACCESS. The enclosing production pipeline is unmodified.
    logic access_done, completed_error;
    wire first_access = access && !access_done && pclk_reset_n;
    logic access_error;
    always_ff @(posedge pclk_i or negedge pclk_reset_n) begin
        if (!pclk_reset_n) begin access_done <= 0; completed_error <= 0; end
        else if (!access) access_done <= 0;
        else if (!access_done) begin access_done <= 1; completed_error <= access_error; end
    end
    always_comb pslverr_o = access && (access_done ? completed_error : access_error);

    initial begin
        if (CLK_HZ < 1_000_000 || CLK_HZ % 1_000_000 != 0 || WATCHDOG_US < 1)
            $fatal(1,"FDC mailbox requires integer microsecond clock and positive watchdog");
        if (BASE[6:0] != 0) $fatal(1,"FDC mailbox BASE must be 128-byte aligned");
    end
    typedef struct packed {
        logic [63:0] freeze_us;
        logic [31:0] present, errors, overwritten, diagnostic, quality, raw1, raw0;
        logic [63:0] capture_us;
        logic [31:0] serial, epoch;
    } snapshot_t;
    typedef struct packed {
        logic active;
        logic [16:0] gain;
        logic [13:0] pitch;
        logic [31:0] serial, epoch;
    } control_t;
    // Explicit packed formats are local RTL only. Firmware uses the documented
    // words, never memcpy of a C structure with implementation-dependent padding.
    generate if (ENABLE) begin : g_enabled
        logic [31:0] cpu_status;
        logic [95:0] telemetry_stage;
        logic [2:0] telemetry_written;
        logic [127:0] cpu_telemetry;
        logic [31:0] telemetry_serial;
        assign cpu_telemetry_o=cpu_telemetry;
        assign cpu_status_o = cpu_status;
        (* ASYNC_REG = "TRUE" *) logic [1:0] sys_release;
        always_ff @(posedge sys_clk_i or negedge rst_ni) begin
            if (!rst_ni) sys_release <= 0;
            else sys_release <= {sys_release[0],1'b1};
        end
        wire sys_reset_n = sys_release[1];
        logic [UW-1:0] sub_us;
        logic [63:0] now_us;
        logic [31:0] epoch, frame_seq, overwritten, error_count;
        logic [27:0] latest_raw0, latest_raw1;
        logic [1:0] latest_flags0, latest_flags1;
        logic [15:0] latest_status;
        logic [63:0] latest_us;
        logic latest_present, latest_unseen, identity_fault;
        logic restart_pulse;
        logic [31:0] accepted_sequence, accepted_epoch;
        logic [13:0] accepted_pitch;
        logic [16:0] accepted_gain;
        logic accepted_active, alive, timed_out, accepted_pulse;
        logic [63:0] accepted_at_us;
        assign restart_o = restart_pulse;
        assign ctrl_valid_o = accepted_pulse;
        assign ctrl_epoch_o = accepted_epoch;
        assign ctrl_sequence_o = accepted_sequence;
        assign ctrl_pitch_o = accepted_pitch;
        assign ctrl_alive_o = alive;
        assign ctrl_active_o = alive && accepted_active;
        assign ctrl_timed_out_o = timed_out;
        assign ctrl_gain_o = alive && accepted_active ? accepted_gain : 17'd0;

        logic command_send, command_kind, command_busy, command_is_restart;
        wire command_src_ready, command_valid_sys, command_ready_sys;
        wire command_kind_sys;
        held_bus_cdc #(.WIDTH(1)) u_command (
            .src_clk_i(pclk_i),.dst_clk_i(sys_clk_i),.rst_n_i(rst_ni),
            .src_data_i(command_kind),.src_valid_i(command_send),.src_ready_o(command_src_ready),
            .dst_data_o(command_kind_sys),.dst_valid_o(command_valid_sys),.dst_ready_i(command_ready_sys));
        snapshot_t source_snapshot, received_snapshot, snapshot;
        logic snapshot_send;
        wire snapshot_src_ready, snapshot_received;
        held_bus_cdc #(.WIDTH($bits(snapshot_t))) u_snapshot (
            .src_clk_i(sys_clk_i),.dst_clk_i(pclk_i),.rst_n_i(rst_ni),
            .src_data_i(source_snapshot),.src_valid_i(snapshot_send),.src_ready_o(snapshot_src_ready),
            .dst_data_o(received_snapshot),.dst_valid_o(snapshot_received),.dst_ready_i(1'b1));
        assign command_ready_sys = !snapshot_send && snapshot_src_ready;
        wire command_fire_sys = command_valid_sys && command_ready_sys;
        wire snapshot_fire_sys = command_fire_sys && !command_kind_sys;
        wire restart_fire_sys = command_fire_sys && command_kind_sys;

        logic control_send, control_busy;
        control_t control_shadow, control_tx, control_sys;
        logic [4:0] shadow_written;
        wire control_src_ready, control_valid_sys, control_ready_sys;
        held_bus_cdc #(.WIDTH($bits(control_t))) u_control (
            .src_clk_i(pclk_i),.dst_clk_i(sys_clk_i),.rst_n_i(rst_ni),
            .src_data_i(control_tx),.src_valid_i(control_send),.src_ready_o(control_src_ready),
            .dst_data_o(control_sys),.dst_valid_o(control_valid_sys),.dst_ready_i(control_ready_sys));
        logic control_response_send;
        logic [64:0] control_response;
        wire control_response_ready, control_response_valid;
        wire [64:0] control_response_pclk;
        held_bus_cdc #(.WIDTH(65)) u_control_response (
            .src_clk_i(sys_clk_i),.dst_clk_i(pclk_i),.rst_n_i(rst_ni),
            .src_data_i(control_response),.src_valid_i(control_response_send),.src_ready_o(control_response_ready),
            .dst_data_o(control_response_pclk),.dst_valid_o(control_response_valid),.dst_ready_i(1'b1));
        assign control_ready_sys = !control_response_send && control_response_ready;
        wire control_fire_sys = control_valid_sys && control_ready_sys;
        wire control_accept_sys = control_fire_sys && !restart_fire_sys && !identity_fault &&
            control_sys.epoch == epoch && control_sys.serial != 0 &&
            control_sys.serial > accepted_sequence && control_sys.gain <= 17'd65536;

        // Status observation is another held bundle, not bitwise synchronizers
        // on a multibit epoch/ACK. Older status from before a command response
        // cannot lower the destination's epoch or accepted serial.
        wire [66:0] status_source = {epoch,accepted_sequence,timed_out,alive && accepted_active,alive};
        logic [66:0] status_sent;
        wire status_src_ready, status_received;
        wire [66:0] status_pclk;
        wire status_send = status_source != status_sent;
        held_bus_cdc #(.WIDTH(67)) u_status (
            .src_clk_i(sys_clk_i),.dst_clk_i(pclk_i),.rst_n_i(rst_ni),
            .src_data_i(status_source),.src_valid_i(status_send),.src_ready_o(status_src_ready),
            .dst_data_o(status_pclk),.dst_valid_o(status_received),.dst_ready_i(1'b1));
        always_ff @(posedge sys_clk_i or negedge sys_reset_n) begin
            if (!sys_reset_n) status_sent <= 0;
            else if (status_send && status_src_ready) status_sent <= status_source;
        end
        logic [31:0] observed_epoch, observed_ack;
        logic [2:0] observed_status;
        logic control_rejected, snapshot_valid;
        wire restart_pending = command_busy && command_is_restart;
        wire any_control_busy = control_busy || !control_src_ready;
        wire shadow_legal = &shadow_written && control_shadow.epoch == observed_epoch &&
            control_shadow.epoch != 0 && control_shadow.serial != 0 &&
            control_shadow.serial > observed_ack && control_shadow.gain <= 17'd65536;
        wire write_fire = first_access && pwrite_i && !access_error;
        wire snapshot_address = offset >= 32'h14 && offset <= 32'h44;
        logic read_known, write_known, write_legal;
        always_comb begin
            read_known = 1;
            write_known = 0;
            write_legal = 0;
            prdata_o = 0;
            case (offset)
                32'h00: prdata_o = 32'h46444331;
                32'h04: prdata_o = 32'h00010000;
                32'h08: prdata_o = 7;
                32'h0c: begin
                    // COMMAND is write-only.
                    read_known = 0; write_known = 1;
                    if (pwdata_i == 1) write_legal = !command_busy && !snapshot_valid && command_src_ready;
                    else if (pwdata_i == 2) write_legal = snapshot_valid && !command_busy;
                    else if (pwdata_i == 4) write_legal = !command_busy && !any_control_busy && command_src_ready;
                end
                32'h10: prdata_o = {28'd0,any_control_busy,restart_pending,command_busy && !command_is_restart,snapshot_valid};
                32'h14: prdata_o = snapshot.epoch;
                32'h18: prdata_o = snapshot.serial;
                32'h1c: prdata_o = snapshot.capture_us[31:0];
                32'h20: prdata_o = snapshot.capture_us[63:32];
                32'h24: prdata_o = snapshot.raw0;
                32'h28: prdata_o = snapshot.raw1;
                32'h2c: prdata_o = snapshot.quality;
                32'h30: prdata_o = snapshot.diagnostic;
                32'h34: prdata_o = snapshot.overwritten;
                32'h38: prdata_o = snapshot.errors;
                32'h3c: prdata_o = snapshot.present;
                32'h40: prdata_o = snapshot.freeze_us[31:0];
                32'h44: prdata_o = snapshot.freeze_us[63:32];
                32'h48: prdata_o = observed_epoch;
                32'h50: begin prdata_o = control_shadow.epoch;write_known = 1;write_legal = !any_control_busy && !restart_pending;end
                32'h54: begin prdata_o = control_shadow.serial;write_known = 1;write_legal = !any_control_busy && !restart_pending;end
                32'h58: begin prdata_o = {18'd0,control_shadow.pitch};write_known = 1;write_legal = !any_control_busy && !restart_pending && pwdata_i[31:14] == 0;end
                32'h5c: begin prdata_o = {15'd0,control_shadow.gain};write_known = 1;write_legal = !any_control_busy && !restart_pending && pwdata_i <= 65536;end
                32'h60: begin prdata_o = {31'd0,control_shadow.active};write_known = 1;write_legal = !any_control_busy && !restart_pending && pwdata_i[31:1] == 0;end
                32'h64: begin read_known = 0;write_known = 1;write_legal = pwdata_i == 1 && !any_control_busy && !restart_pending && shadow_legal;end
                32'h68: prdata_o = observed_ack;
                32'h6c: prdata_o = {28'd0,control_rejected,observed_status};
                32'h70: begin prdata_o=cpu_status;write_known=1;write_legal=1;end
                32'h74: begin prdata_o=telemetry_stage[0+:32];write_known=1;write_legal=1;end
                32'h78: begin prdata_o=telemetry_stage[32+:32];write_known=1;write_legal=1;end
                32'h7c: begin prdata_o=telemetry_stage[64+:32];write_known=1;
                    write_legal=pwdata_i[16:0]<=17'd65536 && pwdata_i[23:21]==0;end
                default: read_known = 0;
            endcase
            access_error = !address_ok || (pwrite_i ?
                (!write_known || !write_legal || pstrb_i != 4'hf) :
                (!read_known || (snapshot_address && !snapshot_valid)));
        end

        always_ff @(posedge pclk_i or negedge pclk_reset_n) begin
            if (!pclk_reset_n) begin
                cpu_status <= 0;telemetry_stage<=0;telemetry_written<=0;cpu_telemetry<=0;telemetry_serial<=0;
                command_send <= 0;command_kind <= 0;command_busy <= 0;command_is_restart <= 0;
                control_send <= 0;control_busy <= 0;control_shadow <= '0;control_tx <= '0;shadow_written <= 0;
                snapshot <= '0;snapshot_valid <= 0;observed_epoch <= 0;observed_ack <= 0;observed_status <= 0;control_rejected <= 0;
            end else begin
                command_send <= 0;control_send <= 0;
                if (status_received && (status_pclk[66:35] > observed_epoch ||
                    (status_pclk[66:35] == observed_epoch && status_pclk[34:3] >= observed_ack))) begin
                    if (status_pclk[66:35] > observed_epoch) begin
                        observed_epoch <= status_pclk[66:35];observed_ack <= status_pclk[34:3];
                    end else if (status_pclk[34:3] >= observed_ack) observed_ack <= status_pclk[34:3];
                    observed_status <= status_pclk[2:0];
                end
                if (snapshot_received && command_busy) begin
                    command_busy <= 0;
                    if (command_is_restart) begin
                        snapshot_valid <= 0;control_rejected <= 0;shadow_written <= 0;
                        if (received_snapshot.epoch >= observed_epoch) begin
                            observed_epoch <= received_snapshot.epoch;observed_ack <= 0;observed_status <= 0;
                        end
                    end else begin snapshot <= received_snapshot;snapshot_valid <= 1; end
                end
                if (control_response_valid && control_busy) begin
                    control_busy <= 0;control_rejected <= !control_response_pclk[64];
                    if (control_response_pclk[64] && control_response_pclk[63:32] >= observed_epoch) begin
                        observed_epoch <= control_response_pclk[63:32];observed_ack <= control_response_pclk[31:0];
                    end
                end
                if (write_fire) case (offset)
                    32'h0c: if (pwdata_i == 2) snapshot_valid <= 0;
                        else begin
                            command_kind <= pwdata_i == 4;command_is_restart <= pwdata_i == 4;
                            command_send <= 1;command_busy <= 1;
                            if (pwdata_i == 4) begin snapshot_valid <= 0;shadow_written <= 0;end
                        end
                    32'h70: begin
                        cpu_status<=pwdata_i;
                        if(&telemetry_written)begin
                            cpu_telemetry<={telemetry_stage,telemetry_serial+32'd1};
                            telemetry_serial<=telemetry_serial+32'd1;
                        end
                        telemetry_written<=0;
                    end
                    32'h74: begin telemetry_stage[0+:32]<=pwdata_i;telemetry_written[0]<=1;end
                    32'h78: begin telemetry_stage[32+:32]<=pwdata_i;telemetry_written[1]<=1;end
                    32'h7c: begin telemetry_stage[64+:32]<=pwdata_i;telemetry_written[2]<=1;end
                    32'h50: begin control_shadow.epoch <= pwdata_i;shadow_written[0] <= 1;end
                    32'h54: begin control_shadow.serial <= pwdata_i;shadow_written[1] <= 1;end
                    32'h58: begin control_shadow.pitch <= pwdata_i[13:0];shadow_written[2] <= 1;end
                    32'h5c: begin control_shadow.gain <= pwdata_i[16:0];shadow_written[3] <= 1;end
                    32'h60: begin control_shadow.active <= pwdata_i[0];shadow_written[4] <= 1;end
                    32'h64: begin control_tx <= control_shadow;control_send <= 1;control_busy <= 1;shadow_written <= 0;end
                    default: ;
                endcase
            end
        end

        always_ff @(posedge sys_clk_i or negedge sys_reset_n) begin
            if (!sys_reset_n) begin
                sub_us <= 0;now_us <= 0;epoch <= 1;frame_seq <= 0;overwritten <= 0;error_count <= 0;
                latest_raw0 <= 0;latest_raw1 <= 0;latest_flags0 <= 0;latest_flags1 <= 0;latest_status <= 0;latest_us <= 0;
                latest_present <= 0;latest_unseen <= 0;identity_fault <= 0;
                restart_pulse <= 0;snapshot_send <= 0;source_snapshot <= '0;
                accepted_sequence <= 0;accepted_epoch <= 0;accepted_pitch <= 0;accepted_gain <= 0;
                accepted_active <= 0;alive <= 0;timed_out <= 0;accepted_pulse <= 0;accepted_at_us <= 0;
                control_response_send <= 0;control_response <= 0;
            end else begin
                if (sub_us == UW'(US_CYCLES-1)) begin sub_us <= 0;now_us <= now_us + 1'b1;end
                else sub_us <= sub_us + 1'b1;
                restart_pulse <= 0;accepted_pulse <= 0;
                if (snapshot_send && snapshot_src_ready) snapshot_send <= 0;
                if (control_response_send && control_response_ready) control_response_send <= 0;
                if (alive && now_us - accepted_at_us >= 64'(WATCHDOG_US)) begin alive <= 0;timed_out <= 1;end
                if (transport_error_i && error_count != 32'hffffffff) error_count <= error_count + 1'b1;
                if (snapshot_fire_sys) latest_unseen <= 0;
                if (frame_complete_i && !identity_fault) begin
                    if (frame_seq == 32'hffffffff) identity_fault <= 1;
                    else begin
                        frame_seq <= frame_seq + 1'b1;
                        latest_raw0 <= raw0_i;latest_raw1 <= raw1_i;
                        latest_flags0 <= flags0_i;latest_flags1 <= flags1_i;latest_status <= status_i;
                        latest_us <= now_us;latest_present <= 1;latest_unseen <= 1;
                        if (latest_unseen && !snapshot_fire_sys && overwritten != 32'hffffffff)
                            overwritten <= overwritten + 1'b1;
                    end
                end
                if (command_fire_sys) begin
                    source_snapshot.epoch <= epoch;
                    source_snapshot.serial <= frame_seq;
                    source_snapshot.capture_us <= latest_us;
                    source_snapshot.raw0 <= {4'd0,latest_raw0};source_snapshot.raw1 <= {4'd0,latest_raw1};
                    source_snapshot.quality <= {12'd0,latest_flags1,latest_flags0,latest_status};
                    source_snapshot.diagnostic <= {24'd0,(identity_fault ? 4'hf : fault_code_i),2'd0,(fault_i || identity_fault),configured_i};
                    source_snapshot.overwritten <= overwritten;source_snapshot.errors <= error_count;
                    source_snapshot.present <= {31'd0,latest_present};source_snapshot.freeze_us <= now_us;snapshot_send <= 1;
                    if (command_kind_sys) begin
                        // Explicit recovery creates a new session; no old held
                        // control or raw identity is valid in the new epoch.
                        restart_pulse <= 1;latest_present <= 0;latest_unseen <= 0;frame_seq <= 0;
                        overwritten <= 0;error_count <= 0;accepted_sequence <= 0;accepted_epoch <= 0;
                        alive <= 0;accepted_active <= 0;accepted_gain <= 0;timed_out <= 0;
                        if (epoch != 32'hffffffff) begin epoch <= epoch + 1'b1;identity_fault <= 0;end
                        else identity_fault <= 1;
                        source_snapshot.epoch <= epoch == 32'hffffffff ? epoch : epoch + 1'b1;
                        source_snapshot.serial <= 0;source_snapshot.present <= 0;
                        source_snapshot.raw0 <= 0;source_snapshot.raw1 <= 0;source_snapshot.quality <= 0;
                    end
                end
                if (control_fire_sys) begin
                    control_response <= {control_accept_sys,epoch,control_sys.serial};control_response_send <= 1;
                    if (control_accept_sys) begin
                        accepted_epoch <= epoch;accepted_sequence <= control_sys.serial;
                        accepted_pitch <= control_sys.pitch;accepted_gain <= control_sys.gain;accepted_active <= control_sys.active;
                        accepted_at_us <= now_us;alive <= 1;timed_out <= 0;accepted_pulse <= 1;
                    end
                end
            end
        end
    end else begin : g_disabled
        assign cpu_status_o = 0;
        assign cpu_telemetry_o = 0;
        assign restart_o = 0;assign ctrl_valid_o = 0;assign ctrl_alive_o = 0;assign ctrl_active_o = 0;
        assign ctrl_timed_out_o = 0;assign ctrl_epoch_o = 0;assign ctrl_sequence_o = 0;assign ctrl_pitch_o = 0;assign ctrl_gain_o = 0;
        always_comb begin
            prdata_o = 0;
            case (offset)
                32'h00: prdata_o = 32'h46444331;
                32'h04: prdata_o = 32'h00010000;
                default: ;
            endcase
            access_error = !address_ok || pwrite_i ||
                !(offset == 0 || offset == 4 || offset == 8 || offset == 16 || offset == 32'h48 || offset == 32'h68 || offset == 32'h6c || offset == 32'h70);
        end
    end endgenerate
endmodule
`default_nettype wire
