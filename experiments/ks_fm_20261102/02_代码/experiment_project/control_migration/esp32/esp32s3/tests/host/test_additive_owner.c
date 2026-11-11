/* Production owner logic: HTTP identity may finish only after actual ACK. */
#define main existing_raw_owner_main
#include "test_raw_web_owner.c"
#undef main
#include "fake_additive_link.c"
#include "web_additive.h"

static void reset_additive_owner(void)
{
    reset_owner();memset(&s_additive_owner,0,sizeof(s_additive_owner));
    memset(&s_additive_applied,0,sizeof(s_additive_applied));
    s_additive_failed=false;s_additive_instance=0;fake_additive_cancels=0;
    memset(&fake_additive,0,sizeof(fake_additive));
    fake_additive.available=fake_additive.valid=fake_additive.ready=true;
    fake_additive.selected=true;fake_additive.instance_id=7;
    additive_patch_default(&fake_additive.confirmed_patch);
    s_requested_engine=WEB_ENGINE_ADDITIVE;
    web_additive_queue_cancel_all();
}
static void patch_ack(void)
{
    reset_additive_owner();
    web_additive_request_t request={.token=111,.sequence=1,.context_epoch=s_control_epoch};
    additive_patch_default(&request.patch);request.patch.master_gain=12345;
    uint32_t id;assert(web_additive_queue_push(&request,0,fake_time+WEB_LEASE_US,&id));
    web_v2_event_t e={.client=0,.token=111,.sequence=1,.context_epoch=s_control_epoch,
        .a=id,.expires=fake_time+WEB_LEASE_US};
    uint32_t epoch=s_control_epoch;
    handle_web_additive(&e,fake_time);
    assert(s_additive_owner.active && s_control_epoch==epoch);
    assert(!s_web[0].parameter_sequence && !s_v2_result_sequence[0]);
    owner_additive_poll(fake_time);
    assert(fake_additive.busy && s_control_epoch==epoch);
    uint32_t request_id=fake_additive_request_id;
    fake_additive.busy=false;fake_additive.ready=true;fake_additive.revision++;
    fake_additive.completed_request_id=request_id;fake_additive.commit_id=51;
    fake_additive.confirmed_patch=request.patch;
    observed.enabled=observed.ready=false;
    owner_additive_poll(fake_time);
    assert(s_additive_owner.active && s_additive_owner.arming && s_control_epoch==epoch);
    for(unsigned n=0;n<5;n++)owner_additive_poll(fake_time);
    assert(s_additive_owner.active && s_control_epoch==epoch && !s_additive_applied.token);
    unsigned before=performance_true;owner_templates_update();assert(performance_true>before);
    assert(!owner_raw_ready());
    observed.enabled=observed.ready=true;
    owner_additive_poll(fake_time);
    assert(!s_additive_owner.active && s_control_epoch!=epoch);
    assert(s_web[0].parameter_sequence==1 && s_v2_result_sequence[0]==1);
    assert(s_v2_result_code[0]==FM_RESULT_OK && s_v2_result_context[0]==epoch);
    assert(s_additive_applied.token==111 && s_additive_applied.commit_id==51);
}
static void cancelled_ack(void)
{
    reset_additive_owner();
    web_additive_request_t request={.token=111,.sequence=2,.context_epoch=s_control_epoch};
    additive_patch_default(&request.patch);
    uint32_t id;assert(web_additive_queue_push(&request,0,fake_time+WEB_LEASE_US,&id));
    web_v2_event_t e={.client=0,.token=111,.sequence=2,.context_epoch=s_control_epoch,
        .a=id,.expires=fake_time+WEB_LEASE_US};
    handle_web_additive(&e,fake_time);owner_additive_poll(fake_time);
    uint32_t old=fake_additive_request_id;
    all_sound_off();
    fake_additive.busy=false;fake_additive.ready=true;
    fake_additive.completed_request_id=old;fake_additive.commit_id=52;
    owner_additive_poll(fake_time);
    assert(!s_additive_applied.token && s_web[0].parameter_sequence==0);
    assert(!s_additive_owner.active && fake_additive_cancels);
    fake_additive.selected=false;fake_additive.valid=true;fake_additive.fault=ADDITIVE_LINK_CANCELLED;
    assert(owner_additive_blocked());
    unsigned enabled=performance_true;owner_templates_update();assert(performance_true==enabled);
    web_event_t v1={.client=0,.token=111,.sequence=3,.op=WEB_ENGINE,.a=0};
    handle_web(&v1,fake_time);assert(s_requested_engine==WEB_ENGINE_ADDITIVE);
    fake_additive.busy=false;
    web_v2_event_t select={.client=0,.token=111,.sequence=4,.context_epoch=s_control_epoch,
        .op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=FM_ENGINE_DDS,.expires=fake_time+WEB_LEASE_US};
    handle_web_v2(&select,fake_time);assert(!s_additive_failed && s_requested_engine==FM_ENGINE_DDS);
}
static void replaced_mode_stays_fenced(void)
{
    reset_additive_owner();
    fake_additive.selected=false;s_requested_engine=FM_ENGINE_DDS;
    s_additive_owner.active=s_additive_owner.mode=s_additive_owner.routing=true;
    s_additive_owner.event=(web_v2_event_t){.client=0,.token=111,.sequence=1,
        .context_epoch=s_control_epoch,.expires=fake_time+WEB_LEASE_US};
    web_v2_event_t replacement={.client=0,.token=111,.sequence=2,
        .context_epoch=s_control_epoch,.op=FM_HTTP_OPCODE_ENGINE_SELECT,
        .a=WEB_ENGINE_ADDITIVE,.expires=fake_time+WEB_LEASE_US};
    handle_web_v2(&replacement,fake_time);
    assert(!s_additive_owner.active && s_additive_failed);
    fake_additive.busy=false;fake_additive.valid=fake_additive.ready=true;
    fake_additive.selected=false;fake_additive.fault=ADDITIVE_LINK_CANCELLED;
    unsigned enabled=performance_true;owner_templates_update();
    assert(owner_additive_blocked() && performance_true==enabled);
}
int main(void)
{
    patch_ack();cancelled_ack();replaced_mode_stays_fenced();
    reset_additive_owner();publish_web_state();web_control_state_t state;web_control_read(&state);
    assert(state.requested_engine==3 && state.applied_engine==3 && !state.mode_switching);
    puts("additive owner: ACK-only context/result/cache and Panic cancels late ACK PASS");
}
