/* SPDX-License-Identifier: MIT */
#ifndef CONTROL_SERVICE_H
#define CONTROL_SERVICE_H
#include <stdint.h>
#include "control_compile.h"
#include "../control_migration/firmware/control_runtime.h"

#define CS_APB_BASE UINT32_C(0xF8010000)
#define CS_ACK_TIMEOUT_MS 1000u
#define CS_ROUTE_TIMEOUT_MS 1000u
/* Callbacks receive byte offsets from CS_APB_BASE. Return nonzero on success.
 * They must be ordered and nonblocking. NULL io selects volatile RV32 MMIO
 * with fence iorw,iorw after each operation; PSLVERR then uses the CPU trap.
 */
typedef struct {
    int (*read32)(void *context, uint32_t offset, uint32_t *value);
    int (*write32)(void *context, uint32_t offset, uint32_t value);
    void *context;
} cs_io_t;
typedef struct { uint8_t rx, pump, tx, idle, sysex; } cs_budget_t;
enum { CS_WAIT_READY, CS_RUNNING, CS_SWITCHING, CS_FLUSHING, CS_FAILED };
enum {
    CS_FAULT_IO=1u, CS_FAULT_ID=2u, CS_FAULT_ABI=4u,
    CS_FAULT_DIRTY_LINK=8u, CS_FAULT_RECORD=16u, CS_FAULT_RUNTIME=32u,
    CS_FAULT_ACK_TIMEOUT=64u, CS_FAULT_ROUTE_TIMEOUT=128u,
    CS_FAULT_LINK_LOST=256u, CS_FAULT_BACKEND=512u,
    CS_FAULT_COMPILE=1024u, CS_FAULT_TEMPLATE=2048u
};
typedef enum { CS_OK, CS_BUSY, CS_INVALID, CS_ERROR } cs_result_t;
typedef struct {
    uint32_t polls, raw_bytes, events, commands, command_acks;
    uint32_t idle, idle_stale, flush, flush_complete, last_flush_sequence;
    uint32_t sysex_chunks, sysex_bytes, sysex_aborted;
    uint32_t mode_changes, ack_observations, last_backend_ack;
    uint32_t fault, fault_count, backend_status, not_ready_polls;
    uint32_t backend_fault, tx_result, stale_off;
    uint32_t physical_packets,physical_acks,template_words,template_commits,template_unsupported;
    uint8_t ready, state, mode, requested_mode;
} cs_stats_t;
/* Static storage, one owner in the cooperative main loop. Do not put this
 * approximately 22 KiB object on the stack or mutate it from an ISR.
 * commands/command_acks count logical jobs; physical_packets/physical_acks
 * include fragments and boot/template work. Fields are private.
 */
typedef struct {
    control_runtime_t runtime;
    control_compiler_t compiler;
    cs_io_t io;
    cs_stats_t stats;
    control_sysex_chunk_t last_sysex;
    uint64_t rx_tick;
    uint32_t rx_meta, rx_sequence, tx_started_ms, route_started_ms;
    uint32_t physical_sequence,next_physical_sequence;
    uint32_t mode_sequence,mode_status_published;
    uint8_t engines, ready_once, online_seen, fatal;
    uint8_t rx_held, flush_started, need_recovery, tx_committed;
    uint8_t mode_phase, owner_flush_done, route_target;
} cs_service_t;

extern const cs_budget_t cs_default_budget;
/* Cold init only, after coordinated owner/backend/link reset. BACKEND_STATUS
 * ready means native execution is ready after old ownership/CDC was reset.
 * CPU boot GLOBAL work is sent while online, before waiting for all ready
 * bits; RAW is admitted only after its real ACK and hardware ready. A lost
 * online link requires coordinated reset again.
 */
cs_result_t cs_init(cs_service_t *s,const cs_io_t *io,uint8_t engines,
                    uint8_t initial_mode,uint32_t now_ms);
/* Hard caps per call: 32 RX records, 16 pump calls, 1 TX submit, 8 idle,
 * 8 SysEx chunks and 4 template records. No waiting for ACK, ready or route
 * completion inside a call. All template compilation/refresh has fixed bounds.
 * Returns useful progress, not elapsed-time/WCET. now_ms is FPGA wall time.
 */
unsigned cs_poll(cs_service_t *s,uint32_t now_ms,const cs_budget_t *budget);
/* Stop new RAW reads, finish already captured byte and parser events, flush
 * owners only, drain real ACKs, issue route request and await backend receipt.
 * Parser partial/running status and all generations/sequences survive.
 * requested_mode is the sole desired route. A request during an existing
 * switch coalesces to the latest desired mode; the already sent route target
 * remains immutable until its ACK, then the normal drain/flush runs again.
 */
cs_result_t cs_request_mode(cs_service_t *s,uint8_t mode);
const cs_stats_t *cs_get_stats(const cs_service_t *s);
#endif
