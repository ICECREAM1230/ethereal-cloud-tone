// Copyright (c) 2026 ICECREAM1230
// voice_alloc.h — voices as a firmware concept
//
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
//
// The retained module name identifies the single parameter/Web event owner.
// Production compiles static DDS/KS/FM templates and forwards leased Web
// performance as raw MIDI. FPGA alone owns note allocation and expression.
// The previous S3 allocator is retained only for explicit legacy host tests;
// it never runs alongside the FPGA performance path.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Subscribe to the event bus and start the control owner task.
// Call after event_bus_init() and engine_link_init().
void voice_alloc_init(void);

#ifdef __cplusplus
}
#endif
