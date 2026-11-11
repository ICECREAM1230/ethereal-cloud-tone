#ifndef CONTROL_RUNTIME_H
#define CONTROL_RUNTIME_H
#include "midi_parser.h"
#include "voice_manager.h"
typedef struct {
    midi_parser_t parser;
    voice_manager_t manager[3];
    control_midi_event_t event;
    control_voice_command_t in_flight;
    uint8_t routes[3][16],enabled,event_pending,event_done,in_flight_valid,rr;
    uint8_t flush_mask,flush_done;
    uint32_t next_sequence,in_flight_sequence,events_routed,fault;
} control_runtime_t;
void cr_init(control_runtime_t *r,uint8_t engines);
/* Configure at quiescent ownership handover, before admitting MIDI traffic. */
int cr_set_route(control_runtime_t *r,uint8_t source,uint8_t channel,uint8_t engines);
midi_parser_result_t cr_push(control_runtime_t *r,uint8_t source,uint8_t byte,uint64_t tick);
/* Flush is an ordered fence; event routing pauses while owner cleanup retries. */
void cr_flush(control_runtime_t *r,uint8_t source_mask);
int cr_pump(control_runtime_t *r);
/* Freeze the selected owner until backend ACK. APB COMMIT alone is insufficient. */
int cr_next_command(control_runtime_t *r,control_voice_command_t *out,uint32_t *sequence);
vm_result_t cr_backend_ack(control_runtime_t *r,uint32_t sequence);
vm_result_t cr_idle(control_runtime_t *r,uint8_t engine,uint8_t voice,uint32_t generation);
#endif
