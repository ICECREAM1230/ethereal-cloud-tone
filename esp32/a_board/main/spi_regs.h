// Copyright (c) 2026 ICECREAM1230
// spi_regs.h — ESP32-C3 driver for spi_slave_regs FPGA peripheral
//
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
//
// Protocol summary:
//   Byte 0:  [7]=R/W (1=read, 0=write), [6:0]=7-bit register address
//   Byte 1+: data bytes, address auto-increments after each byte
//
//   Write: 2+N bytes (cmd + N data bytes)
//   Read:  2+N bytes (cmd + 1 dummy + N data bytes)
//          The first response byte is the dummy; register data starts
//          at byte 1 (mem[addr] is combinational, auto-increments).
//
// SPI Mode 0 (CPOL=0, CPHA=0), MSB-first, up to ~20 MHz.

#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Initialisation ──────────────────────────────────────────────────
// Call once at startup.  Configures SPI2 (HSPI) as master.
//   mosi_pin, miso_pin, sclk_pin, cs_pin — GPIO numbers
//   freq_hz — SPI clock (e.g. 10000000 for 10 MHz)
void fpga_spi_init(int mosi_pin, int miso_pin, int sclk_pin, int cs_pin,
                   uint32_t freq_hz);

// ── Single-register operations ──────────────────────────────────────

// Write one byte to a register
void fpga_reg_write(uint8_t addr, uint8_t data);

// Read one byte from a register
uint8_t fpga_reg_read(uint8_t addr);

// ── Multi-register (burst) operations ───────────────────────────────
// addr = starting register, data = buffer, len = number of registers

// Write len bytes starting at addr
void fpga_reg_write_burst(uint8_t addr, const uint8_t *data, size_t len);

// Read len bytes starting at addr into buf.
// Internally reads len+1 bytes and discards the first 1.
void fpga_reg_read_burst(uint8_t addr, uint8_t *buf, size_t len);

// Diagnostic: send 5 bytes, print the raw MISO bytes returned.
// MISO byte 0 is the slave's ID byte (0xA5) on every transaction — if it
// is not 0xA5 the physical MISO path is broken, and nothing else here
// will work.  Check that first.
void fpga_raw_link_probe(void);

// ── Word protocol (spi_bus.sv / docs/memory_map.md) ─────────────────
// 16-bit word addresses, 32-bit data, MSB first on the wire.
// Live only when the loaded bitstream contains spi_bus (not the
// byte-protocol spi_slave_regs).
#include <stdbool.h>
void     fpga_word_write(uint16_t addr, uint32_t value);
void     fpga_word_write_burst(uint16_t addr, const uint32_t *words, size_t n);
typedef enum {
    FPGA_TEMPLATE_SENT=0, FPGA_TEMPLATE_DEFERRED, FPGA_TEMPLATE_IO,
    FPGA_TEMPLATE_OVERFLOW, FPGA_TEMPLATE_FORBIDDEN,
    FPGA_TEMPLATE_UNCERTAIN /* write was attempted but SDK completion failed */
} fpga_template_write_result_t;
/* Single SPI owner only. CPU ABI2: inspect authoritative SCLK free credits
 * immediately before the unchanged write frame, retaining two spare slots.
 * DEFERRED sends no write and must not advance an upload cursor or COMMIT.
 * SENT means dispatched; it never substitutes for CPU/native activation ACK. */
fpga_template_write_result_t fpga_template_write_burst(uint16_t addr,
    const uint32_t *words,size_t n);
uint32_t fpga_word_read(uint16_t addr);
bool     fpga_word_read_burst(uint16_t addr, uint32_t *words, size_t n);
// Read-only observations, ONLY from engine_link's owner. SDK/A5 failure returns
// false without aborting or changing the destination. No retries or write frame.
bool     fpga_word_read_burst_observe(uint16_t addr, uint32_t *words, size_t n);

// ── Bank swap (ping-pong) ───────────────────────────────────────────
// Per-element writes land in the SHADOW bank; this requests the swap
// (CTRL@0x0002 bit 0). fpga_swap_checked() confirms STATUS pending cleared
// and the actual bank changed; fpga_swap() logs a bounded timeout failure. Every
// parameter change is effected through a swap — swaps are cheap
// (thousands per second), there is no "live" write path around them.
// Discipline: a swap flips the WHOLE bank — keep both halves
// populated. After a swap the new shadow holds the previous
// generation; rewrite what you change before the next swap.
void     fpga_swap(void);

// Requires the paired 2026-10-03 STATUS register gateware. STATUS@0x0003:
// bit0 active bank, bit1 swap pending, bit2 live mailbox busy, bit3 reject
// sticky, bits15:8 rejected-write sequence, bits31:16 COMPLETED CSP passes.
// A status read checks the SPI frame ID. A live write returns true only
// after its bus base and both generations have committed, with no rejection.
bool fpga_control_status(uint32_t *status);
bool fpga_swap_checked(void);
bool fpga_live_write(uint16_t bus, uint32_t value);

#ifdef __cplusplus
}
#endif
