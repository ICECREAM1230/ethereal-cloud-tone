# FIFO test example

Read only when testing a FIFO. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### FIFO Test
```python
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, ClockCycles
import random

async def reset_fifo(dut):
    dut.rst.value = 1
    dut.wr_en.value = 0
    dut.rd_en.value = 0
    await ClockCycles(dut.clk, 5)
    dut.rst.value = 0
    await RisingEdge(dut.clk)

@cocotb.test()
async def test_fifo_write_read(dut):
    clock = Clock(dut.clk, 10, units="ns")
    cocotb.start_soon(clock.start())
    await reset_fifo(dut)

    assert dut.empty.value == 1
    written = []
    for i in range(16):
        data = random.randint(0, 255)
        written.append(data)
        dut.din.value = data
        dut.wr_en.value = 1
        await RisingEdge(dut.clk)
    dut.wr_en.value = 0
    await RisingEdge(dut.clk)
    assert dut.full.value == 1

    read = []
    for i in range(16):
        dut.rd_en.value = 1
        await RisingEdge(dut.clk)
        read.append(int(dut.dout.value))
    dut.rd_en.value = 0
    assert read == written
```
