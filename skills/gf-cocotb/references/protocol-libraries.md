# Protocol libraries

Read only when using an installed cocotbext protocol library. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## cocotbext Protocol Libraries

| Package | Protocol | Install |
|---|---|---|
| `cocotbext-axi` | AXI4, AXI-Lite, AXI-Stream | `pip install cocotbext-axi` |
| `cocotbext-wishbone` | Wishbone B4 | `pip install cocotbext-wishbone` |
| `cocotbext-spi` | SPI | `pip install cocotbext-spi` |
| `cocotbext-uart` | UART | `pip install cocotbext-uart` |
| `cocotbext-eth` | Ethernet | `pip install cocotbext-eth` |
| `cocotbext-pcie` | PCIe | `pip install cocotbext-pcie` |

### AXI-Lite Example
```python
from cocotbext.axi import AxiLiteBus, AxiLiteMaster

axil = AxiLiteMaster(AxiLiteBus.from_prefix(dut, "s_axi"), dut.clk, dut.rst)
await axil.write(0x0000, b'\x42\x00\x00\x00')
data = await axil.read(0x0000, 4)
```
