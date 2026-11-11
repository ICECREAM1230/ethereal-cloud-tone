`timescale 1ns/1ps
// SPDX-License-Identifier: MIT
// FDC2214 adaptation of cam2dvi/src/i2c_master/i2c_master_top.v.
// The reference byte/bit controllers are copied without changes; the camera's
// 16-bit pointer/8-bit payload sequencer is replaced by an 8-bit pointer and
// a big-endian 16-bit payload. Every transmitted byte checks ACK.
module fdc_cam_i2c_reg16_master #(
    parameter integer CLK_HZ = 50_000_000,
    parameter integer I2C_HZ = 12_500,
    parameter integer TRANSFER_TIMEOUT_CYCLES = CLK_HZ / 50,
    parameter logic [6:0] I2C_ADDR = 7'h2a
) (
    input wire clk_i, rst_ni,
    input wire req_valid_i,
    output wire req_ready_o,
    input wire req_read_i,
    input wire [7:0] req_register_i,
    input wire [15:0] req_wdata_i,
    output logic rsp_valid_o,
    output logic [15:0] rsp_rdata_o,
    output logic [1:0] rsp_error_o,
    input wire scl_i, sda_i,
    output wire scl_pad_o, sda_pad_o,
    output logic scl_padoen_o, sda_padoen_o
);
    // Matches the reference clk_div_cnt=1000 at 50 MHz. The byte controller
    // uses its own bit-state phases and input filtering; I2C_HZ is an upper bound,
    // not a claim that its waveform equals the old controller clock formula.
    localparam integer PRESCALE = (CLK_HZ + 4*I2C_HZ - 1) / (4*I2C_HZ);
    localparam integer RECOVERY_HOLD_CYCLES = 2*(PRESCALE+1)+4;
    typedef enum logic [4:0] {
        IDLE, DEVICE_WRITE, REGISTER_BYTE, WRITE_HIGH, WRITE_LOW,
        DEVICE_READ, READ_HIGH, READ_LOW, STOP, COMMAND_GAP,
        RECOVER_LOW, RECOVER_RISE, RECOVER_HIGH, RECOVER_IDLE, RESPONSE
    } state_t;
    state_t state, next_phase;
    logic read_q;
    logic [7:0] register_q;
    logic [15:0] write_q;
    integer unsigned transaction_cycles, recovery_cycles;
    (* ASYNC_REG = "TRUE" *) logic [1:0] scl_sync, sda_sync;
    always_ff @(posedge clk_i) begin
        if (!rst_ni) begin scl_sync <= 2'b11; sda_sync <= 2'b11; end
        else begin scl_sync <= {scl_sync[0],scl_i}; sda_sync <= {sda_sync[0],sda_i}; end
    end
    wire recovering = state==RECOVER_LOW || state==RECOVER_RISE ||
                      state==RECOVER_HIGH || state==RECOVER_IDLE;
    wire core_reset = !rst_ni || recovering;
    wire done, nack, arbitration_lost, unused_busy;
    wire [7:0] received;
    wire core_scl_oen, core_sda_oen;
    logic start, stop, read, write, ack_in;
    logic [7:0] tx_byte;
    assign req_ready_o = rst_ni && state==IDLE;
    i2c_master_byte_ctrl byte_master (
        .clk(clk_i),.rst(core_reset),.nReset(1'b1),.ena(1'b1),
        .clk_cnt(16'(PRESCALE)),.start(start),.stop(stop),.read(read),
        .write(write),.ack_in(ack_in),.din(tx_byte),.cmd_ack(done),
        .ack_out(nack),.i2c_busy(unused_busy),.i2c_al(arbitration_lost),
        .dout(received),.scl_i(scl_sync[1]),.scl_o(scl_pad_o),
        .scl_oen(core_scl_oen),.sda_i(sda_sync[1]),.sda_o(sda_pad_o),
        .sda_oen(core_sda_oen)
    );
    always_comb begin
        start=0; stop=0; read=0; write=0; ack_in=1; tx_byte=0;
        case (state)
            DEVICE_WRITE: begin start=1; write=1; tx_byte={I2C_ADDR,1'b0}; end
            REGISTER_BYTE: begin write=1; tx_byte=register_q; end
            WRITE_HIGH: begin write=1; tx_byte=write_q[15:8]; end
            WRITE_LOW: begin write=1; tx_byte=write_q[7:0]; end
            DEVICE_READ: begin start=1; write=1; tx_byte={I2C_ADDR,1'b1}; end
            READ_HIGH: begin read=1; ack_in=0; end
            READ_LOW: begin read=1; ack_in=1; end
            STOP: stop=1;
            default: begin end
        endcase
        if (!rst_ni || recovering) begin start=0; stop=0; read=0; write=0; end
    end
    // Same active-low OE / constant-low pad convention as i2c_config.v.
    // Bounded STOP recovery is retained for an interrupted/stuck transaction.
    always_comb begin
        scl_padoen_o=core_scl_oen; sda_padoen_o=core_sda_oen;
        case (state)
            RECOVER_LOW: begin scl_padoen_o=0; sda_padoen_o=0; end
            RECOVER_RISE,RECOVER_HIGH: begin scl_padoen_o=1; sda_padoen_o=0; end
            RECOVER_IDLE,RESPONSE: begin scl_padoen_o=1; sda_padoen_o=1; end
            default: begin end
        endcase
        if (!rst_ni) begin scl_padoen_o=1; sda_padoen_o=1; end
    end
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            state<=IDLE; next_phase<=IDLE; read_q<=0; register_q<=0; write_q<=0;
            transaction_cycles<=0; recovery_cycles<=0;
            rsp_valid_o<=0; rsp_rdata_o<=0; rsp_error_o<=0;
        end else begin
            rsp_valid_o<=0;
            if (state!=IDLE && state!=RESPONSE && !recovering)
                transaction_cycles<=transaction_cycles+1;
            if (state!=IDLE && state!=RESPONSE && !recovering &&
                (transaction_cycles>=TRANSFER_TIMEOUT_CYCLES-1 || arbitration_lost)) begin
                rsp_error_o<=2; recovery_cycles<=0; state<=RECOVER_LOW;
            end else begin
                case (state)
                    IDLE: if (req_valid_i) begin
                        read_q<=req_read_i; register_q<=req_register_i; write_q<=req_wdata_i;
                        rsp_rdata_o<=0; rsp_error_o<=0; transaction_cycles<=0;
                        state<=DEVICE_WRITE;
                    end
                    DEVICE_WRITE,REGISTER_BYTE,WRITE_HIGH,WRITE_LOW,DEVICE_READ:
                        if (done) begin
                            state<=COMMAND_GAP;
                            if (nack) begin rsp_error_o<=1; next_phase<=STOP; end
                            else case (state)
                                DEVICE_WRITE: next_phase<=REGISTER_BYTE;
                                REGISTER_BYTE: next_phase<=read_q ? DEVICE_READ : WRITE_HIGH;
                                WRITE_HIGH: next_phase<=WRITE_LOW;
                                WRITE_LOW: next_phase<=STOP;
                                default: next_phase<=READ_HIGH;
                            endcase
                        end
                    READ_HIGH: if (done) begin
                        rsp_rdata_o[15:8]<=received; next_phase<=READ_LOW; state<=COMMAND_GAP;
                    end
                    READ_LOW: if (done) begin
                        rsp_rdata_o[7:0]<=received; next_phase<=STOP; state<=COMMAND_GAP;
                    end
                    STOP: if (done) state<=RESPONSE;
                    COMMAND_GAP: state<=next_phase;
                    RECOVER_LOW: begin
                        if (recovery_cycles>=RECOVERY_HOLD_CYCLES-1) begin
                            recovery_cycles<=0; state<=RECOVER_RISE;
                        end else recovery_cycles<=recovery_cycles+1;
                    end
                    RECOVER_RISE: begin
                        if (scl_sync[1] || recovery_cycles>=RECOVERY_HOLD_CYCLES-1) begin
                            recovery_cycles<=0; state<=RECOVER_HIGH;
                        end else recovery_cycles<=recovery_cycles+1;
                    end
                    RECOVER_HIGH: begin
                        if (recovery_cycles>=RECOVERY_HOLD_CYCLES-1) begin
                            recovery_cycles<=0; state<=RECOVER_IDLE;
                        end else recovery_cycles<=recovery_cycles+1;
                    end
                    RECOVER_IDLE: begin
                        if (recovery_cycles>=RECOVERY_HOLD_CYCLES-1) state<=RESPONSE;
                        else recovery_cycles<=recovery_cycles+1;
                    end
                    RESPONSE: begin rsp_valid_o<=1; state<=IDLE; end
                    default: state<=IDLE;
                endcase
            end
        end
    end
`ifndef SYNTHESIS
    initial begin
        if (PRESCALE<1 || PRESCALE>65535 || TRANSFER_TIMEOUT_CYCLES<1)
            $fatal(1,"Invalid cam2dvi IIC divider/timeout");
    end
`endif
endmodule
