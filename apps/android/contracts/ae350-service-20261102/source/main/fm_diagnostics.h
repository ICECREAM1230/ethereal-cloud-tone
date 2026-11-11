// Read-only, non-atomic observations from the FM validation image.
#pragma once
#include "raw_midi.h"

#define ENGINE_FM_DIAGNOSTIC_BLOCKS 4u
#define ENGINE_FM_DIAGNOSTIC_WORDS 11u
#define ENGINE_FM_DIAGNOSTIC_INTERVAL_US 250000
#define ENGINE_FM_DIAGNOSTIC_RESERVE_US 200
#define ENGINE_FM_DIAGNOSTIC_TIMEOUT_US 500000

enum { FM_DIAGNOSTIC_OK, FM_DIAGNOSTIC_UNSUPPORTED, FM_DIAGNOSTIC_NOT_READY,
       FM_DIAGNOSTIC_IO, FM_DIAGNOSTIC_IDENTITY, FM_DIAGNOSTIC_CONTEXT,
       FM_DIAGNOSTIC_TIMEOUT };
typedef struct {
    uint16_t address;
    uint8_t count;
    bool read_ok;
    int64_t started_at_us, completed_at_us;
    uint32_t words[ENGINE_FM_DIAGNOSTIC_WORDS];
} engine_fm_diagnostic_block_t;
typedef struct {
    bool valid;
    uint32_t session_id, engine_epoch, topology_generation;
} engine_fm_diagnostic_context_t;
typedef struct {
    bool supported, pending, valid, raw_cache_valid;
    uint32_t request_id, sample_request_id, error, error_count;
    int64_t requested_at_us, started_at_us, completed_at_us, raw_cached_at_us;
    engine_fm_diagnostic_block_t blocks[ENGINE_FM_DIAGNOSTIC_BLOCKS];
    engine_fm_diagnostic_context_t context_before, context_after;
    raw_midi_state_t raw_cache;
} engine_fm_diagnostics_t;

// RAM only. Repeated requests share an in-flight/rate-limited request ID.
uint32_t engine_link_fm_diagnostics_request(void);
void engine_link_fm_diagnostics_state(engine_fm_diagnostics_t *out);
