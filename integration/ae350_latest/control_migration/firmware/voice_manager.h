/* SPDX-License-Identifier: MIT */
#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H
#include <stddef.h>
#include <stdint.h>
#include "../include/control_event.h"

#define VM_MAX_VOICES 32u
#define VM_QUEUE_CAPACITY 64u
#define VM_MIDI_SOURCES 3u /* source 3 is the separate native sensor input. */
#define VM_CHANNELS (VM_MIDI_SOURCES * 16u)
enum { VM_SUBTRACTIVE = 0, VM_KS = 1, VM_FM = 2 };
enum { VM_ON = 0, VM_OFF = 1, VM_PITCH = 2, VM_GAIN = 3, VM_KILL = 4,
       VM_SUB_EXPRESSION = 5, VM_KS_REFRESH_LOSS = 6, VM_SUB_BEND = 7, VM_SENSOR = 8 };
enum { VM_IDLE = 0, VM_HELD = 1, VM_RELEASE = 2 };
typedef enum {
    VM_OK = 0, VM_IGNORED, VM_BACKPRESSURE, VM_INVALID,
    VM_NOT_READY, VM_GENERATION_EXHAUSTED, VM_STALE
} vm_result_t;
typedef struct {
    uint8_t voices, has_bend, has_gain_cc, minimum_note;
    uint8_t requires_template, requires_element_publish, has_loss_refresh;
} vm_engine_caps_t;
typedef struct {
    uint16_t bend;
    uint8_t volume, expression, pan, sustain;
    uint8_t wheel, touch_x, touch_y;
} vm_channel_t;
typedef struct {
    uint64_t age, backend_age;
    /* Release order belongs to the ACKed backend occupancy. Preserve it
     * while a replacement ON is retractable; reset only on its real ACK. */
    uint64_t released_at;
    uint32_t generation, pitch;
    uint8_t state, key_down, source, channel, note, velocity;
    uint8_t backend_live, backend_on;
    control_voice_command_t backend;
} vm_voice_t;
typedef struct {
    vm_voice_t voices[VM_MAX_VOICES];
    vm_channel_t channels[VM_CHANNELS];
    control_voice_command_t commands[VM_QUEUE_CAPACITY];
    uint64_t age;
    uint32_t generation;
    uint8_t engine, voice_count, adapter_ready, queue_limit, queue_count, extended;
} voice_manager_t;

/* One instance is the sole allocator of its engine. No dynamic allocation.
 * Serialize all calls in the firmware control task, never directly in an ISR.
 * init is COLD initialization after the matching FPGA engine has reset; it
 * must not be used to clear an operating backend. Use vm_reset at runtime. */
const vm_engine_caps_t *vm_engine_caps(uint8_t engine);
vm_result_t vm_init(voice_manager_t *manager, uint8_t engine, uint8_t queue_limit);
/* Cold ownership handover only. Enables the AE350 backend's full expression
 * and template-dependent raw bend protocol, preserving legacy candidate tests. */
vm_result_t vm_enable_extended(voice_manager_t *manager);
/* Set only after template/patch publication and backend ownership handover.
 * Revoking readiness requires vm_reset plus a drained, idle backend first. */
vm_result_t vm_set_adapter_ready(voice_manager_t *manager, uint8_t ready);
/* ACCEPTED/IGNORED consumes the event. BACKPRESSURE/NOT_READY does not;
 * retain the exact event and retry after progress. Other errors are explicit.
 * nbytes is the full MIDI message length including status (3 for these
 * channel messages). NoteOn duplicates allocate independently; NoteOff matches oldest key_down. */
vm_result_t vm_submit(voice_manager_t *manager, const control_midi_event_t *event);
#if ENABLE_AE350_FDC
/* The existing manager remains the only allocator; source3 is NOT a MIDI
 * channel. No channel array growth or permanently reserved slot is needed.
 * Caller owns one handle and runs in the cooperative control task.
 * transport_busy MUST reflect any cf_packet/tx_committed native transaction:
 * all four mutating APIs return NOT_READY unchanged when it is nonzero.
 * Recompute it immediately before the call; never call from an ISR.
 * Mode/source switch: call vm_sensor_flush(manager,1,transport_busy), wait for
 * its real native ACK (and idle feedback as ownership requires), then perform
 * ordinary MIDI flush7/route. vm_flush_sources remains MIDI-only; it MUST NOT
 * be mistaken for sensor cleanup. Reset includes sensor backend KILL jobs.
 * CLAIM/UPDATE/RELEASE must also be serialized with ordinary vm_submit,
 * flush/reset and feedback. A peeked command is immutable until real ACK.
 */
typedef struct { uint8_t voice; uint32_t generation; } vm_sensor_handle_t;
vm_result_t vm_sensor_claim(voice_manager_t *manager, uint16_t pitch14,
    uint32_t gain_q16, uint8_t transport_busy, vm_sensor_handle_t *handle);
vm_result_t vm_sensor_update(voice_manager_t *manager, const vm_sensor_handle_t *handle,
    uint16_t pitch14, uint32_t gain_q16, uint8_t transport_busy);
vm_result_t vm_sensor_release(voice_manager_t *manager, const vm_sensor_handle_t *handle,
    uint8_t kill, uint8_t transport_busy);
/* Source3-only atomic cleanup, including cancelled unaccepted ONs and real
 * backend OFF/KILL. Source3 remains excluded from ordinary MIDI flush7.
 * The control service must call this in its mode M_OWNERS engine0 phase,
 * then drain actual packet ACKs before the route request. */
vm_result_t vm_sensor_flush(voice_manager_t *manager, uint8_t kill, uint8_t transport_busy);
#endif
/* Copies the current retractable queue head. The copy is valid only until
 * the next mutating API call. While a transport transaction is in flight,
 * serialize event/flush/reset submission until its real backend ACK. */
vm_result_t vm_command_peek(const voice_manager_t *manager,
                            control_voice_command_t *command);
/* Consume only after FPGA acceptance, with the unchanged peeked payload.
 * APB write completion alone is not backend acceptance. */
vm_result_t vm_command_consume(voice_manager_t *manager,
                               const control_voice_command_t *command);
/* Feedback must carry generation captured by the FPGA for that idle edge;
 * do not label an old idle bitmap with the manager's current generation. */
vm_result_t vm_feedback_idle(voice_manager_t *manager, uint8_t voice,
                             uint32_t generation);
/* Atomic: backpressure leaves controllers, keys and queue unchanged. flush
 * clears only selected source controllers, cancels queued Ons, releases tails.
 * reset queues KILL for backend-live slots, clears all controllers and keys,
 * retains monotonic generation and waits for matching FPGA idle feedback. */
vm_result_t vm_flush_sources(voice_manager_t *manager, uint8_t source_mask);
vm_result_t vm_reset(voice_manager_t *manager);
/* Number of queued command snapshots, including retractable Ons. */
size_t vm_pending_commands(const voice_manager_t *manager);
#endif
