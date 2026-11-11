// Copyright (c) 2026 ICECREAM1230
// slider.h — panel slider (10k linear pot) → resonance
//
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
//
// The wiper feeds GPIO8/ADC1_CH7 on S3 (GPIO1 on the legacy C3) through
// the existing divider (680R to GND,
// 3k6 to 3.3V around the pot) that keeps the voltage inside the
// max-attenuation ADC range. Readings map through a stored
// calibration (min/max raw counts, persisted in NVS flash) to an
// internal MIDI CC 71 event on the event bus, so the slider IS the
// resonance knob (voice_alloc's temporary CC 71 handler does the
// rest; when the MIDI schema lands, only the CC number moves).
//
// Serial-monitor keys (USB-Serial-JTAG console) — two-point settled
// calibration: park the fader at an end, press the key, the reading
// averages for one second and that resting value becomes the end
// (each end persists independently and can be redone alone):
//   1  capture the MIN end
//   2  capture the MAX end
//   r  print one raw reading + ends + working range
//   p  BLE: print stack state + re-trigger advertising (ble_midi.c)

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initializes NVS, the board-mapped ADC poll task and USB console.
// S3 GPIO8 is active; GPIO1 remains the blue status LED. Call after
// voice_alloc_init() (events published before subscribers exist are
// harmlessly dropped, but why waste them).
void slider_init(void);

#ifdef __cplusplus
}
#endif
