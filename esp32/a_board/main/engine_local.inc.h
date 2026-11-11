// ENG1 ownership is observed before the legacy backend services. This file
// shares their SPI task and lock; public entry points never access SPI.
#define LOCAL_MAGIC 0x454e4731u
#define LOCAL_KNOWN_STATUS 0x001f0fffu
static int64_t s_local_health_at;
static bool local_blocked_locked(void)
{
    return s_local.supported && (!s_local.valid || s_local_health_fault ||
        s_local.busy || s_local.fault || s_local.remote_pending ||
        (s_local.local_active && (!s_local.ready || !s_local.adopted || s_local_owner_ack!=s_local.generation)));
}
void engine_link_local_state(engine_local_state_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&s_lock);*out=s_local;
    out->blocked=local_blocked_locked();
    if(s_local_health_fault)out->valid=false;
    portEXIT_CRITICAL(&s_lock);
}
void engine_link_local_ack(uint32_t generation)
{
    portENTER_CRITICAL(&s_lock);
    if(generation==s_local.generation)s_local_owner_ack=generation;
    portEXIT_CRITICAL(&s_lock);
}
void engine_link_remote_request(void)
{
    portENTER_CRITICAL(&s_lock);
    if(s_local.supported && !s_local.remote_pending) {
        s_local.remote_pending=true;s_local.remote_ready=false;s_local_release_sent=false;
    }
    portEXIT_CRITICAL(&s_lock);
}
static bool local_read(uint32_t words[4])
{
    uint32_t again[3];
    return fpga_word_read_burst(0x070c,words,4) &&
        fpga_word_read_burst(0x070c,again,3) && !memcmp(words,again,sizeof(again));
}
static void local_cancel(void)
{
    portENTER_CRITICAL(&s_lock);
    if(!++s_local.generation)++s_local.generation;
    s_local.adopted=false;s_local_owner_ack=0;s_local_health_fault=false;
    s_local.remote_pending=s_local.remote_ready=s_local_release_sent=false;
    s_performance_ready=false;s_control_idle=false;
    // Discard old dispatch work, retaining only the parameter RAM image.
    s_flushing=s_swap_requested=false;s_ks_wait_bank_reset=false;
    memset(s_pending,0,sizeof(s_pending));memset(s_ppending,0,sizeof(s_ppending));
    memset(s_dirty_now,0,sizeof(s_dirty_now));memset(s_dirty_prev,0,sizeof(s_dirty_prev));
    memset(s_pdirty_now,0,sizeof(s_pdirty_now));memset(s_pdirty_prev,0,sizeof(s_pdirty_prev));
    memset(s_bus_dirty,0,sizeof(s_bus_dirty));
    for(unsigned v=0;v<ENGINE_NUM_VOICES;v++) {
        s_notes[v].on=s_notes[v].ready=false;s_notes[v].seq++;
        s_notes[v].snapshot_seq=0;s_bus[GATE_BASE+v]=0;
    }
    for(unsigned e=0;e<ENGINE_NUM_ELEMENTS;e++)s_desired[e][4]=0;
    s_bus[1023]=0;
    for(unsigned b=0;b<1024;b++)s_bus_done[b]=s_bus_seq[b];
    memset(&s_ks_inflight,0,sizeof(s_ks_inflight));memset(s_ks_desired,0,sizeof(s_ks_desired));
    s_ks_panic_pending=s_ks_owner_pending=s_ks_need_probe=false;
    s_ks_quarantined=false;s_ks_probe_at=0;s_fm_quarantined=false;
    s_ks.switching=s_ks.recovering=false;s_ks.valid=false;s_ks_seen=false;
    fm_clear_intents();memset(&s_fm_inflight,0,sizeof(s_fm_inflight));memset(&s_fm_panic,0,sizeof(s_fm_panic));
    s_fm_mode_pending=s_fm_panic_pending=s_fm_owner_pending=s_fm_fault_panic=false;
    s_fm.status.switching=s_fm.status.recovering=s_fm.status.needs_select=false;
    s_fm.status.valid=false;s_fm.parameter_count=0;
    s_flute.switching=false;s_flute_pending_word=0;s_flute.fault=0;
    portEXIT_CRITICAL(&s_lock);
    raw_midi_pause(true);midi_template_link_invalidate();
    s_local_adopt_pending=true;s_local_health_at=0;
}
static bool local_adopt(uint32_t status)
{
    unsigned applied=((status>>2)&3u)|((status>>9)&4u);
    if(applied>FM_ENGINE_FM && applied!=ENGINE_MODE_FLUTE)return false;
    if(applied==ENGINE_MODE_FLUTE && (!flute_readback() || !s_flute.ready ||
       !s_flute.applied_mode || s_flute.busy || s_flute.fault))return false;
    uint32_t caps[2];
    if(!fpga_word_read_burst(0x0500,caps,2)) {
        ks_recover(KS_FAULT_READBACK);
        if(applied==1 || s_local_health_fault)return false;
    } else {
        portENTER_CRITICAL(&s_lock);
        s_ks.available=caps[0]==ENGINE_KS_CAPABILITY && caps[1]==ENGINE_KS_VERSION;
        portEXIT_CRITICAL(&s_lock);
        if(s_ks.available && !ks_readback() && (applied==1 || s_local_health_fault))return false;
        if(applied==1 && !s_ks.available)return false;
    }
    bool fm_ok=true;
    if(s_local.engine_mask&4u) {
        if(!s_fm.status.available)fm_probe();
        s_local_fm_adopting=true;fm_ok=fm_readback();s_local_fm_adopting=false;
    }
    if((applied==2 && !fm_ok) || s_local_health_fault)return false;
    if((s_ks.valid && s_ks.applied_mode!=(applied==1)) ||
       (s_fm.status.valid && s_fm.status.selected!=(applied==2)))return false;
    if(applied==2 && (!s_fm.status.available || !s_fm.status.ready ||
       (s_fm.status.flags&(FM_FLAG_MUTED|FM_FLAG_FAULT))))return false;
    uint32_t again[4];
    if(!local_read(again) || again[0]!=status || again[1]!=s_local.revision || again[2]!=s_local.completed || again[3])return false;
    portENTER_CRITICAL(&s_lock);
    s_ks.requested_mode=s_ks.applied_mode=applied==1;s_ks.switching=s_ks.recovering=false;
    s_ks_mode_sent=s_ks_mode_generation;if(!s_ks_quarantined)s_ks.fault=0;
    s_ks_sequence=s_ks.accepted_sequence&0x7fffffffu;
    s_fm.status.requested_backend=s_fm.status.applied_backend=applied==2?FM_BACKEND_JT51_OPM:FM_BACKEND_UNBOUND;
    s_fm.status.switching=s_fm.status.recovering=s_fm.status.needs_select=false;
    s_fm_sequence=s_fm_panic_sequence=0;
    s_flute_sequence=s_flute.accepted_word>>1;
    s_flute.requested_mode=applied==ENGINE_MODE_FLUTE;s_flute.switching=false;s_flute_pending_word=0;
    s_local.adopted=true;s_local_adopt_pending=false;s_local_health_at=esp_timer_get_time();
    portEXIT_CRITICAL(&s_lock);
    return true;
}
static bool local_mode_observe(void)
{
    if(CONFIG_FPGA_FM_ONLY) {s_local_probed=true;return false;}
    if(!s_local_probed) {
        uint32_t magic;
        if(!fpga_word_read_burst(0x070b,&magic,1))return true;
        portENTER_CRITICAL(&s_lock);
        s_local_probed=true;s_local.supported=magic==LOCAL_MAGIC;
        s_local.valid=true;s_local.remote_ready=!s_local.supported;
        portEXIT_CRITICAL(&s_lock);
    }
    if(!s_local.supported)return false;
    uint32_t words[4];
    if(!local_read(words) || (words[0]&~LOCAL_KNOWN_STATUS)) {
        portENTER_CRITICAL(&s_lock);s_local.valid=false;s_local.blocked=true;portEXIT_CRITICAL(&s_lock);
        raw_midi_pause(true);return true;
    }
    uint32_t status=words[0],revision=words[1];
    bool reset=s_local.generation && revision!=s_local.revision &&
        (!revision || (int32_t)(revision-s_local.revision)<0);
    bool new_local=revision!=s_local.revision || (!s_local.generation && (status&(1u<<6)));
    bool released=s_local.remote_pending && s_local_release_sent && !(status&((1u<<6)|(1u<<4)|(1u<<5))) && !words[3];
    bool new_fault=(words[3] || (status&(1u<<5))) && !s_local.fault;
    if(new_local || reset || new_fault)local_cancel();
    portENTER_CRITICAL(&s_lock);
    if(reset)s_local.reset_count++;
    s_local.valid=true;s_local.requested=(status&3u)|((status>>8)&4u);
    s_local.applied=((status>>2)&3u)|((status>>9)&4u);
    s_local.local_active=(status&(1u<<6))!=0;s_local.busy=(status&(1u<<4))!=0;
    s_local.ready=(s_local.requested<=FM_ENGINE_FM || s_local.requested==ENGINE_MODE_FLUTE) &&
        (s_local.applied<=FM_ENGINE_FM || s_local.applied==ENGINE_MODE_FLUTE) &&
        (status&(1u<<9)) && !(status&(1u<<7)) &&
        (((status>>8)&1u)==(s_local.applied==2));
    s_local.fault=words[3]?words[3]:((status&(1u<<5))?6u:0u);
    s_local.revision=revision;s_local.completed=words[2];s_local.engine_mask=(status>>16)&31u;
    if(reset)s_local_health_fault=true;
    portEXIT_CRITICAL(&s_lock);
    bool complete=s_local.local_active && revision && revision==words[2] &&
        !s_local.busy && !s_local.fault && s_local.ready && s_local.requested==s_local.applied &&
        !(status&(1u<<7)) && (((status>>8)&1u)==(s_local.applied==2));
    if(complete && s_local_adopt_pending && !s_local_health_fault)local_adopt(status);
    if(released && !new_local) {
        portENTER_CRITICAL(&s_lock);
        s_local.remote_pending=false;s_local.remote_ready=true;s_local.adopted=false;
        s_local_health_fault=false;s_local_adopt_pending=false;
        // Explicit remote acquisition may rebuild the old muted bank images.
        memset(s_pending,0xff,sizeof(s_pending));memset(s_ppending,0xff,sizeof(s_ppending));
        s_ks_seen=false;s_ks.valid=false;s_ks.recovering=s_ks.switching=false;
        s_fm.status.needs_select=s_fm.status.recovering=s_fm.status.switching=false;
        portEXIT_CRITICAL(&s_lock);
        s_local_fm_adopting=true;fm_probe();fm_readback();s_local_fm_adopting=false;
        portENTER_CRITICAL(&s_lock);raw_enable_locked(false);portEXIT_CRITICAL(&s_lock);
    }
    if(s_local.remote_pending && !s_local_release_sent) {
        fpga_word_write(0x0710,0x52454d54u);s_local_release_sent=true;
    }
    if(s_local.local_active && s_local.adopted && !s_local.remote_pending &&
       !s_local_health_fault && esp_timer_get_time()-s_local_health_at>=20000) {
        s_local_health_at=esp_timer_get_time();
        if(s_local.applied==ENGINE_MODE_FLUTE && (!flute_readback() || !s_flute.ready ||
           !s_flute.applied_mode || s_flute.busy || s_flute.fault))s_local_health_fault=true;
        if(s_ks_quarantined)ks_service();
        else if(s_ks.available)ks_readback();
        if(s_ks.valid && s_ks.applied_mode!=(s_local.applied==1))ks_recover(KS_FAULT_READBACK);
        if(s_local.engine_mask&4u) {
            if(s_fm_quarantined)fm_service(false);else fm_readback();
            if(s_fm.status.valid && s_fm.status.selected!=(s_local.applied==2))fm_fault(FM_LINK_CONTEXT);
            if(s_local.applied==2 && (!s_fm.status.ready ||
               (s_fm.status.flags&(FM_FLAG_MUTED|FM_FLAG_FAULT))))fm_fault(FM_LINK_BACKEND);
        }
    }
    portENTER_CRITICAL(&s_lock);s_local.blocked=local_blocked_locked();bool blocked=s_local.blocked;portEXIT_CRITICAL(&s_lock);
    return blocked;
}
static bool local_mode_poll(void)
{
    // fm_readback may recheck physical ownership before reporting a context
    // fault. A nested health read must not recursively run another service.
    if(s_local_polling) {
        portENTER_CRITICAL(&s_lock);bool blocked=local_blocked_locked();portEXIT_CRITICAL(&s_lock);
        return blocked;
    }
    s_local_polling=true;bool blocked=local_mode_observe();s_local_polling=false;
    return blocked;
}
