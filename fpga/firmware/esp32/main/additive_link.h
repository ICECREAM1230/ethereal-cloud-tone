#pragma once
/* 加法合成器保留开发代码：未完成、未接入（2026-10-24）。
 * 当前产品不编译或调用该模块；接口、契约和历史测试仅供后续继续开发，
 * 不代表已完成当前 FPGA 的握手、控制或实板验收。
 */
#include "additive_patch.h"
#include <stdint.h>

typedef enum {
    ADDITIVE_LINK_NONE=0, ADDITIVE_LINK_UNAVAILABLE, ADDITIVE_LINK_TIMEOUT,
    ADDITIVE_LINK_IO, ADDITIVE_LINK_CANCELLED
} additive_link_fault_t;
typedef struct {
    bool available, valid, selected, ready, busy, patch_known;
    uint32_t fault, instance_id, revision, commit_id, completed_request_id;
    additive_patch_t confirmed_patch;
} additive_link_state_t;

/* Readiness/identity checks do not need a copy of all 32 partials. */
typedef struct {
    bool available, valid, selected, ready, busy, patch_known;
    uint32_t fault, instance_id, revision, commit_id, completed_request_id;
} additive_link_status_t;

void additive_link_init(void);
/* RAM-only immutable copy. Reject invalid/zero-id, unprobed, faulted or busy
 * requests. HTTP binding/context authorization stays with voice_alloc. */
bool additive_link_request(const additive_patch_t *patch, bool selected, uint32_t request_id);
/* RAM-only cancellation, safe from the owner. Unknown COMMIT remains fenced;
 * after its ACK, service submits a confirmed/default patch with selected=0. */
void additive_link_cancel(void);
/* SPI-owner only. Changes instance, clears confirmed identity/cache and queues
 * disabled default initialization; preserves any unknown COMMIT fence. */
void additive_link_invalidate(void);
/* SPI-owner only. Local mode changes cancel host identities in RAM without
 * submitting DISABLE/PROBE. An unknown COMMIT remains a read-only fence. */
void additive_link_local_fence(bool fault);
/* ENG1 completion proves actual selection, never the active patch or a host
 * request's ACK. Remains frozen until explicit remote handoff completes. */
void additive_link_adopt_local(bool available, bool selected);
/* SPI-owner only, after REMT released the local lock. Restore a real disabled
 * default probe, behind any still unknown COMMIT acknowledgement. */
void additive_link_remote_resume(void);
/* SPI-owner only, all SPI work shares the caller's deadline. New shadow writes
 * require the actual global disable + all-source flush acknowledgement. */
void additive_link_service(int64_t deadline_us, bool globally_disabled_and_flushed);
void additive_link_state(additive_link_state_t *out);
/* Coherent RAM-only metadata snapshot under the same lock as full state. */
void additive_link_status(additive_link_status_t *out);
