#define main existing_raw_owner_main
#include "test_raw_web_owner.c"
#undef main
extern engine_fm_state_t fake_fm_state;
int main(void)
{
    reset_owner();
    s_requested_engine=FM_ENGINE_FM; // actual cold-boot request, before any backend is selected
    fake_fm_state.status=(engine_fm_status_t){.available=true,.valid=true,.ready=true,
        .backend_id=FM_BACKEND_JT51_OPM,.profile_id=1,.voice_count=8,
        .flags=FM_FLAG_READY|FM_FLAG_MUTED};
    /* Boot may bind only once real capability/READY is available. */
    owner_templates_update();
    assert(fake_fm_state.status.requested_backend==FM_BACKEND_JT51_OPM);
    fake_fm_state.status.selected=true;fake_fm_state.status.switching=false;
    fake_fm_state.status.applied_backend=FM_BACKEND_JT51_OPM;
    fake_fm_state.status.flags=FM_FLAG_READY|FM_FLAG_SELECTED;
    web_v2_event_t event={.client=0,.token=111,.sequence=1,.context_epoch=s_control_epoch,
        .op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=FM_ENGINE_DDS};
    handle_web_v2(&event,fake_time);
    assert(s_v2_result_code[0]==FM_RESULT_UNSUPPORTED);
    event.sequence=2;event.a=FM_ENGINE_KS;handle_web_v2(&event,fake_time);
    assert(s_v2_result_code[0]==FM_RESULT_UNSUPPORTED && !global_flushes);
    assert(!dds_selected());
    fake_fm_state.status.needs_select=true;fake_fm_state.status.recovering=true;
    fake_fm_state.status.requested_backend=0;s_template_valid=false;
    owner_templates_update();assert(!fake_fm_state.status.requested_backend);
    puts("FM_ONLY owner: boot FM binding, DDS/KS selection rejected before mutation, no automatic recovery SELECT PASS");
}
