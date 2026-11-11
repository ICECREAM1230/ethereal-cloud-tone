// Actual DDS worker/owner and compiler; only transport and clock are host fakes.
#define _POSIX_C_SOURCE 200809L
#define main unused_original_raw_owner_main
#include "test_raw_web_owner.c"
#undef main
#undef TAG
#include "dds_presets.c"

extern engine_fm_state_t fake_fm_state;
static engine_cpu_mode_state_t cpu_mode={.supported=true,.valid=true,.ready=true,.channel_routes=true};
void engine_link_cpu_mode_state(engine_cpu_mode_state_t *state){*state=cpu_mode;}
bool engine_link_cpu_mode_request(uint32_t mode,uint32_t id){(void)mode;(void)id;return false;}
bool engine_link_cpu_mode_request_source(control_source_ref_t source,uint32_t mode,uint32_t id)
{(void)source;return engine_link_cpu_mode_request(mode,id);}
static const char *scenario;
static bool is(const char *name){return !strcmp(scenario,name);}
static void phase_is(dds_phase_t phase)
{
    dds_job_status_t status;dds_presets_status(&status);
    if(status.phase!=phase)fprintf(stderr,"%s: actual_phase=%u expected=%u error=%s\n",scenario,status.phase,phase,status.error);
    assert(status.phase==phase);
}
static control_source_ref_t source;
static uint32_t request(dds_operation_t op,const char *path,const char *body,size_t n)
{
    char error[DDS_ERROR_MAX];uint32_t id;
    assert(control_source_claim(CONTROL_SOURCE_WIFI,fake_time,&source));
    assert(dds_presets_request_source(source,op,path,body,n,0,&id,error));
    phase_is(DDS_WAIT_OWNER);dds_owner_request(id);return id;
}
static void worker(uint32_t id)
{phase_is(DDS_WORKING);uint32_t queued;assert(xQueueReceive(s_work,&queued,0)==pdTRUE && queued==id);work(id);}
static patch_t old;
static void retained(void){assert(!memcmp(&old,&g_patch,sizeof old));}
static void failed(bool sub1)
{
    phase_is(DDS_FAILED);retained();assert(!s_dds_switch_id);
    dds_job_status_t status;dds_presets_status(&status);
    if(sub1)assert(strstr(status.error,"SUB1"));
    printf("PASS %s phase=failed old_patch_retained=1 STOP_requested=%u error=%s\n",scenario,observed.stop_requested,status.error);
}
int main(int argc,char **argv)
{
    assert(argc==4);scenario=argv[1];setvbuf(stdout,NULL,_IONBF,0);
    FILE *in=fopen("storage/Patches/Factory/Pads/Galaxy.toml","rb");assert(in);
    char body[2048];size_t n=fread(body,1,sizeof body-1,in);assert(n && n<sizeof body-1 && feof(in) && !ferror(in) && !fclose(in));body[n]=0;
    s_root=argv[3];s_mounted=true;
    control_source_init();event_bus_init();reset_owner();observed.cpu_control=true;
    template_state.sub_patch_valid=template_state.sub_patch_supported=true;
    if(is("global-ks-channel") || is("global-ks-fmc") || is("single-editor-ks"))cpu_mode.desired=cpu_mode.applied=FM_ENGINE_KS;
    if(is("global-fm-channel") || is("global-fm-fmc"))cpu_mode.desired=cpu_mode.applied=FM_ENGINE_FM;
    if(is("single-editor-ks") || is("single-editor-dds") || is("single-editor-dds-fmc"))cpu_mode.channel_routes=false;
    // Re-read a full state captured by the real engine service in this run.
    bool found=strstr(scenario,"-fmc")!=NULL;
    FILE *snapshot=fopen(argv[2],"rb");assert(snapshot);
    assert(!fseek(snapshot,(long)((cpu_mode.applied*2+found)*sizeof(fake_fm_state)),SEEK_SET));
    assert(fread(&fake_fm_state,sizeof(fake_fm_state),1,snapshot)==1 && !fclose(snapshot));
    assert(fake_fm_state.status.available==found && fake_fm_state.status.selected==found);
    if(found)assert(fake_fm_state.status.requested_backend==FM_BACKEND_JT51_OPM);
    uint32_t original_global=cpu_mode.applied;
    dds_presets_init();owner_local_sync();dds_presets_owner_context(s_control_epoch);
    if(cpu_mode.channel_routes) {
        web_v2_event_t select={.client=0,.token=111,.sequence=1,.context_epoch=s_control_epoch,
            .op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=FM_ENGINE_DDS,.expires=fake_time+WEB_LEASE_US};
        assert(owner_remote_begin(&select,fake_time));
        assert(s_v2_result_code[0]==FM_RESULT_OK && s_web[0].midi_channel==FM_ENGINE_DDS);
        assert(cpu_mode.desired==original_global && cpu_mode.applied==original_global);
    }
    old=g_patch;
    const char *path="Patches/User/Synth Lead/Galaxy.toml";
    uint32_t id=request(DDS_IMPORT,path,body,n);worker(id);phase_is(DDS_DONE);retained();
    if(is("blocked-request"))cpu_mode.fault=true;
    if(is("out-of-range-request"))cpu_mode.desired=cpu_mode.applied=3;
    if(is("desired3-request"))cpu_mode.desired=3;
    if(is("applied3-request"))cpu_mode.applied=3;
    if(is("applied42-request"))cpu_mode.applied=42;
    id=request(DDS_LOAD,path,NULL,0);
    if(is("single-editor-ks") || is("single-editor-dds-fmc") || is("blocked-request") || is("out-of-range-request") ||
       is("desired3-request") || is("applied3-request") || is("applied42-request")) {
        phase_is(DDS_FAILED);retained();uint32_t unused;assert(xQueueReceive(s_work,&unused,0)==pdFALSE);
        failed(false);return 0;
    }
    worker(id);phase_is(DDS_READY);retained();
    if(is("cap-invalid-ready"))template_state.sub_patch_valid=false;
    if(is("cap-unsupported-ready"))template_state.sub_patch_supported=false;
    if(is("route-fault-ready"))cpu_mode.fault=true;
    if(is("route-invalid-ready"))cpu_mode.valid=false;
    if(is("source-revoke-ready"))control_source_revoke(source);
    if(is("context-change-ready"))s_control_epoch++;
    (void)dds_owner_poll(fake_time);
    if(is("cap-invalid-ready") || is("cap-unsupported-ready") || is("route-fault-ready") ||
       is("route-invalid-ready") || is("source-revoke-ready") || is("context-change-ready")) {
        failed(is("cap-invalid-ready") || is("cap-unsupported-ready"));
        assert(!observed.stop_requested);return 0;
    }
    phase_is(DDS_CLOSING);retained();assert(s_dds_stop_ticket && observed.stop_pending);
    assert(!dds_owner_poll(fake_time));phase_is(DDS_CLOSING);retained();
    if(is("stop-timeout")) {
        observed.stop_completed=s_dds_stop_ticket-1;observed.stop_pending=false;
        assert(!dds_owner_poll(fake_time));phase_is(DDS_CLOSING);retained();
        (void)dds_owner_poll(s_dds_switch_deadline+1);failed(false);return 0;
    }
    if(is("cap-invalid-closing"))template_state.sub_patch_valid=false;
    if(is("cap-unsupported-closing"))template_state.sub_patch_supported=false;
    if(is("route-fault-closing"))cpu_mode.fault=true;
    if(is("source-revoke-closing"))control_source_revoke(source);
    if(is("context-change-closing"))s_control_epoch++;
    if(is("reset-closing"))observed.reset_count++;
    if(is("stop-fault-closing"))observed.stop_fault=true;
    if(is("stop-unavailable-closing"))observed.available=false;
    observed.stop_completed=observed.stop_requested;observed.stop_pending=false;observed.ready=true;
    (void)dds_owner_poll(fake_time);
    if(is("cap-invalid-closing") || is("cap-unsupported-closing") || is("route-fault-closing") ||
       is("source-revoke-closing") || is("context-change-closing") || is("reset-closing") ||
       is("stop-fault-closing") || is("stop-unavailable-closing")) {
        failed(is("cap-invalid-closing") || is("cap-unsupported-closing"));return 0;
    }
    phase_is(DDS_DONE);assert(memcmp(&old,&g_patch,sizeof old) && g_patch.osc[0].wave==WAVE_PULSE);
    assert(!s_dds_switch_id && !template_state.applied_generation);
    fake_time+=30000;owner_templates_update();
    assert(publishes==1 && s_template_compiled && s_template_full_sub);
    assert(published_image.elements[2][5]==patch_adsr_rate1(&g_patch.env[0]));
    assert(published_image.elements[2][6]==patch_adsr_rate2(&g_patch.env[0]));
    assert(published_image.elements[2][7]==patch_adsr_rate1(&g_patch.env[1]));
    assert(published_image.elements[3][5]==patch_adsr_rate2(&g_patch.env[1]));
    assert(cpu_mode.desired==original_global && cpu_mode.applied==original_global);
    printf("PASS %s phase=done patch_changed=1 full_SUB1_compiled=1 native_ACK=0 global_editor=%u client_route=%u STOP_requested=%u\n",scenario,cpu_mode.applied,s_web[0].midi_channel,observed.stop_requested);
    return 0;
}
