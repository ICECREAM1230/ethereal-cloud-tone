#define main previous_raw_owner_main
#include "test_raw_web_owner.c"
#undef main
extern engine_local_state_t fake_local_state;
extern unsigned fake_remote_requests;
extern engine_fm_state_t fake_fm_state;
extern unsigned fake_fm_panics;
static void setup_local(bool complete)
{
    memset(&fake_local_state,0,sizeof(fake_local_state));
    s_owner_local_generation=s_owner_local_adopted=0;memset(&s_remote_mode,0,sizeof(s_remote_mode));
    reset_owner();s_requested_engine=FM_ENGINE_DDS;
    memset(&fake_fm_state,0,sizeof(fake_fm_state));fake_fm_panics=fake_remote_requests=0;
    fake_local_state=(engine_local_state_t){.supported=true,.valid=true,.local_active=true,
        .busy=!complete,.blocked=!complete,.adopted=complete,.ready=complete,
        .generation=1,.revision=1,.completed=complete,.requested=FM_ENGINE_FM,.applied=FM_ENGINE_FM,.engine_mask=7};
}
static void cancel_old_context(void)
{
    setup_local(false);web_v2_event_t old=keys(0,111,1,1);
    s_web[0].stop_ticket=7;s_web[0].stop_wait_sequence=9;
    s_dds_switch_id=42;
    handle_web_v2(&old,fake_time);
    assert(!stream_length && "a physical revision must cancel old keys before owner dispatch");
    assert(s_v2_result_code[0]!=FM_RESULT_OK && !s_dds_switch_id);
    assert(!fake_fm_panics && !global_flushes && !performance_false);
    assert(s_web[0].stop_ticket==7 && s_web[0].stop_wait_sequence==9 && !s_web[0].stop_sequence);
    assert(!s_web[0].armed);
    owner_templates_update();apply_dirty(fake_time+10000);assert(!publishes && !performance_false);
    observed.stop_completed=7;web_confirm_stops(&observed);assert(s_web[0].stop_sequence==9);
}
static void adopt_modes(void)
{
    for(unsigned mode=0;mode<3;mode++) {
        setup_local(true);fake_local_state.applied=fake_local_state.requested=mode;
        assert(owner_local_sync() && s_requested_engine==mode);
        publish_web_state();static web_control_state_t state;web_control_read(&state);
        assert(state.applied_engine==mode && state.requested_engine==mode && !state.mode_switching);
    }
}
static web_v2_event_t remote(void)
{
    return (web_v2_event_t){.client=0,.token=111,.sequence=1,.context_epoch=s_control_epoch,
        .op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=FM_ENGINE_DDS,.expires=fake_time+WEB_LEASE_US};
}
static void remote_handoff(void)
{
    setup_local(true);owner_local_sync();web_v2_event_t e=remote();
    handle_web_v2(&e,fake_time);assert(s_remote_mode.active && fake_remote_requests==1);
    assert(!performance_false && !global_flushes && !fake_fm_panics);
    handle_web_v2(&e,fake_time);assert(fake_remote_requests==1);
    assert(!owner_remote_poll(fake_time) && !performance_false);
    fake_local_state.local_active=fake_local_state.blocked=fake_local_state.remote_pending=false;
    fake_local_state.remote_ready=true;
    assert(owner_remote_poll(fake_time) && s_remote_mode.applying && performance_false);
    assert(!s_web[0].parameter_sequence);
    assert(owner_remote_poll(fake_time) && !s_remote_mode.active);
    assert(s_web[0].parameter_sequence==1 && s_v2_result_code[0]==FM_RESULT_OK);
    setup_local(true);owner_local_sync();e=remote();handle_web_v2(&e,fake_time);
    fake_local_state.generation++;owner_local_sync();assert(!s_remote_mode.active && s_v2_result_code[0]==FM_RESULT_CANCELLED);
    setup_local(true);owner_local_sync();e=remote();handle_web_v2(&e,fake_time);
    owner_remote_poll(e.expires);assert(!s_remote_mode.active && s_v2_result_code[0]==FM_RESULT_CANCELLED);
    setup_local(true);owner_local_sync();e=remote();handle_web_v2(&e,fake_time);web_stop(0,2);
    assert(!s_remote_mode.active && observed.stop_pending && s_web[0].stop_wait_sequence==2);
    setup_local(false);fake_local_state.fault=5;owner_local_sync();e=remote();handle_web_v2(&e,fake_time);
    assert(s_remote_mode.active && fake_remote_requests==1 && !performance_false);
}
static void removed_engine_select_is_rejected(void)
{
    setup_local(true);owner_local_sync();
    fake_local_state.engine_mask=15; // A stale capability bit never revives engine3.
    web_v2_event_t e=remote();e.a=3;
    uint32_t context=s_control_epoch;
    handle_web_v2(&e,fake_time);
    assert(s_v2_result_code[0]==FM_RESULT_BAD_VALUE && !s_remote_mode.active);
    assert(!fake_remote_requests && !performance_false && !global_flushes);
    assert(s_control_epoch==context && !s_web[0].parameter_sequence);
}
int main(void)
{
    cancel_old_context();adopt_modes();remote_handoff();removed_engine_select_is_rejected();
    puts("local owner: old-context cancellation, STOP receipts, three modes, deferred REMT completion/cancellation and removed SELECT3 rejection PASS");
}
