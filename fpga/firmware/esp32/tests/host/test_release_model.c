#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "engine_link.h"
static uint32_t committed_rates2, committed_revision;
static bool gate_ack=true;
bool engine_link_send(const engine_cmd_t*c){(void)c;return true;}
bool engine_link_bus_write(uint16_t b,uint32_t v){(void)b;(void)v;return true;}
bool engine_link_prod_write(uint8_t e,uint8_t w,uint32_t v){(void)e;(void)w;(void)v;return true;}
bool engine_link_note_on(const engine_note_t*n){(void)n;return true;}
uint32_t engine_link_drops(void){return 0;}
uint32_t engine_link_prod_committed(uint8_t e,uint8_t w){(void)e;(void)w;return committed_rates2;}
uint32_t engine_link_release_coefficient(uint8_t e,uint32_t *r){(void)e;*r=committed_revision;return (committed_rates2>>4)&0x3FFFF;}
bool engine_link_bus_committed(uint16_t b,uint32_t v){(void)b;(void)v;return gate_ack;}
#include "voice_alloc.c"
extern int64_t fake_time;
int main(void){
    patch_default(&g_patch);
    unsigned rates[]={0,0x28,0x80,0xFE};
    for(unsigned i=0;i<4;i++){
        g_patch.env[0].release=rates[i];
        double k=((patch_adsr_rate2(&g_patch.env[0])>>4)&0x3FFFF)/(double)(1u<<24);
        int64_t tail=release_tail_us();
        double residual=exp((tail*0.096)*log1p(-k));
        assert(residual<=1.0/1024 && "hard mute must wait for the 96 kHz RC threshold");
    }
#ifdef RC_RELEASE
    g_patch.env[0].release=0xFE;committed_rates2=patch_adsr_rate2(&g_patch.env[0]);
    fake_time=0;s_voices[0]=(voice_t){.state=V_HELD,.note=60};gate_ack=false;
    note_off(60);promote_idle(100000);assert(s_voices[0].state==V_RELEASING);
    gate_ack=true;promote_idle(100000);assert(s_voices[0].state==V_RELEASING);
    int64_t fast_end=s_voices[0].release_until;
    promote_idle(101000);
    double before=s_voices[0].release_log;committed_revision+=2;
    promote_idle(101500);assert(s_voices[0].release_log==before);

    // Runtime slow edit: measured committed coefficient, rather than the
    // desired patch byte, determines the remainder of a releasing voice.
    g_patch.env[0].release=0x28;committed_rates2=patch_adsr_rate2(&g_patch.env[0]);committed_revision++;
    promote_idle(102000);assert(s_voices[0].release_until>fast_end+1000000);
    promote_idle(fast_end);assert(s_voices[0].state==V_RELEASING);
    // Speed it up again; integrate elapsed time conservatively at the slower
    // of the old/new committed rate, then retire at the recomputed threshold.
    g_patch.env[0].release=0xFE;committed_rates2=patch_adsr_rate2(&g_patch.env[0]);committed_revision++;
    promote_idle(fast_end+1000);int64_t new_end=s_voices[0].release_until;
    promote_idle(new_end+1);assert(s_voices[0].state==V_IDLE);
    puts("release: 96 kHz RC fast/slow threshold, gate-ack delay, runtime slower/faster edits PASS");
#endif
    return 0;
}
