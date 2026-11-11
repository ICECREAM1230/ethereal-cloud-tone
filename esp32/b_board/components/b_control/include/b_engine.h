#pragma once
#include "b_protocol.h"

#define B_ENGINE_DRAIN_TIMEOUT_MS 500u
typedef enum { B_ACTION_NONE, B_ACTION_RECONFIGURE, B_ACTION_SAVE } b_action_t;
typedef struct {
    b_config_t config;
    harp_model_t model;
    uint32_t revision, stream_period_ms;
    b_lifecycle_t lifecycle;
    b_config_t pending_config;
    b_request_t pending_request;
    uint32_t drain_target_seq, drain_started_ms;
    bool pending, gpio_changed;
} b_engine_t;

bool b_engine_init(b_engine_t *, const b_config_t *defaults);
/* Owner calls after parsing. For maintenance, drain_target_seq MUST name the
 * new panic/quiet target that the caller publishes after this call. A previous
 * quiet report cannot release the barrier. Replies have ready=false until
 * tick/complete produces the final result. No action performs I/O here. */
void b_engine_command(b_engine_t *, const b_request_t *, uint32_t now_ms,
                      uint32_t drain_target_seq, b_reply_t *);
/* router_seq must be the last target the router actually processed. Matching
 * target seq + quiet returns one action and enters SAVING/RECONFIGURING.
 * Timeout resumes RUNNING, rearms inputs, and returns a drain_timeout reply. */
b_action_t b_engine_tick(b_engine_t *, uint32_t now_ms, uint32_t router_seq,
                          bool router_quiet, b_reply_t *);
/* Call only after the returned action's GPIO/storage operation has finished.
 * Failed GPIO action leaves config/revision unchanged. For channel/polarity
 * only, gpio_changed=false and the platform may complete immediately. */
void b_engine_complete(b_engine_t *, bool success, b_reply_t *);
/* Sampling is suppressed during maintenance. Physical controls are copied
 * back to config and advance revision before subsequent RPC comparisons. */
void b_engine_sample(b_engine_t *, uint16_t levels, const uint8_t ab[4],
                     uint8_t pressed, uint8_t enabled, uint32_t now_ms);
bool b_engine_sync_controls(b_engine_t *);
/* Fills only engine-owned fields; caller adds raw/links/timing/drops/events. */
void b_engine_snapshot(const b_engine_t *, b_state_t *);
