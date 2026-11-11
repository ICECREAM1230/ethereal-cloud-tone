# IP detection patterns

Read only when analyzing module gaps or selected protocol/CDC patterns. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## What This Detects

### 1. Missing Module Implementations
Modules instantiated but never defined in the project:
```bash
# Find all module instantiations
grep -rn "^\s*\w\+\s\+\w\+\s*(" rtl/ tb/ --include="*.sv" --include="*.v" --include="*.vhd"

# Find all module definitions
grep -rn "^\s*module\s\+\w\+" rtl/ --include="*.sv" --include="*.v"

# Diff = missing implementations
```

### 2. Stub Modules (Empty or TODO)
Modules defined but with no real implementation:
```bash
# Find modules with TODO/FIXME/stub markers
grep -rn "TODO\|FIXME\|STUB\|NOT IMPLEMENTED" rtl/ --include="*.sv"

# Find modules with empty bodies (just endmodule after ports)
```

### 3. Standard IP Pattern Matching
Detect common hardware patterns that could use verified IP blocks:

| Pattern Detected | Matching IP Block | Confidence |
|-----------------|-------------------|------------|
| FIFO-like read/write with full/empty | `fifo_sync` or `fifo_async` | High |
| 2+ flip-flop chain (synchronizer) | `cdc_2ff` | High |
| Req/ack handshake across clocks | `cdc_handshake` | High |
| UART-like shift register with baud | `uart` | Medium |
| SPI-like SCLK/MOSI/MISO/CS_N | `spi_master` | High |
| AXI-like valid/ready with addr/data | `axi4lite_slave` | Medium |
| Counter with debounce logic | `debouncer` | Medium |

### 4. Interface Gaps
Ports declared in a top module but not connected to any implementation:
```bash
# Find top module ports
# Check which ports connect to instantiated submodules
# Unconnected ports = potential missing IP
```

### 5. Vendor IP Placeholders
Detect instantiations of vendor-specific IP that could have open-source alternatives:
```bash
# Xilinx primitives
grep -rn "IBUF\|OBUF\|BUFG\|MMCME2\|PLLE2\|BRAM" rtl/ --include="*.sv"

# Lattice primitives
grep -rn "SB_IO\|SB_GB\|SB_PLL\|SB_RAM" rtl/ --include="*.sv"
```

## Pattern Detection Rules

### FIFO Detection
```
Confidence: HIGH if module has:
- wr_en/write + rd_en/read signals
- full + empty signals
- data_in/wr_data + data_out/rd_data signals
- Single clock → fifo_sync
- Dual clock → fifo_async
```

### CDC Detection
```
Confidence: HIGH if:
- Signal assigned in always_ff @(posedge clk_a)
- Signal read in always_ff @(posedge clk_b) where clk_a != clk_b
- No synchronizer between domains

Confidence: MEDIUM if:
- 2+ flip-flop chain detected but not using standard sync pattern
```

### Protocol Detection
```
SPI: sclk + mosi + miso + cs_n (any naming variant)
UART: tx/rx + baud-related parameter
I2C: scl + sda (bidirectional)
AXI: *valid + *ready + *addr + *data patterns
```

### Vendor IP Detection
```
Xilinx: IBUF, OBUF, BUFG, MMCME2, PLLE2, BRAM_TDP, DSP48E1
Lattice: SB_IO, SB_GB, SB_PLL40, SB_SPRAM, SB_RAM
Gowin: IBUF, OBUF, rPLL, SDPB, pROM
Intel: altpll, altsyncram, altddio
```

---

## Extended Vendor IP Detection

Additional primitives to detect beyond existing list:
- **Xilinx UltraScale+:** URAM288, DSP48E2, BUFGCE, MMCME4_ADV, GTHE4_CHANNEL, STARTUPE3
- **Intel Agilex:** IOPLL, RAM20K, M20K, MLAB, tennm_ph2_iopll
- **Lattice ECP5:** EHXPLLL, DP16KD, TRELLIS_FF, DCUA, EXTREFB
- **Gowin (extended):** rPLL, PLLVR, SDPB, DPB, pROM, EMCU, DHCEN
- **Microchip PolarFire:** LSRAM, uSRAM, MACC, CCC, SERDES_IF
- **Efinix:** EFX_PLL, EFX_RAM_5K, EFX_DPRAM_5K, EFX_GBUFCE

## False Positive Reduction

| Pattern | False Positive When | Action |
|---|---|---|
| 2FF chain | Inside shift register (3+ stages) | Skip |
| 2FF chain | Both FFs same clock | Skip |
| FIFO signals | Inside module named `*fifo*` | Skip |
| valid/ready | AXI bus already using IP block | Skip |
| SPI signals | Inside testbench files (tb_*, *_tb.sv) | Skip |
| Vendor primitive | In comments or string literals | Skip |

## Severity Scoring

| Severity | Criteria | Examples |
|---|---|---|
| CRITICAL | Data corruption, metastability | CDC without synchronizer |
| HIGH | Missing module, empty stubs | Instantiated but undefined |
| MEDIUM | Ad-hoc reimplementation | Hand-rolled FIFO, manual 2FF |
| LOW | Style, minor optimization | Vendor primitive with OSS equivalent |
| INFO | Already correct | Verified IP properly used |
