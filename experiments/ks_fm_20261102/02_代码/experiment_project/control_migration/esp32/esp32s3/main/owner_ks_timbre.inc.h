/* Included by voice_alloc.c. Single sound owner; KST1 is confirmed by the
 * existing SPI owner, persistence by the dedicated NVS worker. */
#if !defined(S3_LEGACY_HOST_TEST) && (defined(ESP_PLATFORM) || defined(S3_KS_TIMBRE_HOST_TEST))
static ks_timbre_state_t s_kst;
static ks_timbre_setting_t s_kst_actual;
static ks_timbre_request_t s_kst_job;
static bool s_kst_initialized,s_kst_job_active,s_kst_model_job,s_kst_store_job,s_kst_boot_checked,s_kst_cancel_wait,s_kst_boot_legacy_pending,s_kst_refresh_pending;
static uint32_t s_kst_model_generation,s_kst_generation_counter,s_template_kst_generation;
static uint32_t s_kst_expected_crc,s_kst_actual_generation,s_kst_expected_revision;
static int64_t s_kst_started;
static bool owner_kst_unknown(void){return s_kst.phase==KST_UNKNOWN || s_kst_cancel_wait;}
static bool owner_kst_blocked(void)
{return s_kst_model_job || s_kst.phase==KST_UNKNOWN || s_kst_cancel_wait;}
static void owner_kst_finish(uint32_t code)
{
    const web_v2_event_t *e=&s_kst_job.event;
    s_kst.result=(ks_timbre_result_t){.token=e->token,.sequence=e->sequence,.context_epoch=e->context_epoch,.request_id=e->c,.code=code};
    if(e->client<WEB_CLIENTS && s_web[e->client].token==e->token) {
        if(e->sequence>=s_v2_result_sequence[e->client]) {s_v2_result_sequence[e->client]=e->sequence;s_v2_result_context[e->client]=e->context_epoch;s_v2_result_code[e->client]=code;}
        if(code==FM_RESULT_OK && e->sequence>s_web[e->client].parameter_sequence)s_web[e->client].parameter_sequence=e->sequence;
    }
    ks_timbre_release(e->c);s_kst_job_active=s_kst_model_job=s_kst_store_job=false;
}
static void owner_kst_cancel(void)
{
    if(!s_kst_job_active && !ks_timbre_reserved(&s_kst_job)) {
        /* Candidate lifetime exceeds its HTTP receipt. STOP must still retire
         * a pre-COMMIT upload after timeout, preserving the old failure result. */
        bool candidate=s_kst_model_job || s_kst_cancel_wait ||
            (s_kst.phase==KST_UNKNOWN && s_kst.requested_generation!=s_kst_actual_generation);
        if(!candidate && s_kst.phase!=KST_UNKNOWN)return;
        s_kst_cancel_wait=candidate;s_kst_model_job=false;s_kst.phase=KST_CANCELED;
        if(candidate)midi_template_link_cancel_timbre(s_kst.requested_generation);
        s_template_valid=false;return;
    }
    s_kst_job_active=true;s_kst_cancel_wait=s_kst_model_job;
    if(s_kst_cancel_wait)midi_template_link_cancel_timbre(s_kst.requested_generation);
    s_kst.phase=KST_CANCELED;owner_kst_finish(FM_RESULT_CANCELLED);
    /* An unknown COMMIT is kept fenced by midi_template_link. Never publish
     * a replacement on cancellation until an actual terminal observation. */
    s_template_valid=false;
}
static bool owner_kst_begin(const ks_timbre_setting_t *setting,int64_t now)
{
    if(owner_local_frozen() || s_kst_boot_legacy_pending)return false;
    uint32_t floor=s_kst.link.timbre_requested;
    if(s_kst.link.timbre_applied>floor)floor=s_kst.link.timbre_applied;
    if(s_kst_generation_counter<floor)s_kst_generation_counter=floor;
    if(s_kst_generation_counter==UINT32_MAX)return false;
    s_kst_model_generation=++s_kst_generation_counter;s_kst_boot_legacy_pending=false;s_kst.initializing=false;
    s_kst.setting=*setting;s_kst.requested_instrument=setting->preset.instrument_id;
    s_kst.requested_generation=s_kst_model_generation;s_kst.phase=KST_QUEUED;s_kst.dirty=true;
    s_kst_model_job=true;s_kst_started=now;s_template_valid=false;
    ks_voice_decay(setting->decay_ms);
    for(unsigned i=0;i<WEB_CLIENTS;i++){s_web[i].keys=s_web[i].desired_keys=0;s_web[i].armed=false;}
    return true;
}
static bool owner_kst_compile(midi_template_image_t *image,unsigned decay)
{
    if(s_kst.setting.decay_ms!=decay){s_kst.setting.decay_ms=decay;s_kst.dirty=true;}
    if(!s_kst_model_generation)return true;
    static ks_timbre_image_t physical;
    if(!ks_timbre_compile(&s_kst.setting.preset,decay,g_patch.volume,g_patch.pan,s_kst_model_generation,&physical))return false;
    memcpy(image->ks_notes,physical.notes,sizeof image->ks_notes);
    memcpy(image->ks_pan,physical.pan,sizeof image->ks_pan);
    memcpy(image->ks_header,physical.header,sizeof image->ks_header);
    s_kst_expected_crc=physical.header[KST_H_MODEL_CRC];return true;
}
static void owner_kst_published(void)
{
    midi_template_link_state(&s_kst.link);s_kst_expected_revision=s_kst.link.ks_desired_revision;
    if(s_kst_boot_checked && s_kst.link.ks_applied_revision!=s_kst_expected_revision) {s_kst_refresh_pending=true;}
}
static void owner_kst_update(void)
{
    if(!s_kst_initialized) {
        ks_timbre_state_read(&s_kst);s_kst_actual=s_kst.setting;s_kst_initialized=true;
        midi_template_link_timbre_enable();
    }
    midi_template_link_state(&s_kst.link);
}
static bool owner_kst_native_match(void)
{
    if(!s_kst_model_generation && s_kst.requested_instrument==0)
        return s_kst.link.timbre_valid && !s_kst.link.timbre_state && s_kst.link.timbre_id==0 &&
            s_kst.link.timbre_applied==s_kst.requested_generation && s_kst.link.timbre_template &&
            s_kst.link.timbre_template==s_kst.link.ks_applied_generation &&
            s_kst.link.ks_applied_model_generation==0 && s_kst.link.ks_applied_revision==s_kst_expected_revision;
    return s_kst.link.timbre_valid && !s_kst.link.timbre_state &&
        s_kst.link.timbre_applied==s_kst.requested_generation && s_kst.link.timbre_id==s_kst.requested_instrument &&
        s_kst.link.timbre_crc==s_kst_expected_crc && s_kst.link.timbre_template &&
        s_kst.link.timbre_template==s_kst.link.ks_applied_generation &&
        s_kst.link.ks_applied_model_generation==s_kst.requested_generation &&
        s_kst.link.ks_applied_crc==s_kst_expected_crc &&
        s_kst.link.timbre_image_crc==s_kst.link.ks_applied_image_crc &&
        s_kst.link.ks_applied_revision==s_kst_expected_revision;
}
static bool owner_kst_poll(int64_t now)
{
    owner_kst_update();bool changed=false;
    if(!s_kst_job_active) {
        static ks_timbre_request_t waiting;
        if(ks_timbre_reserved(&waiting) && now>=waiting.event.expires) {s_kst_job=waiting;s_kst_job_active=true;owner_kst_cancel();changed=true;}
    }
    if(s_kst.link.timbre_available && s_kst.link.timbre_valid && !s_kst_boot_checked && !owner_local_frozen()) {
        s_kst_boot_checked=true;
        /* Reboot never assumes a factory name describes surviving FPGA audio.
         * A configured startup setting is uploaded with a fresh model token. */
        static ks_timbre_setting_t boot;
        if(ks_timbre_startup_setting(&boot))owner_kst_begin(&boot,now);
        else {s_kst.phase=KST_IDLE;s_kst_boot_legacy_pending=true;s_kst.initializing=true;}
        changed=true;
    }
    if(s_kst_boot_legacy_pending && s_kst.link.timbre_valid && !s_kst.link.timbre_state &&
       s_kst.link.timbre_id==0 && s_kst.link.timbre_template && !s_kst.link.fault &&
       s_kst.link.timbre_template==s_kst.link.ks_applied_generation &&
       s_kst.link.ks_applied_model_generation==0) {
        s_kst_actual=s_kst.setting;s_kst_actual_generation=s_kst.link.timbre_applied;
        s_kst.requested_generation=s_kst.link.timbre_applied;s_kst.requested_instrument=0;
        s_kst.phase=KST_APPLIED;s_kst_boot_legacy_pending=false;s_kst.initializing=false;changed=true;
    }
    if(s_kst_refresh_pending && !s_kst_model_job && !s_kst_cancel_wait) {
        if(owner_kst_native_match()) {s_kst_actual=s_kst.setting;s_kst.dirty=!ks_timbre_setting_saved(&s_kst.setting);s_kst.phase=KST_APPLIED;s_kst_refresh_pending=false;changed=true;}
        else if(s_kst.link.fault)s_kst.phase=KST_UNKNOWN;
        else if(s_kst.phase==KST_APPLIED)s_kst.phase=KST_UPLOADING;
    }
    if(s_kst_model_job && owner_local_frozen()) {owner_kst_cancel();changed=true;}
    if(s_kst_job_active) {
        const web_v2_event_t *e=&s_kst_job.event;
        if(e->client>=WEB_CLIENTS || s_web[e->client].token!=e->token || e->context_epoch!=s_control_epoch || now>=e->expires) {
            if(s_kst_model_job)engine_link_ks_panic();
            owner_kst_cancel();changed=true;
        }
    }
    if(s_kst_store_job) {
        bool success;
        if(ks_timbre_store_result(s_kst_job.event.c,&success)) {
            if(success && s_kst_job.op==KST_SAVE &&
               s_kst.setting.decay_ms==s_kst_job.setting.decay_ms &&
               !memcmp(&s_kst.setting.preset,&s_kst_job.setting.preset,sizeof s_kst.setting.preset))s_kst.dirty=false;
            s_kst.phase=!success?KST_FAILED:s_kst_refresh_pending?KST_UPLOADING:KST_APPLIED;
            owner_kst_finish(success?FM_RESULT_OK:FM_RESULT_BACKEND_FAULT);changed=true;
        }
    }
    if(s_kst_model_job) {
        if(!s_kst.link.timbre_available || !s_kst.link.timbre_valid || s_kst.link.fault) {
            if(now-s_kst_started>1000000LL) {s_kst.phase=KST_UNKNOWN;if(s_kst_job_active)owner_kst_finish(FM_RESULT_BACKEND_FAULT);else s_kst_model_job=false;changed=true;}
        } else if(s_kst.link.ks_upload_model_generation==s_kst.requested_generation &&
                  s_kst.link.ks_upload_generation &&
                  s_kst.link.timbre_completed_template==s_kst.link.ks_upload_generation &&
                  s_kst.link.timbre_requested_template==s_kst.link.ks_upload_generation && s_kst.link.timbre_result) {
            s_kst.phase=s_kst.link.timbre_result==1?KST_CANCELED:KST_FAILED;
            if(s_kst_job_active)owner_kst_finish(s_kst.link.timbre_result==1?FM_RESULT_CANCELLED:FM_RESULT_BACKEND_FAULT);else s_kst_model_job=false;
            changed=true;
        } else if(owner_kst_native_match()) {
            s_kst_actual=s_kst.setting;s_kst_actual_generation=s_kst.requested_generation;s_kst.dirty=!ks_timbre_setting_saved(&s_kst.setting);s_kst.phase=KST_APPLIED;s_kst_refresh_pending=false;
            if(s_kst_job_active)owner_kst_finish(FM_RESULT_OK);else s_kst_model_job=false;changed=true;
        } else {
            s_kst.phase=(s_kst.link.timbre_state&15)?KST_SWITCHING:KST_UPLOADING;
            if(now-s_kst_started>1000000LL) {s_kst.phase=KST_UNKNOWN;if(s_kst_job_active)owner_kst_finish(FM_RESULT_BACKEND_FAULT);else s_kst_model_job=false;changed=true;}
        }
    }
    if((s_kst.phase==KST_UNKNOWN || s_kst_cancel_wait) && owner_kst_native_match() && !s_kst.link.fault) {
        /* A late real adoption updates the actual display but never changes
         * the retired request's CANCELLED/FAULT result into success. */
        s_kst_actual=s_kst.setting;s_kst_actual_generation=s_kst.requested_generation;
        s_kst.phase=KST_APPLIED;s_kst_cancel_wait=false;changed=true;
    }
    bool explicit_terminal=s_kst.link.ks_upload_model_generation==s_kst.requested_generation &&
        s_kst.link.ks_upload_generation && s_kst.link.timbre_valid &&
        s_kst.link.timbre_completed_template==s_kst.link.ks_upload_generation &&
        s_kst.link.timbre_requested_template==s_kst.link.ks_upload_generation && s_kst.link.timbre_result;
    bool local_canceled=s_kst.link.ks_canceled_model_generation==s_kst.requested_generation && s_kst.requested_generation;
    if(s_kst.phase==KST_UNKNOWN && explicit_terminal) {
        s_kst.phase=s_kst.link.timbre_result==1?KST_CANCELED:KST_FAILED;changed=true;
    }
    if(!s_kst_model_job && (s_kst.phase==KST_CANCELED || s_kst.phase==KST_FAILED) &&
       s_kst.link.timbre_valid && !s_kst.link.timbre_state && !s_kst.link.busy &&
       s_kst.link.timbre_applied==s_kst_actual_generation &&
       (!s_kst_cancel_wait || explicit_terminal || local_canceled)) {
        s_kst.setting=s_kst_actual;s_kst_model_generation=s_kst_actual_generation;
        s_kst_cancel_wait=false;s_template_valid=false;
    }
    ks_timbre_state_publish(&s_kst);return changed;
}
static void handle_web_ks_timbre(const web_v2_event_t *event,int64_t now)
{
    static ks_timbre_request_t job;
    if(!ks_timbre_claim(event,&job))return;
    owner_local_sync();owner_kst_update();uint32_t code=FM_RESULT_OK;
    if(event->client>=WEB_CLIENTS || s_web[event->client].token!=event->token) {ks_timbre_release(event->c);return;}
    if(event->context_epoch!=s_control_epoch || now>=event->expires || event->sequence<=s_web[event->client].sequence)code=FM_RESULT_CANCELLED;
    else if(owner_local_frozen() || s_kst_boot_legacy_pending)code=FM_RESULT_BUSY;
    else if(s_kst_job_active || owner_kst_blocked() || s_kst.link.busy || (s_kst.link.timbre_state&0x100))code=FM_RESULT_BUSY;
    else if(!s_kst.link.timbre_available || !s_kst.link.timbre_valid)code=FM_RESULT_UNSUPPORTED;
    if(code==FM_RESULT_OK) {
        if(job.op==KST_APPLY || job.op==KST_LOAD) {
            static ks_timbre_setting_t setting;
            setting=job.setting;
            if(job.has_slot) {if(!ks_timbre_slot_read(job.slot,&setting))code=FM_RESULT_BAD_VALUE;}
            else if(job.has_instrument) {
                memset(&setting,0,sizeof setting);setting.decay_ms=ks_voice_decay_ms();
                snprintf(setting.name,sizeof setting.name,"%s",job.instrument_id==1?"Harp":job.instrument_id==2?"Guzheng":"Legacy");
                if(!ks_timbre_factory(job.instrument_id,&setting.preset))code=FM_RESULT_BAD_VALUE;
            } else setting.decay_ms=ks_voice_decay_ms();
            if(code==FM_RESULT_OK && !owner_kst_begin(&setting,now))code=FM_RESULT_BACKEND_FAULT;
        } else {
            if(job.op==KST_SAVE) {
                if(s_kst.phase!=KST_APPLIED || s_kst_refresh_pending ||
                   s_kst.setting.decay_ms!=ks_voice_decay_ms() || !owner_kst_native_match())code=FM_RESULT_BUSY;
                else {char name[KS_TIMBRE_NAME];memcpy(name,job.setting.name,sizeof name);job.setting=s_kst.setting;job.setting.decay_ms=ks_voice_decay_ms();memcpy(job.setting.name,name,sizeof name);}
            }
            if(job.op==KST_STARTUP && job.enabled && job.has_slot) {static ks_timbre_setting_t ignored;if(!ks_timbre_slot_read(job.slot,&ignored))code=FM_RESULT_BAD_VALUE;}
            if(code==FM_RESULT_OK && !ks_timbre_store_request(&job))code=FM_RESULT_BUSY;
            if(code==FM_RESULT_OK)s_kst_store_job=true;
        }
    }
    s_kst_job=job;s_kst_job_active=true;
    if(event->sequence>s_web[event->client].sequence)s_web[event->client].sequence=event->sequence;
    if(code!=FM_RESULT_OK){s_kst.phase=code==FM_RESULT_CANCELLED?KST_CANCELED:KST_FAILED;owner_kst_finish(code);}
    ks_timbre_state_publish(&s_kst);
}
#else
static uint32_t s_kst_model_generation,s_template_kst_generation;
static bool owner_kst_unknown(void){return false;}
static bool owner_kst_blocked(void){return false;}
static void owner_kst_cancel(void){}
static void owner_kst_published(void){}
static void owner_kst_update(void){}
static bool owner_kst_compile(midi_template_image_t *image,unsigned decay){(void)image;(void)decay;return true;}
static bool owner_kst_poll(int64_t now){(void)now;return false;}
static void handle_web_ks_timbre(const web_v2_event_t *e,int64_t now){(void)e;(void)now;}
#endif
