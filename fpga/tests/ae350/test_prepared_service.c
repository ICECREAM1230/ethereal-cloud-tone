/* SPDX-License-Identifier: GPL-3.0-or-later
 * Ordered MMIO host unit model only: no claim of RTL/AE350/PCM execution. */
#include "control_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static cs_service_t s;
typedef struct {uint32_t meta,seq;uint64_t tick;} raw_t;
typedef struct {uint16_t addr;uint32_t data;} template_t;
typedef struct {unsigned active,voice,flags,ctx,params;uint32_t revision,generation;} stage_t;
static struct {
 raw_t raw[64];unsigned rn;
 template_t tpl[64];unsigned tn;
 uint32_t shadow[5],pending[5],pending_seq,ack,result,ready,ackword[8],last_revision[3];
 uint32_t generation[3][32];unsigned gate[3][32];stage_t stage[3];
 unsigned reg[256],global_writes,initialized,packet_count,on[3],off[3],route,route_busy,route_target;
 unsigned operations,max_operations,raw_pops,flush_pops,template_pops,acks_written[256],aw;
 unsigned idle_engine,idle_voice,idle_valid;uint32_t idle_generation;
 unsigned auto_ack,overflow;
} h;
static unsigned now,checks,groups;
#define CHECK(x) do{checks++;assert(x);}while(0)
static void native_accept(void){
 unsigned e=h.pending[0]&255u,v=(h.pending[0]>>8)&255u,op=(h.pending[0]>>16)&255u,f=h.pending[0]>>24;
 CHECK(e<3 && v<vm_engine_caps((uint8_t)e)->voices);h.result=0;h.packet_count++;
 uint32_t gen=h.pending[4],rev=h.pending[1];stage_t *t=&h.stage[e];
 if(op==PC_GLOBAL){
  CHECK(!v&&!gen);
  if(e==2){unsigned n=f>>2;CHECK((f&3u)<=2);if((f&3u)==1){CHECK(n&&n<=6);for(unsigned j=0;j<n;j++){unsigned p=h.pending[1+j/2]>>((j&1u)*16u);h.reg[p&255u]=(p>>8)&255u;h.global_writes++;}}
   if((f&3u)==2){h.initialized=1;h.ready|=4;}}
  else CHECK(e==0 && !f && !h.pending[2]&&!h.pending[3] && h.pending[1]<=65535u);
 }else if(op==PC_RELEASE||op==PC_KILL){
  if(gen!=h.generation[e][v])h.result=PC_STALE;
  else {h.gate[e][v]=0;h.off[e]++;if(t->active&&t->voice==v)t->active=0;}
 }else if(op==PC_BEGIN){
  CHECK(!t->active && rev && rev>h.last_revision[e]);h.last_revision[e]=rev;
  unsigned fresh=e==1?f==1:f==0;
  CHECK(fresh?gen>h.generation[e][v]:gen==h.generation[e][v]);
  t->active=1;t->voice=v;t->generation=gen;t->revision=rev;t->flags=f;t->ctx=t->params=0;
  if(fresh){h.generation[e][v]=gen;h.gate[e][v]=0;}
 }else{
  CHECK(t->active && t->voice==v && t->generation==gen && t->revision==rev);
  if(op==PC_DATA){
   if(e==0){CHECK(f<16);if(f&8u)CHECK(!h.pending[3] && !(h.pending[2]>>26));t->ctx|=1u<<f;}
   if(e==1){CHECK(f<2);if(f)CHECK(!(h.pending[3]&0x80000000u));t->ctx|=1u<<f;}
   if(e==2){CHECK(f&&f<=4);for(unsigned j=0;j<f;j++){unsigned p=h.pending[2+j/2]>>((j&1u)*16u);h.reg[p&255u]=(p>>8)&255u;}t->ctx++;}
  }else if(op==PC_PARAMS){CHECK(e==0&&f<3);if(f==2)CHECK(!h.pending[3] && !(h.pending[2]>>12));t->params|=1u<<f;}
  else {CHECK(op==PC_COMMIT && !f && !h.pending[2]&&!h.pending[3]);
   if(e==0)CHECK(t->params==7 && (t->flags==PC_SUB_SCALARS || t->ctx==65535));
   if(e==1)CHECK(t->ctx==3);
   unsigned fresh=e==1?t->flags==1:t->flags==0;
   if(fresh){h.gate[e][v]=1;h.on[e]++;}
   if(e==1&&(t->flags==2||t->flags==7)){h.gate[e][v]=0;h.off[e]++;}
   t->active=0;
  }
 }
 h.ack=h.pending_seq;h.pending_seq=0;
}
static int rd(void *ctx,uint32_t off,uint32_t *v){
 (void)ctx;h.operations++;*v=0;
 switch(off){case 0:*v=PC_LINK_ID;break;case 4:*v=PC_LINK_ABI;break;
 case 0xc:*v=(h.rn?1u:0u)|(h.rn&&(h.raw[0].meta&0x10000u)?32u:0u)|(h.pending_seq?128u:0u)|(h.idle_valid?1024u:0u);break;
 case 0x10:CHECK(h.rn);*v=h.raw[0].meta;break;case 0x14:*v=(uint32_t)h.raw[0].tick;break;case 0x18:*v=(uint32_t)(h.raw[0].tick>>32);break;case 0x20:*v=h.raw[0].seq;break;
 case 0x58:*v=h.ack;break;case 0x5c:*v=h.pending_seq!=0;break;
 case 0x60:*v=h.idle_engine|(h.idle_voice<<8);break;case 0x64:*v=h.idle_generation;break;
 case 0x70:*v=h.ready;break;case 0x74:break;case 0x78:*v=h.result;break;
 case 0x84:*v=h.route|(h.route_busy?256u:0u);break;
 case PC_TEMPLATE_STATUS:*v=(64u-h.tn)|(h.overflow?65536u:0u);break;
 case PC_TEMPLATE_META:*v=h.tn?0x80000000u|h.tpl[0].addr:0;break;
 case PC_TEMPLATE_DATA:CHECK(h.tn);*v=h.tpl[0].data;break;
 default:CHECK(0);return 0;
 }return 1;
}
static int wr(void *ctx,uint32_t off,uint32_t v){
 (void)ctx;h.operations++;
 if(off>=0x40&&off<=0x50){h.shadow[(off-0x40)/4]=v;return 1;}
 if(off>=PC_TEMPLATE_ACK_BASE&&off<=0xbc){unsigned k=(off-PC_TEMPLATE_ACK_BASE)/4;h.ackword[k]=v;CHECK(h.aw<256);h.acks_written[h.aw++]=k;return 1;}
 switch(off){
 case 0x54:CHECK(v&&!h.pending_seq);memcpy(h.pending,h.shadow,sizeof(h.pending));h.pending_seq=v;if(h.auto_ack)native_accept();break;
 case 0x1c:CHECK(v==1&&h.rn);if(h.raw[0].meta&0x10000u){CHECK(!s.runtime.in_flight_valid&&!cf_packet(&s.compiler)&&!h.pending_seq);h.flush_pops++;}else h.raw_pops++;
  memmove(h.raw,h.raw+1,(--h.rn)*sizeof(h.raw[0]));break;
 case 0x28:CHECK(!h.rn);break;
 case 0x68:CHECK(v==1&&h.idle_valid);h.idle_valid=0;break;
 case 0x80:CHECK(v<3&&!h.route_busy);h.route_target=v;h.route_busy=1;break;
 case PC_TEMPLATE_POP:CHECK(v==1&&h.tn);h.template_pops++;memmove(h.tpl,h.tpl+1,(--h.tn)*sizeof(h.tpl[0]));break;
 default:CHECK(0);return 0;
 }return 1;
}
static void poll(void){unsigned before=h.operations;(void)cs_poll(&s,now++,&cs_default_budget);unsigned n=h.operations-before;if(n>h.max_operations)h.max_operations=n;CHECK(n<=512);}
static void tick(void){poll();CHECK(!s.fatal);if(h.pending_seq)native_accept();if(h.route_busy){h.route=h.route_target;h.route_busy=0;}}
static int quiet(void){
 if(h.rn||h.tn||h.pending_seq||s.runtime.in_flight_valid||s.runtime.event_pending||cf_packet(&s.compiler)||cf_maintenance(&s.compiler))return 0;
 for(unsigned e=0;e<3;e++)if(vm_pending_commands(&s.runtime.manager[e]))return 0;
 for(unsigned src=0;src<3;src++)if(s.runtime.parser.sources[src].event_count)return 0;
 return 1;
}
static void drain(void){for(unsigned n=0;n<5000;n++){tick();if(quiet())return;}CHECK(0);}
static void setup(unsigned mode){
 memset(&s,0,sizeof(s));memset(&h,0,sizeof(h));now=0;h.ready=0x103;
 const cs_io_t io={rd,wr,0};CHECK(cs_init(&s,&io,7,(uint8_t)mode,now)==CS_OK);
 for(unsigned i=0;i<200&&s.stats.state!=CS_RUNNING;i++)tick();
 CHECK(s.stats.state==CS_RUNNING&&s.compiler.boot_done&&h.initialized&&h.global_writes==240);
 CHECK(!s.stats.commands && s.stats.physical_acks==43);
}
static void raw(unsigned src,unsigned byte){CHECK(h.rn<64);h.raw[h.rn++]=(raw_t){byte|(src<<8),0,UINT64_C(0x100000001)+h.raw_pops};}
static void msg(unsigned src,unsigned status,unsigned a,unsigned b){raw(src,status);raw(src,a);raw(src,b);}
static void tpl(unsigned a,uint32_t d){CHECK(h.tn<64);h.tpl[h.tn++]=(template_t){(uint16_t)a,d};}
static void idle(unsigned e,unsigned v,uint32_t g){CHECK(!h.idle_valid);h.idle_engine=e;h.idle_voice=v;h.idle_generation=g;h.idle_valid=1;}
static void boot_and_expansion(void){
 setup(0);msg(0,0x90,60,100);
 for(unsigned n=0;n<20&&!h.pending_seq;n++)poll();
 CHECK(h.pending_seq&&s.runtime.in_flight_valid);CHECK(vm_pending_commands(&s.runtime.manager[0])==2);
 uint32_t frozen[5];memcpy(frozen,h.pending,sizeof(frozen));for(unsigned n=0;n<8;n++){poll();CHECK(!memcmp(frozen,h.pending,sizeof(frozen)));CHECK(!s.stats.command_acks);}
 for(unsigned n=0;n<20;n++){native_accept();poll();CHECK(!s.stats.command_acks);CHECK(s.runtime.in_flight_valid);}
 CHECK(((h.pending[0]>>16)&255u)==PC_COMMIT);CHECK(!h.on[0]);native_accept();CHECK(h.on[0]==1);poll();CHECK(s.stats.command_acks==1);
 drain();CHECK(s.stats.commands==2&&s.stats.command_acks==2);CHECK(s.stats.physical_packets==43+26);groups++;
}
static void sources_running(void){
 setup(2);for(unsigned src=0;src<3;src++)raw(src,0x90);for(unsigned src=0;src<3;src++)raw(src,60+src);for(unsigned src=0;src<3;src++)raw(src,100-src);drain();CHECK(h.on[2]==3);
 for(unsigned src=0;src<3;src++){CHECK(s.runtime.manager[2].voices[src].source==src);raw(src,60+src);raw(src,0);}drain();CHECK(h.off[2]==3&&s.stats.raw_bytes==15);groups++;
}
static void flush_in_fragment(void){
 setup(0);msg(0,0x90,60,100);msg(1,0x90,64,100);drain();
 msg(0,0x90,61,100);for(unsigned i=0;i<10&&!h.pending_seq;i++)poll();
 CHECK(h.pending_seq);
 h.raw[h.rn++]=(raw_t){0x1ff01,71,1000};poll();CHECK(s.flush_started&&!h.flush_pops);
 for(unsigned i=0;i<5;i++)poll();
 CHECK(!h.flush_pops);
 drain();CHECK(h.flush_pops==1&&s.stats.flush_complete==1);CHECK(h.gate[0][1]);CHECK(!h.gate[0][0]&&!h.gate[0][2]);
 CHECK(!s.runtime.parser.sources[0].running_status&&s.runtime.parser.sources[1].running_status==0x90);groups++;
}
static void template_tail(void){
 setup(0);msg(0,0x90,60,100);msg(0,0x90,64,100);drain();
 msg(0,0xe0,127,127);msg(0,0xb0,74,0);drain();int32_t bend=s.compiler.sub[0].bend;CHECK(bend==170);
 msg(0,0x80,64,0);drain();CHECK(s.compiler.sub[1].released);
 tpl(0x8040,128);tpl(0x8041,12);tpl(0x8006,16384);tpl(0x80ff,81);
 for(unsigned i=0;i<6;i++){poll();if(h.pending_seq)break;}CHECK(!h.ackword[0]);
 drain();CHECK(h.ackword[0]==81);CHECK(s.compiler.sub[0].keytrack==128&&s.compiler.sub[1].keytrack==64);
 CHECK(s.compiler.sub[0].bend==bend&&s.compiler.sub[1].bend==bend);CHECK(h.on[0]==2);
 msg(0,0xe0,0,0);drain();CHECK(s.compiler.sub[0].bend==-1024&&s.compiler.sub[1].bend==-1024);groups++;
}
static void mode_partial(void){
 setup(2);msg(0,0x90,60,100);drain();raw(0,61);tick();CHECK(cs_request_mode(&s,1)==CS_OK);raw(0,110);
 for(unsigned i=0;i<100&&!h.route_busy;i++){poll();CHECK(!s.fatal);if(h.pending_seq)native_accept();}
 CHECK(h.route_busy&&s.stats.mode==2&&h.off[2]==1);CHECK(h.rn==1);h.route=h.route_target;h.route_busy=0;drain();CHECK(s.stats.mode==1&&h.on[1]==1);CHECK(s.compiler.ks[0].logical.note==61&&s.runtime.parser.sources[0].running_status==0x90);groups++;
}
static void ks_refresh_fm_expression(void){
 setup(1);msg(2,0x90,60,100);drain();uint32_t old=s.compiler.ks_active.word[121];tpl(0xb079,0x7e000000u|(old&65535u));tpl(0xb240,0x12345678);tpl(0xb3ff,18);drain();CHECK(h.ackword[PC_ACK_KS]==18&&h.on[1]==1);CHECK(s.compiler.ks_active.word[320]==0x12345678);CHECK((h.pending[0]>>16&255u)==PC_COMMIT);
 CHECK(cs_request_mode(&s,2)==CS_OK);drain();msg(1,0x90,60,100);drain();unsigned before=h.off[2];tpl(0xb400,0x007f);drain();CHECK(s.compiler.fm_volume==127&&s.compiler.fm_pan==0);CHECK(h.reg[0x20]==0x47&&h.off[2]==before&&h.on[2]==1);CHECK(h.ackword[PC_ACK_FM_SENT]==1);groups++;
}
static void sustain_steal_idle(void){
 setup(2);msg(0,0x90,60,100);drain();msg(0,0xb0,64,127);msg(0,0x80,60,0);drain();CHECK(!h.off[2]&&h.gate[2][0]);msg(0,0xb0,64,0);drain();CHECK(h.off[2]==1);
 uint32_t old=s.compiler.fm[0].logical.generation;idle(2,0,old+10);tick();CHECK(s.stats.idle_stale==1);idle(2,0,old);tick();CHECK(s.stats.idle==1&&!s.compiler.fm[0].live);
 for(unsigned n=0;n<9;n++){msg(0,0x90,60+n,100);drain();}CHECK(h.on[2]==10&&h.off[2]==2);CHECK(s.compiler.fm[1].logical.note==68);idle(2,0,old);tick();CHECK(s.stats.idle_stale==2&&s.compiler.fm[0].live);groups++;
}
static void unsupported_and_faults(void){
 setup(0);tpl(0xb300,0x4b535431);tpl(0xb3fe,79);drain();CHECK(h.ackword[PC_ACK_KST_RESULT]==2&&h.ackword[PC_ACK_KST_COMPLETED]==79&&!h.ackword[PC_ACK_KST]);CHECK(h.acks_written[h.aw-1]==PC_ACK_KST_COMPLETED);CHECK(s.stats.template_unsupported==1);
 h.overflow=1;poll();CHECK(s.fatal&&(s.stats.fault&CS_FAULT_TEMPLATE));
 setup(0);raw(3,0x90);poll();CHECK(s.fatal&&(s.stats.fault&CS_FAULT_RECORD));
 setup(0);msg(0,0x90,60,100);for(unsigned i=0;i<20&&!h.pending_seq;i++)poll();
 CHECK(h.pending_seq);h.ack=h.pending_seq;h.pending_seq=0;h.result=2;poll();CHECK(s.fatal&&(s.stats.fault&CS_FAULT_BACKEND)&&s.runtime.in_flight_valid&&cf_packet(&s.compiler));
 setup(0);msg(0,0x90,60,100);for(unsigned i=0;i<20&&!h.pending_seq;i++)poll();
 CHECK(h.pending_seq);h.ready=0;poll();CHECK(s.fatal&&(s.stats.fault&CS_FAULT_LINK_LOST)&&s.runtime.in_flight_valid);groups++;
}
int main(void){boot_and_expansion();sources_running();flush_in_fragment();template_tail();mode_partial();ks_refresh_fm_expression();sustain_steal_idle();unsupported_and_faults();printf("PASS prepared service: %u groups / %u checks; static=%zu bytes; ordered host MMIO only\n",groups,checks,sizeof(s));return 0;}
