#include "harp_core.h"
#include <stdio.h>
#include <string.h>

static unsigned failures;
#define CHECK(x) do { if(!(x)) { printf("FAIL %s:%d: %s\n",__func__,__LINE__,#x);++failures;return; } } while(0)
static const harp_config_t cfg={.channel=0,.first_note=60,.velocity_min=16,.velocity_max=127,
    .encoder_edges=2,.laser_active_high=true,.laser_debounce_ms=2,.button_debounce_ms=20,
    .duration_min_ms=20,.duration_max_ms=500,.gate_ms=200};
static void mapping_and_gain(void)
{
    harp_model_t m;harp_target_t t;CHECK(harp_init(&m,&cfg));
    for(unsigned i=0;i<12;i++) CHECK(harp_effective_note(&m,i)==60+(int)i && m.velocity_gain[i]==100);
    uint8_t notes[12]={60,62,64,65,67,69,71,72,74,76,77,79};
    uint16_t gains[12]={100,200,25,100,100,100,100,100,100,100,100,100};
    CHECK(harp_set_mapping(&m,notes,gains));
    CHECK(harp_debug_strike(&m,1,100,1000));harp_target(&m,&t);
    CHECK(t.notes[62] && t.velocity[62]==70 && m.hit_count[1]==1 && m.last_synthetic[1]);
    CHECK(harp_debug_strike(&m,2,20,1001));harp_target(&m,&t);
    CHECK(t.notes[64] && t.velocity[64]==4);
    CHECK(harp_debug_strike(&m,1,500,1002));harp_target(&m,&t);CHECK(t.velocity[62]==127);
    uint8_t duplicate[12];memcpy(duplicate,notes,sizeof(notes));duplicate[1]=duplicate[0];
    CHECK(!harp_set_mapping(&m,duplicate,gains) && m.note_map[1]==62);
    gains[0]=0;CHECK(!harp_set_mapping(&m,notes,gains) && m.velocity_gain[0]==100);
}
static void reverse_curve_and_muted_transpose(void)
{
    harp_config_t c=cfg;c.velocity_min=127;c.velocity_max=16;
    harp_model_t m;harp_target_t t;CHECK(harp_init(&m,&c));
    CHECK(harp_debug_strike(&m,0,20,0) && m.velocity[0]==127);
    CHECK(harp_debug_strike(&m,0,260,1) && m.velocity[0]==72);
    CHECK(harp_debug_strike(&m,0,500,2) && m.velocity[0]==16);
    uint8_t notes[12]={0,61,62,63,64,65,66,67,68,69,70,127};uint16_t gains[12];
    for(unsigned i=0;i<12;i++) gains[i]=100;
    CHECK(harp_set_mapping(&m,notes,gains));m.transpose=-24;
    CHECK(harp_effective_note(&m,0)==-1);harp_target(&m,&t);
    for(unsigned i=0;i<128;i++) CHECK(!t.notes[i]);
    m.transpose=24;CHECK(harp_debug_strike(&m,11,100,5));harp_target(&m,&t);
    CHECK(harp_effective_note(&m,11)==-1 && !t.notes[127]);
}
static void injection_preserves_real_measurement(void)
{
    harp_model_t m;CHECK(harp_init(&m,&cfg));const uint8_t ab[4]={3,3,3,3};
    harp_sample(&m,0,ab,0,0,0);harp_sample(&m,0,ab,0,0,2);
    harp_sample(&m,1,ab,0,0,10);harp_sample(&m,1,ab,0,0,12);
    CHECK(harp_debug_strike(&m,0,500,50));
    CHECK(m.shade_started[0]==10 && m.active_beams==1 && m.last_synthetic[0]);
    harp_sample(&m,0,ab,0,0,110);harp_sample(&m,0,ab,0,0,112);
    CHECK(m.last_duration_ms[0]==100 && m.velocity[0]==35 && m.hit_count[0]==2 && !m.last_synthetic[0]);
    CHECK(!harp_debug_strike(&m,12,100,200) && !harp_debug_strike(&m,0,0,200));
}
int main(void)
{
    mapping_and_gain();reverse_curve_and_muted_transpose();injection_preserves_real_measurement();
    printf("%s: mapping, gain, reverse curve, range muting and synthetic trace; failures=%u\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
