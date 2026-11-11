/* SPDX-License-Identifier: MIT */
#ifndef CONTROL_LATENCY_H
#define CONTROL_LATENCY_H
#include <stdint.h>

#define CL_APB_BASE UINT32_C(0xf8010100)
#define CL_ID UINT32_C(0x4c543034)
#define CL_CLOCK_HZ UINT32_C(48000000)
#define CL_LIMIT 20u
#define CL_RAW_WORDS 18u
#define CL_WORDS 26u
#define CL_FRAME_BYTES 220u
enum { CL_FAULT_IO=1u, CL_FAULT_PROTOCOL=2u, CL_FAULT_LOSS=4u,
       CL_FAULT_CONFLICT=8u };
typedef struct {
    int (*read32)(void *context,uint32_t offset,uint32_t *value);
    int (*write32)(void *context,uint32_t offset,uint32_t value);
    void *context;
} cl_io_t;
/* One cooperative-loop owner, static DTCM storage. No heap, IRQ or busy wait.
 * rows[][] retains the original eighteen hardware words without edits.
 * The final eight words contain AE350 modulo-2^64 subtraction results.
 */
typedef struct {
    uint32_t rows[CL_LIMIT][CL_WORDS];
    uint32_t incoming[CL_RAW_WORDS];
    cl_io_t io;
    uint32_t present,sent,fault,reported_fault,report_fault,replay_ms;
    uint16_t crc[CL_LIMIT],error_crc,character;
    uint8_t crc_words[CL_LIMIT];
    uint8_t enabled,copying,copy_word,frame_kind,send_row;
} cl_state_t;
/* NULL io selects ordered volatile RV32 MMIO. ID zero disables the optional
 * feature without further accesses. Other IDs/frequencies fail closed.
 */
void cl_init(cl_state_t *state,const cl_io_t *io);
/* At most one snapshot word is copied per call. UART writes are forbidden
 * when idle==0 and otherwise limited to one byte; readiness is never waited
 * on. Incoming MIDI/control work must run before this call on every slice.
 */
void cl_poll(cl_state_t *state,int idle,uint32_t now_ms);
#endif
