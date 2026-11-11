#include "control_runtime.h"
void cr_init(control_runtime_t *r,uint8_t engines) {
    volatile uint8_t *p=(volatile uint8_t *)r;
    for(unsigned i=0;i<sizeof(*r);i++)p[i]=0;
    midi_parser_init(&r->parser);r->enabled=engines&7u;
    for(unsigned e=0;e<3;e++)(void)vm_init(&r->manager[e],(uint8_t)e,0);
    for(unsigned s=0;s<3;s++)for(unsigned c=0;c<16;c++)r->routes[s][c]=r->enabled;
}
int cr_set_route(control_runtime_t *r,uint8_t s,uint8_t c,uint8_t engines) {
    if(s>=3 || c>=16 || (engines&~r->enabled) || r->event_pending || r->in_flight_valid || r->events_routed)return 0;
    r->routes[s][c]=engines;return 1;
}
midi_parser_result_t cr_push(control_runtime_t *r,uint8_t s,uint8_t byte,uint64_t tick) {
    if(r->fault || r->flush_mask)return MIDI_PARSER_WOULD_BLOCK;
    return midi_parser_push(&r->parser,s,byte,tick);
}
void cr_flush(control_runtime_t *r,uint8_t mask) {
    mask&=7u;if(!mask)return;
    (void)midi_parser_flush(&r->parser,mask);
    if(r->event_pending && (mask&(1u<<r->event.source)))r->event_pending=0;
    r->flush_mask|=mask;r->flush_done=0;
}
int cr_pump(control_runtime_t *r) {
    if(r->fault)return 0;
    if(r->flush_mask) {
        for(unsigned e=0;e<3;e++)if((r->enabled&(1u<<e)) && !(r->flush_done&(1u<<e))) {
            if(r->in_flight_valid && r->in_flight.engine==e)continue;
            vm_result_t v=vm_flush_sources(&r->manager[e],r->flush_mask);
            if(v==VM_OK || v==VM_IGNORED)r->flush_done|=(uint8_t)(1u<<e);
            else if(v!=VM_BACKPRESSURE && v!=VM_NOT_READY)r->fault=0x100u+(unsigned)v;
        }
        if((r->flush_done&r->enabled)==r->enabled){r->flush_mask=0;return 1;}
        return 0;
    }
    if(!r->event_pending) {
        if(!midi_parser_peek_event(&r->parser,&r->event))return 0;
        r->event_pending=1;r->event_done=0;
    }
    uint8_t mask=r->event.status<0xf0 ? r->routes[r->event.source][r->event.status&15u] : r->enabled;
    for(unsigned e=0;e<3;e++)if((mask&(1u<<e)) && !(r->event_done&(1u<<e))) {
        if(r->in_flight_valid && r->in_flight.engine==e)continue;
        vm_result_t v=vm_submit(&r->manager[e],&r->event);
        if(v==VM_OK || v==VM_IGNORED)r->event_done|=(uint8_t)(1u<<e);
        else if(v!=VM_BACKPRESSURE && v!=VM_NOT_READY)r->fault=0x200u+(unsigned)v;
    }
    if((r->event_done&mask)==mask) {
        (void)midi_parser_pop_event(&r->parser,0);r->event_pending=0;r->events_routed++;return 1;
    }
    return 0;
}
int cr_next_command(control_runtime_t *r,control_voice_command_t *out,uint32_t *sequence) {
    if(r->in_flight_valid || !out || !sequence)return 0;
    for(unsigned i=0;i<3;i++) {
        unsigned e=(r->rr+i)%3u;
        if((r->enabled&(1u<<e)) && vm_command_peek(&r->manager[e],out)==VM_OK) {
            r->in_flight.engine=out->engine;r->in_flight.voice=out->voice;
            r->in_flight.kind=out->kind;r->in_flight.source=out->source;
            r->in_flight.channel=out->channel;r->in_flight.note=out->note;
            r->in_flight.velocity=out->velocity;r->in_flight.volume=out->volume;
            r->in_flight.pan=out->pan;r->in_flight.aux=out->aux;
            r->in_flight.generation=out->generation;r->in_flight.pitch=out->pitch;
            r->in_flight_valid=1;r->rr=(uint8_t)((e+1u)%3u);
            r->next_sequence=r->next_sequence==0xffffffffu?1u:r->next_sequence+1u;
            r->in_flight_sequence=r->next_sequence;*sequence=r->next_sequence;return 1;
        }
    }
    return 0;
}
vm_result_t cr_backend_ack(control_runtime_t *r,uint32_t sequence) {
    if(!r->in_flight_valid || sequence!=r->in_flight_sequence)return VM_STALE;
    vm_result_t v=vm_command_consume(&r->manager[r->in_flight.engine],&r->in_flight);
    if(v==VM_OK)r->in_flight_valid=0;else r->fault=0x300u+(unsigned)v;
    return v;
}
vm_result_t cr_idle(control_runtime_t *r,uint8_t e,uint8_t voice,uint32_t gen) {
    if(e>=3)return VM_INVALID;
    if(r->in_flight_valid && r->in_flight.engine==e)return VM_BACKPRESSURE;
    return vm_feedback_idle(&r->manager[e],voice,gen);
}
