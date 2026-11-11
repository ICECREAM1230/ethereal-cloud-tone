# Cocotb runners

Read only when configuring a Makefile or pytest runner. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Makefile Template

```makefile
SIM ?= icarus
TOPLEVEL_LANG ?= verilog
VERILOG_SOURCES += $(PWD)/rtl/$(DUT).sv
TOPLEVEL = $(DUT)
MODULE = test_$(DUT)
COCOTB_HDL_TIMEUNIT = 1ns
COCOTB_HDL_TIMEPRECISION = 1ps

ifeq ($(SIM),verilator)
    EXTRA_ARGS += --trace --trace-structs
endif

SIM_BUILD = sim_build/$(SIM)
include $(shell cocotb-config --makefiles)/Makefile.sim
```

## Python Runner (pytest)

```python
import os
from pathlib import Path
from cocotb_tools.runner import get_runner

def test_module():
    sim = os.getenv("SIM", "icarus")
    runner = get_runner(sim)
    runner.build(
        sources=[Path("rtl/my_module.sv")],
        hdl_toplevel="my_module",
    )
    runner.test(
        hdl_toplevel="my_module",
        test_module="test_my_module",
    )
```
