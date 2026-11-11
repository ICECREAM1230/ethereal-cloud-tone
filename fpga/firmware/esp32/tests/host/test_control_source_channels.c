#define main previous_raw_owner_entry
#include "test_raw_web_owner.c"
#undef main
static engine_cpu_mode_state_t mode_view;
static engine_audio_routes_state_t audio_view;
void engine_link_cpu_mode_state(engine_cpu_mode_state_t *out){*out=mode_view;}
void engine_link_audio_routes_state(engine_audio_routes_state_t *out){*out=audio_view;}
static void begin(control_source_ref_t source,uint32_t token)
{evt_t e={.kind=EVT_WEB,.source=source,.web={.client=0,.token=token,.op=WEB_START}};assert(owner_control_dispatch(&e,fake_time));}
static void key(control_source_ref_t source,uint32_t token,uint32_t seq,uint32_t mask)
{evt_t e={.kind=EVT_WEB_V2,.source=source,.web_v2=keys(0,token,seq,mask)};
 assert(control_source_reserve(source,token,seq,fake_time,e.web_v2.expires,mask?CONTROL_SOURCE_KEYS:CONTROL_SOURCE_IDLE));
 assert(owner_control_dispatch(&e,fake_time));}
int main(void)
{
 reset_owner();control_source_init();observed.cpu_control=true;
 mode_view=(engine_cpu_mode_state_t){.supported=true,.valid=true,.ready=true,.channel_routes=true};
 audio_view=(engine_audio_routes_state_t){.available=true,.valid=true,.ready=true,.pcm_a=1};owner_cpu_mode_sync();
 observed.source[0].session=900;observed.source[2].session=902;observed.source[3].session=903;
 control_source_ref_t wifi,ble;assert(control_source_claim(CONTROL_SOURCE_WIFI,fake_time,&wifi));begin(wifi,111);
 key(wifi,111,1,1);assert(stream_length==3 && stream[0]==0x91);
 uint32_t context=s_control_epoch;unsigned flushes=global_flushes;
 evt_t selection={.kind=EVT_WEB_V2,.source=wifi,.web_v2={.client=0,.token=111,.sequence=2,
  .context_epoch=context,.op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=2,.expires=fake_time+WEB_LEASE_US}};
 assert(control_source_reserve(wifi,111,2,fake_time,selection.web_v2.expires,CONTROL_SOURCE_OUTPUT));
 owner_control_dispatch(&selection,fake_time);assert(s_web[0].midi_channel==2 && s_web[0].keys==1);
 key(wifi,111,3,0);assert(stream[3]==0x81 && !s_web[0].keys);
 assert(s_control_epoch==context && global_flushes==flushes);
 assert(control_source_claim(CONTROL_SOURCE_BLE_CTRL,fake_time,&ble));begin(ble,333);key(ble,333,1,1);
 selection.web_v2.token=333;selection.web_v2.sequence=2;selection.web_v2.a=0;
 owner_control_dispatch(&selection,fake_time);assert(s_web[0].midi_channel==1 && s_web[0].keys==1);
 control_source_revoke(wifi);assert(control_source_valid(ble));assert(!owner_control_sync());
 assert(observed.source[0].session==900 && observed.source[2].session==902 && observed.source[3].session==903);
 assert(!observed.stop_requested && global_flushes==flushes);
 puts("PASS source AE350 channels: KS default, original-channel Off, per-client metadata, old select/cleanup fenced, USB/BLE/sensor retained");
 return 0;
}
