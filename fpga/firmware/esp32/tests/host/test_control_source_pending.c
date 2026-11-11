#define main previous_raw_owner_entry
#include "test_raw_web_owner.c"
#undef main
extern engine_reverb_state_t fake_reverb_state;
extern unsigned fake_reverb_requests,fake_reverb_cancels,fake_remote_requests;
extern engine_local_state_t fake_local_state;
static void fixture_reverb(void)
{
    if(s_reverb_owner.active)web_control_reverb_release(s_reverb_owner.event.c);
    memset(&s_reverb_owner,0,sizeof(s_reverb_owner));
    memset(&s_reverb_applied,0,sizeof(s_reverb_applied));
    memset(&s_remote_mode,0,sizeof(s_remote_mode));
    memset(&fake_local_state,0,sizeof(fake_local_state));
    s_owner_local_generation=s_owner_local_adopted=0;reset_owner();
    fake_reverb_state=(engine_reverb_state_t){.available=true,.valid=true,
        .wet_q15=1000,.damp_q15=2000,.applied_sequence=8,.generation=4};
    fake_reverb_requests=fake_reverb_cancels=fake_remote_requests=0;
}
static web_v2_event_t reverb_event(uint32_t sequence,uint32_t wet,uint32_t damp)
{
    web_v2_event_t event={.client=0,.token=111,.sequence=sequence,.context_epoch=s_control_epoch,
        .expires=fake_time+WEB_LEASE_US,.a=wet,.b=damp};
    assert(web_control_reverb_reserve(&event,fake_time));return event;
}
static void ack_reverb(const web_v2_event_t *event)
{
    fake_reverb_state.pending=false;fake_reverb_state.completed_request_id=event->c;
    fake_reverb_state.wet_q15=event->a;fake_reverb_state.damp_q15=event->b;
    fake_reverb_state.applied_sequence++;
}

static control_source_ref_t pending_setup(void)
{
    fixture_reverb();control_source_init();
    control_source_ref_t source;
    assert(control_source_claim(CONTROL_SOURCE_WIFI,fake_time,&source));
    s_web[0].source=source;return source;
}
static evt_t pending_reverb(control_source_ref_t source)
{
    evt_t event={.kind=EVT_WEB_REVERB,.source=source,.web_v2=reverb_event(1,123,456)};
    assert(control_source_reserve(source,111,1,fake_time,event.web_v2.expires,CONTROL_SOURCE_OUTPUT));
    assert(owner_control_dispatch(&event,fake_time));
    assert(s_reverb_owner.active);return event;
}
int main(void)
{
    control_source_ref_t source=pending_setup(),ble;
    evt_t reverb=pending_reverb(source);
    evt_t idle={.kind=EVT_WEB_V2,.source=source,.web_v2=keys(0,111,2,0)};
    assert(control_source_reserve(source,111,2,fake_time,idle.web_v2.expires,CONTROL_SOURCE_IDLE));
    owner_control_dispatch(&idle,fake_time);
    assert(!control_source_claim(CONTROL_SOURCE_BLE_CTRL,fake_time,&ble));
    ack_reverb(&reverb.web_v2);assert(owner_reverb_poll(fake_time));
    assert(control_source_claim(CONTROL_SOURCE_BLE_CTRL,fake_time,&ble));

    source=pending_setup();reverb=pending_reverb(source);
    control_source_revoke(source);assert(owner_reverb_poll(fake_time));
    assert(!s_reverb_owner.active && fake_reverb_cancels==1);
    ack_reverb(&reverb.web_v2);owner_reverb_poll(fake_time);
    assert(!s_reverb_applied.token && !global_flushes && !observed.stop_requested);

    source=pending_setup();
    fake_local_state=(engine_local_state_t){.supported=true,.valid=true,.local_active=true,
        .adopted=true,.generation=8,.engine_mask=7};
    owner_local_sync();uint32_t context=s_control_epoch;
    evt_t remote={.kind=EVT_WEB_V2,.source=source,.web_v2={.client=0,.token=111,.sequence=1,
        .context_epoch=context,.op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=FM_ENGINE_DDS,
        .expires=fake_time+WEB_LEASE_US}};
    assert(control_source_reserve(source,111,1,fake_time,remote.web_v2.expires,CONTROL_SOURCE_OUTPUT));
    assert(owner_control_dispatch(&remote,fake_time));
    assert(s_remote_mode.active && fake_remote_requests==1);
    assert(!control_source_claim(CONTROL_SOURCE_BLE_CTRL,fake_time,&ble));
    control_source_revoke(source);assert(owner_remote_poll(fake_time));
    assert(!s_remote_mode.active && s_control_epoch==context);
    assert(!global_flushes && !observed.stop_requested);
    puts("Control source pending: empty heartbeat preserves asynchronous hold; revoked reverb/remote transactions cannot adopt PASS");
    return 0;
}
