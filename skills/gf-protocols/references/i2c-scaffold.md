# I2C scaffold example

Read only when an I2C master scaffold is requested. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## I2C Master Scaffold

```systemverilog
module i2c_master_scaffold #(parameter int CLK_FREQ=100_000_000, I2C_FREQ=100_000) (
    input  logic clk, rst_n,
    input  logic [6:0] slave_addr, input logic rw, input logic [7:0] wr_data,
    input  logic start, output logic [7:0] rd_data, output logic done, ack_err, busy,
    output logic scl_oen, sda_oen, input logic scl_i, sda_i
);
    // Quarter-period counter for I2C timing
    // FSM: IDLE -> START -> ADDR_BIT(8) -> ADDR_ACK -> WR/RD_BIT(8) -> WR/RD_ACK -> STOP
    // Open-drain: oen=0 drives low, oen=1 releases high via external pull-up
    // Customize: multi-byte transfers, clock stretching, repeated start
endmodule
```
