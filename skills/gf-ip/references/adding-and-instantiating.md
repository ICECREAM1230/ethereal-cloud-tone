# Adding an installed IP block

Read only when implementation is authorized and a real block passed the availability gate. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Add Flow

For a Gowin official project (`.gprj`), do not replace an existing Gowin IP with a GateFlow block automatically. First inspect the official `.ipc`, wrapper, and device-specific requirements; add a portable GateFlow block only when the user explicitly requests a vendor-neutral replacement or simulation-only model. Preserve the `.gprj` file list as the implementation authority.

When user says "add a FIFO" or invokes `$gf-ip add fifo_sync`:
1. Read block.yaml for metadata and parameters
2. Copy RTL to `rtl/` (or user-specified directory)
3. Copy testbench to `tb/`
4. Copy formal properties to `formal/`
5. Update `.gateflow/project.yaml` — add to `ip_blocks`
6. Show instantiation example from README.md

## Instantiation Examples

### fifo_sync
```systemverilog
fifo_sync #(.WIDTH(8), .DEPTH(32)) u_fifo (.clk(sys_clk), .rst_n(sys_rst_n), .wr_en(wr_valid && !fifo_full), .wr_data(wr_data), .rd_en(rd_consume), .rd_data(rd_out), .full(fifo_full), .empty(fifo_empty));
```

### fifo_async
```systemverilog
fifo_async #(.WIDTH(32), .DEPTH(16)) u_cdc (.wr_clk(clk_fast), .wr_rst_n(rst_fast_n), .wr_en(producer_valid), .wr_data(producer_data), .wr_full(full), .rd_clk(clk_slow), .rd_rst_n(rst_slow_n), .rd_en(consumer_ready), .rd_data(consumer_data), .rd_empty(empty));
```

### cdc_2ff
```systemverilog
cdc_2ff u_sync (.clk(clk_dst), .rst_n(rst_dst_n), .d(async_signal), .q(synced_signal));
```

### uart
```systemverilog
uart #(.CLK_FREQ(100_000_000), .BAUD_RATE(115200)) u_uart (.clk, .rst_n, .tx_data(tx_byte), .tx_valid(tx_start), .tx_ready(tx_idle), .tx(uart_tx_pin), .rx(uart_rx_pin), .rx_data(rx_byte), .rx_valid(rx_ready));
```

### spi_master
```systemverilog
spi_master #(.CLK_DIV(8)) u_spi (.clk, .rst_n, .cpol(1'b0), .cpha(1'b0), .tx_data(spi_tx), .tx_valid(spi_start), .tx_ready(spi_idle), .rx_data(spi_rx), .rx_valid(spi_done), .sclk(spi_sclk), .mosi(spi_mosi), .miso(spi_miso), .cs_n(spi_cs_n));
```

## GATEFLOW-RESULT Format

```
---GATEFLOW-RESULT---
STATUS: PASS | FAIL | BLOCKED | NOT_RUN
OPERATION: add | list | info
ACTION_REQUIRED: true | false
BLOCK: <name>
VERSION: <version>
FILES: [copied files]
DETAILS: [summary]
---END-GATEFLOW-RESULT---
```
