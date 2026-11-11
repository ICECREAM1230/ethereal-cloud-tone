#define S3_KS_TIMBRE_HOST_TEST 1
#define main previous_raw_owner_main
#include "test_raw_web_owner.c"
#undef main
extern engine_local_state_t fake_local_state;
void midi_template_link_timbre_enable(void){}
void midi_template_link_cancel_timbre(uint32_t model){template_state.ks_canceled_model_generation=model;}
static void fixture(void)
{
    memset(&fake_local_state,0,sizeof fake_local_state);s_owner_local_generation=s_owner_local_adopted=0;
    reset_owner();ks_timbre_store_init();
    memset(&s_kst,0,sizeof s_kst);s_kst_initialized=s_kst_job_active=s_kst_model_job=s_kst_store_job=s_kst_boot_checked=s_kst_cancel_wait=s_kst_boot_legacy_pending=s_kst_refresh_pending=false;
    s_kst_actual_generation=s_kst_expected_revision=0;
    s_kst_model_generation=s_kst_generation_counter=s_template_kst_generation=0;
    template_state=(midi_template_link_state_t){.ready=true,.available=true,.timbre_available=true,.timbre_valid=true,.timbre_abi=65536,.timbre_capabilities=63,.timbre_applied=3,.timbre_requested=3,.timbre_id=0,.timbre_version=1,.timbre_factory=1,.timbre_template=4,.ks_applied_generation=4};
    owner_kst_poll(fake_time);
}
static web_v2_event_t apply(unsigned seq,unsigned id)
{
    ks_timbre_request_t r={.op=KST_APPLY,.has_instrument=true,.instrument_id=id,.event={.client=0,.token=111,.sequence=seq,.context_epoch=s_control_epoch,.expires=fake_time+WEB_LEASE_US}};
    assert(ks_timbre_reserve(&r,fake_time));handle_web_ks_timbre(&r.event,fake_time);return r.event;
}
static void real_adoption(unsigned model,unsigned instrument,unsigned table,unsigned crc)
{
    template_state.timbre_applied=template_state.timbre_requested=model;template_state.timbre_id=instrument;
    template_state.timbre_crc=s_kst_expected_crc;template_state.timbre_template=template_state.ks_applied_generation=table;
    template_state.ks_applied_model_generation=model;template_state.ks_applied_crc=s_kst_expected_crc;
    template_state.ks_applied_image_crc=template_state.timbre_image_crc=crc;
    template_state.ks_applied_revision=s_kst_expected_revision;template_state.timbre_state=0;
}
static void late_results_and_decay(void)
{
    fixture();web_v2_event_t e=apply(1,2);assert(owner_kst_compile(&s_template_image,2000));
    s_kst_expected_revision=7;template_state.ks_upload_model_generation=4;template_state.ks_upload_generation=9;
    fake_time+=1000001;assert(owner_kst_poll(fake_time));assert(s_kst.phase==KST_UNKNOWN&&!s_kst_job_active);
    uint32_t code=s_kst.result.code;assert(code!=FM_RESULT_OK);real_adoption(4,2,9,101);
    assert(owner_kst_poll(fake_time));assert(s_kst.phase==KST_APPLIED&&s_kst.result.request_id==e.c&&s_kst.result.code==code);
    /* Ordinary T60 refresh is exported only after its real table/image ACK. */
    ks_voice_decay(4000);assert(owner_kst_compile(&s_template_image,4000));
    template_state.ks_desired_revision=8;owner_kst_published();assert(s_kst_refresh_pending);
    owner_kst_poll(fake_time);assert(s_kst.phase==KST_UPLOADING);
    real_adoption(4,2,10,102);assert(owner_kst_poll(fake_time));assert(s_kst.phase==KST_APPLIED);
    char document[4097];assert(ks_timbre_document_emit(&s_kst.setting,document,sizeof document));assert(strstr(document,"decay_ms = 4000"));assert(s_kst_actual.decay_ms==4000);
    /* A canceled pre-COMMIT request retains the last confirmed Guzheng. */
    e=apply(2,1);owner_kst_cancel();owner_kst_poll(fake_time);
    assert(!s_kst_cancel_wait&&s_kst_model_generation==4&&s_kst.setting.preset.instrument_id==2);
    assert(owner_kst_compile(&s_template_image,4000)&&s_template_image.ks_header[2]==2&&s_template_image.ks_header[3]==4);
    fixture();e=apply(1,1);assert(owner_kst_compile(&s_template_image,3000));
    template_state.ks_upload_model_generation=4;template_state.ks_upload_generation=9;
    fake_time+=1000001;owner_kst_poll(fake_time);assert(s_kst.phase==KST_UNKNOWN);code=s_kst.result.code;
    template_state.timbre_requested=0;template_state.timbre_completed_template=template_state.timbre_requested_template=9;template_state.timbre_result=1;
    assert(owner_kst_poll(fake_time));assert(s_kst.phase==KST_CANCELED&&s_kst.result.code==code&&s_kst_model_generation==3);
}
static void stop_after_timeout_before_commit(void)
{
    fixture();web_v2_event_t e=apply(1,1);assert(owner_kst_compile(&s_template_image,3000));
    fake_time+=1000001;owner_kst_poll(fake_time);assert(s_kst.phase==KST_UNKNOWN&&!s_kst_job_active&&!s_kst_model_job);
    ks_timbre_result_t failed=s_kst.result;assert(failed.request_id==e.c&&failed.code!=FM_RESULT_OK);
    owner_kst_cancel();assert(s_kst_cancel_wait&&s_kst.phase==KST_CANCELED);
    assert(!memcmp(&failed,&s_kst.result,sizeof failed));
    owner_kst_poll(fake_time);assert(!s_kst_cancel_wait&&s_kst_model_generation==3);
    assert(!memcmp(&failed,&s_kst.result,sizeof failed));
}
static void save_rejects_unpublished_decay(void)
{
    fixture();apply(1,2);assert(owner_kst_compile(&s_template_image,2000));real_adoption(4,2,9,101);owner_kst_poll(fake_time);
    ks_voice_decay(4000);
    ks_timbre_request_t save={.op=KST_SAVE,.has_slot=true,.slot=5,.setting={.name="Not yet applied"},.event={.client=0,.token=111,.sequence=2,.context_epoch=s_control_epoch,.expires=fake_time+WEB_LEASE_US}};
    assert(ks_timbre_reserve(&save,fake_time));handle_web_ks_timbre(&save.event,fake_time);
    assert(!s_kst_store_job&&s_kst.result.code==FM_RESULT_BUSY);
}
static void save_snapshot_and_live_decay(void)
{
    fixture();apply(1,2);assert(owner_kst_compile(&s_template_image,2000));
    real_adoption(4,2,9,101);assert(owner_kst_poll(fake_time));
    ks_voice_decay(2000);
    ks_timbre_request_t save={.op=KST_SAVE,.has_slot=true,.slot=6,.setting={.name="Stored 2000"},.event={.client=0,.token=111,.sequence=2,.context_epoch=s_control_epoch,.expires=fake_time+WEB_LEASE_US}};
    assert(ks_timbre_reserve(&save,fake_time));handle_web_ks_timbre(&save.event,fake_time);assert(s_kst_store_job);
    ks_voice_decay(4000);assert(owner_kst_compile(&s_template_image,4000));
    template_state.ks_desired_revision=8;owner_kst_published();assert(s_kst.dirty&&s_kst_refresh_pending);
    ks_timbre_store_host_step();assert(owner_kst_poll(fake_time));
    ks_timbre_setting_t stored;assert(ks_timbre_slot_read(6,&stored)&&stored.decay_ms==2000);
    assert(s_kst.setting.decay_ms==4000&&s_kst.dirty&&s_kst.phase==KST_UPLOADING);
    real_adoption(4,2,10,102);assert(owner_kst_poll(fake_time));assert(s_kst.dirty&&s_kst.phase==KST_APPLIED);
}
static void reboot_old_harp_defaults_safely(void)
{
    fixture();s_kst_initialized=s_kst_boot_checked=false;s_kst_model_generation=0;
    template_state.timbre_id=1;template_state.timbre_applied=template_state.timbre_requested=33;
    template_state.ks_applied_model_generation=33;template_state.timbre_template=template_state.ks_applied_generation=45;
    owner_kst_poll(fake_time);assert(s_kst.phase==KST_IDLE&&s_kst_boot_legacy_pending);
    assert(s_kst.requested_generation!=template_state.timbre_applied); /* HTTP preset=null, cannot save */
    apply(1,2);assert(!s_kst_model_job&&!s_kst_cancel_wait&&s_kst.result.code==FM_RESULT_BUSY&&s_kst_model_generation==0);
    assert(midi_templates_compile(&g_patch,3000,&s_template_image)&&owner_kst_compile(&s_template_image,3000));
    assert(s_template_image.ks_header[0]==0); /* existing B3FF safe Legacy path */
    template_state.timbre_id=0;template_state.timbre_applied=34;template_state.timbre_template=template_state.ks_applied_generation=46;template_state.ks_applied_model_generation=0;
    owner_kst_poll(fake_time);assert(s_kst.phase==KST_APPLIED&&!s_kst_boot_legacy_pending&&s_kst.requested_generation==34&&s_kst.setting.preset.instrument_id==0);
}
static void local_owner_and_stale_sequence(void)
{
    fixture();s_web[0].sequence=10;
    ks_timbre_request_t stale={.op=KST_APPLY,.has_instrument=true,.instrument_id=2,.event={.client=0,.token=111,.sequence=9,.context_epoch=s_control_epoch,.expires=fake_time+WEB_LEASE_US}};
    assert(ks_timbre_reserve(&stale,fake_time));handle_web_ks_timbre(&stale.event,fake_time);
    assert(s_web[0].sequence==10&&!s_kst_model_job&&s_kst.result.code==FM_RESULT_CANCELLED);
    fixture();fake_local_state=(engine_local_state_t){.supported=true,.valid=true,.local_active=true,.adopted=true,.generation=1,.requested=FM_ENGINE_KS,.applied=FM_ENGINE_KS,.engine_mask=7};
    owner_local_sync();apply(1,2);assert(!s_kst_model_job&&!s_kst_job_active&&s_kst.result.code==FM_RESULT_BUSY);
    /* Startup waits while local owns the engine; local takeover also retires
     * a startup candidate that has no HTTP reservation/lease. */
    ks_timbre_request_t boot={.op=KST_STARTUP,.enabled=true,.has_instrument=true,.instrument_id=1,.event={.c=900}};
    assert(ks_timbre_store_request(&boot));ks_timbre_store_host_step();s_kst_boot_checked=false;
    owner_kst_poll(fake_time);assert(!s_kst_boot_checked&&!s_kst_model_job);
    fake_local_state.local_active=false;owner_kst_poll(fake_time);assert(s_kst_boot_checked&&s_kst_model_job&&!s_kst_job_active);
    fake_local_state.local_active=true;fake_local_state.generation++;owner_local_sync();owner_kst_poll(fake_time);owner_kst_poll(fake_time);
    assert(!s_kst_model_job&&!s_kst_cancel_wait&&s_kst.phase==KST_CANCELED);
    boot.enabled=false;boot.event.c=901;assert(ks_timbre_store_request(&boot));ks_timbre_store_host_step();
}
int main(void)
{
    assert(previous_raw_owner_main()==0); /* existing owner scenarios with extension code compiled in */
    fixture();web_v2_event_t e=apply(1,2);
    assert(s_kst_model_job&&s_kst_job_active&&s_kst.requested_generation==4);
    assert(!s_web[0].parameter_sequence&&owner_kst_blocked());
    assert(!owner_kst_poll(fake_time));assert(s_kst.phase==KST_UPLOADING);
    assert(owner_kst_compile(&s_template_image,3000));
    assert(s_template_image.ks_header[2]==2&&s_template_image.ks_header[3]==4);
    template_state.timbre_applied=4;template_state.timbre_requested=4;template_state.timbre_id=2;template_state.timbre_crc=s_kst_expected_crc;
    template_state.timbre_template=template_state.ks_applied_generation=5;
    template_state.ks_applied_model_generation=4;template_state.ks_applied_crc=s_kst_expected_crc;template_state.ks_applied_image_crc=template_state.timbre_image_crc=42;
    assert(owner_kst_poll(fake_time));assert(!s_kst_job_active&&s_kst.phase==KST_APPLIED&&s_kst.result.code==0&&s_kst.result.request_id==e.c);
    assert(s_web[0].parameter_sequence==1&&!owner_kst_blocked());
    ks_timbre_request_t save={.op=KST_SAVE,.has_slot=true,.slot=7,.setting={.name="古筝"},.event={.client=0,.token=111,.sequence=2,.context_epoch=s_control_epoch,.expires=fake_time+WEB_LEASE_US}};
    assert(s_kst.dirty);assert(ks_timbre_reserve(&save,fake_time));handle_web_ks_timbre(&save.event,fake_time);assert(s_kst_store_job);
    ks_timbre_store_host_step();assert(owner_kst_poll(fake_time));assert(!s_kst.dirty&&s_kst.result.code==0);
    ks_timbre_setting_t stored;assert(ks_timbre_slot_read(7,&stored)&&stored.preset.instrument_id==2&&!strcmp(stored.name,"古筝"));
    e=apply(3,1);s_control_epoch++;assert(owner_kst_poll(fake_time));assert(s_kst.phase==KST_CANCELED&&s_kst.result.code==FM_RESULT_CANCELLED);
    assert(!s_kst_job_active&&s_web[0].parameter_sequence==2);
    late_results_and_decay();stop_after_timeout_before_commit();save_rejects_unpublished_decay();save_snapshot_and_live_decay();reboot_old_harp_defaults_safely();local_owner_and_stale_sequence();
    puts("KS actual voice owner: fresh native model tokens, no success before ACK, dirty save, NVS worker and context cancellation PASS");
}
