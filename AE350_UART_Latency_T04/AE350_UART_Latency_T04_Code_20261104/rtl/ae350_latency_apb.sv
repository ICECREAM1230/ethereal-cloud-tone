`timescale 1ns/1ps
`default_nettype none
// Small CPU-owned measurement window. The capture mailbox owns result_i and
// keeps every word immutable until the single successful POP transfer.
// UART accepts one byte without waiting; formatting and scheduling live in C.
module ae350_latency_apb #(
    parameter integer BAUD=200_000, CLK_HZ=50_000_000
)(
    input wire pclk_i,preset_ni,
    input wire [31:0] paddr_i,pwdata_i,
    input wire [3:0] pstrb_i,
    input wire psel_i,penable_i,pwrite_i,
    output logic [31:0] prdata_o,
    output wire pready_o,
    output logic pslverr_o,
    input wire [575:0] result_i,
    input wire result_valid_i,
    output wire result_ready_o,
    input wire loss_i,
    input wire uart_clk_i,uart_rst_ni,
    output wire tx_o
);
    wire access=preset_ni && psel_i && penable_i;
    wire address_legal=paddr_i[31:8]==24'hf80101 && paddr_i[1:0]==2'd0;
    wire [7:0] offset=paddr_i[7:0];
    wire uart_ready,uart_valid,uart_byte_ready;
    wire [7:0] uart_byte;
    wire write_ok=access && pwrite_i && !pslverr_o;
    wire uart_write=write_ok && offset==8'h0c;

    assign pready_o=1'b1;
    assign result_ready_o=write_ok && offset==8'h08;
    always_comb begin
        prdata_o=32'd0;pslverr_o=1'b0;
        if(access)begin
            if(!address_legal)pslverr_o=1'b1;
            else if(pwrite_i)begin
                case(offset)
                    8'h08:pslverr_o=pstrb_i!=4'hf || pwdata_i!=32'd1 || !result_valid_i;
                    8'h0c:pslverr_o=pstrb_i!=4'hf || |pwdata_i[31:8] || !uart_ready;
                    default:pslverr_o=1'b1;
                endcase
            end else begin
                case(offset)
                    8'h00:prdata_o=32'h4c543034;
                    8'h04:prdata_o={29'd0,loss_i,uart_ready,result_valid_i};
                    8'h10:prdata_o=32'd48_000_000;
                    default:begin
                        if(offset>=8'h20 && offset<=8'h64)begin
                            prdata_o=result_i[(int'(offset[6:2])-8)*32+:32];
                            pslverr_o=!result_valid_i;
                        end else pslverr_o=1'b1;
                    end
                endcase
            end
        end
    end

    held_bus_cdc #(.WIDTH(8)) u_uart_cdc(
        .src_clk_i(pclk_i),.dst_clk_i(uart_clk_i),.rst_n_i(preset_ni && uart_rst_ni),
        .src_data_i(pwdata_i[7:0]),.src_valid_i(uart_write),.src_ready_o(uart_ready),
        .dst_data_o(uart_byte),.dst_valid_o(uart_valid),.dst_ready_i(uart_byte_ready));
    midi_diag_tx #(.CLK_HZ(CLK_HZ),.BAUD(BAUD)) u_tx(
        .clk(uart_clk_i),.rstn(preset_ni && uart_rst_ni),.data(uart_byte),
        .valid(uart_valid),.ready(uart_byte_ready),.tx(tx_o));
`ifndef SYNTHESIS
    initial begin
        if(CLK_HZ<1 || BAUD<1 || CLK_HZ%BAUD!=0 || CLK_HZ/BAUD<1)
            $fatal(1,"Invalid CPU latency UART parameters");
    end
`endif
endmodule
`default_nettype wire
