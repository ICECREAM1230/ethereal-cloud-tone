#include "b_engine.h"
#include <string.h>

static bool apply_model(harp_model_t *m,const b_config_t *c)
{
    uint8_t notes[12];uint16_t gains[12];
    for(unsigned i=0;i<12;i++) { notes[i]=(uint8_t)c->notes[i];gains[i]=(uint16_t)c->gains[i]; }
    if(!harp_set_mapping(m,notes,gains)) return false;
    if(m->config.encoder_edges!=c->enc_edges || m->config.reverse_mask!=c->enc_reverse) {
        memset(m->encoder_accum,0,sizeof(m->encoder_accum));
        m->encoder_seen=0;
    }
    m->config.channel=(uint8_t)(c->channel-1);
    m->config.laser_active_high=c->polarity!=0;
    m->config.laser_debounce_ms=(uint16_t)c->debounce;
    m->config.encoder_edges=(uint8_t)c->enc_edges;
    m->config.reverse_mask=(uint8_t)c->enc_reverse;
    m->config.duration_min_ms=(uint16_t)c->velocity[0];
    m->config.duration_max_ms=(uint16_t)c->velocity[1];
    m->config.velocity_min=(uint8_t)c->velocity[2];
    m->config.velocity_max=(uint8_t)c->velocity[3];
    m->config.gate_ms=(uint16_t)c->gate;
    m->transpose=c->transpose;m->fine_cents=c->fine;
    m->volume=(uint8_t)c->volume;m->cutoff=(uint8_t)c->cutoff;m->resonance=(uint8_t)c->resonance;
    return true;
}

bool b_engine_init(b_engine_t *e,const b_config_t *defaults)
{
    if(!e) return false;
    b_config_t c;
    if(defaults) c=*defaults;else b_config_defaults(&c);
    if(!b_config_valid(&c)) return false;
    b_engine_t next={0};
    harp_config_t core={.channel=(uint8_t)(c.channel-1),.first_note=60,
        .velocity_min=(uint8_t)c.velocity[2],.velocity_max=(uint8_t)c.velocity[3],
        .encoder_edges=(uint8_t)c.enc_edges,.reverse_mask=(uint8_t)c.enc_reverse,
        .laser_active_high=c.polarity!=0,.laser_debounce_ms=(uint16_t)c.debounce,.button_debounce_ms=20,
        .duration_min_ms=(uint16_t)c.velocity[0],.duration_max_ms=(uint16_t)c.velocity[1],.gate_ms=(uint16_t)c.gate};
    if(!harp_init(&next.model,&core) || !apply_model(&next.model,&c)) return false;
    next.config=c;next.pending_config=c;next.stream_period_ms=200;
    next.lifecycle=B_LIFECYCLE_RUNNING;
    *e=next;
    return true;
}

static void finish_reply(const b_engine_t *e,const b_request_t *r,b_code_t code,b_reply_t *out)
{
    *out=(b_reply_t){.ready=true,.include_config=r->verb!=B_VERB_STATUS,
        .include_state=r->verb==B_VERB_STATUS && code==B_OK,.id=r->id,.revision=e->revision,.code=code};
}

static void resume(b_engine_t *e)
{
    harp_reset_inputs(&e->model);
    e->pending=false;e->gpio_changed=false;e->lifecycle=B_LIFECYCLE_RUNNING;
}

void b_engine_command(b_engine_t *e,const b_request_t *r,uint32_t now,uint32_t seq,b_reply_t *out)
{
    if(!out) return;
    memset(out,0,sizeof(*out));
    if(!e || !r) return;
    if(e->lifecycle==B_LIFECYCLE_RUNNING) b_engine_sync_controls(e);
    if(r->verb==B_VERB_GET || r->verb==B_VERB_PING || r->verb==B_VERB_STATUS) {
        finish_reply(e,r,B_OK,out);return;
    }
    if(r->verb==B_VERB_PANIC) {
        harp_panic(&e->model);
        if(e->pending && e->lifecycle==B_LIFECYCLE_QUIESCING) e->drain_target_seq=seq;
        finish_reply(e,r,B_OK,out);return;
    }
    if(e->lifecycle!=B_LIFECYCLE_RUNNING) { finish_reply(e,r,B_BUSY,out);return; }
    if(r->check_revision && r->expected_revision!=e->revision) {
        finish_reply(e,r,B_REVISION_CONFLICT,out);return;
    }
    if(r->verb==B_VERB_STREAM) {
        if(r->period_ms && (r->period_ms<200 || r->period_ms>2000)) {
            finish_reply(e,r,B_BAD_REQUEST,out);return;
        }
        e->stream_period_ms=r->period_ms;finish_reply(e,r,B_OK,out);return;
    }
    if(r->verb==B_VERB_STRIKE) {
        bool valid=r->beam>=1 && r->beam<=12 && r->duration_ms>=1 && r->duration_ms<=60000;
        finish_reply(e,r,valid && harp_debug_strike(&e->model,r->beam-1,r->duration_ms,now)?B_OK:B_BAD_REQUEST,out);
        return;
    }
    b_config_t next=e->config;
    if(r->verb==B_VERB_APPLY) {
        if(!b_config_patch(&e->config,&r->patch,&next)) { finish_reply(e,r,B_INVALID_CONFIG,out);return; }
        if(b_config_equal(&e->config,&next)) { finish_reply(e,r,B_OK,out);return; }
        if(!b_config_needs_maintenance(&e->config,&next)) {
            if(!apply_model(&e->model,&next)) { finish_reply(e,r,B_INVALID_CONFIG,out);return; }
            e->config=next;++e->revision;finish_reply(e,r,B_OK,out);return;
        }
    } else if(r->verb!=B_VERB_SAVE) { finish_reply(e,r,B_UNSUPPORTED,out);return; }
    e->pending_request=*r;e->pending_config=next;e->pending=true;
    e->gpio_changed=memcmp(e->config.pins,next.pins,sizeof(next.pins))!=0;
    e->drain_target_seq=seq;e->drain_started_ms=now;
    e->lifecycle=B_LIFECYCLE_QUIESCING;
    harp_panic(&e->model);
    /* Initial and resumed scans must not count maintenance time as a pulse. */
    harp_reset_inputs(&e->model);
}

b_action_t b_engine_tick(b_engine_t *e,uint32_t now,uint32_t router_seq,bool quiet,b_reply_t *out)
{
    if(!out) return B_ACTION_NONE;
    memset(out,0,sizeof(*out));
    if(!e || !e->pending || e->lifecycle!=B_LIFECYCLE_QUIESCING) return B_ACTION_NONE;
    if((uint32_t)(now-e->drain_started_ms)>=B_ENGINE_DRAIN_TIMEOUT_MS) {
        finish_reply(e,&e->pending_request,B_DRAIN_TIMEOUT,out);resume(e);return B_ACTION_NONE;
    }
    if(router_seq!=e->drain_target_seq || !quiet) return B_ACTION_NONE;
    if(e->pending_request.verb==B_VERB_SAVE) {
        e->lifecycle=B_LIFECYCLE_SAVING;return B_ACTION_SAVE;
    }
    e->lifecycle=B_LIFECYCLE_RECONFIGURING;
    return B_ACTION_RECONFIGURE;
}

void b_engine_complete(b_engine_t *e,bool success,b_reply_t *out)
{
    if(!out) return;
    memset(out,0,sizeof(*out));
    if(!e || !e->pending || (e->lifecycle!=B_LIFECYCLE_SAVING && e->lifecycle!=B_LIFECYCLE_RECONFIGURING)) return;
    b_code_t code=B_OK;
    if(e->lifecycle==B_LIFECYCLE_SAVING) {
        if(!success) code=B_STORAGE_ERROR;
    } else if(!success) code=B_IO_ERROR;
    else if(!apply_model(&e->model,&e->pending_config)) code=B_INVALID_CONFIG;
    else { e->config=e->pending_config;++e->revision; }
    finish_reply(e,&e->pending_request,code,out);
    resume(e);
}

bool b_engine_sync_controls(b_engine_t *e)
{
    if(!e || e->lifecycle!=B_LIFECYCLE_RUNNING) return false;
    b_config_t next=e->config;
    next.transpose=e->model.transpose;next.volume=e->model.volume;next.fine=e->model.fine_cents;
    next.cutoff=e->model.cutoff;next.resonance=e->model.resonance;
    if(b_config_equal(&next,&e->config) || !b_config_valid(&next)) return false;
    e->config=next;++e->revision;
    return true;
}

void b_engine_sample(b_engine_t *e,uint16_t levels,const uint8_t ab[4],uint8_t pressed,uint8_t enabled,uint32_t now)
{
    if(!e || !ab || e->lifecycle!=B_LIFECYCLE_RUNNING) return;
    harp_sample(&e->model,levels,ab,pressed,enabled,now);
    b_engine_sync_controls(e);
}

void b_engine_snapshot(const b_engine_t *e,b_state_t *s)
{
    if(!e || !s) return;
    s->revision=e->revision;s->lifecycle=e->lifecycle;
    s->shade=e->model.active_beams;s->playing=e->model.playing_beams;
    for(unsigned i=0;i<12;i++) {
        s->duration[i]=e->model.last_duration_ms[i];s->velocity[i]=e->model.velocity[i];
        s->notes[i]=harp_effective_note(&e->model,i);s->hits[i]=e->model.hit_count[i];
    }
    s->controls.transpose=e->config.transpose;s->controls.volume=e->config.volume;s->controls.fine=e->config.fine;
    s->controls.cutoff=e->config.cutoff;s->controls.resonance=e->config.resonance;
    s->illegal_edges=e->model.illegal_edges;
}
