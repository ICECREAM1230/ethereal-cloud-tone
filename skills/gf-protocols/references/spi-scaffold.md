# SPI scaffold example

Read only when an SPI master scaffold is requested. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## SPI Master Scaffold

```systemverilog
module spi_master_scaffold #(parameter int CLK_DIV=4) (
    input  logic clk, rst_n, cpol, cpha,
    input  logic [7:0] tx_data, input logic tx_valid, output logic tx_ready,
    output logic [7:0] rx_data, output logic rx_valid,
    output logic sclk, mosi, input logic miso, output logic cs_n
);
    // FSM: IDLE -> LEAD -> ACTIVE (8 bits MSB first) -> TRAIL -> DONE
    // Clock divider generates sclk from clk/(2*CLK_DIV)
    // CPOL/CPHA configure polarity and sample/shift edges
    // Customize: add multi-byte, variable width, DMA interface
endmodule
```
