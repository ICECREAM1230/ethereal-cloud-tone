#include "harp_core.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n",__func__,__LINE__,#x); failures++; return; } } while (0)
static const harp_config_t config={.channel=0,.first_note=60,.velocity_min=16,.velocity_max=127,.encoder_edges=2,
    .laser_active_high=false,.laser_debounce_ms=2,.button_debounce_ms=20,
    .duration_min_ms=20,.duration_max_ms=500,.gate_ms=200};
static harp_target_t blank_target(void) {
    harp_target_t t={.volume=100,.cutoff=127,.bend=8192};
    memset(t.velocity,100,sizeof(t.velocity));return t;
}
static const uint8_t idle_ab[4]={3,3,3,3};
static void sample(harp_model_t *m,uint16_t levels,uint32_t ms) { harp_sample(m,levels,idle_ab,0,0,ms); }
typedef struct { harp_midi_t messages[512]; unsigned count; bool blocked; } receiver_t;
static bool send_message(void *ctx,const harp_midi_t *m,uint32_t epoch) {
    receiver_t *r=ctx;(void)epoch;
    if(r->blocked || r->count==512) return false;
    r->messages[r->count++]=*m;return true;
}
static unsigned count_type(const receiver_t *r,uint8_t type,uint8_t note) {
    unsigned n=0;for(unsigned i=0;i<r->count;i++)
        if((r->messages[i].data[0]&0xf0)==type && r->messages[i].data[1]==note)n++;
    return n;
}
static void defaults_and_limits(void) {
    harp_model_t m;harp_target_t t;
    CHECK(harp_init(&m,&config));harp_target(&m,&t);
    CHECK(m.volume==100 && m.cutoff==127 && m.resonance==0 && t.bend==8192);
    harp_turn(&m,0,500);harp_turn(&m,1,-500);harp_turn(&m,3,500);
    CHECK(m.transpose==24 && m.volume==0 && m.resonance==127);
    harp_config_t bad=config;bad.channel=16;CHECK(!harp_init(&m,&bad));
    bad=config;bad.velocity_min=128;CHECK(!harp_config_valid(&bad));
    bad=config;bad.duration_min_ms=bad.duration_max_ms;CHECK(!harp_config_valid(&bad));
    bad=config;bad.gate_ms=0;CHECK(!harp_config_valid(&bad));
}
static void boot_blocked_and_debounce(void) {
    harp_model_t m;CHECK(harp_init(&m,&config));
    sample(&m,0,0);sample(&m,0,20);CHECK(m.active_beams==0);
    sample(&m,0xfff,21);sample(&m,0xfff,23);
    sample(&m,0xffe,24);sample(&m,0xfff,25);sample(&m,0xffe,26);
    sample(&m,0xffe,27);CHECK(m.active_beams==0);
    sample(&m,0xffe,28);CHECK(m.active_beams==1);
    sample(&m,0xfff,29);sample(&m,0xfff,31);CHECK(m.active_beams==0);
}
static void clock_wrap_and_active_high(void) {
    harp_model_t m;harp_config_t c=config;c.laser_active_high=true;
    CHECK(harp_init(&m,&c));sample(&m,0,UINT32_MAX-3);sample(&m,0,UINT32_MAX-1);
    sample(&m,1,UINT32_MAX);sample(&m,1,1);CHECK(m.active_beams==1);
}
static void quadrature_bounce_direction_and_invalid_edges(void) {
    harp_model_t m;uint8_t a[4]={3,3,3,3};CHECK(harp_init(&m,&config));
    harp_sample(&m,0xfff,a,0,1,0);
    a[0]=2;harp_sample(&m,0xfff,a,0,1,1);
    a[0]=3;harp_sample(&m,0xfff,a,0,1,2);CHECK(m.transpose==0);
    a[0]=2;harp_sample(&m,0xfff,a,0,1,3);
    a[0]=0;harp_sample(&m,0xfff,a,0,1,4);CHECK(m.transpose==1);
    a[0]=2;harp_sample(&m,0xfff,a,0,1,5);
    a[0]=3;harp_sample(&m,0xfff,a,0,1,6);CHECK(m.transpose==0);
    a[0]=0;harp_sample(&m,0xfff,a,0,1,7);
    CHECK(m.illegal_edges==1 && m.transpose==0);
}
static void tuning_buttons_and_panic_rearm(void) {
    harp_model_t m;harp_target_t t;CHECK(harp_init(&m,&config));
    sample(&m,0xfff,0);sample(&m,0xfff,2);sample(&m,0xffe,3);sample(&m,0xffe,5);
    harp_sample(&m,0xffe,idle_ab,1,15,10);harp_sample(&m,0xffe,idle_ab,1,15,30);
    harp_sample(&m,0xffe,idle_ab,1,15,90);harp_target(&m,&t);
    CHECK(m.fine_cents==-1 && t.bend==8151);
    harp_panic(&m);sample(&m,0xffe,100);CHECK(m.active_beams==0);
    sample(&m,0xfff,101);sample(&m,0xfff,103);sample(&m,0xffe,104);sample(&m,0xffe,106);
    CHECK(m.active_beams==1);
}
static void usb_ble_wire_format(void) {
    harp_midi_t m={3,{0x92,60,100}};uint8_t u[4],b[5];
    CHECK(harp_usb_encode(&m,u));CHECK(memcmp(u,(uint8_t[]){9,0x92,60,100},4)==0);
    CHECK(harp_ble_encode(&m,8191,b)==5);
    CHECK(memcmp(b,(uint8_t[]){0xbf,0xff,0x92,60,100},5)==0);
    CHECK(harp_ble_encode(&m,8192,b)==5 && b[0]==0x80 && b[1]==0x80);
    m=(harp_midi_t){2,{0xc0,9,99}};CHECK(harp_usb_encode(&m,u) && u[0]==12 && u[3]==0);
    m.data[1]=128;CHECK(!harp_usb_encode(&m,u) && harp_ble_encode(&m,0,b)==0);
    m=(harp_midi_t){3,{0xf0,1,2}};CHECK(!harp_usb_encode(&m,u));
}
static void sink_retries_release_and_revoices(void) {
    harp_model_t m;harp_target_t t;harp_sink_t s;receiver_t r={0};
    CHECK(harp_init(&m,&config));sample(&m,0xfff,0);sample(&m,0xfff,2);
    sample(&m,0xffe,3);sample(&m,0xffe,5);sample(&m,0xfff,43);sample(&m,0xfff,45);
    harp_target(&m,&t);harp_sink_init(&s);
    harp_sink_pump(&s,&t,true,true,1,32,send_message,&r);
    CHECK(count_type(&r,0x90,60)==1);
    m.transpose=12;harp_target(&m,&t);r.blocked=true;
    harp_sink_pump(&s,&t,true,true,1,32,send_message,&r);
    CHECK(s.notes[60] && !s.notes[72]);r.blocked=false;
    harp_sink_pump(&s,&t,true,true,1,32,send_message,&r);
    CHECK(count_type(&r,0x80,60)==1 && count_type(&r,0x90,72)==1);
    sample(&m,0xfff,245);harp_target(&m,&t);
    r.blocked=true;harp_sink_pump(&s,&t,true,true,1,32,send_message,&r);CHECK(!harp_sink_quiet(&s));
    r.blocked=false;harp_sink_pump(&s,&t,true,true,1,32,send_message,&r);
    CHECK(count_type(&r,0x80,72)==1 && harp_sink_quiet(&s));
}
static void pending_send_preserves_message_until_ack(void) {
    harp_target_t t=blank_target();
    harp_sink_t s;receiver_t r={0};harp_sink_init(&s);
    harp_sink_pump(&s,&t,true,true,10,32,send_message,&r);
    t.notes[60]=true;r.blocked=true;harp_sink_pump(&s,&t,true,true,10,32,send_message,&r);
    CHECK(s.pending);t.notes[60]=false;r.blocked=false;
    harp_sink_pump(&s,&t,true,true,10,32,send_message,&r);
    CHECK(count_type(&r,0x90,60)==1 && count_type(&r,0x80,60)==1 && harp_sink_quiet(&s));
}
static void reconnect_resets_and_inactive_route_releases(void) {
    harp_target_t t=blank_target();
    t.notes[64]=true;harp_sink_t s;receiver_t r={0};harp_sink_init(&s);
    harp_sink_pump(&s,&t,true,true,1,32,send_message,&r);CHECK(s.notes[64]);
    harp_sink_pump(&s,&t,false,true,1,32,send_message,&r);CHECK(!s.notes[64]);
    CHECK(count_type(&r,0x80,64)==1);
    harp_sink_pump(&s,&t,true,false,1,32,send_message,&r);
    r.count=0;harp_sink_pump(&s,&t,true,true,2,32,send_message,&r);
    CHECK(r.count>2 && r.messages[0].data[1]==120 && r.messages[1].data[1]==123);
    CHECK(count_type(&r,0x90,64)==1);
    CHECK(harp_route_mask(HARP_ROUTE_AUTO,true,true)==1);
    CHECK(harp_route_mask(HARP_ROUTE_AUTO,false,true)==2);
    CHECK(harp_route_mask(HARP_ROUTE_BOTH,true,true)==3);
}

static void full_step_reverse_and_twelve_note_chord(void) {
    harp_model_t m;harp_target_t t;harp_config_t c=config;
    c.encoder_edges=4;c.reverse_mask=1;c.first_note=24;
    CHECK(harp_init(&m,&c));
    uint8_t a[4]={3,3,3,3};
    const uint8_t cycle[]={3,2,0,1,3};
    for(unsigned i=0;i<5;i++) { a[0]=cycle[i];harp_sample(&m,0xfff,a,0,1,i); }
    CHECK(m.transpose==-1);
    m.transpose=-24;sample(&m,0,5);sample(&m,0,7);
    sample(&m,0xfff,25);sample(&m,0xfff,27);harp_target(&m,&t);
    for(unsigned i=0;i<128;i++) CHECK(t.notes[i]==(i<12));
    c.first_note=92;CHECK(harp_init(&m,&c));
    sample(&m,0xfff,0);sample(&m,0xfff,2);sample(&m,0,3);sample(&m,0,5);
    sample(&m,0xfff,23);sample(&m,0xfff,25);m.transpose=24;harp_target(&m,&t);
    for(unsigned i=0;i<128;i++) CHECK(t.notes[i]==(i>=116));
    m.fine_cents=100;harp_target(&m,&t);CHECK(t.bend==12288);
    m.fine_cents=-100;harp_target(&m,&t);CHECK(t.bend==4096);
}

typedef struct { bool notes[128],blocked;unsigned duplicates,unmatched_offs,accepted; } synth_t;
static bool synth_accept(void *ctx,const harp_midi_t *m,uint32_t epoch) {
    synth_t *s=ctx;(void)epoch;if(s->blocked) return false;
    uint8_t type=m->data[0]&0xf0,n=m->data[1];
    if(type==0xb0 && (n==120 || n==123)) memset(s->notes,0,sizeof(s->notes));
    else if(type==0x90) { if(s->notes[n]) s->duplicates++;s->notes[n]=true; }
    else if(type==0x80) { if(!s->notes[n]) s->unmatched_offs++;s->notes[n]=false; }
    ++s->accepted;return true;
}

static void backpressure_panic_and_epoch_stress(void) {
    harp_target_t t=blank_target();
    harp_sink_t sink;synth_t synth={0};harp_sink_init(&sink);
    uint32_t rng=0x12345678,epoch=1;
    for(unsigned tick=0;tick<3000;tick++) {
        rng=rng*1664525u+1013904223u;
        for(unsigned i=0;i<12;i++) t.notes[60+i]=(rng&(1u<<i))!=0;
        t.volume=(uint8_t)((rng>>16)&127);
        if(tick%173==0) { ++t.panic_serial;memset(t.notes,0,sizeof(t.notes)); }
        if(tick%311==0) ++epoch;
        synth.blocked=(rng&3u)==0;
        harp_sink_pump(&sink,&t,true,true,epoch,1+(rng%5),synth_accept,&synth);
    }
    synth.blocked=false;memset(t.notes,0,sizeof(t.notes));
    for(unsigned i=0;i<128;i++) harp_sink_pump(&sink,&t,true,true,epoch,8,synth_accept,&synth);
    CHECK(synth.accepted>1000 && !synth.duplicates && !synth.unmatched_offs);
    CHECK(harp_sink_quiet(&sink));
    for(unsigned i=0;i<128;i++) CHECK(!synth.notes[i] && !sink.notes[i]);
}
static void release_duration_velocity_and_independent_gates(void) {
    harp_model_t m;harp_target_t t;harp_config_t c=config;c.gate_ms=1000;
    CHECK(harp_init(&m,&c));sample(&m,0xfff,0);sample(&m,0xfff,2);
    sample(&m,0xffe,10);sample(&m,0xffe,12);harp_target(&m,&t);
    CHECK(m.active_beams==1 && !t.notes[60]);
    sample(&m,0xffc,15);sample(&m,0xffc,17);
    sample(&m,0xffd,30);sample(&m,0xffd,32);harp_target(&m,&t);
    CHECK(t.notes[60] && t.velocity[60]==16 && !t.notes[61]);
    CHECK(m.last_duration_ms[0]==20);
    sample(&m,0xfff,515);sample(&m,0xfff,517);harp_target(&m,&t);
    CHECK(t.notes[60] && t.notes[61] && t.velocity[61]==127);
    CHECK(m.last_duration_ms[1]==500);
    sample(&m,0xfff,1032);harp_target(&m,&t);CHECK(!t.notes[60] && t.notes[61]);
    sample(&m,0xfff,1517);harp_target(&m,&t);CHECK(!t.notes[61]);
}
static void same_pitch_retrigger_and_duration_wrap(void) {
    harp_model_t m;harp_target_t t;harp_sink_t sink;receiver_t r={0};
    CHECK(harp_init(&m,&config));harp_sink_init(&sink);
    sample(&m,0xfff,0);sample(&m,0xfff,2);
    sample(&m,0xffe,10);sample(&m,0xffe,12);sample(&m,0xfff,30);sample(&m,0xfff,32);
    harp_target(&m,&t);harp_sink_pump(&sink,&t,true,true,1,32,send_message,&r);
    CHECK(count_type(&r,0x90,60)==1);
    sample(&m,0xffe,42);sample(&m,0xffe,44);sample(&m,0xfff,142);sample(&m,0xfff,144);
    harp_target(&m,&t);harp_sink_pump(&sink,&t,true,true,1,32,send_message,&r);
    CHECK(count_type(&r,0x80,60)==1 && count_type(&r,0x90,60)==2);
    CHECK(r.messages[r.count-1].data[2]>16);
    CHECK(harp_init(&m,&config));
    sample(&m,0xfff,UINT32_MAX-20);sample(&m,0xfff,UINT32_MAX-18);
    sample(&m,0xffe,UINT32_MAX-10);sample(&m,0xffe,UINT32_MAX-8);
    sample(&m,0xfff,9);sample(&m,0xfff,11);harp_target(&m,&t);
    CHECK(m.last_duration_ms[0]==20 && t.notes[60] && t.velocity[60]==16);
    sample(&m,0xfff,211);harp_target(&m,&t);CHECK(!t.notes[60]);
}
static void velocity_identity_across_transpose_and_pending_ack(void) {
    harp_model_t m;harp_config_t c=config;c.gate_ms=1000;
    harp_target_t t;harp_sink_t sink;receiver_t r={0};
    CHECK(harp_init(&m,&c));harp_sink_init(&sink);
    sample(&m,0xfff,0);sample(&m,0xfff,2);
    sample(&m,0xffe,10);sample(&m,0xffe,12);
    sample(&m,0xffc,15);sample(&m,0xffc,17);
    sample(&m,0xffd,30);sample(&m,0xffd,32);
    sample(&m,0xfff,515);sample(&m,0xfff,517);harp_target(&m,&t);
    CHECK(t.velocity[60]==16 && t.velocity[61]==127);
    harp_sink_pump(&sink,&t,true,true,1,32,send_message,&r);
    m.transpose=1;harp_target(&m,&t);
    harp_sink_pump(&sink,&t,true,true,1,32,send_message,&r);
    CHECK(count_type(&r,0x90,61)==2 && count_type(&r,0x80,61)==1);
    CHECK(r.messages[r.count-2].data[1]==61 && r.messages[r.count-2].data[2]==16);

    harp_sink_init(&sink);r=(receiver_t){0};t=blank_target();
    harp_sink_pump(&sink,&t,true,true,2,32,send_message,&r);
    t.notes[60]=true;t.velocity[60]=16;t.note_serial[60]=1;r.blocked=true;
    harp_sink_pump(&sink,&t,true,true,2,32,send_message,&r);
    t.velocity[60]=127;t.note_serial[60]=2;r.blocked=false;
    harp_sink_pump(&sink,&t,true,true,2,32,send_message,&r);
    CHECK(count_type(&r,0x90,60)==2 && count_type(&r,0x80,60)==1);
    CHECK(sink.note_serial[60]==2 && r.messages[r.count-1].data[2]==127);
}
static void schematic_high_interval_triggers_only_on_restored_light(void) {
    harp_model_t m;harp_target_t t;harp_config_t c=config;c.laser_active_high=true;
    CHECK(harp_init(&m,&c));
    sample(&m,0,100);sample(&m,0,102); /* all beams illuminated: LM393 sinks */
    for(unsigned n=0;n<12;n++) {
        uint32_t start=200+n*1000;uint16_t bit=(uint16_t)(1u<<n);
        sample(&m,bit,start);sample(&m,bit,start+2);harp_target(&m,&t);
        CHECK(!t.notes[60+n]); /* no sound while shaded */
        sample(&m,0,start+100);sample(&m,0,start+102);harp_target(&m,&t);
        CHECK(m.last_duration_ms[n]==100 && t.notes[60+n] && t.velocity[60+n]==35);
        sample(&m,0,start+302);harp_target(&m,&t);CHECK(!t.notes[60+n]);
        sample(&m,0,start+900);harp_target(&m,&t);CHECK(!t.notes[60+n]);
    }
}
int main(void) {
    defaults_and_limits();boot_blocked_and_debounce();clock_wrap_and_active_high();
    quadrature_bounce_direction_and_invalid_edges();tuning_buttons_and_panic_rearm();
    usb_ble_wire_format();sink_retries_release_and_revoices();
    pending_send_preserves_message_until_ack();reconnect_resets_and_inactive_route_releases();
    full_step_reverse_and_twelve_note_chord();backpressure_panic_and_epoch_stress();
    release_duration_velocity_and_independent_gates();same_pitch_retrigger_and_duration_wrap();
    velocity_identity_across_transpose_and_pending_ack();
    schematic_high_interval_triggers_only_on_restored_light();
    printf("%s: 15 behavior groups, %d failures\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
