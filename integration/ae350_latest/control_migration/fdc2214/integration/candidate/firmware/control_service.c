/* SPDX-License-Identifier: MIT */
#include "control_service.h"

enum { M_NONE, M_DRAIN, M_OWNERS, M_SEND, M_WAIT };
enum { RX_AVAILABLE=1u, RECOVERING=4u, FLUSH_PENDING=32u, TX_FULL=64u,
       CANCEL_HOLD=256u, FB_AVAILABLE=1024u };
const cs_budget_t cs_default_budget={8,8,1,4,2};

static void ordered(void) {
#if defined(__riscv)
    __asm__ volatile("fence iorw,iorw" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}
static int real_read(void *context,uint32_t offset,uint32_t *value) {
    (void)context;*value=*(volatile uint32_t *)(uintptr_t)(CS_APB_BASE+offset);
    ordered();return 1;
}
static int real_write(void *context,uint32_t offset,uint32_t value) {
    (void)context;*(volatile uint32_t *)(uintptr_t)(CS_APB_BASE+offset)=value;
    ordered();return 1;
}
static void fail(cs_service_t *s,uint32_t why) {
    if(!(s->stats.fault&why))s->stats.fault_count++;
    s->stats.fault|=why;s->fatal=1;s->stats.state=CS_FAILED;
}
static int read_reg(cs_service_t *s,uint32_t off,uint32_t *value) {
    if(s->io.read32(s->io.context,off,value))return 1;
    fail(s,CS_FAULT_IO);return 0;
}
static int write_reg(cs_service_t *s,uint32_t off,uint32_t value) {
    if(s->io.write32(s->io.context,off,value))return 1;
    fail(s,CS_FAULT_IO);return 0;
}
static unsigned limit(unsigned value,unsigned cap){return value<cap?value:cap;}
static int commands_empty(const cs_service_t *s) {
    if(s->runtime.in_flight_valid || s->tx_committed || cf_packet(&s->compiler))return 0;
    for(unsigned e=0;e<3;e++)if(vm_pending_commands(&s->runtime.manager[e]))return 0;
    return 1;
}
static int parser_empty(cs_service_t *s) {
    control_midi_event_t e;control_sysex_chunk_t x;
    return !s->runtime.event_pending && !midi_parser_peek_event(&s->runtime.parser,&e) &&
        !midi_parser_peek_sysex(&s->runtime.parser,&x);
}
static void routes(cs_service_t *s,uint8_t mode) {
    /* Sole routing owner; caller has drained events/commands. Preserve all
     * generation counters, source running state and transport sequences. */
    for(unsigned source=0;source<3;source++)for(unsigned channel=0;channel<16;channel++)
        s->runtime.routes[source][channel]=(uint8_t)(1u<<mode);
}
cs_result_t cs_init(cs_service_t *s,const cs_io_t *io,uint8_t engines,
                    uint8_t initial_mode,uint32_t now_ms) {
    uint32_t value;
    if(!s || !engines || (engines&~7u) || initial_mode>2 || !(engines&(1u<<initial_mode)) ||
       (io && (!io->read32 || !io->write32)))return CS_INVALID;
    volatile uint8_t *bytes=(volatile uint8_t *)s;
    for(unsigned i=0;i<sizeof(*s);i++)bytes[i]=0;
    s->io.read32=io?io->read32:real_read;s->io.write32=io?io->write32:real_write;
    s->io.context=io?io->context:0;s->engines=engines;
    s->stats.mode=255;s->stats.requested_mode=initial_mode;
    s->mode_phase=M_SEND;s->route_started_ms=now_ms;
    cr_init(&s->runtime,engines);cf_init(&s->compiler,engines);
    for(unsigned e=0;e<3;e++) {
        if(vm_enable_extended(&s->runtime.manager[e])!=VM_OK ||
           vm_set_adapter_ready(&s->runtime.manager[e],0)!=VM_OK){fail(s,CS_FAULT_RUNTIME);return CS_ERROR;}
    }
    if(!read_reg(s,0,&value))return CS_ERROR;
    if(value!=PC_LINK_ID){fail(s,CS_FAULT_ID);return CS_ERROR;}
    if(!read_reg(s,4,&value))return CS_ERROR;
    if(value!=PC_LINK_ABI){fail(s,CS_FAULT_ABI);return CS_ERROR;}
    if(!read_reg(s,0x5c,&value))return CS_ERROR;
    if(value){fail(s,CS_FAULT_DIRTY_LINK);return CS_ERROR;}
    if(!read_reg(s,0x58,&value))return CS_ERROR;
    s->next_physical_sequence=value;s->stats.last_backend_ack=value;
    return CS_OK;
}
const cs_stats_t *cs_get_stats(const cs_service_t *s){return s?&s->stats:0;}
cs_result_t cs_request_mode(cs_service_t *s,uint8_t mode) {
    if(!s || mode>2 || !(s->engines&(1u<<mode)))return CS_INVALID;
    if(s->fatal)return CS_ERROR;
    s->stats.requested_mode=mode;
    if(s->mode_phase || mode==s->stats.mode)return CS_OK;
    s->mode_phase=M_DRAIN;s->owner_flush_done=0;
    return CS_OK;
}
static int update_ready(cs_service_t *s) {
    uint32_t value;
    if(!read_reg(s,0x70,&value))return 0;
    s->stats.backend_status=value;
    if(s->online_seen && !(value&256u)){fail(s,CS_FAULT_LINK_LOST);return 0;}
    if(value&256u)s->online_seen=1;
    s->stats.ready=(uint8_t)(s->compiler.boot_done && ((value&256u)!=0) && ((value&s->engines)==s->engines));
    if(!s->stats.ready){s->stats.not_ready_polls++;return 1;}
    if(!s->ready_once) {
        for(unsigned e=0;e<3;e++)if(s->engines&(1u<<e))
            if(vm_set_adapter_ready(&s->runtime.manager[e],1)!=VM_OK){fail(s,CS_FAULT_RUNTIME);return 0;}
        s->ready_once=1;
    }
    return 1;
}
static unsigned collect_ack(cs_service_t *s,uint32_t now_ms) {
    uint32_t ack;
    if(!s->tx_committed)return 0;
    if(!read_reg(s,0x58,&ack))return 0;
    if(ack!=s->stats.last_backend_ack){s->stats.ack_observations++;s->stats.last_backend_ack=ack;}
    if(ack==s->physical_sequence && ack!=0) {
        uint32_t result;
        if(!read_reg(s,0x78,&result))return 0;
        s->stats.tx_result=result;
        if(result && !(result==1 && !s->compiler.background && s->runtime.in_flight.kind==VM_OFF)) {
            (void)read_reg(s,0x74,&s->stats.backend_fault);
            fail(s,CS_FAULT_BACKEND);return 0;
        }
        if(result==1){
            s->stats.stale_off++;
            /* A stale RELEASE BEGIN has no stage; never send its fragments. */
            s->compiler.index=(uint8_t)(s->compiler.count-1u);
        }
        int logical=!s->compiler.background;
        int done=cf_ack(&s->compiler);
        s->tx_committed=0;s->stats.physical_acks++;
        if(done && logical){
            if(cr_backend_ack(&s->runtime,s->runtime.in_flight_sequence)!=VM_OK){fail(s,CS_FAULT_RUNTIME);return 0;}
            s->stats.command_acks++;
        }
        return 1;
    }
    if((uint32_t)(now_ms-s->tx_started_ms)>CS_ACK_TIMEOUT_MS)fail(s,CS_FAULT_ACK_TIMEOUT);
    return 0;
}
static unsigned idle(cs_service_t *s,unsigned budget) {
    unsigned work=0;
    for(unsigned n=0;n<limit(budget,8) && !s->fatal;n++) {
        uint32_t status,meta,generation;
        if(!read_reg(s,0x0c,&status) || !(status&FB_AVAILABLE))break;
        if(!read_reg(s,0x60,&meta) || !read_reg(s,0x64,&generation))break;
        const prepared_packet_t *packet=cf_packet(&s->compiler);
        if(packet && (packet->word[0]&255u)==(meta&255u))break;
        vm_result_t result=cr_idle(&s->runtime,(uint8_t)meta,(uint8_t)(meta>>8),generation);
        if(result==VM_BACKPRESSURE)break; /* Keep the immutable FPGA snapshot. */
        if(result==VM_STALE && (meta&255u)<3) {
            const voice_manager_t *m=&s->runtime.manager[meta&255u];
            unsigned voice=(meta>>8)&255u;
            if(voice<m->voice_count) {
                const vm_voice_t *v=&m->voices[voice];
                /* VM_STALE also means a matching release still has queued
                 * ON/OFF/KILL. Retain that idle edge until real command ACK;
                 * dropping it could leave a completed tail owned forever. */
                if(v->state==VM_RELEASE && v->generation==generation &&
                   v->backend_live && v->backend.generation==generation)break;
            }
        }
        if(result!=VM_OK && result!=VM_STALE && result!=VM_INVALID){fail(s,CS_FAULT_RUNTIME);break;}
        if(!write_reg(s,0x68,1))break;
        if(result==VM_OK){cf_idle(&s->compiler,(uint8_t)meta,(uint8_t)(meta>>8),generation);s->stats.idle++;}
        else s->stats.idle_stale++;
        work++;
    }
    return work;
}
static unsigned receive(cs_service_t *s,unsigned budget) {
    unsigned work=0;
    for(unsigned n=0;n<limit(budget,32) && !s->fatal;n++) {
        if(!s->rx_held) {
            uint32_t status,lo,hi;
            if(!read_reg(s,0x0c,&status) || !(status&RX_AVAILABLE))break;
            /* Session flush remains serviceable while RAW admission is paused. */
            if((s->mode_phase || cf_maintenance(&s->compiler) || !s->stats.ready) && !(status&FLUSH_PENDING))break;
            if(!read_reg(s,0x10,&s->rx_meta) || !read_reg(s,0x14,&lo) ||
               !read_reg(s,0x18,&hi) || !read_reg(s,0x20,&s->rx_sequence))break;
            s->rx_tick=((uint64_t)hi<<32)|lo;s->rx_held=1;
            if(s->rx_meta&0xfffc0000u){fail(s,CS_FAULT_RECORD);break;}
        }
        if(s->rx_meta&0x10000u) {
            if(!s->flush_started) {
                if((s->rx_meta&0xff00u)!=0xff00u || (s->rx_meta&0xf0u) ||
                   !(s->rx_meta&15u) || !s->rx_sequence){fail(s,CS_FAULT_RECORD);break;}
                cr_flush(&s->runtime,(uint8_t)(s->rx_meta&7u));
                s->flush_started=1;s->stats.flush++;
                if(s->rx_meta&0x20000u)s->need_recovery=1;
            }
            break;
        }
        if(!s->stats.ready || (s->mode_phase && s->mode_phase!=M_DRAIN))break;
        unsigned source=(s->rx_meta>>8)&255u;
        if(source>=3 || (s->rx_meta&0x30000u)){fail(s,CS_FAULT_RECORD);break;}
        midi_parser_result_t result=cr_push(&s->runtime,(uint8_t)source,(uint8_t)s->rx_meta,s->rx_tick);
        if(result==MIDI_PARSER_WOULD_BLOCK)break;
        if(result!=MIDI_PARSER_OK){fail(s,CS_FAULT_RECORD);break;}
        if(!write_reg(s,0x1c,1))break;
        s->rx_held=0;s->stats.raw_bytes++;work++;
    }
    return work;
}
static unsigned drain_sysex(cs_service_t *s,unsigned budget) {
    unsigned work=0;
    /* Current synth control has no SysEx interpreter. Explicitly account for
     * unsupported chunks and retain the last full record; never silently wedge
     * the parser. A future interpreter can replace this bounded sink. */
    while(work<limit(budget,8) && midi_parser_pop_sysex(&s->runtime.parser,&s->last_sysex)) {
        s->stats.sysex_chunks++;s->stats.sysex_bytes+=s->last_sysex.count;
        if(s->last_sysex.aborted)s->stats.sysex_aborted++;
        work++;
    }
    return work;
}
static unsigned template_input(cs_service_t *s){
    uint32_t status;
    if(!read_reg(s,PC_TEMPLATE_STATUS,&status))return 0;
    if(status&65536u){fail(s,CS_FAULT_TEMPLATE);return 0;}
    if(!s->compiler.boot_done || s->compiler.pending || s->compiler.refresh_phase)return 0;
    unsigned work=0;
    for(unsigned n=0;n<CF_TEMPLATE_BUDGET;n++){
        uint32_t meta,data;
        if(!read_reg(s,PC_TEMPLATE_META,&meta) || !(meta&UINT32_C(0x80000000)))break;
        if(!read_reg(s,PC_TEMPLATE_DATA,&data) || !write_reg(s,PC_TEMPLATE_POP,1))break;
        s->stats.template_words++;work++;
        if((uint16_t)meta==PC_MODE_REQUEST){
            uint32_t sequence=(data>>8)&PC_MODE_SEQUENCE_MASK;
            uint32_t delta=(sequence-s->mode_sequence)&PC_MODE_SEQUENCE_MASK;
            /* Invalid/replayed UI requests must not make audio fatal. The
             * single FIFO owner retries only the same immutable request. */
            if((data&UINT32_C(0xf00000fc))==PC_MODE_VERSION && sequence &&
               (data&3u)<3u && (!s->mode_sequence ||
               (delta && delta<UINT32_C(0x00080000)))){
                if(cs_request_mode(s,(uint8_t)(data&3u))==CS_OK)
                    s->mode_sequence=sequence;
            }
            continue;
        }
        int result=cf_template_record(&s->compiler,(uint16_t)meta,data);
        if(!result){fail(s,CS_FAULT_TEMPLATE);break;}
        if(result==CF_COMMIT_PENDING){s->stats.template_commits++;break;}
    }
    return work;
}
static void template_ack(cs_service_t *s){
    /* Results/CRC precede generation publication, including rejected KST. */
    static const uint8_t order[8]={PC_ACK_KST_RESULT,PC_ACK_KST_CRC,PC_ACK_RESERVED,
        PC_ACK_SUB,PC_ACK_KS,PC_ACK_KST,PC_ACK_KST_COMPLETED,PC_ACK_FM_SENT};
    for(unsigned i=0;i<8;i++){
        unsigned k=order[i];
        if(s->compiler.ack_dirty&(1u<<k)){
            if(!write_reg(s,PC_TEMPLATE_ACK_BASE+4u*k,s->compiler.ack[k]))return;
            s->compiler.ack_dirty&=(uint8_t)~(1u<<k);
        }
    }
    s->stats.template_unsupported=s->compiler.unsupported_templates;
}
static unsigned submit(cs_service_t *s,uint32_t now_ms,unsigned budget) {
    uint32_t status,sequence;control_voice_command_t c;
    if(!budget || s->tx_committed || !(s->stats.backend_status&256u))return 0;
    if(!cf_packet(&s->compiler)){
        if(cf_maintenance(&s->compiler) && commands_empty(s) && parser_empty(s) &&
           !s->rx_held && !s->runtime.flush_mask && !s->flush_started){
            (void)cf_background(&s->compiler,s->runtime.manager);
            if(s->compiler.fault){fail(s,CS_FAULT_COMPILE);return 0;}
            template_ack(s);if(s->fatal)return 0;
        }
        if(!cf_packet(&s->compiler) && s->compiler.boot_done && !s->compiler.refresh_phase &&
           s->stats.ready && !s->runtime.in_flight_valid){
            if(cr_next_command(&s->runtime,&c,&sequence)){
                if(!cf_begin(&s->compiler,&c)){fail(s,CS_FAULT_COMPILE);return 0;}
                s->stats.commands++;
            }
        }
    }
    const prepared_packet_t *p=cf_packet(&s->compiler);
    if(!p)return 0;
    if(!read_reg(s,0x0c,&status) || (status&TX_FULL))return 0;
    for(unsigned i=0;i<5;i++)if(!write_reg(s,0x40u+4u*i,p->word[i]))return 0;
    s->next_physical_sequence=s->next_physical_sequence==UINT32_MAX?1u:s->next_physical_sequence+1u;
    s->physical_sequence=s->next_physical_sequence;
    if(!write_reg(s,0x54,s->physical_sequence))return 0;
    s->tx_committed=1;s->tx_started_ms=now_ms;s->stats.physical_packets++;return 1;
}
static unsigned finish_flush(cs_service_t *s) {
    if(!s->flush_started || s->runtime.flush_mask || !commands_empty(s))return 0;
    if(!write_reg(s,0x1c,1))return 0;
    s->stats.flush_complete++;s->stats.last_flush_sequence=s->rx_sequence;
    s->flush_started=0;s->rx_held=0;return 1;
}
static void recover(cs_service_t *s) {
    uint32_t status;
    if(!s->need_recovery || s->rx_held || s->runtime.flush_mask || !commands_empty(s))return;
    if(!read_reg(s,0x0c,&status))return;
    if(status&(RX_AVAILABLE|FLUSH_PENDING|CANCEL_HOLD))return;
    if(status&RECOVERING) {
        if(write_reg(s,0x28,3))s->need_recovery=0;
    } else s->need_recovery=0;
}
static unsigned mode_step(cs_service_t *s,uint32_t now_ms) {
    uint32_t state;
    if(!s->mode_phase || !s->stats.ready || cf_maintenance(&s->compiler))return 0;
    if(s->mode_phase==M_DRAIN) {
        if(s->rx_held || s->runtime.flush_mask || !commands_empty(s) || !parser_empty(s))return 0;
        s->mode_phase=M_OWNERS;
    }
    if(s->mode_phase==M_OWNERS) {
        for(unsigned e=0;e<3;e++)if((s->engines&(1u<<e)) && !(s->owner_flush_done&(1u<<e))) {
            if(s->runtime.in_flight_valid && s->runtime.in_flight.engine==e)continue;
            /* Source3 is not RAW MIDI and cannot use the three-source mask.
             * Stop its native occupancy through the same VM before routing. */
#if ENABLE_AE350_FDC
            if(e==0){
                vm_result_t sensor=vm_sensor_flush(&s->runtime.manager[0],1,
                    (uint8_t)(s->tx_committed || cf_packet(&s->compiler)!=0));
                if(sensor==VM_BACKPRESSURE || sensor==VM_NOT_READY)continue;
                if(sensor!=VM_OK && sensor!=VM_IGNORED){fail(s,CS_FAULT_RUNTIME);return 0;}
            }
#endif
            vm_result_t result=vm_flush_sources(&s->runtime.manager[e],7);
            if(result==VM_OK || result==VM_IGNORED)s->owner_flush_done|=(uint8_t)(1u<<e);
            else if(result!=VM_BACKPRESSURE && result!=VM_NOT_READY){fail(s,CS_FAULT_RUNTIME);return 0;}
        }
        if((s->owner_flush_done&s->engines)!=s->engines || !commands_empty(s))return 0;
        s->mode_phase=M_SEND;
    }
    if(s->mode_phase==M_SEND) {
        if(s->flush_started || s->runtime.flush_mask || !commands_empty(s))return 0;
        if(!read_reg(s,0x84,&state) || (state&256u))return 0;
        s->route_target=s->stats.requested_mode;
        if(!write_reg(s,0x80,s->route_target))return 0;
        s->route_started_ms=now_ms;s->mode_phase=M_WAIT;return 1;
    }
    if(s->mode_phase==M_WAIT) {
        if(!read_reg(s,0x84,&state))return 0;
        if(!(state&256u) && (state&3u)==s->route_target && !s->flush_started &&
           !s->runtime.flush_mask && commands_empty(s) && parser_empty(s)) {
            routes(s,s->route_target);s->stats.mode=s->route_target;
            s->stats.mode_changes++;s->owner_flush_done=0;
            s->mode_phase=s->stats.mode==s->stats.requested_mode?M_NONE:M_DRAIN;
            return 1;
        }
        if((uint32_t)(now_ms-s->route_started_ms)>CS_ROUTE_TIMEOUT_MS)fail(s,CS_FAULT_ROUTE_TIMEOUT);
    }
    return 0;
}
static unsigned poll_service(cs_service_t *s,uint32_t now_ms,const cs_budget_t *budget) {
    unsigned work=0;
    if(!s || s->fatal)return 0;
    if(!budget)budget=&cs_default_budget;
    s->stats.polls++;
    if(!update_ready(s))return 0;
    work+=collect_ack(s,now_ms);
    if(s->fatal)return work;
    if(!read_reg(s,0x74,&s->stats.backend_fault))return work;
    if(s->stats.backend_fault){fail(s,CS_FAULT_BACKEND);return work;}
    work+=idle(s,budget->idle);
    if(s->fatal)return work;
    work+=template_input(s);if(s->fatal)return work;
    work+=receive(s,budget->rx);
    if(s->fatal)return work;
    work+=drain_sysex(s,budget->sysex);
    for(unsigned n=0;n<limit(budget->pump,16);n++) {
        if(cr_pump(&s->runtime))work++;
        if(s->runtime.fault){fail(s,CS_FAULT_RUNTIME);return work;}
    }
    work+=submit(s,now_ms,budget->tx);
    if(s->fatal)return work;
    work+=finish_flush(s);if(s->fatal)return work;
    recover(s);if(s->fatal)return work;
    work+=mode_step(s,now_ms);
    s->stats.events=s->runtime.events_routed;
    if(!s->fatal)s->stats.state=!s->stats.ready?CS_WAIT_READY:
        s->flush_started?CS_FLUSHING:s->mode_phase?CS_SWITCHING:CS_RUNNING;
    return work;
}
unsigned cs_poll(cs_service_t *s,uint32_t now_ms,const cs_budget_t *budget) {
    unsigned work=poll_service(s,now_ms,budget);
    if(!s || (s->stats.fault&CS_FAULT_IO))return work;
    unsigned applied=s->stats.mode<3?s->stats.mode:3u;
    unsigned busy=s->mode_phase || applied!=s->stats.requested_mode;
    unsigned ready=!s->fatal && s->stats.ready && !busy && s->stats.state==CS_RUNNING;
    uint32_t status=PC_MODE_VERSION|(s->mode_sequence<<8)|PC_MODE_CAP|
        (s->fatal?PC_MODE_FAULT:0u)|(ready?PC_MODE_READY:0u)|
        (busy?PC_MODE_BUSY:0u)|(applied<<2)|s->stats.requested_mode;
    /* Publish independently of template refresh, including a terminal fault.
     * No extra APB write is issued while this coherent word is unchanged. */
    if(status!=s->mode_status_published &&
       write_reg(s,PC_TEMPLATE_ACK_BASE+4u*PC_ACK_RESERVED,status))
        s->mode_status_published=status;
    return work;
}
