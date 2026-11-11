#include <assert.h>
#include <stdio.h>
#if !__has_include("fm_engine.h")
int main(void) { assert(!"FM runtime bridge is absent"); }
#else
#include <string.h>
#include "fm_engine.h"
#include "spi_regs.h"
extern int64_t fake_time;
static uint32_t regs[0x700], generations[ENGINE_FM_VOICES];
static fm_command_t last;
static unsigned writes, commits, panics, reads;
static bool complete=true, transport=true, apply_query=true, complete_panic=true;
static uint32_t pending_query;
bool fpga_word_read_burst(uint16_t a,uint32_t *w,size_t n)
{ reads++;if(!transport)return false; for(size_t i=0;i<n;i++)w[i]=regs[a+i];return true; }
bool fpga_control_status(uint32_t *s){*s=0;return true;}
bool fpga_live_write(uint16_t b,uint32_t v){(void)b;(void)v;return true;}
bool fpga_swap_checked(void){return true;}
static void done(void)
{
 regs[0x609]=last.sequence_id;regs[0x624]=last.session_id;regs[0x625]=last.engine_epoch;
 bool selected=last.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND ? last.value_u32!=0 : (regs[0x60e]&FM_FLAG_SELECTED)!=0;
 regs[0x60e]=FM_FLAG_READY|(selected?FM_FLAG_SELECTED:FM_FLAG_MUTED);
 regs[0x607]=2;regs[0x60d]++;
}
static void panic_done(void)
{
 regs[0x632]=regs[0x633];
 regs[0x60e]=FM_FLAG_READY|((regs[0x60e]&FM_FLAG_SELECTED)?FM_FLAG_SELECTED:FM_FLAG_MUTED);
 regs[0x607]=FM_PHASE_READY;regs[0x60d]++;
}
void fpga_word_write(uint16_t a,uint32_t v)
{
 if(a<0x600||a>=0x700)return;
 writes++;
 if(a!=0x647)regs[a]=v;
 if(a==0x647){pending_query=v;if(apply_query){regs[0x647]=v;regs[0x648]=generations[v];regs[0x649]=4;}}
 if(a==0x61c) {
  commits++;assert(fm_deserialize_command(&regs[0x610],12,&last));
  assert(last.sequence_id==v&&v);
  regs[0x60a]=v;regs[0x60b]=0;regs[0x620]=last.session_id;regs[0x621]=last.engine_epoch;
  regs[0x608]=v;regs[0x622]=last.session_id;regs[0x623]=last.engine_epoch;
  regs[0x60e]|=8;regs[0x60d]++;
  if(last.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND || last.opcode_id==FM_SPI_OPCODE_APPLY_PRESET)regs[0x60e]|=FM_FLAG_MUTED;
  if(last.opcode_id==0){regs[0x60f]=last.arg0_u32;regs[0x606]++;memset(generations,0,sizeof(generations));}
  if(last.opcode_id==2)generations[last.target_word>>16]=last.voice_generation;
  if(complete)done();
 }
 if(a==0x630) {
  panics++;regs[0x631]=regs[0x633]=v;regs[0x634]=0;
  regs[0x635]=regs[0x626];regs[0x636]=regs[0x627];regs[0x606]++;
  regs[0x60e]|=FM_FLAG_BUSY|FM_FLAG_MUTED;regs[0x607]=FM_PHASE_RECOVERING;
  memset(generations,0,sizeof(generations));regs[0x60d]++;
  if(complete_panic)panic_done();
 }
}
void fpga_word_write_burst(uint16_t a,const uint32_t *w,size_t n)
{for(size_t i=0;i<n;i++)fpga_word_write(a+i,w[i]);}
#include "engine_link.c"
static void tick(unsigned n){while(n--){fake_time+=1000;fm_service(true);}}
static void fixture(void)
{
 memset(regs,0,sizeof(regs));memset(generations,0,sizeof(generations));
 regs[0x600]=0x464d4331;regs[0x601]=0x10001;regs[0x602]=1;
 regs[0x603]=8;regs[0x604]=4;regs[0x605]=0x3ff;
 regs[0x606]=1;regs[0x607]=2;regs[0x60c]=1;regs[0x60d]=1;regs[0x60e]=5;
 regs[0x640]=3579545;regs[0x641]=64;regs[0x642]=96000;regs[0x643]=1;
 regs[0x644]=0x10000;regs[0x645]=13;regs[0x646]=3;
 memcpy(&regs[0x660],fm_profiles_sha256_words,32);
 complete=transport=apply_query=complete_panic=true;writes=commits=panics=reads=0;s_ready=true;
 fm_init();
}
static engine_fm_status_t status(void){engine_fm_status_t s;engine_link_fm_status(&s);return s;}
static void idle_poll_regression(void)
{
 fixture();tick(2);
 engine_fm_state_t unbound;engine_link_fm_state(&unbound);
 for(unsigned v=0;v<ENGINE_FM_VOICES;v++)assert(unbound.operators_per_voice[v]==4);
 assert(!unbound.status.selected && !writes);
 unsigned before=reads;tick(8);
 assert(reads==before && !writes); // No voice-query writes into an unbound FM backend.
 assert(engine_link_fm_mode(1));tick(1);assert(commits==1);
 tick(2);assert(status().selected && !status().switching);
 assert(engine_link_fm_note_on(0,60u<<16,100,100,64));tick(3);
 assert(commits==2 && last.opcode_id==FM_SPI_OPCODE_NOTE_ON);
 engine_link_fm_panic();tick(1);assert(panics==1);
 tick(3);regs[0x60e]&=~FM_FLAG_READY;regs[0x60d]++;tick(21);
 assert(!engine_link_fm_note_on(0,60u<<16,100,100,64));
 puts("FM idle polling: verified operator dimensions before SELECT, 20 ms health bound, no unbound voice query; SELECT/NoteOn/Panic bypass idle interval PASS");
}
static void flags_regression(void)
{
 fixture();tick(2);assert(status().flags==(FM_FLAG_READY|FM_FLAG_MUTED));
 assert(engine_link_fm_mode(1));tick(3);assert(status().flags==(FM_FLAG_READY|FM_FLAG_SELECTED));
 // Short normal-mailbox BUSY preserves a chord's new intent and a safe Off.
 complete=false;assert(engine_link_fm_note_on(0,60u<<16,100,100,64));tick(3);
 unsigned before=commits;assert(status().flags==(FM_FLAG_READY|FM_FLAG_SELECTED|FM_FLAG_BUSY));
 assert(engine_link_fm_note_on(1,64u<<16,100,100,64));
 assert(engine_link_fm_note_on(2,67u<<16,100,100,64));
 assert(engine_link_fm_note_on(3,69u<<16,100,100,64));engine_link_fm_note_off(3);
 assert(engine_link_fm_parameter(16,0xffff0000u,15)==FM_RESULT_OK);
 assert(!engine_link_fm_mode(1));tick(5);assert(commits==before);
 done();complete=true;tick(30);assert(commits==before+3);
 assert(generations[0]==1&&generations[1]==1&&generations[2]==1&&!generations[3]);
 // A musical intent queued before MUTED is cancelled, never replayed later.
 assert(engine_link_fm_note_on(4,72u<<16,100,100,64));
 assert(engine_link_fm_parameter(18,0xffff0000u,31)==FM_RESULT_OK);before=commits;
 regs[0x60e]=FM_FLAG_READY|FM_FLAG_SELECTED|FM_FLAG_MUTED;regs[0x60d]++;tick(5);
 assert(!engine_link_fm_note_on(5,74u<<16,100,100,64));
 assert(engine_link_fm_parameter(18,0xffff0000u,31)==FM_RESULT_BUSY && commits==before);
 regs[0x60e]=FM_FLAG_READY|FM_FLAG_SELECTED;regs[0x60d]++;tick(20);
 assert(commits==before && !generations[4]);
 s_fm.status.applied_backend=0;
 assert(!engine_link_fm_note_on(5,74u<<16,100,100,64));
 assert(engine_link_fm_parameter(18,0xffff0000u,31)==FM_RESULT_BUSY);
 fixture();tick(2);assert(engine_link_fm_mode(1));tick(3);
 // An explicitly unaccepted BUSY reply cannot replay a parameter after mute.
 complete=false;assert(engine_link_fm_parameter(18,0xffff0000u,31)==FM_RESULT_OK);tick(1);
 assert(last.opcode_id==FM_SPI_OPCODE_SET_PARAMETER && !s_fm_inflight.accepted);
 before=commits;regs[0x608]=0;regs[0x60b]=FM_RESULT_BUSY;
 regs[0x60e]=FM_FLAG_READY|FM_FLAG_SELECTED|FM_FLAG_MUTED;regs[0x60d]++;tick(5);
 assert(commits==before && !s_fm_inflight.active);
 regs[0x60e]=FM_FLAG_READY|FM_FLAG_SELECTED;regs[0x60d]++;tick(15);
 assert(commits==before && !s_fm.parameter_count);
 fixture();tick(2);assert(engine_link_fm_mode(1));tick(3);
 assert(engine_link_fm_note_on(0,60u<<16,100,100,64));tick(10);before=commits;
 regs[0x60e]=FM_FLAG_READY|FM_FLAG_SELECTED|FM_FLAG_MUTED;regs[0x60d]++;tick(1);
 engine_link_fm_note_off(0);tick(10);
 assert(commits==before+1 && last.opcode_id==FM_SPI_OPCODE_NOTE_OFF);
 fixture();regs[0x60e]|=FM_FLAG_BUSY;tick(2);
 assert(status().available && !engine_link_fm_mode(1) && !commits);
 fixture();regs[0x60e]|=FM_FLAG_FAULT;tick(2);
 assert(!engine_link_fm_mode(1) && !commits);
 puts("FM flags: boot 0x5 SELECT, selected 0x3, BUSY chord queue/Off cancellation, MUTED rejects and cancels On/parameter without replay, applied binding gate PASS");
}
/* Model either a shared music status snapshot or an independent control
 * snapshot: local high-bit identities must never confirm a low-bit command. */
static void local_progress(uint32_t sequence)
{
 assert(sequence & 0x80000000u);
 regs[0x608]=regs[0x609]=regs[0x60a]=sequence;
 regs[0x620]=regs[0x622]=regs[0x624]=regs[0x60f];
 regs[0x621]=regs[0x623]=regs[0x625]=regs[0x606];
 regs[0x60b]=FM_RESULT_OK;regs[0x60d]++;
}
static void control_done(void)
{
 regs[0x608]=regs[0x60a]=last.sequence_id;
 regs[0x620]=regs[0x622]=last.session_id;
 regs[0x621]=regs[0x623]=last.engine_epoch;
 regs[0x60b]=FM_RESULT_OK;
 done();
}
static void local_sequence_regression(void)
{
 fixture();tick(2);assert(engine_link_fm_mode(1));complete=false;tick(1);
 local_progress(0x80000001u);tick(1);
 assert(s_fm_inflight.active && status().switching && !status().needs_select);
 assert(!s_fm_inflight.accepted && !panics);
 control_done();complete=true;tick(2);assert(!s_fm_inflight.active && !status().switching);
 local_progress(0xffffffffu);tick(21);
 assert(status().valid && !status().needs_select && !panics);
 complete=false;assert(engine_link_fm_parameter(16,0xffff0000u,15)==FM_RESULT_OK);tick(1);
 assert(last.sequence_id==1 && last.opcode_id==FM_SPI_OPCODE_SET_PARAMETER);
 local_progress(0x80000001u);tick(2);
 assert(s_fm_inflight.active && !s_fm_inflight.accepted && !s_fm.parameter_count && !panics);
 /* Matching sequence alone cannot borrow another session/epoch's completion. */
 control_done();regs[0x624]^=1;tick(1);assert(s_fm_inflight.active && !s_fm.parameter_count);
 control_done();regs[0x625]^=1;tick(1);assert(s_fm_inflight.active && !s_fm.parameter_count);
 control_done();complete=true;tick(1);assert(!s_fm_inflight.active && s_fm.parameter_count==1);
 local_progress(0x80000001u);tick(21);
 assert(engine_link_fm_parameter(18,0xffff0000u,31)==FM_RESULT_OK);tick(3);
 assert(last.sequence_id==2 && !s_fm_inflight.active && !status().needs_select && !panics);
 puts("FM local sequence: high-bit progress/rollover neither faults nor confirms SELECT/parameter; low-bit full identities keep advancing PASS");
}
static void sequence_namespace_regression(void)
{
 fixture();tick(2);assert(engine_link_fm_mode(1));tick(3);
 uint32_t old_session=status().session_id;
 s_fm_sequence=0x7ffffffeu;
 assert(engine_link_fm_parameter(16,0xffff0000u,15)==FM_RESULT_OK);tick(3);
 assert(last.sequence_id==0x7fffffffu && !s_fm_inflight.active);
 unsigned before=commits;
 assert(engine_link_fm_parameter(18,0xffff0000u,31)==FM_RESULT_OK);tick(1);
 /* This catches incrementing into the FPGA local-performance namespace. */
 assert(commits==before && status().needs_select && status().recovering);
 tick(30);assert(panics==1 && !s_fm_panic.sequence_id && s_fm_sequence==0);
 engine_link_fm_owner_ack();assert(engine_link_fm_mode(1));tick(3);
 assert(last.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND && last.sequence_id==1);
 assert(status().session_id!=old_session && !status().needs_select && !status().recovering);
 assert(engine_link_fm_parameter(16,0xffff0000u,15)==FM_RESULT_OK);tick(3);
 assert(last.sequence_id==1 && !s_fm_inflight.active);
 puts("FM SPI namespace: max low-bit sequence allowed, overflow requires panic fence and new SELECT session before low-bit restart PASS");
}
int main(void)
{
 fixture();regs[0x601]=0x10000;tick(2);assert(!status().available&&!writes);
 assert(!engine_link_fm_mode(1));
 fixture();regs[0x660]^=1;tick(2);assert(!status().available&&!writes);
 fixture();tick(2);assert(status().available&&!status().selected&&!commits&&!panics);
 assert(engine_link_fm_mode(1));complete=false;tick(2);
 assert(commits==1&&last.opcode_id==0&&last.arg0_u32&&last.session_id==0&&last.engine_epoch==1);
 uint32_t nonce=last.arg0_u32;assert(!engine_link_fm_note_on(0,60u<<16,100,100,64));
 done();tick(3);assert(status().selected&&!status().switching);
 complete=true;assert(engine_link_fm_note_on(0,60u<<16,100,100,64));tick(20);
 assert(commits==2&&last.opcode_id==2&&last.target_word==0x0000ffff&&last.voice_generation==1);
 assert(last.arg0_u32==0x6464&&last.arg1_u32==64&&last.session_id==nonce&&last.engine_epoch==2);
 engine_link_fm_note_off(0);tick(20);assert(last.opcode_id==3&&last.voice_generation==1);
 assert(engine_link_fm_parameter(16,0xffff0000u,15)==0);tick(10);
 engine_fm_state_t snapshot;engine_link_fm_state(&snapshot);assert(snapshot.parameter_count==1);
 assert(snapshot.parameters[0].value_u32==15&&snapshot.parameters[0].sequence_id);
 assert(engine_link_fm_parameter(16,0xffff0000u,16)==4);
 complete=false;assert(engine_link_fm_note_on(1,62u<<16,100,100,64));tick(20);
 unsigned before=commits;engine_link_fm_panic();tick(5);assert(panics==1&&commits==before);
 assert(!status().switching);complete=true;tick(20);assert(commits==before);
 // Native PLL loss resets the session. Never replay old notes or select.
 regs[0x60f]=0;regs[0x606]=1;regs[0x60e]=5;regs[0x60d]++;tick(25);
 assert(status().needs_select&&status().recovering&&!engine_link_fm_note_on(0,60u<<16,100,100,64));
 assert(!status().applied_backend);
 before=commits;tick(50);assert(commits==before);
 engine_link_fm_owner_ack();assert(engine_link_fm_mode(1));tick(15);
 assert(last.opcode_id==0&&last.arg0_u32!=nonce&&!status().recovering);
 // READY is distinct from installed capability and selected mode.
 regs[0x60e]&=~1u;regs[0x60d]++;tick(21);
 assert(!engine_link_fm_note_on(0,60u<<16,100,100,64));
 fixture();tick(2);assert(engine_link_fm_mode(1));complete=false;tick(2);
 before=commits;engine_link_fm_panic();tick(20);assert(commits==before&&panics==1);
 // Applied query index must match before a command uses voice generation.
 fixture();tick(2);assert(engine_link_fm_mode(1));tick(3);apply_query=false;
 assert(engine_link_fm_note_on(3,65u<<16,100,100,64));before=commits;tick(5);assert(commits==before&&pending_query==3);
 regs[0x647]=3;regs[0x648]=7;regs[0x649]=4;apply_query=true;tick(3);
 assert(commits==before+1&&last.voice_generation==8&&last.target_word==0x0003ffff);
 // An ambiguous accepted On is never replayed. After rediscovery, priority
 // Panic silences the existing context, while fresh input stays blocked.
 complete=false;assert(engine_link_fm_note_on(4,66u<<16,100,100,64));tick(5);
 before=commits;fake_time+=100001;tick(2);
 assert(status().needs_select&&!engine_link_fm_note_on(0,60u<<16,100,100,64));
 tick(30);assert(commits==before&&panics==1&&status().needs_select);
 fixture();tick(2);assert(engine_link_fm_mode(1));complete=false;tick(1);
 regs[0x60f]=0;regs[0x606]=1;regs[0x60e]=5;regs[0x608]=0;
 regs[0x60b]=2;regs[0x60d]++;tick(1);
 assert(status().needs_select && commits==1); // Rejected SELECT is not auto-reissued.
 flags_regression();
 idle_poll_regression();
 local_sequence_regression();
 sequence_namespace_regression();
 puts("FM SPI lifecycle: capability/ABI/profile gate, SELECT nonce and completed fence, target/voice generation, parameter confirmation, priority Panic cancellation, PLL reset/new selection PASS");
 return 0;
}
#endif
