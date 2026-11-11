# FSM test example

Read only when testing a state machine. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### FSM Test
```python
from enum import IntEnum

class State(IntEnum):
    IDLE = 0
    ACTIVE = 1
    DONE = 2

@cocotb.test()
async def test_fsm(dut):
    clock = Clock(dut.clk, 10, units="ns")
    cocotb.start_soon(clock.start())
    # reset...
    assert int(dut.state.value) == State.IDLE
    dut.start.value = 1
    await RisingEdge(dut.clk)
    dut.start.value = 0
    await RisingEdge(dut.clk)
    assert int(dut.state.value) == State.ACTIVE
```
