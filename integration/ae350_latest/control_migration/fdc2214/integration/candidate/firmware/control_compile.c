/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native arithmetic/layout provenance: frozen ae350_voice_backend_sub/ks,
 * jt51_audio_backend, jt51_adapter_pkg and fm_contract_pkg (preset zero).
 * No ESP-IDF, libm, heap, transport assumption or synchronous wait. */
#include "control_compile.h"
#include "control_data/control_boot_tables.h"

static void zero(void *p,unsigned n){volatile uint8_t *b=p;while(n--)*b++=0;}
static void copy(void *to,const void *from,unsigned n){volatile uint8_t *d=to;const uint8_t *s=from;while(n--)*d++=*s++;}
static uint16_t clamp14(int32_t x){return x<0?0:x>16383?16383:(uint16_t)x;}
static int32_t signed_bits(uint32_t x,unsigned n){uint32_t mask=(UINT32_C(1)<<n)-1u;x&=mask;return (x&(UINT32_C(1)<<(n-1u)))?(int32_t)x-(int32_t)(UINT32_C(1)<<n):(int32_t)x;}
int32_t cf_floor64(int32_t x){return x>=0?x/64:-(int32_t)(((0u-(uint32_t)x)+63u)/64u);}
int32_t cf_raw_bend(uint16_t raw,uint8_t range){return (((int32_t)raw-8192)*(int32_t)range)/96;}
int32_t cf_q16_bend(uint32_t q,uint8_t note){
    uint32_t u=q-((uint32_t)note<<16);
    uint32_t magnitude=(u&UINT32_C(0x80000000))?0u-u:u;
    int32_t quotient=(int32_t)(magnitude/768u);
    if(u&UINT32_C(0x80000000))quotient=-quotient;
    return signed_bits((uint32_t)quotient,18);
}
uint16_t cf_note_pitch(uint8_t n){return (uint16_t)(((uint32_t)n*1024u)/12u);}
uint16_t cf_keytrack_cutoff(uint16_t p,uint8_t k){return clamp14(5632+(((int32_t)p-5120)*(int32_t)k)/64);}
uint16_t cf_fm_pitch_codes(uint32_t p){
    static const uint8_t code[12]={0,1,2,4,5,6,8,9,10,12,13,14};
    uint32_t grid=(p-UINT32_C(851968)+512u)>>10,semi=grid>>6;
    return (uint16_t)((((semi/12u)&7u)<<10)|((uint32_t)code[semi%12u]<<6)|(grid&63u));
}
static void put(uint32_t *w,unsigned at,unsigned bits,uint32_t value){
    for(unsigned i=0;i<bits;i++){
        uint32_t mask=UINT32_C(1)<<((at+i)&31u);
        if(value&(UINT32_C(1)<<i))w[(at+i)>>5]|=mask;else w[(at+i)>>5]&=~mask;
    }
}
static prepared_packet_t *add(control_compiler_t *c,unsigned e,unsigned v,unsigned op,unsigned flags,uint32_t gen){
    if(c->count>=CF_MAX_PACKETS)return 0;
    prepared_packet_t *p=&c->packet[c->count++];zero(p,sizeof(*p));
    p->word[0]=(uint32_t)e|((uint32_t)v<<8)|((uint32_t)op<<16)|((uint32_t)flags<<24);p->word[4]=gen;return p;
}
static int new_revision(control_compiler_t *c,unsigned e,uint32_t *rev){
    if(c->revision[e]==UINT32_MAX){c->fault=1;return 0;}
    *rev=++c->revision[e];return 1;
}
static int start(control_compiler_t *c,unsigned e,unsigned v,unsigned flags,uint32_t gen,uint32_t *rev){
    if(!new_revision(c,e,rev))return 0;
    prepared_packet_t *p=add(c,e,v,PC_BEGIN,flags,gen);if(!p)return 0;p->word[1]=*rev;return 1;
}
static void chunk(control_compiler_t *c,unsigned e,unsigned v,unsigned op,unsigned flags,uint32_t gen,uint32_t rev,uint32_t lo,uint32_t hi){
    prepared_packet_t *p=add(c,e,v,op,flags,gen);p->word[1]=rev;p->word[2]=lo;p->word[3]=hi;
}
static void commit(control_compiler_t *c,unsigned e,unsigned v,uint32_t gen,uint32_t rev){chunk(c,e,v,PC_COMMIT,0,gen,rev,0,0);}
static uint8_t pan_bits(unsigned p){return p<32?0x40:p<96?0xc0:0x80;}
static unsigned carrier(unsigned algorithm,unsigned op){static const uint8_t mask[8]={8,8,8,8,10,14,14,15};return (mask[algorithm]& (1u<<op))!=0;}
static uint8_t tl(unsigned base,unsigned vel,unsigned vol,unsigned alg,unsigned op){
    unsigned sum=base+cf_fm_attenuation[vel]+cf_fm_attenuation[vol];
    if(!vel || !vol)return 127;
    return carrier(alg,op)?(uint8_t)(sum>127?127:sum):(uint8_t)base;
}
static void default_fm(cf_fm_voice_t *v){
    zero(v,sizeof(*v));v->algorithm=7;v->keys=1;
    for(unsigned o=0;o<4;o++){v->op[o][0]=1;v->op[o][1]=(uint8_t)(o?127:8);v->op[o][2]=31;v->op[o][5]=15;}
}
static unsigned fm_pairs(const control_compiler_t *c,const cf_fm_voice_t *v,unsigned voice,unsigned kind,uint16_t pairs[30],int boot){
    static const uint8_t slot[4]={0,2,1,3};unsigned n=0;
    const control_voice_command_t *l=&v->logical;
    unsigned vol=boot?127u:((unsigned)c->fm_volume*l->volume)/127u;
    int pan=boot?64:(int)c->fm_pan+(int)l->pan-64;
    if(pan<0)pan=0;
    if(pan>127)pan=127;
    uint16_t pitch=cf_fm_pitch_codes(l->pitch);
#define PAIR(a,d) do{pairs[n++]=(uint16_t)((unsigned)(a)|((unsigned)(d)<<8));}while(0)
    if(kind==VM_ON){
        PAIR(8,voice);PAIR(0x20u+voice,pan_bits((unsigned)pan)|(v->feedback<<3)|v->algorithm);
        PAIR(0x28u+voice,pitch>>6);PAIR(0x30u+voice,(pitch&63u)<<2);PAIR(0x38u+voice,0);
        for(unsigned o=0;o<4;o++)for(unsigned g=0;g<6;g++){
            unsigned data=v->op[o][g];if(g==1)data=tl(data,boot?127u:l->velocity,vol,v->algorithm,o);
            PAIR(0x40u+g*32u+slot[o]*8u+voice,data);
        }
        PAIR(8,voice|((boot||!vol)?0u:(unsigned)v->keys<<3));
    }else if(kind==VM_PITCH){PAIR(0x28u+voice,pitch>>6);PAIR(0x30u+voice,(pitch&63u)<<2);}
    else{
        if(!vol)PAIR(8,voice);
        PAIR(0x20u+voice,pan_bits((unsigned)pan)|(v->feedback<<3)|v->algorithm);
        for(unsigned o=0;o<4;o++)PAIR(0x60u+slot[o]*8u+voice,tl(v->op[o][1],l->velocity,vol,v->algorithm,o));
    }
#undef PAIR
    return n;
}
static void fm_data(control_compiler_t *c,unsigned voice,uint32_t gen,uint32_t rev,const uint16_t *pairs,unsigned n){
    for(unsigned i=0;i<n;i+=4){unsigned k=n-i;if(k>4)k=4;prepared_packet_t *p=add(c,2,voice,PC_DATA,k,gen);p->word[1]=rev;
        for(unsigned j=0;j<k;j++)p->word[2+j/2]|=(uint32_t)pairs[i+j]<<((j&1u)*16u);
    }
}
static int sub_job(control_compiler_t *c,const control_voice_command_t *l,unsigned flags,int refresh){
    unsigned v=l->voice;uint32_t rev;
    copy(&c->next_sub,&c->sub[v],sizeof(c->next_sub));cf_sub_voice_t *s=&c->next_sub;
    if(l->kind==VM_ON && !refresh){
        zero(s,sizeof(*s));copy(&s->logical,l,sizeof(*l));s->live=1;
#if ENABLE_AE350_FDC
        if (l->source==3u) {
            s->bend=(int32_t)(l->pitch&16383u)-(int32_t)cf_note_pitch(69);
            s->sensor_gain=l->pitch>>14;
        } else
#endif
        s->bend=(l->aux&0x8000u)?cf_raw_bend(l->aux&0x3fffu,c->sub_active.range):cf_q16_bend(l->pitch,l->note);
    }else if(!refresh){
        if(!s->live || s->logical.generation!=l->generation)return 0;
#if ENABLE_AE350_FDC
        if (l->kind==VM_SENSOR) {
            s->bend=(int32_t)(l->pitch&16383u)-(int32_t)cf_note_pitch(69);
            s->sensor_gain=l->pitch>>14;s->logical.pitch=l->pitch;
        } else
#endif
        if(l->kind==VM_SUB_BEND)s->bend=cf_raw_bend((uint16_t)l->pitch,c->sub_active.range);
        else if(l->kind==VM_PITCH)s->bend=cf_q16_bend(l->pitch,s->logical.note);
        else if(l->kind==VM_SUB_EXPRESSION){s->wheel=(uint8_t)l->pitch;s->x=(uint8_t)((l->pitch>>8)&127u);s->y=(uint8_t)((l->pitch>>15)&127u);}
    }
    uint16_t pitch=cf_note_pitch(s->logical.note);
    if(flags!=PC_SUB_SCALARS){
        s->keytrack=c->sub_active.keytrack;s->cutoff=cf_keytrack_cutoff(pitch,s->keytrack);
        s->amp=c->sub_active.amp[s->logical.velocity]&0x3ffffu;s->mod=c->sub_active.mod[s->logical.velocity]&0x3ffffu;
    }
    unsigned begin_flags=flags;
#if ENABLE_AE350_FDC
    /* Only a fresh sensor job can renew hardware liveness. A background
     * template HOT/SCALARS refresh must never impersonate a sensor heartbeat. */
    if(s->logical.source==3u && !refresh && (l->kind==VM_ON || l->kind==VM_SENSOR)) begin_flags|=4u;
#endif
    if(!start(c,0,v,begin_flags,l->generation,&rev))return 0;
    if(flags!=PC_SUB_SCALARS)for(unsigned e=0;e<8;e++){
        const uint32_t *t=&c->sub_active.element[e*8u];uint32_t w[3];zero(w,sizeof(w));
        put(w,0,14,clamp14((int32_t)pitch+signed_bits(t[0],18)));
        put(w,18,19,t[3]&0x7ffffu);put(w,37,14,t[2]&0x3fffu);put(w,51,24,t[1]&0xffffffu);
        put(w,75,2,t[4]&3u);put(w,77,1,(t[4]>>8)&1u);
        chunk(c,0,v,PC_DATA,e,l->generation,rev,w[0],w[1]);chunk(c,0,v,PC_DATA,e|8u,l->generation,rev,w[2],0);
    }
    uint32_t w[6];zero(w,sizeof(w));
    put(w,0,18,(uint32_t)s->bend);put(w,18,20,(uint32_t)cf_floor64(s->bend*s->keytrack));
    put(w,38,14,s->cutoff);put(w,52,18,s->amp);put(w,70,18,s->mod);put(w,88,8,s->keytrack);put(w,96,8,s->wheel);
    put(w,104,18,(uint32_t)cf_floor64(signed_bits(s->y,7)*signed_bits(c->sub_active.element[6],16)));
    put(w,122,18,(uint32_t)cf_floor64(signed_bits(s->x,7)*signed_bits(c->sub_active.element[7],16)));
#if ENABLE_AE350_FDC
    if(s->logical.source==3u){put(w,140,17,s->sensor_gain);put(w,157,1,1);}
#endif
    for(unsigned i=0;i<3;i++)chunk(c,0,v,PC_PARAMS,i,l->generation,rev,w[i*2],w[i*2+1]);
    commit(c,0,v,l->generation,rev);c->effect_engine=0;c->effect_voice=(uint8_t)v;c->effect_valid=1;return 1;
}
static int ks_job(control_compiler_t *c,const control_voice_command_t *l){
    unsigned v=l->voice;uint32_t rev;copy(&c->next_ks,&c->ks[v],sizeof(c->next_ks));
    cf_ks_voice_t *s=&c->next_ks;
    if(l->kind==VM_ON){copy(&s->logical,l,sizeof(*l));s->live=1;s->released=0;}
    else if(!s->live || s->logical.generation!=l->generation)return 0;
    else if(l->kind==VM_GAIN){s->logical.volume=l->volume;s->logical.pan=l->pan;}
    if(l->kind==VM_OFF || l->kind==VM_KILL)s->released=1;
    const control_voice_command_t *a=&s->logical;
    uint32_t w0=c->ks_active.word[(unsigned)a->note*2],w1=c->ks_active.word[(unsigned)a->note*2+1];
    if(l->kind==VM_ON && (!(w0&0x80000000u)||(w0&32767u)<2||(w0&32767u)>4096||(w1>>16)>32767||(w1&65535u)==32768u))return 0;
    unsigned op=l->kind==VM_ON?1:l->kind==VM_OFF?2:l->kind==VM_KILL?7:l->kind==VM_KS_REFRESH_LOSS?6:5;
    if(!start(c,1,v,op,l->generation,&rev))return 0;
    uint32_t gains=c->ks_active.word[256u+a->pan],w[4];zero(w,sizeof(w));
    uint32_t seed=a->generation^UINT32_C(0x9e3779b9)^((uint32_t)a->note<<16);if(!seed)seed=1;
    put(w,0,15,w0&32767u);put(w,15,16,w1&65535u);put(w,31,16,w1>>16);
    put(w,47,16,(uint32_t)a->velocity*8192u/127u);
    put(w,63,16,(gains&65535u)*a->volume/127u);put(w,79,16,(gains>>16)*a->volume/127u);put(w,95,32,seed);
    for(unsigned i=0;i<2;i++)chunk(c,1,v,PC_DATA,i,l->generation,rev,w[i*2],w[i*2+1]);
    commit(c,1,v,l->generation,rev);c->effect_engine=1;c->effect_voice=(uint8_t)v;c->effect_valid=1;return 1;
}
static int fm_job(control_compiler_t *c,const control_voice_command_t *l){
    unsigned v=l->voice;uint32_t rev;copy(&c->next_fm,&c->fm[v],sizeof(c->next_fm));cf_fm_voice_t *s=&c->next_fm;
    if(l->kind==VM_ON){default_fm(s);copy(&s->logical,l,sizeof(*l));s->live=1;}
    else if(!s->live || s->logical.generation!=l->generation)return 0;
    else if(l->kind==VM_GAIN){s->logical.volume=l->volume;s->logical.pan=l->pan;}
    else if(l->kind==VM_PITCH)s->logical.pitch=l->pitch;
    if(s->logical.pitch<UINT32_C(851968)||s->logical.pitch>UINT32_C(7142400))return 0;
    if(!start(c,2,v,l->kind==VM_ON?PC_FM_NEW:PC_FM_HOT,l->generation,&rev))return 0;
    uint16_t pairs[30];unsigned n=fm_pairs(c,s,v,l->kind,pairs,0);
    fm_data(c,v,l->generation,rev,pairs,n);commit(c,2,v,l->generation,rev);
    c->effect_engine=2;c->effect_voice=(uint8_t)v;c->effect_valid=1;return 1;
}
void cf_init(control_compiler_t *c,uint8_t engines){
    zero(c,sizeof(*c));c->engines=engines;c->fm_volume=c->fm_stage_volume=127;c->fm_pan=c->fm_stage_pan=64;
    c->sub_stage.keytrack=64;c->sub_stage.range=2;
    c->sub_stage.element[2]=c->sub_stage.element[10]=0x200;
    c->sub_stage.element[3]=c->sub_stage.element[11]=0x1d7d7;
    c->sub_stage.element[4]=0x100;c->sub_stage.element[8]=UINT32_C(0xfffffc00);c->sub_stage.element[12]=0x103;
    for(unsigned v=0;v<128;v++)c->sub_stage.amp[v]=(10240u*(65536u-64u*(65536u-v*65536u/127u)/127u))>>16;
    copy(&c->sub_active,&c->sub_stage,sizeof(c->sub_active));
    for(unsigned i=0;i<384;i++)c->ks_stage.word[i]=c->ks_active.word[i]=cf_ks_boot_words[i];
}
const prepared_packet_t *cf_packet(const control_compiler_t *c){return c->index<c->count?&c->packet[c->index]:0;}
int cf_begin(control_compiler_t *c,const control_voice_command_t *l){
    if(cf_packet(c)||!l||l->engine>2||l->voice>=vm_engine_caps(l->engine)->voices||!l->generation)return 0;
#if ENABLE_AE350_FDC
    unsigned sensor=l->engine==0 && l->source==3u;
    if (sensor) {
        if (!(l->kind==VM_ON || l->kind==VM_OFF || l->kind==VM_KILL || l->kind==VM_SENSOR) ||
            l->channel!=0 || l->note!=69 || l->velocity!=127 ||
            (l->volume!=127 && !(l->kind==VM_KILL && l->volume==0)) ||
            l->pan!=64 || l->aux || (l->pitch>>14)>65536u) return 0;
    }
    if((l->source>=3 && !sensor) || l->channel>=16 || l->note>=128 || l->velocity>=128 || l->volume>=128 || l->pan>=128)return 0;
#else
    if(l->source>=3 || l->channel>=16 || l->note>=128 || l->velocity>=128 || l->volume>=128 || l->pan>=128)return 0;
#endif
    if(l->kind!=VM_ON && l->aux)return 0;
    if(l->kind==VM_ON && (!l->velocity || (l->aux && ((l->aux&0xc000u)!=0x8000u))))return 0;
    if(l->engine==0 && !(l->kind==VM_ON||l->kind==VM_OFF||l->kind==VM_KILL||l->kind==VM_PITCH||l->kind==VM_SUB_BEND||l->kind==VM_SUB_EXPRESSION
#if ENABLE_AE350_FDC
        ||(sensor && l->kind==VM_SENSOR)
#endif
        ))return 0;
    if(l->engine==1 && !(l->kind==VM_ON||l->kind==VM_OFF||l->kind==VM_KILL||l->kind==VM_GAIN||l->kind==VM_KS_REFRESH_LOSS))return 0;
    if(l->engine==2 && !(l->kind==VM_ON||l->kind==VM_OFF||l->kind==VM_KILL||l->kind==VM_GAIN||l->kind==VM_PITCH))return 0;
    if(l->kind==VM_SUB_BEND && l->pitch>16383u)return 0;
    if(l->kind==VM_SUB_EXPRESSION && ((l->pitch>>22)||(l->pitch&255u)>128u))return 0;
    c->count=c->index=c->effect_valid=0;c->background=0;
    if(l->kind==VM_OFF || l->kind==VM_KILL){
        if(l->engine==1)return ks_job(c,l);
        (void)add(c,l->engine,l->voice,l->kind==VM_OFF?PC_RELEASE:PC_KILL,0,l->generation);
        c->effect_engine=l->engine;c->effect_voice=l->voice;c->effect_valid=2;return 1;
    }
    if(l->engine==0)return sub_job(c,l,l->kind==VM_ON?PC_SUB_NEW:PC_SUB_SCALARS,0);
    if(l->engine==1)return ks_job(c,l);
    return fm_job(c,l);
}
int cf_ack(control_compiler_t *c){
    if(!cf_packet(c))return 0;
    if(++c->index<c->count)return 0;
    if(c->effect_valid==1){
        if(c->effect_engine==0)copy(&c->sub[c->effect_voice],&c->next_sub,sizeof(c->next_sub));
        else if(c->effect_engine==1)copy(&c->ks[c->effect_voice],&c->next_ks,sizeof(c->next_ks));
        else copy(&c->fm[c->effect_voice],&c->next_fm,sizeof(c->next_fm));
    }else if(c->effect_valid==2){
        if(c->effect_engine==0)c->sub[c->effect_voice].released=1;
        else {c->fm[c->effect_voice].released=1;c->fm[c->effect_voice].keys=0;}
    }
    c->count=c->index=c->effect_valid=0;return 1;
}
void cf_idle(control_compiler_t *c,uint8_t e,uint8_t v,uint32_t gen){
    if(e==0&&v<32&&c->sub[v].logical.generation==gen)c->sub[v].live=0;
    if(e==1&&v<16&&c->ks[v].logical.generation==gen)c->ks[v].live=0;
    if(e==2&&v<8&&c->fm[v].logical.generation==gen)c->fm[v].live=0;
}
int cf_maintenance(const control_compiler_t *c){return !c->boot_done || c->pending || c->refresh_phase;}
int cf_template_record(control_compiler_t *c,uint16_t a,uint32_t data){
    if(c->pending || c->refresh_phase)return CF_ERROR;
    if(a>=0x8000 && a<0x8040)c->sub_stage.element[a-0x8000]=data;
    else if(a==0x8040)c->sub_stage.keytrack=(uint8_t)data;
    else if(a==0x8041)c->sub_stage.range=(uint8_t)(data&127u);
    else if(a>=0x8100&&a<0x8180)c->sub_stage.amp[a-0x8100]=data&0x3ffffu;
    else if(a>=0x8180&&a<0x8200)c->sub_stage.mod[a-0x8180]=data&0x3ffffu;
    else if(a>=0xb000&&a<0xb100)c->ks_stage.word[a-0xb000]=data;
    else if(a>=0xb200&&a<0xb280)c->ks_stage.word[256u+a-0xb200]=data;
    else if(a>=0xb300&&a<0xb340)c->ks_header[a-0xb300]=data;
    else if(a==0xb400){
        if((data&0xffff8080u)!=0)return CF_ERROR;
        c->fm_stage_volume=(uint8_t)data;c->fm_stage_pan=(uint8_t)(data>>8);
        c->pending_engine=2;c->pending_generation=++c->fm_sent;c->pending=1;return CF_COMMIT_PENDING;
    }else if(a==0x80ff||a==0xb3ff||a==0xb3fe){
        if(!data)return CF_ERROR;
        c->pending_engine=(uint8_t)(a==0x80ff?0:a==0xb3ff?1:3);c->pending_generation=data;c->pending=1;return CF_COMMIT_PENDING;
    }else return CF_ERROR;
    return CF_OK;
}
static void sub_global(control_compiler_t *c){prepared_packet_t *p=add(c,0,0,PC_GLOBAL,0,0);p->word[1]=c->sub_active.element[5]&65535u;}
int cf_background(control_compiler_t *c,const voice_manager_t manager[3]){
    if(cf_packet(c))return 0;
    c->count=c->index=c->effect_valid=0;c->background=1;
    if(!c->boot_done){
        if(c->boot_phase==0){c->boot_phase=1;if(c->engines&1u){sub_global(c);return 1;}}
        if(c->boot_phase==1){
            c->boot_phase=2;
            if(c->engines&4u){
                (void)add(c,2,0,PC_GLOBAL,PC_FM_RESET,0);
                for(unsigned v=0;v<8;v++){
                    cf_fm_voice_t f;uint16_t pairs[30];default_fm(&f);f.logical.pitch=UINT32_C(4521984);
                    unsigned n=fm_pairs(c,&f,v,VM_ON,pairs,1);
                    for(unsigned i=0;i<n;i+=6){prepared_packet_t *p=add(c,2,0,PC_GLOBAL,PC_FM_GLOBAL_WRITE|(6u<<2),0);
                        for(unsigned j=0;j<6;j++)p->word[1+j/2]|=(uint32_t)pairs[i+j]<<((j&1u)*16u);
                    }
                }
                (void)add(c,2,0,PC_GLOBAL,PC_FM_FINISH,0);return 1;
            }
        }
        c->boot_done=1;
    }
    if(c->pending&&!c->refresh_phase){
        c->refresh_engine=c->pending_engine;c->refresh_voice=0;c->refresh_phase=1;
        if(c->pending_engine==0){copy(&c->sub_active,&c->sub_stage,sizeof(c->sub_active));sub_global(c);return 1;}
        if(c->pending_engine==1)copy(&c->ks_active,&c->ks_stage,sizeof(c->ks_active));
        if(c->pending_engine==2){c->fm_volume=c->fm_stage_volume;c->fm_pan=c->fm_stage_pan;}
    }
    if(c->refresh_phase){
        unsigned e=c->refresh_engine;
        if(e<3)while(c->refresh_voice<vm_engine_caps((uint8_t)e)->voices){
            unsigned v=c->refresh_voice++;
            if(e==0&&c->sub[v].live){
                unsigned flags=manager[0].voices[v].state==VM_HELD?PC_SUB_HOT:PC_SUB_SCALARS;
                return sub_job(c,&c->sub[v].logical,flags,1);
            }
            if(e==1&&c->ks[v].live){
                control_voice_command_t l;copy(&l,&c->ks[v].logical,sizeof(l));
                l.kind=(uint8_t)(c->refresh_phase==1?VM_GAIN:VM_KS_REFRESH_LOSS);
                if(c->refresh_phase==1){c->refresh_phase=2;c->refresh_voice--;}
                else c->refresh_phase=1;
                return ks_job(c,&l);
            }
            if(e==2&&c->fm[v].live){control_voice_command_t l;copy(&l,&c->fm[v].logical,sizeof(l));l.kind=VM_GAIN;return fm_job(c,&l);}
        }
        unsigned index=e==0?PC_ACK_SUB:e==1?PC_ACK_KS:e==2?PC_ACK_FM_SENT:PC_ACK_KST_COMPLETED;
        if(e==3){c->ack[PC_ACK_KST_RESULT]=2;c->ack[PC_ACK_KST_CRC]=0;c->ack_dirty|=(1u<<PC_ACK_KST_RESULT)|(1u<<PC_ACK_KST_CRC);c->unsupported_templates++;}
        c->ack[index]=c->pending_generation;c->ack_dirty|=(uint8_t)(1u<<index);
        c->pending=c->refresh_phase=0;
    }
    return 0;
}
