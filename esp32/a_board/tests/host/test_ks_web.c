#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine_link.h"
extern int64_t fake_time;
static engine_ks_state_t ks={.available=true,.valid=true,.requested_mode=true,.applied_mode=true};
static unsigned on[ENGINE_KS_VOICES],off[ENGINE_KS_VOICES],panics,dds_notes,gain_updates,owner_acks;
bool engine_link_send(const engine_cmd_t*c){(void)c;return true;}
bool engine_link_bus_write(uint16_t b,uint32_t v){(void)v;if(b>=16&&b<48)gain_updates++;return true;}
bool engine_link_prod_write(uint8_t e,uint8_t w,uint32_t v){(void)e;(void)w;(void)v;return true;}
bool engine_link_note_on(const engine_note_t*n){(void)n;dds_notes++;return true;}
uint32_t engine_link_drops(void){return 0;}
uint32_t engine_link_prod_committed(uint8_t e,uint8_t w){(void)e;(void)w;return 0;}
uint32_t engine_link_release_coefficient(uint8_t e,uint32_t *r){(void)e;*r=0;return 0;}
bool engine_link_bus_committed(uint16_t b,uint32_t v){(void)b;(void)v;return true;}
void engine_link_ks_state(engine_ks_state_t*s){*s=ks;}
bool engine_link_ks_note_on(unsigned v,const engine_ks_note_t*n){assert(v<ENGINE_KS_VOICES&&n->length>=2);if(ks.switching)return false;on[v]++;ks.pending_mask|=1u<<v;return true;}
void engine_link_ks_note_off(unsigned v){off[v]++;ks.pending_mask|=1u<<v;}
void engine_link_ks_panic(void){panics++;ks.pending_mask=ks.active_mask=ks.init_mask=0;}
bool engine_link_ks_mode(bool mode){ks.requested_mode=mode;ks.switching=true;return true;}
void engine_link_ks_gain(unsigned v,uint16_t l,uint16_t r){(void)v;(void)l;(void)r;}
void engine_link_ks_loss(unsigned v,uint16_t loss){(void)v;(void)loss;}
void engine_link_owner_panic_ack(void){owner_acks++;}
#include "voice_alloc.c"
static void midi(uint8_t source,uint8_t status,uint8_t a,uint8_t b)
{midi_message_t m={.status=status,.len=2,.data={a,b}};handle_midi_source(&m,source);}
static void web(uint32_t seq,unsigned op,uint32_t a,unsigned b)
{web_event_t e={.client=0,.token=123,.sequence=seq,.op=op,.a=a,.b=b,.expires=fake_time+WEB_LEASE_US};handle_web(&e,fake_time);}
int main(void)
{
    patch_default(&g_patch);fake_time=1000;
    midi(MIDI_SOURCE_BLE,0x90,60,100);midi(MIDI_SOURCE_DIN,0x90,60,100);
    assert(on[0]==1&&on[1]==1&&!dds_notes);
    midi(MIDI_SOURCE_DIN,0x80,60,0);assert(off[1]==1&&!off[0]);
    midi(MIDI_SOURCE_BLE,0xB0,64,127);midi(MIDI_SOURCE_BLE,0x80,60,0);assert(!off[0]);
    web(0,WEB_START,0,0);web(1,WEB_KEYS_UPDATE,1u<<12,100);web_advance(fake_time+1000);
    assert(on[2]==1);fake_time=s_web[0].expires+1;assert(web_expire(fake_time));assert(off[2]==1&&!off[0]);
    midi(MIDI_SOURCE_BLE,0xB0,64,0);assert(off[0]==1);
    // The transport's existing EVT_PANIC clears a queued bitmap and must
    // acknowledge only after all source/lease ownership has been removed.
    web(2,WEB_KEYS_UPDATE,0,100);web(3,WEB_KEYS_UPDATE,1u<<12,100);web_advance(fake_time+1000);
    ks.recovering=ks.switching=true;ks.requested_mode=false;
    event_bus_init();int sub=event_bus_subscribe_reliable(xQueueCreate(64,sizeof(evt_t)));
    const evt_t panic={.kind=EVT_PANIC};event_bus_publish(&panic);evt_t delivered;
    assert(event_bus_receive(sub,&delivered,0)==pdTRUE&&delivered.kind==EVT_PANIC);
    all_sound_off();assert(owner_acks==1&&!s_web[0].armed&&!s_web[0].keys&&!s_web[0].desired_keys);
    web(4,WEB_KEYS_UPDATE,1u<<12,100);web_advance(fake_time+1000);assert(!s_web[0].keys);
    ks.recovering=ks.switching=false;ks.requested_mode=true;
    patch_t saved=g_patch;int16_t bend=s_bend;
    midi(MIDI_SOURCE_BLE,0xB0,7,90);assert(g_patch.volume==180&&(s_dirty&D_GAIN));
    apply_dirty(fake_time);assert(gain_updates==32&&!(s_dirty&D_GAIN));
    midi(MIDI_SOURCE_BLE,0xE0,127,127);midi(MIDI_SOURCE_BLE,0xB0,20,96);
    assert(s_bend==bend&&g_patch.osc[0].wave==saved.osc[0].wave);
    web(5,WEB_KEYS_UPDATE,0,100);web(6,WEB_KEYS_UPDATE,1u<<12,100);web_advance(fake_time+1000);
    uint32_t panic_before=s_panic_revision;midi(MIDI_SOURCE_BLE,0xB0,120,0);
    assert(s_panic_revision==panic_before+1&&!s_web[0].keys&&!s_web[0].desired_keys&&!s_web[0].armed);
    web(7,WEB_KS_DECAY,200,0);assert(ks_voice_decay_ms()==200);
    // Switching clears every owner and stops queued DDS transactions too.
    s_voices[0]=(voice_t){.state=V_HELD,.note=70};web(8,WEB_ENGINE,0,0);
    assert(!s_web[0].armed&&!s_web[0].keys&&!s_web[0].desired_keys&&s_voices[0].state==V_IDLE&&panics);
    midi(MIDI_SOURCE_BLE,0x90,70,100);assert(!dds_notes);
    ks.applied_mode=false;ks.switching=false;midi(MIDI_SOURCE_BLE,0x90,70,100);assert(dds_notes==1);
    assert(g_patch.osc[0].wave==saved.osc[0].wave&&g_patch.osc[1].coarse==saved.osc[1].coarse);
    publish_web_state();web_control_state_t s;web_control_read(&s);assert(!s.engine.requested_mode&&!s.engine.applied_mode&&s.ks_decay_ms==200);
    puts("KS owner integration: BLE/DIN same-pitch isolation, sustain, web lease, shared gain synchronization, MIDI panic clears web, DDS-only CC/bend isolation, mode clears/blocks/returns DDS PASS");
    return 0;
}
