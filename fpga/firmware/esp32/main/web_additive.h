#pragma once
#include <stddef.h>
#include <stdint.h>
#include "additive_patch.h"

#define WEB_ADDITIVE_MAX_BODY_BYTES 4096u

typedef struct {
    uint32_t token, sequence, context_epoch;
    additive_patch_t patch;
} web_additive_request_t;

typedef struct {
    web_additive_request_t request;
    uint8_t client;
    int64_t expires;
    uint32_t id;
} web_additive_job_t;

// Exact complete additive v1 JSON, including 4 envelopes and 32 partials.
// Bounded span, no NUL terminator required; failure clears the whole output.
bool web_additive_parse(const char *body,size_t length,web_additive_request_t *out);

// One immutable in-flight reservation. No SPI, allocation or blocking wait.
bool web_additive_queue_push(const web_additive_request_t *request,uint8_t client,
                             int64_t expires,uint32_t *id);
// Take an owned copy at most once; reservation remains until cancel. The owner
// must revalidate token/context/lease and cancel on success/failure/abortion.
bool web_additive_queue_take(uint32_t id,web_additive_job_t *job);
bool web_additive_queue_cancel(uint32_t id);
void web_additive_queue_cancel_all(void);
