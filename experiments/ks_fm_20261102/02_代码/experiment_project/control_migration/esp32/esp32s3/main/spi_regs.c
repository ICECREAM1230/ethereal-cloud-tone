// Copyright (c) 2026 ICECREAM1230
// spi_regs.c — ESP32 SPI transport init + LEGACY byte-protocol
// reference (#109)
//
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
//
// LIVE: fpga_spi_init() — the ESP-IDF SPI-master bus/device setup
// every current transfer rides (engine_link.c and the word-protocol
// helpers use the device handle exported here).
//
// REFERENCE ONLY: the fpga_reg_* byte-protocol functions below talk
// to spi_slave_regs.sv, the bring-up peripheral. The live gateware
// peripheral is spi_bus.sv (16-bit word addresses, 32-bit data —
// docs/memory_map.md); these functions are kept, like the .sv file,
// as the known-good A/B reference for SPI link debugging.

#include "spi_regs.h"
#include "ae350_control.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>

static const char *TAG = "fpga_spi";

static spi_device_handle_t g_spi;
static bool cpu_link_verified;
static uint32_t cpu_reject_count;
void fpga_cpu_link_authorize(bool verified){cpu_link_verified=verified;}
uint32_t fpga_cpu_rejected_writes(void){return cpu_reject_count;}


// ── Initialisation ──────────────────────────────────────────────────
void fpga_spi_init(int mosi_pin, int miso_pin, int sclk_pin, int cs_pin,
                   uint32_t freq_hz)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = mosi_pin,
        .miso_io_num     = miso_pin,
        .sclk_io_num     = sclk_pin,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 64,   // plenty for register bursts
    };

    spi_device_interface_config_t dev_cfg = {
        .mode          = 0,                  // CPOL=0, CPHA=0
        .clock_speed_hz = freq_hz,
        .spics_io_num  = cs_pin,
        .queue_size    = 1,
//        .flags         = SPI_DEVICE_HALFDUPLEX,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_DISABLED));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &g_spi));
    ESP_LOGI(TAG, "SPI init: MOSI=%d MISO=%d SCLK=%d CS=%d freq=%lu",
             mosi_pin, miso_pin, sclk_pin, cs_pin, freq_hz);
}

// ── Low-level: transmit N bytes in ONE CS-framed transaction ────────
// A single spi_device_transmit asserts CS, clocks all `length` bits, then
// deasserts CS — so CS stays LOW across every byte in `buf`.  This is the
// native burst mode: do NOT call this once per byte (that would raise CS
// between bytes and break the FPGA's command/data framing).
static void fpga_xfer_bytes(const uint8_t *tx, size_t nbytes)
{
    spi_transaction_t t = {
        .length    = nbytes * 8,
        .tx_buffer = tx,
        .rx_buffer = NULL,
    };
    // POLLING transmit, not the interrupt/queue path: engine_link is
    // the sole SPI owner, and these are tiny frequent frames. The
    // interrupt driver's per-transaction cost (bus-lock bg request,
    // esp_intr_enable, a semaphore wait) dominated and pinned
    // engine_link under a CC flood (#70). Polling busy-waits the ~5 µs
    // of wire time instead — far less overhead per word burst.
    ESP_ERROR_CHECK(spi_device_polling_transmit(g_spi, &t));
}

// ── Low-level: full-duplex N bytes in ONE CS-framed transaction ─────
// Like fpga_xfer_bytes, but also captures MISO into `rx`.  `tx` and `rx`
// must each hold nbytes.  Used for reads.
static void fpga_xfer_bytes_duplex(const uint8_t *tx, uint8_t *rx, size_t nbytes)
{
    spi_transaction_t t = {
        .length    = nbytes * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    // Reads are just as short and frequent as writes. Queue/ISR scheduling
    // can otherwise consume the owner's whole service budget before it gets
    // to the bank mute needed for a mode switch or recovery.
    ESP_ERROR_CHECK(spi_device_polling_transmit(g_spi, &t));
}

// ── Single-register write ──────────────────────────────────────────
// Command byte + data byte go out in ONE CS-framed transaction (16 bits).
void fpga_reg_write(uint8_t addr, uint8_t data)
{
    if(CONFIG_FPGA_AE350_CONTROL){cpu_reject_count++;return;}
    uint8_t frame[2] = { (0 << 7) | (addr & 0x7F), data };
    fpga_xfer_bytes(frame, 2);
}

// ── Raw link probe (diagnostic) ────────────────────────────────────
// Sends 5 bytes and prints every MISO byte returned.  This is NOT a
// loopback: the FPGA decodes byte 0 as a command like any other
// transaction.  What matters is the first byte.
//
//   MISO[0] == 0xA5  → the physical MISO path works.  The slave returns
//                      its ID byte here on every transaction, so this is
//                      a free link check.
//   MISO[0] == 0x00  → MISO is dead (wiring, pin constraint, or the
//                      slave is not being clocked).  Nothing downstream
//                      is worth debugging until this reads 0xA5.
//   MISO[0] == 0xFF  → line floating / not driven.
//
// Bytes 1..4 are register contents: 0xAA is a READ command for address
// 0x2A & 0x0F = 10, so they show mem[10..13].
void fpga_raw_link_probe(void)
{
    uint8_t tx[5] = {0xAA, 0x55, 0x11, 0x22, 0x33};
    uint8_t rx[5] = {0xEE, 0xEE, 0xEE, 0xEE, 0xEE};
    fpga_xfer_bytes_duplex(tx, rx, 5);
    printf("MISO: %02X %02X %02X %02X %02X   (byte 0 must be A5)\n",
           rx[0], rx[1], rx[2], rx[3], rx[4]);
}

// ── Single-register read ───────────────────────────────────────────
// Read protocol (matches rtl/spi/spi_slave_regs.sv): command byte
// (R/W=1, addr) + data bytes, all in ONE CS-framed transaction.  The
// command byte itself is the turnaround, so mem[addr] appears on MISO at
// byte index 1: the slave latches addr on the 8th rising edge of the
// command byte and loads the combinational mem[addr] onto MISO at the
// falling edge immediately after.  MISO byte 0 is the slave's ID (0xA5).
uint8_t fpga_reg_read(uint8_t addr)
{
    uint8_t frame[3];   // cmd + 1 dummy + 1 data = 3 bytes
    uint8_t rxb[3];

    frame[0] = (1 << 7) | (addr & 0x7F);   // command: read
    frame[1] = 0x00;                       // dummy
    frame[2] = 0x00;                       // data (MISO carries mem[addr])

    fpga_xfer_bytes_duplex(frame, rxb, 3);
    return rxb[1];   // data is at byte index 1 (after the command byte)
}

// ── Burst write ────────────────────────────────────────────────────
// Command byte + N data bytes in ONE CS-framed transaction.
void fpga_reg_write_burst(uint8_t addr, const uint8_t *data, size_t len)
{
    if(CONFIG_FPGA_AE350_CONTROL){cpu_reject_count++;return;}
    // Small local buffer for the command + data; registers use short bursts.
    uint8_t frame[33];   // 1 cmd + up to 32 data bytes
    frame[0] = (0 << 7) | (addr & 0x7F);
    for (size_t i = 0; i < len && i < 32; i++)
        frame[1 + i] = data[i];
    fpga_xfer_bytes(frame, 1 + (len < 32 ? len : 32));
}

// ── Burst read ─────────────────────────────────────────────────────
// Read `len` bytes starting at `addr` (auto-increment on the FPGA side).
// One CS-framed transaction: cmd + 1 dummy + len data bytes.  Data starts
// at rxb index 1.
void fpga_reg_read_burst(uint8_t addr, uint8_t *buf, size_t len)
{
    // cmd + 1 dummy + up to 32 data bytes
    uint8_t tx[34];
    uint8_t rx[34];

    tx[0] = (1 << 7) | (addr & 0x7F);
    tx[1] = 0x00;   // dummy
    for (size_t i = 0; i < len && i < 32; i++)
        tx[2 + i] = 0x00;

    fpga_xfer_bytes_duplex(tx, rx, 2 + (len < 32 ? len : 32));

    for (size_t i = 0; i < len && i < 32; i++)
        buf[i] = rx[1 + i];   // data starts at byte index 1 (after 1 dummy)
}

// ═══════════════════════════════════════════════════════════════════
// Word protocol (rtl/spi/spi_bus.sv — the docs/memory_map.md format)
//
//   byte 0      command: [7] R/W (1=read), [6] auto-increment
//   bytes 1..2  16-bit word address, MSB first
//   write:      + 4N data bytes (32-bit words, MSB first)
//   read:       + 2 dummy bytes (fetch turnaround) + 4N data bytes
//
// MISO byte 0 is always 0xA5. These functions coexist with the byte
// protocol above; which one is live depends on the loaded bitstream
// (spi_slave_regs = bytes, spi_bus = words).
// ═══════════════════════════════════════════════════════════════════

#define WORDS_MAX 12  // FM shadow: 51 B write / 53 B read, below 64 B bus limit

static int fpga_word_write_frame(uint16_t addr,const uint32_t *words,size_t n)
{
    uint8_t frame[3 + 4 * WORDS_MAX];
    if (n > WORDS_MAX) n = WORDS_MAX;
    frame[0] = 0x40;                    // write, auto-increment
    frame[1] = (uint8_t)(addr >> 8);
    frame[2] = (uint8_t)(addr & 0xFF);
    for (size_t i = 0; i < n; i++) {
        frame[3 + 4*i] = (uint8_t)(words[i] >> 24);
        frame[4 + 4*i] = (uint8_t)(words[i] >> 16);
        frame[5 + 4*i] = (uint8_t)(words[i] >> 8);
        frame[6 + 4*i] = (uint8_t)(words[i]);
    }
    spi_transaction_t t={.length=(3+4*n)*8,.tx_buffer=frame,.rx_buffer=NULL};
    return spi_device_polling_transmit(g_spi,&t);
}

fpga_template_write_result_t fpga_template_write_burst(uint16_t addr,const uint32_t *words,size_t n)
{
    if(!words || !n || n>WORDS_MAX || addr==UINT16_C(0x0701) ||
       (CONFIG_FPGA_AE350_CONTROL && (!cpu_link_verified || !ae350_control_write_allowed(addr,n)))) {
        cpu_reject_count++;return FPGA_TEMPLATE_FORBIDDEN;
    }
    // AOR1 has its own immutable slot and audio-frame ACK, not the CPU FIFO.
    if(CONFIG_FPGA_AE350_CONTROL && addr!=UINT16_C(0x07b5)) {
        uint32_t credit=0;
        if(!fpga_word_read_burst_observe(AE350_TEMPLATE_CREDIT_REG,&credit,1))return FPGA_TEMPLATE_IO;
        if(credit&UINT32_C(0x10000))return FPGA_TEMPLATE_OVERFLOW;
        if((credit&~UINT32_C(0x1ffff)) || credit>AE350_TEMPLATE_CAPACITY)return FPGA_TEMPLATE_IO;
        if(credit<n+AE350_TEMPLATE_CREDIT_RESERVE)return FPGA_TEMPLATE_DEFERRED;
    }
    if(fpga_word_write_frame(addr,words,n)!=0)return FPGA_TEMPLATE_UNCERTAIN;
    return FPGA_TEMPLATE_SENT;
}

void fpga_word_write_burst(uint16_t addr,const uint32_t *words,size_t n)
{
    if(CONFIG_FPGA_AE350_CONTROL) {
        if(!cpu_link_verified || !words || !ae350_control_write_allowed(addr,n)) {
            cpu_reject_count++;return;
        }
        if(addr!=UINT16_C(0x0701) && addr!=UINT16_C(0x0725) && addr!=UINT16_C(0x0726)) {
            // Defensive path for callers outside the template scheduler. The
            // scheduler uses the result-returning API and never assumes send.
            (void)fpga_template_write_burst(addr,words,n);return;
        }
        // Scalar RVR1 writes do not consume the CPU template FIFO credits.
    }
    ESP_ERROR_CHECK(fpga_word_write_frame(addr,words,n));
}

void fpga_word_write(uint16_t addr, uint32_t value)
{
    fpga_word_write_burst(addr, &value, 1);
}

// Returns false if the ID byte is missing (link fault).
bool fpga_word_read_burst(uint16_t addr, uint32_t *words, size_t n)
{
    uint8_t tx[5 + 4 * WORDS_MAX] = {0};
    uint8_t rx[5 + 4 * WORDS_MAX] = {0};
    if (n > WORDS_MAX) n = WORDS_MAX;
    tx[0] = 0xC0;                       // read, auto-increment
    tx[1] = (uint8_t)(addr >> 8);
    tx[2] = (uint8_t)(addr & 0xFF);
    fpga_xfer_bytes_duplex(tx, rx, 5 + 4 * n);
    for (size_t i = 0; i < n; i++) {
        words[i] = ((uint32_t)rx[5 + 4*i] << 24)
                 | ((uint32_t)rx[6 + 4*i] << 16)
                 | ((uint32_t)rx[7 + 4*i] << 8)
                 |  (uint32_t)rx[8 + 4*i];
    }
    return rx[0] == 0xA5;
}

bool fpga_word_read_burst_observe(uint16_t addr, uint32_t *words, size_t n)
{
    if(!words || !n || n>WORDS_MAX)return false;
    uint8_t tx[5+4*WORDS_MAX]={0},rx[5+4*WORDS_MAX]={0};
    tx[0]=0xC0;tx[1]=(uint8_t)(addr>>8);tx[2]=(uint8_t)addr;
    spi_transaction_t t={.length=(5+4*n)*8,.tx_buffer=tx,.rx_buffer=rx};
    // Optional observations must not abort the running music engine on a
    // diagnostic transport failure. Keep the original music paths above.
    if(spi_device_polling_transmit(g_spi,&t)!=0 || rx[0]!=0xA5)return false;
    for(size_t i=0;i<n;i++)words[i]=((uint32_t)rx[5+4*i]<<24)|
        ((uint32_t)rx[6+4*i]<<16)|((uint32_t)rx[7+4*i]<<8)|rx[8+4*i];
    return true;
}

uint32_t fpga_word_read(uint16_t addr)
{
    uint32_t v = 0;
    fpga_word_read_burst(addr, &v, 1);
    return v;
}

// ── Bank swap (ping-pong) ───────────────────────────────────────────
#include "esp_rom_sys.h"

bool fpga_swap_checked(void)
{
    if(CONFIG_FPGA_AE350_CONTROL)return false;
    uint32_t before, after;
    if (!fpga_control_status(&before) || (before & 2u)) return false;
    fpga_word_write(0x0002, 1);
    int64_t deadline = esp_timer_get_time() + 250;
    do {
        if (!fpga_control_status(&after)) return false;
        if (!(after & 2u) && ((after ^ before) & 1u)) return true;
    } while (esp_timer_get_time() < deadline);
    return false;
}

void fpga_swap(void)
{
    if (!fpga_swap_checked()) ESP_LOGE(TAG, "bank swap not acknowledged");
}


bool fpga_control_status(uint32_t *status)
{
    return fpga_word_read_burst(0x0003, status, 1);
}

bool fpga_live_write(uint16_t bus, uint32_t value)
{
    if(CONFIG_FPGA_AE350_CONTROL)return false;
    uint32_t before, after;
    if (!fpga_control_status(&before) || (before & 4u)) return false;
    fpga_word_write(0x0800 + bus, value & 0x3FFFFu);
    // Bound the polling work independently of the 100 Hz RTOS tick. A
    // fault/rejection leaves the desired write pending for the next wake.
    int64_t deadline = esp_timer_get_time() + 100;
    do {
        if (!fpga_control_status(&after)) return false;
        if ((before & 0xFF00u) != (after & 0xFF00u)) return false;
        if (!(after & 4u)) return true;
    } while (esp_timer_get_time() < deadline);
    return false;
}
