// Transport-independent v1/v2 control semantics. The voice and SPI owners
// remain the only consumers that may adopt or apply musical state.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    CONTROL_API_WIFI = 1,
    CONTROL_API_BLE_CONTROL = 2,
} control_api_transport_t;

typedef struct {
    control_api_transport_t transport;
    // Wi-Fi uses zero across its short-lived HTTP sockets. Each BLE physical
    // connection supplies a distinct nonzero epoch, including reconnects.
    uint64_t connection_epoch;
} control_api_peer_t;

typedef struct {
    // Relative /api/ path, with its original query string when present.
    const char *path;
    // NULL means GET. Non-NULL means POST, including an empty string.
    // Explicit length preserves rejection of embedded NUL/trailing bytes.
    const char *body;
    size_t body_length;
    control_api_peer_t peer;
    // The HTTP adapter validates X-FPGA-Control: 1. A validated private BLE
    // envelope supplies true; this flag never bypasses session/source checks.
    bool control_authorized;
    // Compatibility fallback for /api/v1/midi/raw. JSON binding takes
    // precedence, as in the existing HTTP handler. BLE normally leaves NULL.
    const char *raw_token;
    const char *raw_sequence;
    const char *raw_context;
    // Optional owned receive buffer. If non-NULL, *owned_body must equal
    // body. prepare consumes it (sets *owned_body=NULL), transferring an
    // accepted DDS document without allocating a second complete copy.
    char **owned_body;
} control_api_request_t;

typedef struct {
    void *context;
    esp_err_t (*write)(void *context, const char *bytes, size_t length);
} control_api_sink_t;

typedef struct {
    unsigned code;
    const char *content_type;
    void *private_data;
} control_api_response_t;

// Initialize response to zero and release it before preparing another one.
// This is the sole dispatch step: it freezes all observations and performs
// any accepted reservation/event publication exactly once. ESP_OK includes
// protocol errors represented by response.code. No transport I/O occurs.
esp_err_t control_api_prepare(const control_api_request_t *request,
                             control_api_response_t *response);

// Repeated emits yield the same complete body until release, even if the
// owner or sessions change. Counting and real sinks may call this separately.
// emit does not read owner state, publish events, or hold a common core lock.
// The caller adds its transport's framing and end-of-body marker.
esp_err_t control_api_emit(const control_api_response_t *response,
                          const control_api_sink_t *sink);

// Idempotent. Frees lazy snapshots/formatting data and immutable DDS views.
void control_api_release(control_api_response_t *response);

// Callback-safe: fence this physical peer immediately without allocation,
// transport I/O or the common session mutex. Closed BLE epochs cannot claim
// a source again. The worker subsequently calls disconnect to clean slots.
// Wi-Fi represents the whole AP peer group; individual HTTP sockets must not
// call this hook. AP_STOP/confirmed zero stations invalidate its exact ref.
void control_api_peer_invalidate(control_api_peer_t peer);

// Invalidate only this peer's session bindings and pending ingress claims.
// The source arbiter owns the associated musical release/fencing behavior.
void control_api_disconnect(control_api_peer_t peer);
