/* SPDX-License-Identifier: MIT */
#ifndef FDC_AE350_H
#define FDC_AE350_H
#include <stdbool.h>
#include <stdint.h>
#include "theremin_control.h"

#ifndef ENABLE_AE350_FDC
#define ENABLE_AE350_FDC 1
#endif
#define FDC_AE350_BASE UINT32_C(0xf8010100)
#define FDC_AE350_ID UINT32_C(0x46444331)
#define FDC_AE350_ABI UINT32_C(0x00010000)
#define FDC_AE350_POLL_US UINT32_C(1000)
#define FDC_AE350_TIMEOUT_US UINT32_C(500000)

enum {
    FDC_REG_ID=0x00, FDC_REG_ABI=0x04, FDC_REG_CAP=0x08,
    FDC_REG_COMMAND=0x0c, FDC_REG_STATE=0x10,
    FDC_REG_EPOCH=0x14, FDC_REG_SEQUENCE=0x18,
    FDC_REG_CAPTURE_LO=0x1c, FDC_REG_CAPTURE_HI=0x20,
    FDC_REG_RAW0=0x24, FDC_REG_RAW1=0x28, FDC_REG_QUALITY=0x2c,
    FDC_REG_DIAGNOSTIC=0x30, FDC_REG_OVERWRITTEN=0x34,
    FDC_REG_ERRORS=0x38, FDC_REG_FRAME_PRESENT=0x3c,
    FDC_REG_FREEZE_LO=0x40, FDC_REG_FREEZE_HI=0x44,
    FDC_REG_LATEST_EPOCH=0x48,
    FDC_REG_CTRL_EPOCH=0x50, FDC_REG_CTRL_SEQUENCE=0x54,
    FDC_REG_CTRL_PITCH=0x58, FDC_REG_CTRL_GAIN=0x5c,
    FDC_REG_CTRL_FLAGS=0x60, FDC_REG_CTRL_COMMIT=0x64,
    FDC_REG_CTRL_ACK=0x68, FDC_REG_CTRL_STATUS=0x6c
};
enum {
    FDC_FAULT_IO=1u, FDC_FAULT_ID=2u, FDC_FAULT_ABI=4u,
    FDC_FAULT_TIME=8u, FDC_FAULT_SNAPSHOT_TIMEOUT=16u,
    FDC_FAULT_CONTROL_TIMEOUT=32u, FDC_FAULT_CONTROL_REJECTED=64u,
    FDC_FAULT_SEQUENCE_EXHAUSTED=128u
};
enum {
    FDC_PUBLISH_CANCELLED=-2, FDC_PUBLISH_ERROR=-1,
    FDC_PUBLISH_BUSY=0, FDC_PUBLISH_APPLIED=1
};

typedef struct {
    uint32_t epoch, sequence; /* Driver control sequence, not raw sequence. */
    uint16_t pitch_code;      /* Absolute pitch14, never MIDI bend. */
    uint32_t gain_q16;        /* 0..65536; must retain bit16. */
    uint8_t active;
    theremin_quality_t pitch_quality, volume_quality;
} fdc_ae350_control_t;

typedef struct {
    /* Ordered, nonblocking accesses, byte offsets from FDC_AE350_BASE.
     * A zero result reports a real bus/access failure. NULL read/write use
     * volatile MMIO with the existing RISC-V fence iorw,iorw convention. */
    int (*read32)(void *context, uint32_t offset, uint32_t *value);
    int (*write32)(void *context, uint32_t offset, uint32_t value);
    uint64_t (*now_us)(void *context); /* Monotonic64 microseconds, required. */
    /* Optional native-owner integration. BUSY retries identical payload;
     * APPLIED means actually applied, ERROR failed. CANCELLED is a nonfatal
     * terminal result after actual stop/MIDI replacement; it never writes a
     * control COMMIT or pretends that this output was applied. The shared owner
     * must define native completion, not equate enqueue/APB write with ACK.
     * This module itself never touches the shared native command queue. */
    int (*publish)(void *context, const fdc_ae350_control_t *control);
    void *context;
} fdc_ae350_io_t;

typedef struct {
    uint32_t polls, frames, duplicates, old_frames, aged_frames, epoch_changes;
    uint32_t snapshots, control_commits, control_acks, output_applied, output_cancelled;
    uint32_t source_overwritten, source_errors, diagnostic, quality_status, faults;
    uint32_t telemetry_raw0,telemetry_raw1,telemetry_sequence;
    uint64_t telemetry_at_us;
    uint8_t telemetry_present,telemetry_fresh,telemetry_flags0,telemetry_flags1;
    uint8_t available, probed, enabled;
} fdc_ae350_stats_t;

/* Static storage, single cooperative-loop owner; fields after stats private.
 * No heap, interrupts, polling waits or multiple APB owners. */
typedef struct {
    fdc_ae350_io_t io;
    fdc_ae350_stats_t stats;
    theremin_t algorithm;
    theremin_config_t staged;
    theremin_output_t output;
    fdc_ae350_control_t latest, sending;
    uint32_t words[13], epoch, frame_sequence, control_sequence;
    uint32_t config_revision, source_errors;
    uint64_t last_now, last_update, next_snapshot, snapshot_started;
    uint64_t control_started;
    uint64_t sample_at;
    uint8_t have_now, have_epoch, staged_valid, latest_valid, latest_known;
    uint8_t probe, snapshot_phase, snapshot_index, control_phase, control_index;
    uint8_t restart_requested, restart_sent, await_epoch, frame_pending;
} fdc_ae350_t;

/* init performs no bus access; a disabled hardware capability probes cleanly.
 * Runtime performance is disabled until set_enabled(true). */
bool fdc_ae350_init(fdc_ae350_t *state, const fdc_ae350_io_t *io,
                    const theremin_config_t *config);
unsigned fdc_ae350_poll(fdc_ae350_t *state); /* At most2MMIO per invocation. */
/* Observes a gap with no private control/CTRL_ACK, raw snapshot/frame, or
 * restart/epoch transition in flight. It performs no IO. Continue bounded
 * poll calls until true before applying configuration or starting restart. */
bool fdc_ae350_quiescent(const fdc_ae350_t *state);
bool fdc_ae350_set_enabled(fdc_ae350_t *state, bool enabled);
bool fdc_ae350_request_restart(fdc_ae350_t *state);
bool fdc_ae350_stage_config(fdc_ae350_t *state,
                           const theremin_config_t *config);
bool fdc_ae350_apply_config(fdc_ae350_t *state);
const theremin_config_t *fdc_ae350_active_config(const fdc_ae350_t *state);
const theremin_config_t *fdc_ae350_staged_config(const fdc_ae350_t *state);
const fdc_ae350_stats_t *fdc_ae350_stats(const fdc_ae350_t *state);
#endif
