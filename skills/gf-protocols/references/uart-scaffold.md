# UART scaffold example

Read only when a UART transmitter scaffold is requested. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## UART TX Scaffold

```systemverilog
module uart_tx_scaffold #(parameter int CLK_FREQ=100_000_000, BAUD_RATE=115_200) (
    input  logic clk, rst_n,
    input  logic [7:0] tx_data, input logic tx_valid, output logic tx_ready,
    output logic tx
);
    // Baud generator: counter to CLK_FREQ/BAUD_RATE
    // FSM: IDLE -> START (1 low bit) -> DATA (8 bits LSB first) -> STOP (1 high bit)
    // tx_ready when IDLE
    // Customize: add parity, configurable data bits, RX companion
endmodule
```
