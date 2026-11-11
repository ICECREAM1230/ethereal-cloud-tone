#pragma once
#include "b_config.h"

#define B_PROTOCOL_MAX_LINE 1024u
#define B_PROTOCOL_FRAME_CAP 4096u
#define B_PROTOCOL_MAX_EVENTS 8u

typedef enum {
    B_OK, B_BAD_REQUEST, B_INVALID_CONFIG, B_REVISION_CONFLICT, B_BUSY,
    B_QUEUE_FULL, B_IO_ERROR, B_STORAGE_ERROR, B_DRAIN_TIMEOUT, B_UNSUPPORTED
} b_code_t;
typedef enum {
    B_VERB_PING, B_VERB_GET, B_VERB_STATUS, B_VERB_APPLY, B_VERB_PANIC,
    B_VERB_SAVE, B_VERB_STRIKE, B_VERB_STREAM
} b_verb_t;
typedef enum {
    B_LIFECYCLE_BOOT, B_LIFECYCLE_RUNNING, B_LIFECYCLE_QUIESCING,
    B_LIFECYCLE_SAVING, B_LIFECYCLE_RECONFIGURING, B_LIFECYCLE_FAULT
} b_lifecycle_t;
typedef struct {
    uint32_t id, expected_revision;
    bool check_revision;
    b_verb_t verb;
    b_patch_t patch;
    uint32_t beam, duration_ms, period_ms;
} b_request_t;
typedef struct {
    bool ready, include_config, include_state;
    uint32_t id, revision;
    b_code_t code;
} b_reply_t;
typedef struct {
    uint32_t seq, time_ms, channel, duration_ms, velocity;
    int32_t note;
    bool synthetic;
    uint32_t revision;
} b_event_t;
typedef struct {
    uint32_t uptime_ms, revision;
    b_lifecycle_t lifecycle;
    uint16_t raw, shade, playing;
    uint32_t duration[12], velocity[12], hits[12];
    int32_t notes[12];
    struct { int32_t transpose, volume, fine, cutoff, resonance; } controls;
    struct {
        bool usb, ble;
        uint32_t selected, usb_sent, ble_sent, usb_retries, ble_retries, ble_errors;
    } links;
    struct { uint32_t last_gap_us, max_gap_us, max_work_us, missed_periods; } timing;
    struct { uint32_t commands, replies, events; } drops;
    uint32_t illegal_edges, encoder_enabled, ab[4], pressed, heap_free;
    b_event_t events[B_PROTOCOL_MAX_EVENTS];
    size_t event_count;
} b_state_t;

const char *b_code_name(b_code_t);
const char *b_lifecycle_name(b_lifecycle_t);
/* Input excludes LF. Optional single trailing CR supports serial CRLF.
 * ASCII only, 1024 bytes maximum, no quoted strings or shell expansion.
 * out is zeroed on rejection, except a syntactically valid RPC id is retained. */
b_code_t b_protocol_parse_line(const char *line, size_t length, b_request_t *out);
b_code_t b_protocol_parse_argv(int argc, const char *const argv[], b_request_t *out);
/* Returns complete @B JSON + LF length. Returns 0 and sets out[0]=0 on
 * insufficient capacity or invalid input; truncated JSON is never emitted. */
size_t b_protocol_format_reply(char *out, size_t capacity, const b_reply_t *,
                               const b_config_t *, const b_state_t *);
size_t b_protocol_format_state(char *out, size_t capacity, const b_state_t *);
