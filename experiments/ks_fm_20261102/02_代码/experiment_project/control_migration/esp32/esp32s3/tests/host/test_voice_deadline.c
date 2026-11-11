#include <assert.h>
#include <stdio.h>
#include "engine_link.h"
extern int64_t fake_time;
static unsigned cut_writes, render_mask;
bool engine_link_send(const engine_cmd_t*c){fake_time+=10;if(c->word==0)render_mask |= 1u<<(c->elem/8);return true;}
bool engine_link_bus_write(uint16_t b,uint32_t v){(void)v;if(b==4)cut_writes++;return true;}
bool engine_link_prod_write(uint8_t e,uint8_t w,uint32_t v){(void)e;(void)w;(void)v;return true;}
bool engine_link_note_on(const engine_note_t*n){(void)n;return true;}
uint32_t engine_link_drops(void){return 0;}
uint32_t engine_link_prod_committed(uint8_t e,uint8_t w){(void)e;(void)w;return 0;}
uint32_t engine_link_release_coefficient(uint8_t e,uint32_t *r){(void)e;*r=0;return 0;}
bool engine_link_bus_committed(uint16_t b,uint32_t v){(void)b;(void)v;return true;}
#include "voice_alloc.c"
extern int64_t fake_time;
int main(void){
    patch_default(&g_patch);s_last_apply=0;fake_time=1000;
    wheel_update(90);apply_dirty(1000);
    assert(cut_writes==1 && "expression must apply at the next 1 ms control deadline");
    for(unsigned v=0;v<32;v++)s_voices[v]=(voice_t){.state=V_HELD,.note=60,.vel=100};
    fake_time=4000;s_dirty=D_RENDER;apply_dirty(4000);
    assert(render_mask!=~0u && s_apply_active && "reconfiguration must split into bounded batches");
    for(unsigned i=0;i<100 && render_mask!=~0u;i++) {
        s_dirty |= D_RENDER;fake_time+=1000;apply_dirty(fake_time);
    }
    assert(render_mask==~0u && "new CC traffic must not starve the last voice");
    wheel_update(100);fake_time+=1000;apply_dirty(fake_time);assert(cut_writes==2);
    puts("voice: expression 1 ms deadline, bounded render, final-voice fairness PASS");return 0;
}
