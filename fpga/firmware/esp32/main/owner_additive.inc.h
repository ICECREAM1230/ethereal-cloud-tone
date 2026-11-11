/* Included by voice_alloc.c. Only this owner binds HTTP identity to hardware
 * completion. RAM queues and additive_link never publish browser success. */
#ifndef S3_LEGACY_HOST_TEST
#include "web_additive.h"
static struct {
    bool active, mode, submitted, routing, arming;
    web_v2_event_t event;
    additive_patch_t patch;
    uint32_t id, request_id, target, instance;
} s_additive_owner;
static struct { uint32_t token,sequence,context_epoch,commit_id; } s_additive_applied;
static uint32_t s_additive_next_request=1,s_additive_instance;
static bool s_additive_failed;

static void owner_additive_cancel(void)
{
    if(s_additive_owner.active) {
        s_additive_failed=true;
        unsigned i=s_additive_owner.event.client;
        if(i<WEB_CLIENTS && s_web[i].token==s_additive_owner.event.token) {
            s_v2_result_sequence[i]=s_additive_owner.event.sequence;
            s_v2_result_context[i]=s_additive_owner.event.context_epoch;
            s_v2_result_code[i]=FM_RESULT_CANCELLED;
        }
        additive_link_cancel();
    }
    web_additive_queue_cancel_all();
    memset(&s_additive_owner,0,sizeof(s_additive_owner));
}
static void owner_additive_fence(void)
{
    engine_link_performance_ready(false);
    raw_midi_flush(MIDI_RAW_SOURCE_ALL);
    web_raw_queue_reset();s_template_valid=false;
    for(unsigned i=0;i<WEB_CLIENTS;i++) {
        s_web[i].keys=s_web[i].desired_keys=0;s_web[i].armed=false;
    }
}
static bool owner_additive_start(const web_v2_event_t *event,const additive_patch_t *patch,
                                  uint32_t id,bool mode,uint32_t target)
{
    additive_link_status_t link;additive_link_status(&link);
    if(!link.available || !link.valid || link.busy || CONFIG_FPGA_FM_ONLY || !s_additive_next_request)return false;
    owner_additive_fence();
    s_additive_owner.event=*event;s_additive_owner.patch=*patch;
    s_additive_owner.id=id;s_additive_owner.mode=mode;s_additive_owner.target=target;
    s_additive_owner.request_id=s_additive_next_request++;
    s_additive_owner.instance=link.instance_id;s_additive_owner.active=true;
    s_additive_failed=false;
    if(mode)s_requested_engine=target;
    s_web[event->client].sequence=event->sequence;
    return true;
}
static void handle_web_additive(const web_v2_event_t *event,int64_t now)
{
    static web_additive_job_t job;
    if(!web_additive_queue_take(event->a,&job))return;
    unsigned i=event->client;
    bool binding=i<WEB_CLIENTS && job.client==i && job.request.token==event->token &&
        job.request.sequence==event->sequence && job.request.context_epoch==event->context_epoch &&
        job.expires==event->expires && s_web[i].token==event->token;
    uint32_t result=FM_RESULT_OK;
    additive_link_status_t link;additive_link_status(&link);
    if(!binding || event->sequence<=s_web[i].sequence) {web_additive_queue_cancel(event->a);return;}
    if(event->context_epoch!=s_control_epoch)result=FM_RESULT_STALE_EPOCH;
    else if(now>=event->expires || !s_web[i].armed)result=FM_RESULT_CANCELLED;
    else if(s_additive_owner.active || !link.valid || !link.selected || link.busy ||
            s_requested_engine!=WEB_ENGINE_ADDITIVE)result=FM_RESULT_BUSY;
    else if(owner_additive_start(event,&job.request.patch,event->a,false,WEB_ENGINE_ADDITIVE))return;
    else result=FM_RESULT_BUSY;
    web_additive_queue_cancel(event->a);
    s_web[i].sequence=event->sequence;
    s_v2_result_sequence[i]=event->sequence;s_v2_result_context[i]=event->context_epoch;
    s_v2_result_code[i]=result;
}
static bool owner_additive_mode(const web_v2_event_t *event)
{
    additive_link_state_t link;additive_link_state(&link);
    if(event->a!=WEB_ENGINE_ADDITIVE && !link.selected && !s_additive_owner.active)return false;
    // A later mode request cancels the old identity; the link retains an
    // unknown COMMIT until ACK and compensates with selected=false first.
    if(s_additive_owner.active)owner_additive_cancel();
    return owner_additive_start(event,&link.confirmed_patch,0,true,event->a);
}
static bool owner_additive_poll(int64_t now)
{
    additive_link_status_t link;additive_link_status(&link);
    if(s_additive_instance!=link.instance_id) {
        bool changed=s_additive_instance!=0;s_additive_instance=link.instance_id;
        memset(&s_additive_applied,0,sizeof(s_additive_applied));
        if(changed && s_additive_owner.active) {
            owner_additive_cancel();owner_additive_fence();s_additive_failed=true;
        }
    }
    if(!link.valid)memset(&s_additive_applied,0,sizeof(s_additive_applied));
    if(!s_additive_owner.active)return false;
    const web_v2_event_t *e=&s_additive_owner.event;
    unsigned i=e->client;
    if(s_web[i].token!=e->token || e->context_epoch!=s_control_epoch ||
       now>=e->expires || link.instance_id!=s_additive_owner.instance ||
       (link.fault && link.fault!=ADDITIVE_LINK_CANCELLED)) {
        owner_additive_cancel();owner_additive_fence();s_additive_failed=true;return true;
    }
    if(!s_additive_owner.submitted) {
        if(link.busy)return false;
        if(!link.valid) {owner_additive_cancel();s_additive_failed=true;return true;}
        if(!additive_link_request(&s_additive_owner.patch,
                s_additive_owner.target==WEB_ENGINE_ADDITIVE,s_additive_owner.request_id))return false;
        s_additive_owner.submitted=true;return true;
    }
    if(link.busy || link.completed_request_id!=s_additive_owner.request_id)return false;
    if(s_additive_owner.mode) {
        // The disable COMMIT is confirmed before touching original routing.
        if(!s_additive_owner.routing) {
            engine_fm_status_t fm;engine_link_fm_status(&fm);
            uint32_t backend=s_additive_owner.target==FM_ENGINE_FM?e->b:FM_BACKEND_UNBOUND;
            if((backend || fm.selected || fm.requested_backend || fm.needs_select || fm.switching) &&
                !engine_link_fm_mode(backend))return false;
            if(!engine_link_ks_mode(s_additive_owner.target==FM_ENGINE_KS))return false;
            s_additive_owner.routing=true;return true;
        }
        engine_fm_status_t fm;engine_ks_state_t ks;
        engine_link_fm_status(&fm);engine_link_ks_state(&ks);
        if(fm.switching || fm.recovering || fm.needs_select || ks.switching || ks.recovering ||
           ks.requested_mode!=ks.applied_mode ||
           (s_additive_owner.target==FM_ENGINE_FM && !engine_fm_playable(&fm)))return false;
    }
    if(!s_additive_owner.arming) {
        s_additive_owner.arming=true;s_template_valid=false;return true;
    }
    raw_midi_state_t raw;raw_midi_state(&raw);
    if(!raw.enabled || !raw.ready)return false;
    // Publish the original request identity and the new epoch together. A
    // transport ACK without the current HTTP identity never becomes success.
    if(!s_additive_owner.mode) {
        s_additive_applied.token=e->token;s_additive_applied.sequence=e->sequence;
        s_additive_applied.context_epoch=e->context_epoch;s_additive_applied.commit_id=link.commit_id;
    }
    s_web[i].parameter_sequence=e->sequence;
    s_v2_result_sequence[i]=e->sequence;s_v2_result_context[i]=e->context_epoch;
    s_v2_result_code[i]=FM_RESULT_OK;
    if(!++s_control_epoch)++s_control_epoch;
    dds_presets_owner_context(s_control_epoch);
    web_additive_queue_cancel(s_additive_owner.id);
    memset(&s_additive_owner,0,sizeof(s_additive_owner));
    s_template_valid=false;return true;
}
#endif
