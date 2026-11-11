/* Production voice owner; the driver ACK is injected independently. */
#define main previous_raw_owner_main
#include "test_raw_web_owner.c"
#undef main
extern engine_reverb_state_t fake_reverb_state;
extern unsigned fake_reverb_requests,fake_reverb_cancels;
extern uint32_t fake_reverb_wet,fake_reverb_damp;
extern engine_local_state_t fake_local_state;
extern unsigned fake_remote_requests;
static uint32_t last_job;
static void fixture_reverb(void)
{
    web_control_reverb_release(last_job);
    memset(&s_reverb_owner,0,sizeof(s_reverb_owner));
    memset(&s_reverb_result,0,sizeof(s_reverb_result));
    memset(&s_reverb_applied,0,sizeof(s_reverb_applied));
    memset(&fake_local_state,0,sizeof(fake_local_state));fake_remote_requests=0;
    reset_owner();
    fake_reverb_state=(engine_reverb_state_t){.available=true,.valid=true,
        .wet_q15=1000,.damp_q15=2000,.applied_sequence=8,.generation=4};
    fake_reverb_requests=fake_reverb_cancels=0;
}
static web_v2_event_t reverb_event(uint32_t sequence,uint32_t wet,uint32_t damp)
{
    web_v2_event_t e={.client=0,.token=111,.sequence=sequence,.context_epoch=s_control_epoch,
        .expires=fake_time+WEB_LEASE_US,.a=wet,.b=damp};
    assert(web_control_reverb_reserve(&e,fake_time));last_job=e.c;return e;
}
static void ack_reverb(const web_v2_event_t *e)
{
    fake_reverb_state.pending=false;fake_reverb_state.completed_request_id=e->c;
    fake_reverb_state.wet_q15=e->a;fake_reverb_state.damp_q15=e->b;
    fake_reverb_state.applied_sequence++;
}
static void immutable_reservation(void)
{
    fixture_reverb();web_v2_event_t e=reverb_event(1,32768,28672),other=e;
    other.c=0;assert(!web_control_reverb_reserve(&other,fake_time));
    other=e;other.a--;assert(!web_control_reverb_matches(&other));
    handle_web_reverb(&other,fake_time);assert(!fake_reverb_requests);
    assert(web_control_reverb_matches(&e));
    other=e;other.c=0;other.sequence++;
    other.expires=e.expires+WEB_LEASE_US;
    assert(web_control_reverb_reserve(&other,e.expires));last_job=other.c;
    assert(other.c!=e.c && !web_control_reverb_matches(&e));
    web_control_reverb_release(e.c);assert(web_control_reverb_matches(&other));
    web_control_reverb_release(other.c);
}
static void ack_only_and_independent_control(void)
{
    fixture_reverb();s_web[0].armed=false; // No Web note-source claim is required.
    fake_local_state=(engine_local_state_t){.supported=true,.valid=true,.local_active=true,
        .adopted=true,.generation=9,.requested=FM_ENGINE_FM,.applied=FM_ENGINE_FM,.engine_mask=7};
    owner_local_sync();
    web_v2_event_t e=reverb_event(1,32768,28672);uint32_t context=s_control_epoch;
    handle_web_reverb(&e,fake_time);
    assert(s_reverb_owner.active && fake_reverb_requests==1);
    assert(fake_reverb_wet==32768 && fake_reverb_damp==28672);
    assert(!s_web[0].parameter_sequence && !s_reverb_applied.token);
    assert(!global_flushes && !performance_false && !fake_remote_requests);
    assert(!owner_reverb_poll(fake_time));
    ack_reverb(&e);assert(owner_reverb_poll(fake_time));
    assert(!s_reverb_owner.active && s_reverb_applied.token==111);
    assert(s_reverb_result.code==FM_RESULT_OK && s_reverb_applied.request_id==e.c);
    assert(s_web[0].parameter_sequence==1 && s_control_epoch==context);
    assert(!web_control_reverb_matches(&e));
    publish_web_state();static web_control_state_t snapshot;web_control_read(&snapshot);
    assert(snapshot.reverb.applied.token==111 && snapshot.reverb.link.wet_q15==32768);
    assert(snapshot.local.local_active && !snapshot.reverb.pending);
}
static void cancellation_and_revalidation(void)
{
    fixture_reverb();web_v2_event_t e=reverb_event(1,123,456);
    handle_web_reverb(&e,fake_time);s_control_epoch++;
    assert(owner_reverb_poll(fake_time));
    assert(!s_reverb_owner.active && fake_reverb_cancels==1);
    assert(s_reverb_result.code==FM_RESULT_CANCELLED);
    ack_reverb(&e);owner_reverb_poll(fake_time);
    assert(!s_reverb_applied.token && !s_web[0].parameter_sequence);
    fixture_reverb();e=reverb_event(1,123,456);handle_web_reverb(&e,fake_time);
    fake_reverb_state.pending=false;fake_reverb_state.completed_request_id=e.c+1;
    assert(owner_reverb_poll(fake_time) && !s_reverb_owner.active && !s_reverb_applied.token);
    assert(s_reverb_result.code==FM_RESULT_CANCELLED);
    fixture_reverb();e=reverb_event(1,123,456);
    handle_web_reverb(&e,e.expires);assert(!fake_reverb_requests);
    assert(s_reverb_result.code==FM_RESULT_CANCELLED && !web_control_reverb_matches(&e));
    fixture_reverb();e=reverb_event(1,123,456);fake_reverb_state.available=false;
    handle_web_reverb(&e,fake_time);assert(!fake_reverb_requests && !s_reverb_owner.active);
    assert(s_reverb_result.code==FM_RESULT_UNSUPPORTED);
    fixture_reverb();e=reverb_event(1,123,456);handle_web_reverb(&e,fake_time);
    owner_reverb_cancel();assert(!s_reverb_owner.active && fake_reverb_cancels==1);
    assert(!global_flushes && !performance_false && !fake_remote_requests);
    ack_reverb(&e);owner_reverb_poll(fake_time);assert(!s_reverb_applied.token);
}
static void stop_local_and_fault_retirement(void)
{
    fixture_reverb();web_v2_event_t e=reverb_event(1,123,456);
    handle_web_reverb(&e,fake_time);web_stop(0,2);
    assert(!s_reverb_owner.active && s_reverb_result.code==FM_RESULT_CANCELLED);
    assert(fake_reverb_cancels==1 && observed.stop_pending && global_flushes==1);
    ack_reverb(&e);owner_reverb_poll(fake_time);assert(!s_reverb_applied.token);

    fixture_reverb();e=reverb_event(1,123,456);handle_web_reverb(&e,fake_time);
    fake_local_state=(engine_local_state_t){.supported=true,.valid=true,.local_active=true,
        .blocked=true,.busy=true,.generation=77,.engine_mask=7};
    owner_local_sync();assert(owner_reverb_poll(fake_time));
    assert(!s_reverb_owner.active && s_reverb_result.code==FM_RESULT_CANCELLED);
    assert(!global_flushes && !performance_false && !fake_remote_requests);

    fixture_reverb();e=reverb_event(1,123,456);handle_web_reverb(&e,fake_time);
    fake_reverb_state.pending=false;fake_reverb_state.completed_request_id=0;
    fake_reverb_state.fault=REVERB_LINK_OK; // Fault can clear before owner observes retirement.
    assert(owner_reverb_poll(fake_time) && s_reverb_result.code==FM_RESULT_CANCELLED);
    ack_reverb(&e);owner_reverb_poll(fake_time);assert(!s_reverb_applied.token);

    fixture_reverb();e=reverb_event(1,123,456);handle_web_reverb(&e,fake_time);
    fake_reverb_state.pending=false;fake_reverb_state.completed_request_id=0;
    fake_reverb_state.fault=REVERB_LINK_PROTOCOL;
    assert(owner_reverb_poll(fake_time) && s_reverb_result.code==FM_RESULT_BACKEND_FAULT);

    fixture_reverb();e=reverb_event(1,123,456);handle_web_reverb(&e,fake_time);
    ack_reverb(&e);fake_reverb_state.damp_q15++;
    assert(owner_reverb_poll(fake_time) && s_reverb_result.code==FM_RESULT_BACKEND_FAULT);
    assert(!s_reverb_applied.token);
}
int main(void)
{
    immutable_reservation();ack_only_and_independent_control();cancellation_and_revalidation();
    stop_local_and_fault_retirement();
    puts("reverb owner: immutable single flight, true ACK identity, local independence and stale/cancel fences PASS");
}
