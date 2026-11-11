#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "reverb_control_contract.h"
typedef enum {
    REVERB_LINK_OK=0,REVERB_LINK_UNAVAILABLE,REVERB_LINK_IO,
    REVERB_LINK_TIMEOUT,REVERB_LINK_CANCELLED,REVERB_LINK_PROTOCOL,REVERB_LINK_RESET
} engine_reverb_fault_t;
typedef struct {
    bool available,valid,pending;
    uint32_t fault,wet_q15,damp_q15,applied_sequence,generation;
    uint32_t request_id,completed_request_id;
} engine_reverb_state_t;
// RAM-only interfaces. Only engine_link's private service uses SPI.
bool engine_link_reverb_request(uint32_t wet_q15,uint32_t damp_q15,uint32_t request_id);
void engine_link_reverb_cancel(uint32_t request_id);
void engine_link_reverb_state(engine_reverb_state_t *state);
