`timescale 1ns/1ps
`default_nettype none
// Diagnostic-only UART. Inputs are coherent, already synchronized snapshots.
// Busy report ticks coalesce into the latest pending report; they never alter
// the active line. Positive parameters and CLK_HZ >= BAUD are required.
module lab_debug_uart #(
    parameter integer CLK_HZ=50_000_000,
    parameter integer BAUD=115_200,
    parameter integer REPORT_CYCLES=50_000_000
) (
    input wire clk_i,rst_ni,
    input wire [31:0] status_i,boot_stage_i,apb_count_i,last_addr_i,
    input wire [31:0] last_wdata_i,last_rdata_i,trap_cause_i,trap_pc_i,trap_value_i,
    output wire tx_o
);
    localparam integer RW=REPORT_CYCLES>1 ? $clog2(REPORT_CYCLES) : 1;
    localparam integer LINE_BYTES=105;
    wire [287:0] sample_i={status_i,boot_stage_i,apb_count_i,last_addr_i,
        last_wdata_i,last_rdata_i,trap_cause_i,trap_pc_i,trap_value_i};
    logic [287:0] active_sample,pending_sample;
    logic pending_valid,line_active,last_queued;
    logic [RW-1:0] report_count;
    logic [6:0] byte_index;
    wire report_due=report_count==RW'(REPORT_CYCLES-1);
    wire byte_ready,uart_tx;
    wire byte_valid=rst_ni && line_active && !last_queued;
    wire line_done=line_active && last_queued && byte_ready;
    wire [7:0] byte_data=line_byte(active_sample,byte_index);
    // The exact main-project sender is copied unchanged under vendor/debug_uart.
    // This wrapper only formats coherent byte reports; it owns no bit timing.
    midi_diag_tx #(.CLK_HZ(CLK_HZ),.BAUD(BAUD)) u_tx(
        .clk(clk_i),.rstn(rst_ni),.data(byte_data),.valid(byte_valid),.ready(byte_ready),.tx(uart_tx));
    assign tx_o=rst_ni ? uart_tx : 1'b1;
    function automatic [7:0] hex_digit(input [3:0] nibble);
        hex_digit=nibble<10 ? 8'h30+8'(nibble) : 8'h61+8'(nibble)-8'd10;
    endfunction
    function automatic [7:0] line_byte(input [287:0] sample,input [6:0] index);
        integer relative_index,field,column;
        reg [31:0] word_value;
        reg [7:0] label_value;
        begin
            relative_index=0;field=0;column=0;word_value=0;label_value=0;line_byte=8'h20;
            case(index)
                0:line_byte="A";
                1:line_byte="E";
                2:line_byte="3";
                3:line_byte="5";
                4:line_byte=" ";
                104:line_byte=8'h0a;
                default:begin
                    relative_index=integer'(index)-5;
                    field=relative_index/11;column=relative_index%11;
                    case(field)
                        0:begin label_value="S";word_value=sample[287:256];end
                        1:begin label_value="B";word_value=sample[255:224];end
                        2:begin label_value="N";word_value=sample[223:192];end
                        3:begin label_value="A";word_value=sample[191:160];end
                        4:begin label_value="W";word_value=sample[159:128];end
                        5:begin label_value="R";word_value=sample[127:96];end
                        6:begin label_value="C";word_value=sample[95:64];end
                        7:begin label_value="P";word_value=sample[63:32];end
                        8:begin label_value="V";word_value=sample[31:0];end
                        default:begin end
                    endcase
                    if(column==0)line_byte=label_value;
                    else if(column==1)line_byte="=";
                    else if(column<10)line_byte=hex_digit(4'(word_value>>((9-column)*4)));
                    else line_byte=field==8 ? 8'h0d : 8'h20;
                end
            endcase
        end
    endfunction
    always_ff @(posedge clk_i)begin
        if(!rst_ni)begin
            active_sample<=0;pending_sample<=0;pending_valid<=0;line_active<=0;last_queued<=0;
            report_count<=0;byte_index<=0;
        end else begin
            if(report_due)begin
                report_count<=0;pending_sample<=sample_i;pending_valid<=1;
            end else report_count<=report_count+1'b1;
            if(!line_active || line_done)begin
                if(report_due || pending_valid)begin
                    active_sample<=report_due ? sample_i : pending_sample;
                    pending_valid<=0;line_active<=1;last_queued<=0;byte_index<=0;
                end else line_active<=0;
            end else if(byte_valid && byte_ready)begin
                if(byte_index==7'(LINE_BYTES-1))last_queued<=1;
                else byte_index<=byte_index+1'b1;
            end
        end
    end
endmodule
`default_nettype wire
