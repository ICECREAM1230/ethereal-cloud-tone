/* SPDX-License-Identifier: MIT */
#include "fdc_native_owner.h"
#if ENABLE_AE350_FDC
static int busy(const cs_service_t *s)
{
    return s->runtime.in_flight_valid || s->tx_committed || cf_packet(&s->compiler)!=0;
}
static int same(const fdc_ae350_control_t *a,const fdc_ae350_control_t *b)
{
    return a->epoch==b->epoch && a->sequence==b->sequence &&
        a->pitch_code==b->pitch_code && a->gain_q16==b->gain_q16 && a->active==b->active;
}
static int owned(const fdc_native_owner_t *o)
{
    if(!o->have_handle || o->handle.voice>=vm_engine_caps(VM_FM)->voices)return 0;
    const vm_voice_t *v=&o->service->runtime.manager[VM_FM].voices[o->handle.voice];
    return v->source==3 && v->generation==o->handle.generation && v->state!=VM_IDLE;
}
static int queued(const fdc_native_owner_t *o)
{
    const control_runtime_t *r=&o->service->runtime;
    if(r->in_flight_valid && r->in_flight.engine==VM_FM &&
       r->in_flight.voice==o->handle.voice && r->in_flight.generation==o->handle.generation)return 1;
    const voice_manager_t *m=&r->manager[VM_FM];
    for(unsigned i=0;i<m->queue_count;i++)
        if(m->commands[i].voice==o->handle.voice && m->commands[i].generation==o->handle.generation)return 1;
    return 0;
}
void fdc_native_owner_init(fdc_native_owner_t *o,cs_service_t *s)
{
    if(!o)return;
    volatile unsigned char *p=(volatile unsigned char *)o;
    for(unsigned i=0;i<sizeof(*o);i++)p[i]=0;
    o->service=s;o->suspended=1;
}
int fdc_native_stop(fdc_native_owner_t *o)
{
    if(!o || !o->service)return -1;
    o->suspended=1;
    if(!o->have_handle)return 1;
    if(!owned(o)){
        o->pending=o->stopping=o->have_handle=0;++o->cancelled;return 1;
    }
    if(o->service->fatal)return -1; /* Hardware watchdog remains the last guard. */
    if(!o->stopping){
        vm_result_t r=vm_sensor_release(&o->service->runtime.manager[VM_FM],&o->handle,1,(uint8_t)busy(o->service));
        if(r==VM_BACKPRESSURE || r==VM_NOT_READY)return 0;
        if(r!=VM_OK && r!=VM_IGNORED && r!=VM_STALE)return -1;
        o->stopping=1;o->pending=0;
    }
    if(queued(o))return 0;
    const vm_voice_t *v=&o->service->runtime.manager[VM_FM].voices[o->handle.voice];
    if(v->backend_live && v->backend.source==3 &&
       v->backend.generation==o->handle.generation && v->backend_on)return 0;
    o->have_handle=o->stopping=0;return 1;
}
int fdc_native_resume(fdc_native_owner_t *o)
{
    if(!o || !o->service || o->have_handle || o->pending || o->stopping)return 0;
    o->suspended=0;return 1;
}
int fdc_native_publish(void *context,const fdc_ae350_control_t *c)
{
    fdc_native_owner_t *o=(fdc_native_owner_t *)context;
    if(!o || !o->service || !c || !c->epoch || !c->sequence ||
       c->pitch_code>16383 || c->gain_q16>65536 || c->active>1 ||
       (!c->active && c->gain_q16))return -1;
    if(c->epoch<o->epoch || o->service->fatal)return -1;
    if(c->epoch>o->epoch && o->have_handle && (!o->suspended || o->epoch_restart)){
        o->epoch_restart=1;
        int stopped=fdc_native_stop(o);
        if(stopped!=1)return stopped;
        o->epoch_restart=0;o->suspended=0;
    }
    cs_service_t *s=o->service;
    if(o->suspended || (!s->parallel_sources &&
       (s->stats.mode!=VM_FM || s->stats.requested_mode!=VM_FM || s->mode_phase))){
        int stopped=fdc_native_stop(o);
        if(stopped<=0)return stopped;
        // -2 means cancelled, never "applied". The driver must discard this
        // immutable request without writing a successful CTRL diagnostic ACK.
        return c->active?-2:1;
    }
    if(o->pending && !same(c,&o->sending))return -1;
    if(s->stats.state!=CS_RUNNING || !s->stats.ready)return 0;
    if(o->have_handle && !owned(o)){
        o->have_handle=o->pending=0;o->suspended=1;++o->cancelled;
        return c->active?-2:1; /* Never reacquire over a new MIDI identity. */
    }
    if(!o->pending){
        if(!o->have_handle && !c->active){o->epoch=c->epoch;return 1;}
        vm_result_t r=o->have_handle?
            vm_sensor_update(&s->runtime.manager[VM_FM],&o->handle,c->pitch_code,c->gain_q16,(uint8_t)busy(s)):
            vm_sensor_claim(&s->runtime.manager[VM_FM],c->pitch_code,c->gain_q16,(uint8_t)busy(s),&o->handle);
        if(r==VM_BACKPRESSURE || r==VM_NOT_READY)return 0;
        if(r!=VM_OK)return -1;
        o->have_handle=1;o->pending=1;o->sending=*c;o->epoch=c->epoch;
        return 0;
    }
    if(queued(o))return 0;
    const vm_voice_t *v=&s->runtime.manager[VM_FM].voices[o->handle.voice];
    const cf_fm_voice_t *compiled=&s->compiler.fm[o->handle.voice];
    uint32_t packed=(uint32_t)c->pitch_code|(c->gain_q16<<14);
    if(!v->backend_live || v->backend.source!=3 || v->backend.generation!=o->handle.generation ||
       v->backend.pitch!=packed || !compiled->live || compiled->logical.generation!=o->handle.generation ||
       compiled->logical.pitch!=packed)return -1;
    o->pending=0;++o->applied;return 1;
}
#endif
