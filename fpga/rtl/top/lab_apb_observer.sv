`timescale 1ns/1ps
`default_nettype none
// Delay a completed transaction, including its error, as one coherent record.
// CPU PRESET is deliberately not this observer's reset: the diagnostic history
// must survive a CPU-only reset, like the original passive monitor.
module lab_apb_observer(
    input wire pclk_i,rst_ni,
    input wire [31:0] paddr_i,pwdata_i,prdata_i,
    input wire psel_i,penable_i,pwrite_i,pready_i,pslverr_i,
    output logic valid_o,write_o,error_o,
    output logic [31:0] addr_o,wdata_o,rdata_o
);
    wire local_rst_n;
    jt51_reset_sync u_reset(.clk_i(pclk_i),.rst_n_i(rst_ni),.rst_n_o(local_rst_n));
    wire completed=psel_i&&penable_i&&pready_i;
    always_ff @(posedge pclk_i or negedge local_rst_n)begin
        if(!local_rst_n)begin
            valid_o<=0;write_o<=0;error_o<=0;addr_o<=0;wdata_o<=0;rdata_o<=0;
        end else begin
            valid_o<=completed;
            if(completed)begin
                write_o<=pwrite_i;error_o<=pslverr_i;
                addr_o<=paddr_i;wdata_o<=pwdata_i;rdata_o<=prdata_i;
            end
        end
    end
endmodule
`default_nettype wire
