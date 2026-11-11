// Copyright (c) 2026 ICECREAM1230
// midi_in.h — MIDI input processes (UART1 = DIN, UART0 = panel)
//
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
//
// Two serial MIDI ingress ports, each 31250 baud 8N1 with its own
// parser instance, both publishing to the event bus.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Configure UART1 as the DIN MIDI input port and start its RX task.
//
// rx_pin — GPIO to use for MIDI RX (GPIO16 on the YD-ESP32-S3;
// GPIO0 on the original C3 wiring).
//
// Initialize this UART before fpga_spi_init() so SPI claims its pins last.
void midi_in_init(int rx_pin);

// Configure UART0 as the dev-host panel MIDI port (#90) and start its
// RX task. Reserved for Open Stage Control exclusively (Thor,
// 2026-09-10). Requires the console to be off UART0 (it lives on the
// USB-Serial/JTAG controller since the same date).
//
// rx_pin — GPIO wired to the panel MIDI-IN circuit (S3 GPIO42).
void midi_panel_init(int rx_pin);

#ifdef __cplusplus
}
#endif
