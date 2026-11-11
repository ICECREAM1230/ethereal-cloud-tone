/* SPDX-License-Identifier: MIT */
#include "control_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint32_t meta,sequence;uint64_t tick; } raw_t;
typedef struct { uint8_t engine,voice;uint32_t generation; } idle_t;
typedef struct {
    cs_service_t *service;
    raw_t raw[256];idle_t idle[32];
    unsigned rn,fn,raw_pops,flush_pops,idle_pops,meta_reads,operations,max_ops;
    uint32_t ready,id,abi,ack,shadow[5],pending[5],pending_seq,result,backend_fault;
    uint32_t route,route_target,recovery,overflow;
    unsigned route_busy,route_writes,commits,recover_writes,auto_ack;
    unsigned fail_read,fail_write;
} model_t;
static cs_service_t s;
static model_t h;
static uint32_t now;
static unsigned tests;
static void raw(unsigned source,unsigned byte,uint64_t tick) {
    assert(h.rn<256);h.raw[h.rn++]=(raw_t){byte|(source<<8),0,tick};
}
static void flush(unsigned mask,int recovery) {
    assert(h.rn<256);h.raw[h.rn++]=(raw_t){mask|0xff00u|0x10000u|(recovery?0x20000u:0),91,1000};
    if(recovery){h.recovery=1;h.overflow=1;}
}
static void feedback(unsigned engine,unsigned voice,uint32_t gen) {
    assert(h.fn<32);h.idle[h.fn++]=(idle_t){(uint8_t)engine,(uint8_t)voice,gen};
}
static int empty_commands(void) {
    if(s.runtime.in_flight_valid || s.runtime.flush_mask)return 0;
    for(unsigned e=0;e<3;e++)if(vm_pending_commands(&s.runtime.manager[e]))return 0;
    return 1;
}
static int read32(void *ctx,uint32_t off,uint32_t *value) {
    model_t *m=ctx;m->operations++;
    if(m->fail_read && m->fail_read==off+1)return 0;
    switch(off) {
    case 0:*value=m->id;break;case 4:*value=m->abi;break;
    case 0x0c:*value=(m->rn?1u:0u)|(m->recovery?4u:0u)|(m->overflow?8u:0u)|
        (m->pending_seq?128u:0u)|(m->fn?1024u:0u)|
        (m->rn && (m->raw[0].meta&0x10000u)?32u:0u);break;
    case 0x10:assert(m->rn);m->meta_reads++;*value=m->raw[0].meta;break;
    case 0x14:assert(m->rn);*value=(uint32_t)m->raw[0].tick;break;
    case 0x18:assert(m->rn);*value=(uint32_t)(m->raw[0].tick>>32);break;
    case 0x20:assert(m->rn);*value=m->raw[0].sequence;break;
    case 0x58:*value=m->ack;break;case 0x5c:*value=m->pending_seq?1u:0u;break;
    case 0x60:assert(m->fn);*value=m->idle[0].engine|((uint32_t)m->idle[0].voice<<8);break;
    case 0x64:assert(m->fn);*value=m->idle[0].generation;break;
    case 0x70:*value=m->ready;break;
    case 0x74:*value=m->backend_fault;break;case 0x78:*value=m->result;break;
    case 0x84:*value=m->route|(m->route_busy?256u:0u);break;
    default:assert(0);return 0;
    }
    return 1;
}
static int write32(void *ctx,uint32_t off,uint32_t value) {
    model_t *m=ctx;m->operations++;
    if(m->fail_write && m->fail_write==off+1)return 0;
    if(off>=0x40 && off<=0x50) {m->shadow[(off-0x40)/4]=value;return 1;}
    switch(off) {
    case 0x1c:
        assert(value==1 && m->rn);
        if(m->raw[0].meta&0x10000u){assert(empty_commands());m->flush_pops++;}
        else m->raw_pops++;
        memmove(m->raw,m->raw+1,(--m->rn)*sizeof(m->raw[0]));break;
    case 0x28:assert(!m->rn && empty_commands());m->recover_writes++;m->recovery=0;m->overflow=0;break;
    case 0x54:
        assert(value && !m->pending_seq && s.runtime.in_flight_valid);
        memcpy(m->pending,m->shadow,sizeof(m->pending));m->pending_seq=value;m->commits++;
        if(m->auto_ack){m->ack=value;m->pending_seq=0;}break;
    case 0x68:
        assert(value==1 && m->fn);m->idle_pops++;
        memmove(m->idle,m->idle+1,(--m->fn)*sizeof(m->idle[0]));break;
    case 0x80:
        assert(value<3 && !m->route_busy && empty_commands());
        m->route_target=value;m->route_busy=1;m->route_writes++;break;
    default:assert(0);return 0;
    }
    return 1;
}
static void setup(unsigned mode) {
    const cs_io_t io={read32,write32,&h};
    memset(&h,0,sizeof(h));memset(&s,0,sizeof(s));h.service=&s;
    h.id=0x43544c31;h.abi=0x10000;h.ready=0x107;now=0;
    assert(cs_init(&s,&io,7,(uint8_t)mode,now)==CS_OK);
}
static void poll(void) {
    unsigned before=h.operations;cs_poll(&s,now++,&cs_default_budget);
    unsigned ops=h.operations-before;if(ops>h.max_ops)h.max_ops=ops;
    assert(ops<=512); /* A stopped backend must not make a busy loop. */
}
static void complete(void) {assert(h.pending_seq);h.ack=h.pending_seq;h.pending_seq=0;}
static void route_done(void){assert(h.route_busy);h.route=h.route_target;h.route_busy=0;}
static void boot(unsigned mode) {
    setup(mode);
    for(unsigned i=0;i<20 && !h.route_busy;i++)poll();
    assert(h.route_busy && !h.raw_pops);route_done();poll();
    assert(cs_get_stats(&s)->state==CS_RUNNING && cs_get_stats(&s)->mode==mode);
}
static void drain(unsigned n) {
    for(unsigned i=0;i<n;i++){poll();if(h.pending_seq)complete();if(h.route_busy)route_done();}
    assert(!s.fatal);
}
static void note(unsigned source,unsigned key) {
    raw(source,0x90,1);raw(source,key,2);raw(source,100,3);
}
static void not_ready(void) {
    setup(2);h.ready=0;note(0,60);
    for(unsigned i=0;i<10;i++)poll();
    assert(!h.meta_reads && !h.commits && !s.stats.ready && s.stats.not_ready_polls);
    h.ready=0x103;poll();assert(!h.meta_reads);
    h.ready=0x107;drain(30);assert(s.stats.raw_bytes==3 && s.stats.commands==1);
    tests++;
}
static void delayed_ack_and_idle(void) {
    boot(2);note(0,60);poll();
    assert(h.pending_seq && s.runtime.in_flight_valid && s.stats.command_acks==0);
    uint32_t gen=h.pending[4],seq=h.pending_seq;
    for(unsigned i=0;i<10;i++)poll();
    assert(h.commits==1 && s.runtime.in_flight_valid);
    h.ack=seq+99;poll();assert(s.stats.command_acks==0 && s.runtime.in_flight_valid);
    complete();poll();assert(s.stats.command_acks==1);
    raw(0,60,4);raw(0,0,5);poll();assert(h.pending_seq && ((h.pending[0]>>16)&255u)==VM_OFF);
    feedback(2,0,gen);poll();assert(!h.idle_pops);
    complete();poll();assert(h.idle_pops==1 && s.stats.idle==1);
    feedback(2,0,gen-1);poll();assert(s.stats.idle_stale==1 && h.idle_pops==2);
    tests++;
}
static void source_and_tick(void) {
    boot(2);h.auto_ack=1;
    raw(0,0x90,UINT64_C(0x100000001));raw(1,0xc0,9);
    raw(0,60,UINT64_C(0x100000002));raw(1,7,10);raw(2,0x90,11);
    raw(0,100,UINT64_C(0x100000003));raw(2,64,12);raw(2,80,13);
    drain(40);assert(s.stats.raw_bytes==8 && s.stats.events==3 && s.stats.commands==2);
    assert(s.runtime.manager[2].voices[0].source==0);
    assert(s.runtime.manager[2].voices[1].source==2);
    assert(s.runtime.parser.sources[0].short_start==UINT64_C(0x100000001));
    tests++;
}
static void ordered_flush(void) {
    boot(2);note(0,60);poll();assert(h.pending_seq);
    flush(1,1);poll();assert(s.flush_started && !h.flush_pops && !h.recover_writes);
    complete();
    for(unsigned i=0;i<30 && !h.flush_pops;i++){poll();if(h.pending_seq)complete();}
    assert(h.flush_pops==1 && !s.rx_held && empty_commands());
    drain(2);assert(h.recover_writes==1 && s.stats.flush==1 && s.stats.flush_complete==1);
    assert(s.stats.last_flush_sequence==91);
    tests++;
}
static void sysex_bounded(void) {
    boot(2);raw(0,0xf0,1);for(unsigned i=0;i<90;i++)raw(0,i&127u,i+2);raw(0,0xf7,100);
    drain(80);assert(s.stats.raw_bytes==92 && s.stats.sysex_bytes==92 && s.stats.sysex_chunks==31);
    assert(s.stats.sysex_aborted==0 && s.last_sysex.last);
    tests++;
}
static void route_running_status(void) {
    boot(2);raw(0,0x90,1);raw(0,60,2);poll();
    assert(cs_request_mode(&s,1)==CS_OK);raw(0,100,3);
    unsigned read_before=h.meta_reads;
    for(unsigned i=0;i<30 && !h.route_busy;i++)poll();
    assert(h.route_busy && h.meta_reads==read_before && s.stats.mode==2);
    assert(cs_request_mode(&s,0)==CS_BUSY);
    route_done();drain(30);
    assert(s.stats.mode==1 && s.stats.raw_bytes==3 && s.stats.commands==1);
    assert((h.pending[0]&255u)==1 && s.runtime.parser.sources[0].running_status==0x90);
    tests++;
}
static void route_drains_owner(void) {
    boot(2);note(0,60);poll();complete();poll();
    assert(cs_request_mode(&s,0)==CS_OK);
    unsigned writes=h.route_writes;
    for(unsigned i=0;i<20 && !h.pending_seq;i++)poll();
    assert(h.pending_seq && ((h.pending[0]>>16)&255u)==VM_OFF && h.route_writes==writes);
    for(unsigned i=0;i<4;i++)poll();
    assert(h.route_writes==writes);
    complete();drain(20);assert(s.stats.mode==0 && h.route_writes==writes+1);
    assert(s.runtime.manager[2].generation==1); /* No cold re-init at mode switch. */
    tests++;
}
static void opaque_packing(void) {
    boot(0);voice_manager_t *m=&s.runtime.manager[0];
    control_voice_command_t c={0};
    c.engine=0;c.voice=31;c.kind=7;c.source=2;c.channel=15;c.note=127;
    c.velocity=126;c.volume=125;c.pan=124;c.aux=0x8123;c.generation=0x89abcdef;c.pitch=0xfedcba98;
    m->commands[0]=c;m->queue_count=1;poll();
    assert(h.pending[0]==0x00071f00 && h.pending[1]==0xfedcba98);
    assert(h.pending[2]==0x7c7d7e7f && h.pending[3]==0x81230f02 && h.pending[4]==0x89abcdef);
    tests++;
}
static void backpressure_and_budget(void) {
    boot(2);const cs_budget_t b={1,1,1,1,1};
    for(unsigned i=0;i<80;i++)raw(0,0xf8,i);
    for(unsigned i=0;i<80;i++) {
        unsigned old=h.raw_pops;cs_poll(&s,now++,&b);assert(h.raw_pops-old<=1);
    }
    assert(s.stats.raw_bytes==80 && s.stats.events==80);
    tests++;
}
static void faults_and_ack_wrap(void) {
    boot(2);note(0,60);poll();uint32_t seq=h.pending_seq;
    h.ready=0x7;poll();assert(s.fatal && s.runtime.in_flight_valid && h.pending_seq==seq);
    h.ready=0x107;poll();assert(s.fatal && h.pending_seq==seq);
    boot(2);note(0,60);poll();now+=CS_ACK_TIMEOUT_MS+1;poll();
    assert(s.stats.fault&CS_FAULT_ACK_TIMEOUT);
    boot(2);h.fail_read=0x0d;poll();assert(s.stats.fault&CS_FAULT_IO);
    setup(2);s.runtime.next_sequence=UINT32_MAX;drain(10);note(0,60);poll();assert(h.pending_seq==1);
    tests++;
}
static void backend_results(void) {
    boot(2);note(0,60);poll();h.result=2;complete();poll();
    assert(s.fatal && (s.stats.fault&CS_FAULT_BACKEND) && s.stats.tx_result==2);
    assert(s.runtime.in_flight_valid && s.stats.command_acks==0);
    boot(2);note(0,60);poll();h.result=1;complete();poll();
    assert(s.fatal && s.stats.command_acks==0); /* Stale ON is not success. */
    boot(2);note(0,60);poll();complete();poll();
    raw(0,60,4);raw(0,0,5);poll();assert(h.pending_seq);
    h.result=1;complete();poll();
    assert(!s.fatal && s.stats.stale_off==1 && s.stats.command_acks==2);
    boot(2);h.backend_fault=0x80000001u;note(0,60);poll();
    assert(s.fatal && s.stats.backend_fault==0x80000001u && !h.raw_pops);
    tests++;
}
static void parser_backpressure(void) {
    boot(2);const cs_budget_t hold={32,0,0,0,0};
    for(unsigned i=0;i<30;i++)raw(0,0xf8,i);
    cs_poll(&s,now++,&hold);
    assert(s.rx_held && h.raw_pops==MIDI_PARSER_QUEUE_DEPTH);
    uint32_t meta=s.rx_meta;uint64_t tick=s.rx_tick;unsigned pops=h.raw_pops;
    for(unsigned i=0;i<5;i++)cs_poll(&s,now++,&hold);
    assert(s.rx_held && s.rx_meta==meta && s.rx_tick==tick && h.raw_pops==pops);
    drain(40);assert(s.stats.raw_bytes==30 && s.stats.events==30 && !s.rx_held);
    tests++;
}
static void source_flush_isolation(void) {
    boot(2);h.auto_ack=1;note(0,60);note(2,64);drain(30);
    flush(1,0);drain(20);
    assert(s.stats.flush_complete==1 && !s.runtime.parser.sources[0].running_status);
    assert(s.runtime.parser.sources[2].running_status==0x90);
    assert(s.runtime.manager[2].voices[0].state==VM_RELEASE);
    assert(s.runtime.manager[2].voices[1].state==VM_HELD);
    raw(2,64,9);raw(2,0,10);drain(10);
    assert(s.runtime.manager[2].voices[1].state==VM_RELEASE);
    tests++;
}
static void extended_initial_on(void) {
    boot(0);note(0,60);poll();
    assert(h.pending_seq && (h.pending[0]&0xffffffu)==0 && h.pending[3]>>16==0xa000u);
    complete();poll();assert(h.pending_seq && ((h.pending[0]>>16)&255u)==5);
    complete();poll();assert(s.stats.command_acks==2 && !s.fatal);
    tests++;
}
static void idle_before_queued_off(void) {
    const cs_budget_t hold={8,8,0,4,2};
    boot(2);note(0,60);poll();uint32_t gen=h.pending[4];complete();poll();
    raw(0,60,4);raw(0,0,5);cs_poll(&s,now++,&hold);
    assert(!h.pending_seq && vm_pending_commands(&s.runtime.manager[2])==1);
    feedback(2,0,gen);cs_poll(&s,now++,&hold);
    assert(!h.idle_pops && h.fn==1); /* Same owner, OFF not accepted yet. */
    poll();assert(h.pending_seq && !h.idle_pops);complete();poll();
    assert(h.idle_pops==1 && s.stats.idle==1 && !s.fatal);
    tests++;
}
int main(void) {
    not_ready();delayed_ack_and_idle();source_and_tick();ordered_flush();sysex_bounded();
    route_running_status();route_drains_owner();opaque_packing();backpressure_and_budget();faults_and_ack_wrap();
    backend_results();parser_backpressure();source_flush_isolation();extended_initial_on();
    idle_before_queued_off();
    printf("PASS control_service: %u groups; storage=%zu bytes; bounded MMIO, real ACK, idle, flush, route and opaque packing\n",tests,sizeof(s));
    return 0;
}
