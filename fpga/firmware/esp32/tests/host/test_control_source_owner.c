#define main previous_raw_owner_entry
#include "test_raw_web_owner.c"
#undef main

static control_source_ref_t source_claim(control_source_kind_t kind)
{control_source_ref_t source;assert(control_source_claim(kind,fake_time,&source));return source;}
static void source_start(control_source_ref_t source,unsigned client,uint32_t token)
{
    evt_t event={.kind=EVT_WEB,.source=source,.web={.client=client,.token=token,.op=WEB_START}};
    assert(owner_control_dispatch(&event,fake_time));
}
static void source_keys(control_source_ref_t source,unsigned client,uint32_t token,uint32_t sequence,uint32_t mask)
{
    evt_t event={.kind=EVT_WEB_V2,.source=source,.web_v2=keys(client,token,sequence,mask)};
    assert(control_source_reserve(source,token,sequence,fake_time,event.web_v2.expires,
        mask?CONTROL_SOURCE_KEYS:CONTROL_SOURCE_IDLE));
    assert(owner_control_dispatch(&event,fake_time));
}
int main(void)
{
    reset_owner();memset(s_web,0,sizeof(s_web));control_source_init();
    control_source_ref_t wifi=source_claim(CONTROL_SOURCE_WIFI),ble;
    source_start(wifi,0,111);source_start(wifi,1,222);
    observed.source[MIDI_RAW_SOURCE_BLE].session=999;observed.source[0].session=888;
    source_keys(wifi,0,111,1,1);assert(s_web[0].keys==1 && stream_length==3);
    assert(!control_source_claim(CONTROL_SOURCE_BLE_CTRL,fake_time,&ble));
    uint32_t context=s_control_epoch,panic=s_panic_revision;
    control_source_revoke(wifi);assert(owner_control_sync());
    assert(!s_web[0].token && !s_web[1].token && !s_web[0].keys);
    assert(source_flushes[MIDI_RAW_SOURCE_WEB]==1 && !global_flushes && !observed.stop_requested);
    assert(observed.source[MIDI_RAW_SOURCE_BLE].session==999 && observed.source[0].session==888);
    assert(s_control_epoch==context && s_panic_revision==panic);
    ble=source_claim(CONTROL_SOURCE_BLE_CTRL);source_start(ble,0,333);
    source_start(wifi,0,111);assert(s_web[0].token==333);
    uint8_t volume=g_patch.volume;
    evt_t old_parameter={.kind=EVT_WEB_V2,.source=wifi,.web_v2={.client=0,.token=333,
        .sequence=1,.context_epoch=context,.op=FM_HTTP_OPCODE_CC,.a=7,.b=1,.expires=fake_time+WEB_LEASE_US}};
    assert(owner_control_dispatch(&old_parameter,fake_time));assert(g_patch.volume==volume);
    assert(!s_web[0].sequence);

    source_keys(ble,0,333,1,1);source_keys(ble,0,333,2,0);
    wifi=source_claim(CONTROL_SOURCE_WIFI); // zero was actually adopted
    source_start(wifi,0,444);
    evt_t expired={.kind=EVT_WEB_V2,.source=wifi,.web_v2={.client=0,.token=444,
        .sequence=1,.context_epoch=context,.op=FM_HTTP_OPCODE_CC,.a=7,.b=1,.expires=fake_time-1}};
    assert(owner_control_dispatch(&expired,fake_time));assert(g_patch.volume==volume);
    assert(!s_web[0].sequence);
    const uint8_t on[]={0x90,60,100};
    evt_t raw={.kind=EVT_WEB_RAW,.source=wifi,.web_v2=raw_event(0,444,2,on,sizeof(on))};
    assert(control_source_reserve(wifi,444,2,fake_time,raw.web_v2.expires,CONTROL_SOURCE_RAW));
    unsigned before=(unsigned)stream_length;
    control_source_revoke(wifi);ble=source_claim(CONTROL_SOURCE_BLE_CTRL);
    assert(owner_control_dispatch(&raw,fake_time));assert(stream_length==before);
    web_raw_job_t discarded;assert(!web_raw_queue_take(raw.web_v2.a,&discarded));
    assert(!global_flushes && !observed.stop_requested);
    puts("Control source owner: queued START/parameter/RAW fencing, adopted empty takeover and source-local USB/BLE preservation PASS");
    return 0;
}
