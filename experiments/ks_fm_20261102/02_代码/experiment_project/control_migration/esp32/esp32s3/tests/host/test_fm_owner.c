#define main legacy_owner_regression
#include "test_web_control.c"
#undef main
#include "fm_voice.h"
extern engine_fm_state_t fake_fm_state;
extern unsigned fake_fm_on[ENGINE_FM_VOICES],fake_fm_off[ENGINE_FM_VOICES],fake_fm_panics;
int main(void)
{
 patch_default(&g_patch);fake_time=1000;
 fake_fm_state.status=(engine_fm_status_t){.available=true,.valid=true,.ready=true,.selected=true,
     .requested_backend=1,.applied_backend=1,.backend_id=1,.voice_count=8,.profile_id=1,
     .flags=FM_FLAG_READY|FM_FLAG_SELECTED,.session_id=42,.engine_epoch=4,.topology_generation=1};
 midi_message_t m={.status=0x90,.data={60,100},.len=2};handle_midi_source(&m,MIDI_SOURCE_BLE);
 assert(fake_fm_on[0]==1 && s_voices[0].state==V_IDLE);
 m.status=0x80;handle_midi_source(&m,MIDI_SOURCE_DIN);assert(!fake_fm_off[0]);
 handle_midi_source(&m,MIDI_SOURCE_BLE);assert(fake_fm_off[0]==1);
 web_event_t old={.client=1,.token=333,.op=WEB_START};handle_web(&old,fake_time);
 old.op=WEB_KEYS_UPDATE;old.sequence=1;old.a=3;old.b=100;old.expires=fake_time+WEB_LEASE_US;
 handle_web(&old,fake_time);assert(!s_web[1].desired_keys);
 web_event_t start={.client=0,.token=123,.op=WEB_START};handle_web(&start,fake_time);
 web_v2_event_t keys={.client=0,.token=123,.sequence=1,.context_epoch=s_control_epoch,.op=3,.a=1,.b=100,.expires=fake_time+WEB_LEASE_US};
 handle_web_v2(&keys,fake_time);web_advance(fake_time+1000);assert(fake_fm_on[1]==1&&s_web[0].keys==1);
 fake_time=s_web[0].expires+1;assert(web_expire(fake_time));assert(fake_fm_off[1]==1);
 uint32_t context=s_control_epoch;keys.sequence=2;keys.op=4;keys.a=keys.b=0;handle_web_v2(&keys,fake_time);
 assert(s_control_epoch!=context && fake_fm_panics);
 s_requested_engine=0;publish_web_state();web_control_state_t state;web_control_read(&state);
 assert(state.requested_engine==2 && state.applied_engine==2);
 keys.sequence=3;keys.context_epoch=s_control_epoch;keys.op=3;keys.a=0;keys.b=100;keys.expires=fake_time+WEB_LEASE_US;
 handle_web_v2(&keys,fake_time);keys.sequence=4;keys.a=2;handle_web_v2(&keys,fake_time);
 fake_fm_state.status.switching=true;web_advance(fake_time+1000);
 assert(!s_web[0].keys && !s_web[0].desired_keys && !s_web[0].armed);
 fake_fm_state.status.switching=false;keys.sequence=5;keys.a=0;handle_web_v2(&keys,fake_time);
 fake_fm_state.status.flags|=FM_FLAG_MUTED;keys.sequence=6;keys.a=2;
 handle_web_v2(&keys,fake_time);
 assert(s_v2_result_code[0]==FM_RESULT_BUSY && !s_web[0].desired_keys);
 puts("FM owner integration: MIDI routing, source Off isolation, HTTP v2 held bitmap, lease release and Panic context cancellation PASS");
}
