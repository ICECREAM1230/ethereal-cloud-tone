#pragma once
#include "web_raw_midi.h"

typedef struct {
    web_raw_midi_request_t request;
    uint8_t client;
    int64_t expires;
} web_raw_job_t;

// Four fixed slots, copied under a short lock. HTTP publishes only the job ID;
// the owner must take it and revalidate token/context/lease before submission.
bool web_raw_queue_push(const web_raw_job_t *job, uint32_t *id);
bool web_raw_queue_take(uint32_t id, web_raw_job_t *job);
// Drops pending jobs without reusing IDs. Call at owner recovery boundaries.
void web_raw_queue_reset(void);
