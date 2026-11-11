# AXI4-Lite scaffold example

Read only when an AXI4-Lite interface scaffold is requested. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## AXI4-Lite Slave Scaffold

```systemverilog
module axi4lite_slave_regs #(parameter int ADDR_WIDTH=4, DATA_WIDTH=32) (
    input  logic clk, rst_n,
    input  logic [ADDR_WIDTH-1:0] s_axi_awaddr, input logic s_axi_awvalid, output logic s_axi_awready,
    input  logic [DATA_WIDTH-1:0] s_axi_wdata, input logic [DATA_WIDTH/8-1:0] s_axi_wstrb,
    input  logic s_axi_wvalid, output logic s_axi_wready,
    output logic [1:0] s_axi_bresp, output logic s_axi_bvalid, input logic s_axi_bready,
    input  logic [ADDR_WIDTH-1:0] s_axi_araddr, input logic s_axi_arvalid, output logic s_axi_arready,
    output logic [DATA_WIDTH-1:0] s_axi_rdata, output logic [1:0] s_axi_rresp,
    output logic s_axi_rvalid, input logic s_axi_rready,
    output logic [DATA_WIDTH-1:0] reg0_out, reg1_out,
    input  logic [DATA_WIDTH-1:0] reg2_in, reg3_in
);
    // Write: accept AW+W, write register via wstrb, respond B
    // Read: accept AR, return register data on R
    // Customize: add registers, change address decode width
endmodule
```
