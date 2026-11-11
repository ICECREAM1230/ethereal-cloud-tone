#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Control ownership is independent of MIDI sources, HTTP tokens, the owner
 * context and the BLE connection epoch. None of these functions performs IO. */
typedef enum {
    CONTROL_SOURCE_NONE = 0,
    CONTROL_SOURCE_WIFI,
    CONTROL_SOURCE_BLE_CTRL,
} control_source_kind_t;

typedef struct {
    uint32_t epoch;
    uint8_t kind;
} control_source_ref_t;

typedef enum {
    CONTROL_SOURCE_IDLE = 0, /* empty key heartbeat; never creates a hold */
    CONTROL_SOURCE_OUTPUT,   /* parameter or asynchronous output transaction */
    CONTROL_SOURCE_KEYS,     /* nonempty key snapshot */
    CONTROL_SOURCE_RAW,      /* raw MIDI stream; empty snapshots cannot release it */
} control_source_activity_t;

#define CONTROL_SOURCE_CLIENTS 4u
#define CONTROL_SOURCE_LEASE_US 1200000LL

/* Boot/test setup only; never reset epochs while producers are running. */
void control_source_init(void);
/* Same-kind callers share a mode fence and retain their existing clients.
 * A different kind can take over only after all performance/output holds
 * have completed or expired. GETs must not call this function. Epochs are
 * never recycled; allocation exhaustion rejects new ownership. */
bool control_source_claim(control_source_kind_t kind, int64_t now,
                          control_source_ref_t *out);
bool control_source_valid(control_source_ref_t source);
bool control_source_same(control_source_ref_t a, control_source_ref_t b);
/* Exact-fence revoke is harmless after another claim or reconnect. */
void control_source_revoke(control_source_ref_t source);

/* Call after validation and before publishing an event. expires must be
 * within the existing 1200 ms lease. token=0 is reserved for an anonymous
 * OUTPUT job (sequence=its immutable job ID), in a separate slot which does
 * not consume one of the four session clients. A valid IDLE never creates
 * an output hold or renews performance. */
bool control_source_reserve(control_source_ref_t source, uint32_t token,
                            uint32_t sequence, int64_t now, int64_t expires,
                            control_source_activity_t activity);
/* Anonymous services share this allocator and the separate token=0 OUTPUT
 * slot. Keep the returned ticket apart from service-specific job IDs; never
 * recycle tickets during a boot, including after cancellation. */
bool control_source_reserve_job(control_source_ref_t source, int64_t now,
                                int64_t expires, uint32_t *ticket);
/* The voice owner completes ordinary events after dispatch and asynchronous
 * jobs only after adoption/cancellation. Token and sequence prevent an old
 * completion from clearing a later reservation. accepted IDLE releases a
 * snapshot hold after adoption; a RAW hold survives until its own release
 * or lease expiry. accepted=false cancels this event's performance hold. */
void control_source_complete(control_source_ref_t source, uint32_t token,
                             uint32_t sequence,
                             control_source_activity_t activity, bool accepted);
/* Source-local release after the owner has actually flushed that token.
 * A lower sequence cannot release a later queued performance reservation. */
void control_source_release(control_source_ref_t source, uint32_t token,
                            uint32_t sequence);
