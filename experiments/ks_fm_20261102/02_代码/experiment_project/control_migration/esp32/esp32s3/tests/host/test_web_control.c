#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine_link.h"
extern int64_t fake_time;
static unsigned notes, test_clears, gate_clears, costs;
bool engine_link_send(const engine_cmd_t*c){(void)c;fake_time+=10;costs++;return true;}
bool engine_link_bus_write(uint16_t b,uint32_t v){if(b==1023&&!v)test_clears++;if(b>=80&&b<112&&!v)gate_clears++;return true;}
bool engine_link_prod_write(uint8_t e,uint8_t w,uint32_t v){(void)e;(void)w;(void)v;return true;}
bool engine_link_note_on(const engine_note_t*n){(void)n;notes++;fake_time+=300;return true;}
uint32_t engine_link_drops(void){return 0;}
uint32_t engine_link_prod_committed(uint8_t e,uint8_t w){(void)e;(void)w;return 0;}
uint32_t engine_link_release_coefficient(uint8_t e,uint32_t*r){(void)e;*r=0;return 1;}
bool engine_link_bus_committed(uint16_t b,uint32_t v){(void)b;(void)v;return true;}
#include "voice_alloc.c"
static web_event_t command(unsigned client, uint32_t token, uint32_t sequence, unsigned op, uint32_t a,unsigned b)
{return (web_event_t){.client=client,.token=token,.sequence=sequence,.op=op,.a=a,.b=b,.expires=fake_time+WEB_LEASE_US};}
static void reset(void){memset(s_voices,0,sizeof(s_voices));memset(s_web,0,sizeof(s_web));patch_default(&g_patch);fake_time=1000;s_dirty=s_apply_active=0;s_cut_coarse=64;s_cut_fine=0;s_cut_off=0;s_reso_off=0;notes=costs=gate_clears=test_clears=0;}
static void apply(web_event_t e){handle_web(&e,fake_time);}
static unsigned held(uint32_t owner){unsigned n=0;for(unsigned i=0;i<32;i++)if(s_voices[i].state==V_HELD&&s_voices[i].web_owner==owner)n++;return n;}
int main(void){
 _Static_assert(sizeof(web_event_t)<=32,"event payload must stay bounded");
 web_event_t e;
 assert(web_control_parse("12 1 2 20 96",&e)&&e.a==20&&e.b==96);
 assert(web_control_parse("12 1 5 1 0",&e)&&e.op==WEB_ENGINE&&e.a==1);
 assert(web_control_parse("12 1 6 8000 0",&e)&&e.op==WEB_KS_DECAY&&e.a==8000);
 const char *bad[]={"0 1 2 20 96","12 0 2 20 96","12 1 2 119 0","12 1 2 20 128","12 1 3 33554432 100","12 1 3 1 0","12 1 4 1 0","12 1 2 20 96x","4294967296 1 2 20 96","12 1 2 20 -1"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!web_control_parse(bad[i],&e));
 const char *bad_ks[]={"12 1 5 2 0","12 1 5 1 1","12 1 6 199 0","12 1 6 8001 0","12 1 6 3000 1"};
 for(unsigned i=0;i<sizeof(bad_ks)/sizeof(bad_ks[0]);i++)assert(!web_control_parse(bad_ks[i],&e));
 reset();publish_web_state();web_control_state_t state;web_control_read(&state);
 assert(state.patch.volume==207&&state.patch.osc[1].coarse==-12&&state.patch.env[0].attack==152);
 assert(!notes&&!s_dirty); // Reading/loading a page cannot mutate the patch.
 handle_cc(106,1);assert(s_cut_off==1); // default coarse preserves 0 offset
 apply(command(0,123,0,WEB_START,0,0));apply(command(1,456,0,WEB_START,0,0));
 apply(command(0,123,1,WEB_KEYS_UPDATE,1u<<12,100));web_advance(fake_time+250);
 apply(command(1,456,1,WEB_KEYS_UPDATE,1u<<12,100));web_advance(fake_time+250);
 note_on(0,60,100);assert(held(123)==1&&held(456)==1&&held(0)==1);
 note_off(60);assert(held(123)==1&&held(456)==1&&!held(0));
 apply(command(0,123,2,WEB_KEYS_UPDATE,0,100));web_advance(fake_time+1000);
 assert(!held(123)&&held(456)==1); // Same-pitch browser/MIDI notes stay distinct.
 apply(command(0,123,1,WEB_KEYS_UPDATE,1,100));assert(!s_web[0].desired_keys); // stale request ignored
 apply(command(1,999,2,WEB_KEYS_UPDATE,1,100));assert(s_web[1].desired_keys==(1u<<12)); // wrong token
 fake_time=s_web[1].expires+1;assert(web_expire(fake_time));assert(!held(456)&&!s_web[1].armed);
 apply(command(1,456,2,WEB_KEYS_UPDATE,1u<<12,100));web_advance(fake_time+250);assert(!held(456));
 apply(command(1,456,3,WEB_KEYS_UPDATE,0,100));apply(command(1,456,4,WEB_KEYS_UPDATE,1u<<12,100));web_advance(fake_time+250);assert(held(456)==1);
 handle_cc(119,127);all_sound_off();assert(test_clears&&gate_clears&&!s_test_tone_requested&&!held(456)&&!s_web[1].desired_keys);
 apply(command(0,123,3,WEB_CC,7,99));publish_web_state();web_control_read(&state);assert(state.patch.volume==198&&state.parameter_sequence[0]==3);
 apply(command(0,123,4,WEB_KEYS_UPDATE,0,100));publish_web_state();web_control_read(&state);assert(state.parameter_sequence[0]==3&&state.sequence[0]==4); // heartbeat cannot obscure parameter receipt
 apply(command(0,123,5,WEB_STOP,0,0));publish_web_state();web_control_read(&state);assert(state.stop_sequence[0]==5);
 reset();apply(command(0,123,0,WEB_START,0,0));apply(command(0,123,1,WEB_KEYS_UPDATE,WEB_KEYS_MASK,100));
 web_advance(fake_time+250);assert(notes==1&&s_web[0].keys!=WEB_KEYS_MASK); // one ordinary MIDI transaction per bounded step
 for(unsigned i=0;i<150&&s_web[0].keys!=WEB_KEYS_MASK;i++){fake_time+=1000;web_advance(fake_time+250);}
 assert(s_web[0].keys==WEB_KEYS_MASK&&notes==25);
 e=command(0,123,2,WEB_KEYS_UPDATE,1,100);e.expires=fake_time-1;apply(e);assert(!held(123)&&!s_web[0].armed);
 reset();apply(command(0,123,0,WEB_START,0,0));apply(command(0,123,1,WEB_KEYS_UPDATE,1,100));web_advance(fake_time+250);
 fake_time=s_web[0].expires+1;apply(command(0,123,2,WEB_KEYS_UPDATE,1,100));assert(!held(123)&&!s_web[0].armed); // Renewal after expiry cannot mask the lost lease.
 apply(command(0,123,3,WEB_KEYS_UPDATE,0,100));apply(command(0,123,4,WEB_KEYS_UPDATE,1,100));web_advance(fake_time+250);handle_cc(119,127);
 event_bus_init();QueueHandle_t q=xQueueCreate(64,sizeof(evt_t));int sub=event_bus_subscribe_reliable(q);
 for(unsigned i=0;i<65;i++){evt_t event={.kind=EVT_WEB,.web=command(0,123,5+i,WEB_KEYS_UPDATE,1,100)};event_bus_publish(&event);}
 evt_t out;assert(event_bus_receive(sub,&out,0)==pdTRUE&&out.kind==EVT_PANIC);all_sound_off();
 assert(test_clears&&!held(123));assert(event_bus_receive(sub,&out,0)==pdTRUE&&out.kind==EVT_WEB);handle_web(&out.web,fake_time);web_advance(fake_time+250);assert(!held(123));
 // Legacy capability refusal leaves current DDS notes and parameters intact.
 reset();apply(command(0,123,0,WEB_START,0,0));note_on(0,60,100);
 apply(command(0,123,1,WEB_ENGINE,1,0));assert(held(0)==1&&!s_web[0].parameter_sequence);
 apply(command(0,123,2,WEB_ENGINE,0,0));assert(!held(0)&&!s_web[0].armed&&!s_web[0].desired_keys);
 apply(command(0,123,3,WEB_KS_DECAY,4000,0));assert(ks_voice_decay_ms()==4000);
 // MIDI All Sound Off must stop the diagnostic latch and queued web chord.
 reset();apply(command(0,123,0,WEB_START,0,0));
 apply(command(0,123,1,WEB_KEYS_UPDATE,WEB_KEYS_MASK,100));web_advance(fake_time+250);
 assert(held(123) && s_web[0].keys!=s_web[0].desired_keys);
 handle_cc(119,127);
 midi_message_t stop={.status=0xB0,.len=2,.data={120,0}};
 handle_midi_source(&stop,MIDI_SOURCE_BLE);
 assert(!s_test_tone_requested && test_clears && !held(123));
 assert(!s_web[0].keys && !s_web[0].desired_keys && !s_web[0].armed);
 unsigned stopped_notes=notes;
 apply(command(0,123,2,WEB_KEYS_UPDATE,WEB_KEYS_MASK,100));
 web_advance(fake_time+1000);assert(notes==stopped_notes); // stale held heartbeat cannot revive
 apply(command(0,123,3,WEB_KEYS_UPDATE,0,100));
 apply(command(0,123,4,WEB_KEYS_UPDATE,1,100));
 web_advance(fake_time+250);assert(held(123)==1); // fresh empty target rearms normally
 puts("web: strict input, real defaults, owner isolation, sequence/lease, panic and MIDI CC120 test latch, bounded 25-key chord PASS");
 return 0;
}
