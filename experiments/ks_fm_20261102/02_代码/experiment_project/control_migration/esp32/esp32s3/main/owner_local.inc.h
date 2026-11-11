// Included by voice_alloc.c. Physical control changes cancel RAM ownership,
// while only engine_link observes and changes hardware ownership.
#ifndef S3_LEGACY_HOST_TEST
static uint32_t s_owner_local_generation,s_owner_local_adopted;
static struct {
    bool active, applying,cpu;
    uint32_t generation, applied_context;
    web_v2_event_t event;
} s_remote_mode;
static void owner_remote_result(uint32_t result)
{
    unsigned i=s_remote_mode.event.client;
    if(i<WEB_CLIENTS && s_web[i].token==s_remote_mode.event.token &&
       s_remote_mode.event.sequence>=s_v2_result_sequence[i]) {
        s_v2_result_sequence[i]=s_remote_mode.event.sequence;
        s_v2_result_context[i]=s_remote_mode.event.context_epoch;
        s_v2_result_code[i]=result;
        if(result==FM_RESULT_OK)s_web[i].parameter_sequence=s_remote_mode.event.sequence;
    }
    memset(&s_remote_mode,0,sizeof(s_remote_mode));
}
static void owner_remote_cancel(void)
{if(s_remote_mode.active)owner_remote_result(FM_RESULT_CANCELLED);}
static bool owner_cpu_mode_sync(void)
{
    static bool known;
    static uint32_t signature,resets;
    engine_cpu_mode_state_t mode;engine_link_cpu_mode_state(&mode);
    if(!mode.supported)return false;
    uint32_t observed=mode.desired|(mode.applied<<2)|((uint32_t)mode.busy<<4)|
        ((uint32_t)mode.fault<<5)|((uint32_t)mode.valid<<6)|((uint32_t)mode.channel_routes<<7);
    if(known && signature==observed && resets==mode.reset_count)return false;
    bool ordinary_editor=known && resets==mode.reset_count && mode.valid && !mode.fault &&
        mode.applied<=2 && !( (signature^observed)&128u ) && (owner_parallel_sources() || mode.channel_routes);
    bool ours=s_remote_mode.active && s_remote_mode.cpu && mode.desired==s_remote_mode.event.a &&
        resets==mode.reset_count;
    known=true;signature=observed;resets=mode.reset_count;
    if(ordinary_editor) {
        s_requested_engine=mode.desired;
        return true; // Fixed source ownership and Web leases survive editing.
    }
    if(!ours) {
        owner_remote_cancel();
        if(!++s_control_epoch)++s_control_epoch;
        dds_presets_owner_context(s_control_epoch);
    }
    if(s_dds_switch_id)dds_presets_owner_complete(s_dds_switch_id,s_control_epoch,"CPU engine state changed");
    s_dds_switch_id=0;
    for(unsigned i=0;i<WEB_CLIENTS;i++) {
        s_web[i].keys=s_web[i].desired_keys=0;s_web[i].armed=false;s_web[i].expires=0;
    }
    raw_midi_flush(MIDI_RAW_SOURCE_ALL);web_raw_queue_reset();
    s_template_valid=false;s_dirty=s_apply_active=0;s_test_tone_requested=false;
    if(mode.valid)s_requested_engine=mode.desired;
    return true;
}
static bool owner_local_frozen(void)
{
    if(CONFIG_FPGA_AE350_CONTROL)return false;
    engine_local_state_t local;engine_link_local_state(&local);
    return local.supported && (local.local_active || local.blocked || local.remote_pending);
}
static bool owner_local_sync(void)
{
    if(CONFIG_FPGA_AE350_CONTROL)return owner_cpu_mode_sync();
    engine_local_state_t local;engine_link_local_state(&local);
    if(!local.supported)return false;
    bool changed=false;
    if(local.generation!=s_owner_local_generation) {
        owner_remote_cancel();
        if(s_dds_switch_id)dds_presets_owner_complete(s_dds_switch_id,s_control_epoch,"FPGA local engine change");
        s_dds_switch_id=0;
        if(!++s_control_epoch)++s_control_epoch;
        dds_presets_owner_context(s_control_epoch);
        memset(s_voices,0,sizeof(s_voices));
        for(unsigned i=0;i<WEB_CLIENTS;i++) {
            s_web[i].keys=s_web[i].desired_keys=0;s_web[i].armed=false;s_web[i].expires=0;
            // token/sequence and outstanding actual STOP receipts survive.
        }
        web_raw_queue_reset();s_template_valid=false;s_dirty=s_apply_active=0;
        midi_template_link_state_t image;midi_template_link_state(&image);
        s_template_fault=image.fault;s_template_fault_generation=image.generation;
        raw_midi_state_t raw;raw_midi_state(&raw);s_template_reset_count=raw.reset_count;
        s_test_tone_requested=false;
        s_owner_local_generation=local.generation;s_owner_local_adopted=0;
        engine_link_local_ack(local.generation);changed=true;
    }
    if(local.adopted && local.applied<=ENGINE_MODE_FLUTE && s_owner_local_adopted!=local.generation) {
        s_requested_engine=local.applied;
        s_owner_local_adopted=local.generation;changed=true;
    }
    return changed;
}
static uint32_t owner_mode_apply(const web_v2_event_t *e)
{
    if(CONFIG_FPGA_AE350_CONTROL)return FM_RESULT_UNSUPPORTED;
    engine_fm_status_t fm;engine_link_fm_status(&fm);
    engine_flute_state_t flute;engine_link_flute_state(&flute);
    if(e->a==ENGINE_MODE_FLUTE && !flute.available)return FM_RESULT_UNSUPPORTED;
    all_sound_off();
    if(!engine_link_flute_mode(e->a==ENGINE_MODE_FLUTE))return FM_RESULT_BUSY;
    if((e->a==FM_ENGINE_FM || fm.selected || fm.requested_backend || fm.needs_select || fm.switching) &&
       !engine_link_fm_mode(e->a==FM_ENGINE_FM?e->b:FM_BACKEND_UNBOUND))return FM_RESULT_BUSY;
    if(!engine_link_ks_mode(e->a==FM_ENGINE_KS))return FM_RESULT_BUSY;
    s_requested_engine=e->a;return FM_RESULT_OK;
}
static bool owner_remote_begin(const web_v2_event_t *e,int64_t now)
{
    if(CONFIG_FPGA_AE350_CONTROL) {
        engine_cpu_mode_state_t mode;engine_link_cpu_mode_state(&mode);
        if(!mode.supported)return false;
        uint32_t result=e->a>2 || e->b || e->c?FM_RESULT_UNSUPPORTED:
            now>=e->expires?FM_RESULT_CANCELLED:s_remote_mode.active || !mode.valid || mode.fault?FM_RESULT_BUSY:FM_RESULT_OK;
        if(mode.channel_routes) {
            /* Existing single owner applies per-client transmit metadata. No
             * global editor/FPGA route write or shared-source flush is needed. */
            if(result==FM_RESULT_OK) {
                s_web[e->client].midi_channel=(uint8_t)e->a;
                s_web[e->client].parameter_sequence=e->sequence;
            }
            s_web[e->client].sequence=e->sequence;
            s_v2_result_sequence[e->client]=e->sequence;
            s_v2_result_context[e->client]=e->context_epoch;
            s_v2_result_code[e->client]=result;
            return true;
        }
        static uint32_t request_id;
        if(result==FM_RESULT_OK) {
            if(!++request_id)++request_id;
            if(!engine_link_cpu_mode_request(e->a,request_id))result=FM_RESULT_BUSY;
        }
        if(result==FM_RESULT_OK) {
            if(!owner_parallel_sources())all_sound_off();
            s_requested_engine=e->a;
            s_remote_mode.active=s_remote_mode.cpu=true;s_remote_mode.event=*e;
            s_remote_mode.generation=request_id;s_remote_mode.applied_context=s_control_epoch;
        } else {
            s_v2_result_sequence[e->client]=e->sequence;s_v2_result_context[e->client]=e->context_epoch;s_v2_result_code[e->client]=result;
        }
        s_web[e->client].sequence=e->sequence;return true;
    }
    engine_local_state_t local;engine_link_local_state(&local);
    if(!local.supported || (!local.local_active && !local.blocked && !local.remote_pending && local.remote_ready))return false;
    if(now>=e->expires || !(local.engine_mask&(1u<<e->a))) {
        s_web[e->client].sequence=e->sequence;
        s_v2_result_sequence[e->client]=e->sequence;s_v2_result_context[e->client]=e->context_epoch;
        s_v2_result_code[e->client]=now>=e->expires?FM_RESULT_CANCELLED:FM_RESULT_UNSUPPORTED;
        return true;
    }
    owner_remote_cancel();s_remote_mode.active=true;s_remote_mode.event=*e;
    s_remote_mode.generation=local.generation;s_web[e->client].sequence=e->sequence;
    engine_link_remote_request();return true;
}
static bool owner_remote_poll(int64_t now)
{
    if(!s_remote_mode.active)return false;
    const web_v2_event_t *e=&s_remote_mode.event;
    if(s_remote_mode.cpu) {
        if(s_web[e->client].token!=e->token || s_control_epoch!=s_remote_mode.applied_context || now>=e->expires) {
            owner_remote_cancel();return true;
        }
        engine_cpu_mode_state_t mode;engine_link_cpu_mode_state(&mode);
        if(mode.completed_request_id==s_remote_mode.generation) {
            owner_remote_result(mode.result);return true;
        }
        return false;
    }
    engine_local_state_t local;engine_link_local_state(&local);
    uint32_t context=s_remote_mode.applying?s_remote_mode.applied_context:e->context_epoch;
    if(s_web[e->client].token!=e->token || context!=s_control_epoch || now>=e->expires ||
       local.generation!=s_remote_mode.generation) {owner_remote_cancel();return true;}
    if(local.local_active || local.blocked || local.remote_pending || !local.remote_ready)return false;
    engine_ks_state_t ks;engine_fm_status_t fm;
    engine_link_ks_state(&ks);engine_link_fm_status(&fm);
    engine_flute_state_t flute;engine_link_flute_state(&flute);
    if(s_remote_mode.applying) {
        if(flute.switching || ((flute.requested_mode || flute.applied_mode) && flute.fault) || flute.applied_mode!=(e->a==ENGINE_MODE_FLUTE) ||
           ks.switching || ks.recovering || fm.switching || fm.recovering || fm.needs_select ||
           ks.applied_mode!=(e->a==FM_ENGINE_KS) ||
           (fm.applied_backend!=0)!=(e->a==FM_ENGINE_FM))return false;
        owner_remote_result(FM_RESULT_OK);return true;
    }
    // A local fault may have reset the FM bridge. Recheck only the
    // retained routes before applying the explicit remote selection.
    if(ks.recovering ||
       (engine_fm_control_active(&fm) &&
        (!fm.available || !fm.valid || !fm.ready || (fm.flags&FM_FLAG_BUSY))))return false;
    if((e->a==FM_ENGINE_FM && (!engine_fm_selectable(&fm) || e->b!=fm.backend_id)) ||
       (e->a==FM_ENGINE_KS && !ks.available) ||
       (e->a==ENGINE_MODE_FLUTE && !flute.available)) {
        owner_remote_result(FM_RESULT_UNSUPPORTED);return true;
    }
    uint32_t result=owner_mode_apply(e);
    if(result!=FM_RESULT_OK) {owner_remote_result(result);return true;}
    s_remote_mode.applying=true;s_remote_mode.applied_context=s_control_epoch;return true;
}
#endif
