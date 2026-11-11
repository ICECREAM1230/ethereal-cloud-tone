/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "control_compile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static control_compiler_t c;
static voice_manager_t m[3];
static unsigned checks,groups;
#define CHECK(x) do{checks++;assert(x);}while(0)
static uint32_t bits(const uint32_t *w,unsigned at,unsigned count){
    uint32_t n=0;for(unsigned i=0;i<count;i++)if(w[(at+i)/32]&(1u<<((at+i)%32)))n|=1u<<i;return n;
}
static void scalars(uint32_t w[6]){
    memset(w,0,24);for(unsigned i=0;i<c.count;i++)if(((c.packet[i].word[0]>>16)&255u)==PC_PARAMS){
        unsigned k=c.packet[i].word[0]>>24;w[k*2]=c.packet[i].word[2];w[k*2+1]=c.packet[i].word[3];}
}
static void finish(void){unsigned n=0;while(cf_packet(&c)){CHECK(n++<CF_MAX_PACKETS);cf_ack(&c);}}
static void init(void){cf_init(&c,7);memset(m,0,sizeof(m));for(unsigned e=0;e<3;e++)CHECK(vm_init(&m[e],(uint8_t)e,0)==VM_OK);}
static control_voice_command_t logical(unsigned e,unsigned v,unsigned kind,unsigned gen,unsigned note){
    control_voice_command_t l={0};l.engine=(uint8_t)e;l.voice=(uint8_t)v;l.kind=(uint8_t)kind;l.generation=gen;l.note=(uint8_t)note;l.velocity=100;l.volume=127;l.pan=64;l.pitch=note*65536u;l.aux=e==0&&kind==VM_ON?0xa000:0;return l;
}
static int32_t floor_div(int64_t n,int64_t d){return (int32_t)(n>=0?n/d:-((-n+d-1)/d));}
static void math(void){
    for(unsigned raw=0;raw<16384;raw++)for(unsigned range=0;range<128;range++)
        CHECK(cf_raw_bend((uint16_t)raw,(uint8_t)range)==((int32_t)raw-8192)*(int32_t)range/96);
    for(unsigned n=0;n<128;n++)for(unsigned k=0;k<256;k++){
        unsigned p=(n/12)*1024+((n%12)*1024)/12;
        int x=5632+(((int)p-5120)*(int)k)/64;x=x<0?0:x>16383?16383:x;
        CHECK(cf_note_pitch((uint8_t)n)==p);CHECK(cf_keytrack_cutoff((uint16_t)p,(uint8_t)k)==x);
    }
    for(int b=-131072;b<=131071;b+=19)for(unsigned k=0;k<256;k+=7)
        CHECK(cf_floor64(b*(int)k)==floor_div((int64_t)b*k,64));
    for(unsigned n=0;n<128;n++)for(int delta=-10837;delta<=10836;delta+=31){
        uint32_t q=(n<<16)+(uint32_t)(delta*768);CHECK(cf_q16_bend(q,(uint8_t)n)==delta);
    }
    CHECK(cf_q16_bend(0x80000000u,127)==-98219); /* explicit wrap/trunc golden below */
    CHECK(cf_floor64(INT32_MIN)==-33554432);CHECK(cf_floor64(INT32_MAX)==33554431);
    groups++;
}
static void sub(void){
    init();control_voice_command_t l=logical(0,3,VM_ON,1,60);CHECK(cf_begin(&c,&l));CHECK(c.count==21);
    CHECK(c.packet[0].word[0]==0x00100300u);CHECK(c.packet[0].word[1]==1);
    uint32_t ctx[3]={c.packet[1].word[2],c.packet[1].word[3],c.packet[2].word[2]};
    CHECK(bits(ctx,0,14)==5120);CHECK(bits(ctx,18,19)==0x1d7d7);CHECK(bits(ctx,37,14)==512);CHECK(bits(ctx,75,2)==0);CHECK(bits(ctx,77,1)==1);
    ctx[0]=c.packet[3].word[2];ctx[1]=c.packet[3].word[3];ctx[2]=c.packet[4].word[2];
    CHECK(bits(ctx,0,14)==4096 && bits(ctx,75,2)==3);
    uint32_t w[6];scalars(w);CHECK(bits(w,0,18)==0);CHECK(bits(w,38,14)==5632);CHECK(bits(w,88,8)==64);CHECK(bits(w,52,18)==10240u*(65536u-64u*(65536u-100u*65536u/127u)/127u)>>16);
    CHECK(!c.sub[3].live);finish();CHECK(c.sub[3].live);
    c.sub_active.range=127;c.sub_active.element[6]=32767;c.sub_active.element[7]=(uint32_t)-32768;
    l.kind=VM_SUB_BEND;l.pitch=8191;l.aux=0;CHECK(cf_begin(&c,&l));CHECK(c.count==5);scalars(w);
    CHECK(bits(w,0,18)==0x3ffffu);CHECK(bits(w,18,20)==0xfffffu);finish();
    l.kind=VM_SUB_EXPRESSION;l.pitch=128u|(64u<<8)|(127u<<15);CHECK(cf_begin(&c,&l));scalars(w);
    CHECK(bits(w,96,8)==128);CHECK(bits(w,104,18)==0x3fe00u);CHECK(bits(w,122,18)==32768);finish();
    groups++;
}
static void ks(void){
    init();control_voice_command_t l=logical(1,0,VM_ON,9,60);l.aux=0;CHECK(cf_begin(&c,&l));CHECK(c.count==4);
    uint32_t w[4]={c.packet[1].word[2],c.packet[1].word[3],c.packet[2].word[2],c.packet[2].word[3]};
    CHECK(bits(w,0,15)==c.ks_active.word[120]%32768);CHECK(bits(w,15,16)==(c.ks_active.word[121]&65535u));
    CHECK(bits(w,31,16)==c.ks_active.word[121]>>16);CHECK(bits(w,47,16)==100u*8192/127);
    CHECK(bits(w,63,16)==(c.ks_active.word[320]&65535u));CHECK(bits(w,79,16)==(c.ks_active.word[320]>>16));
    CHECK(bits(w,95,32)==(9u^0x9e3779b9u^(60u<<16)));finish();
    l.kind=VM_OFF;CHECK(cf_begin(&c,&l));CHECK(c.packet[0].word[0]>>24==2);finish();CHECK(c.ks[0].released);
    groups++;
}
static void fm(void){
    init();CHECK(cf_fm_pitch_codes(60u<<16)==(62u<<6));CHECK(cf_fm_pitch_codes(69u<<16)==(74u<<6));
    control_voice_command_t l=logical(2,2,VM_ON,4,60);l.aux=0;CHECK(cf_begin(&c,&l));CHECK(c.count==10);
    CHECK(c.packet[1].word[2]==0xc7220208u); /* keyoff2 + algorithm7/panC0 */
    CHECK(c.packet[1].word[3]==0x00323e2au); /* KC62 at2A; KF0 at32 */
    CHECK((c.packet[2].word[3]&65535u)==0x0b62u); /* carrier TL8+velocity3 */
    CHECK((c.packet[8].word[2]>>16)==0x0a08u);finish();
    l.kind=VM_GAIN;l.volume=0;CHECK(cf_begin(&c,&l));CHECK(c.count==4);CHECK((c.packet[1].word[2]&65535u)==0x0208u);finish();
    groups++;
}
static void boot_templates(void){
    init();CHECK(cf_background(&c,m));CHECK(c.count==1);CHECK((c.packet[0].word[0]>>16)==PC_GLOBAL);finish();
    CHECK(cf_background(&c,m));CHECK(c.count==42);CHECK(c.packet[0].word[0]==0x00150002u);CHECK(c.packet[1].word[0]>>24==25);finish();
    CHECK(!cf_background(&c,m));CHECK(c.boot_done);
    control_voice_command_t l=logical(0,0,VM_ON,1,72);CHECK(cf_begin(&c,&l));finish();m[0].voices[0].state=VM_HELD;
    CHECK(cf_template_record(&c,0x8040,128)==CF_OK);CHECK(cf_template_record(&c,0x8041,12)==CF_OK);CHECK(cf_template_record(&c,0x80ff,81)==CF_COMMIT_PENDING);
    CHECK(!c.ack_dirty);CHECK(cf_background(&c,m));finish();CHECK(cf_background(&c,m));CHECK(c.count==21);finish();CHECK(!cf_background(&c,m));
    CHECK(c.ack[0]==81 && c.sub[0].keytrack==128 && c.sub[0].bend==0);
    CHECK(cf_template_record(&c,0xb3fe,99)==CF_COMMIT_PENDING);CHECK(!cf_background(&c,m));CHECK(c.ack[PC_ACK_KST_COMPLETED]==99 && c.ack[PC_ACK_KST_RESULT]==2 && !c.ack[PC_ACK_KST]);
    groups++;
}
static void every_slot_and_invalid(void){
    init();
    for(unsigned e=0;e<3;e++)for(unsigned v=0;v<vm_engine_caps((uint8_t)e)->voices;v++){
        control_voice_command_t l=logical(e,v,VM_ON,v+1,60+v%24);
        CHECK(cf_begin(&c,&l));CHECK((c.packet[0].word[0]&65535u)==(e|(v<<8)));finish();
    }
    control_voice_command_t l=logical(0,0,VM_ON,100,128);CHECK(!cf_begin(&c,&l));
    l=logical(0,0,VM_ON,100,60);l.velocity=128;CHECK(!cf_begin(&c,&l));
    l=logical(0,0,VM_ON,100,60);l.source=3;CHECK(!cf_begin(&c,&l));
    l=logical(0,0,VM_SUB_BEND,100,60);l.pitch=16384;CHECK(!cf_begin(&c,&l));
    c.revision[0]=UINT32_MAX;l=logical(0,0,VM_ON,100,60);CHECK(!cf_begin(&c,&l)&&c.fault);
    groups++;
}
int main(void){every_slot_and_invalid();math();sub();ks();fm();boot_templates();printf("PASS control_compile: %u groups / %u checks; compiler=%zu bytes\n",groups,checks,sizeof(c));return 0;}
