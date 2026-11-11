#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ks_voice.h"
#include "ks_params.h"
static engine_ks_state_t state={.available=true,.valid=true,.requested_mode=true,.applied_mode=true};
static unsigned ons[16],offs[16],panics,gains,losses;
static engine_ks_note_t last[16];
void engine_link_ks_state(engine_ks_state_t*out){*out=state;}
bool engine_link_ks_note_on(unsigned v,const engine_ks_note_t*n){assert(v<16);ons[v]++;last[v]=*n;state.pending_mask|=1u<<v;return true;}
void engine_link_ks_note_off(unsigned v){offs[v]++;state.pending_mask|=1u<<v;}
void engine_link_ks_panic(void){panics++;state.active_mask=state.init_mask=state.release_mask=state.pending_mask=0;}
void engine_link_ks_gain(unsigned v,uint16_t l,uint16_t r){(void)v;assert(l>0||r>0);gains++;}
void engine_link_ks_loss(unsigned v,uint16_t loss){(void)v;assert(loss<=32767);losses++;}
#include "ks_voice.c"
static void clean(void){ks_voice_panic();memset(ons,0,sizeof(ons));memset(offs,0,sizeof(offs));}
int main(void)
{
    patch_default(&g_patch);clean();
    engine_ks_note_t invalid;
    for(unsigned n=0;n<19;n++) {
        assert(!ks_params_note(n,127,3000,207,0,1,&invalid));
        assert(!ks_params_loss(n,3000));
    }
    for(unsigned i=0;i<16;i++)ks_voice_on(1,0,0,40+i,100);
    for(unsigned i=0;i<16;i++)assert(ons[i]==1 && s_strings[i].note==40+i);
    unsigned stamp=s_ks_stamp;
    for(unsigned n=0;n<19;n++)ks_voice_on(1,0,0,n,100);
    assert(s_ks_stamp==stamp);
    for(unsigned i=0;i<16;i++)assert(ons[i]==1 && s_strings[i].note==40+i);
    ks_voice_sustain(1,0,true);ks_voice_off(1,0,0,55);
    assert(s_strings[15].state==K_HELD && !s_strings[15].key_down && !offs[15]);
    ks_voice_sustain(1,0,false);assert(offs[15]==1);
    unsigned high_voice=15;state.pending_mask=1u<<high_voice;state.active_mask=1u<<high_voice;ks_voice_poll();
    assert(s_strings[15].state==K_RELEASE);
    state.pending_mask=state.active_mask=0;ks_voice_poll();assert(s_strings[15].state==K_IDLE);
    clean();
    ks_voice_on(1,0,0,60,100);ks_voice_on(2,0,0,60,100);ks_voice_on(0,0,123,60,100);ks_voice_on(0,0,456,60,100);
    assert(s_strings[0].owner!=s_strings[1].owner&&s_strings[2].owner!=s_strings[3].owner);
    ks_voice_off(2,0,0,60);assert(offs[1]==1&&!offs[0]&&!offs[2]&&!offs[3]);
    ks_voice_release_owner(123);assert(offs[2]==1&&!offs[3]);
    // Released but unacknowledged Off cannot be promoted idle from stale zero masks.
    ks_voice_poll();assert(s_strings[1].state==K_RELEASE);
    state.pending_mask=0;ks_voice_poll();assert(s_strings[1].state==K_IDLE);
    clean();ks_voice_sustain(1,0,true);ks_voice_on(1,0,0,60,100);ks_voice_off(1,0,0,60);
    assert(s_strings[0].state==K_HELD&&!s_strings[0].key_down&&!offs[0]);
    ks_voice_sustain(2,0,false);assert(!offs[0]);ks_voice_sustain(1,0,false);assert(offs[0]==1);
    clean();for(unsigned i=0;i<16;i++)ks_voice_on(1,0,0,60+i,100);
    ks_voice_on(1,0,0,70,100);assert(ons[0]==2&&s_strings[0].note==70);ks_voice_off(1,0,0,60);assert(!offs[0]);
    ks_voice_off(1,0,0,62);ks_voice_on(1,0,0,72,100);assert(ons[2]==2&&s_strings[2].note==72);
    ks_voice_expression();assert(gains==16);ks_voice_decay(8000);assert(losses==16&&ks_voice_decay_ms()==8000);
    ks_voice_decay(8001);assert(ks_voice_decay_ms()==8000);
    ks_voice_all_notes_off();for(unsigned v=0;v<16;v++)assert(s_strings[v].state==K_RELEASE);
    ks_voice_panic();for(unsigned v=0;v<16;v++)assert(s_strings[v].state==K_IDLE);
    engine_ks_note_t n;assert(!ks_params_note(60,0,3000,207,0,1,&n));assert(!ks_params_note(60,100,199,207,0,1,&n));
    assert(ks_params_note(69,127,3000,207,0,0,&n)&&n.seed==1&&n.amplitude==8192&&n.length==217);
    uint16_t l,r;ks_params_gains(255,63,&l,&r);assert(l==0&&r==65280);ks_params_gains(0,-64,&l,&r);assert(!l&&!r);
    puts("KS voices: sixteen pool, independent MIDI/web ownership, source sustain, pending release, release-first/oldest-held steal, expression/decay/panic PASS");
    return 0;
}
