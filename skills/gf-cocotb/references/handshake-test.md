# Handshake test example

Read only when testing valid/ready backpressure. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### Valid/Ready Handshake Test
```python
@cocotb.test()
async def test_handshake(dut):
    clock = Clock(dut.clk, 10, units="ns")
    cocotb.start_soon(clock.start())
    # ... reset ...

    async def driver():
        for data in range(20):
            dut.s_data.value = data
            dut.s_valid.value = 1
            while True:
                await RisingEdge(dut.clk)
                if dut.s_ready.value == 1:
                    break
        dut.s_valid.value = 0

    async def backpressure():
        while True:
            dut.m_ready.value = random.randint(0, 1)
            await ClockCycles(dut.clk, random.randint(1, 4))

    cocotb.start_soon(driver())
    cocotb.start_soon(backpressure())
    await ClockCycles(dut.clk, 200)
```
