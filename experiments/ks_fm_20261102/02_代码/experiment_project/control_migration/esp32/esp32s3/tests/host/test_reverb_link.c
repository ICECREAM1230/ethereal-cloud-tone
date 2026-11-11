#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine_link.h"
#include "reverb_control.h"
#include "spi_regs.h"
#include "raw_midi.h"
extern int64_t fake_time;
static uint32_t regs[0x800],captured_pair,commit_sequence;
static unsigned reverb_reads,reverb_writes,other_writes,commits,raw_services;
static bool auto_ack,torn,fail_read,ignore_commit,shadow_valid;
static raw_midi_state_t raw_state;

void raw_midi_state(raw_midi_state_t *out) {*out=raw_state;}
void raw_midi_service(int64_t deadline) {(void)deadline;raw_services++;}
void raw_midi_request_enable(bool enabled) {(void)enabled;}
void raw_midi_pause(bool paused) {(void)paused;}
#define REAL_RAW_MIDI 1
#include "fake_performance_link.c"
static bool is_reverb(uint16_t a) {return a>=REVERB_REG_MAGIC && a<=REVERB_REG_GENERATION;}
static void hardware_ack(void)
{
    regs[REVERB_REG_STATUS]=REVERB_STATUS_READY;
    regs[REVERB_REG_APPLIED_PARAMS]=captured_pair;
    regs[REVERB_REG_APPLIED_SEQUENCE]=commit_sequence;
    regs[REVERB_REG_GENERATION]++;
}
bool fpga_word_read_burst(uint16_t a,uint32_t *v,size_t n)
{
    fake_time+=5;
    if(is_reverb(a)) {reverb_reads++;if(fail_read)return false;}
    for(size_t i=0;i<n;i++)v[i]=a+i<0x800?regs[a+i]:0;
    if(torn && a==REVERB_REG_STATUS)regs[REVERB_REG_GENERATION]++;
    return true;
}
void fpga_word_write(uint16_t a,uint32_t v)
{
    fake_time+=5;
    if(!is_reverb(a)) {other_writes++;return;}
    reverb_writes++;
    assert(a==REVERB_REG_SHADOW_PARAMS || a==REVERB_REG_COMMIT);
    if(a==REVERB_REG_SHADOW_PARAMS) {regs[a]=v;shadow_valid=true;}
    else {
        commits++;captured_pair=regs[REVERB_REG_SHADOW_PARAMS];commit_sequence=v;
        if(ignore_commit || !shadow_valid)return;
        shadow_valid=false;
        regs[REVERB_REG_STATUS]|=REVERB_STATUS_BUSY;
        regs[REVERB_REG_GENERATION]++;
        if(auto_ack)hardware_ack();
    }
}
void fpga_word_write_burst(uint16_t a,const uint32_t *v,size_t n)
{for(size_t i=0;i<n;i++)fpga_word_write(a+i,v[i]);}
bool fpga_swap_checked(void){return true;}
bool fpga_control_status(uint32_t *v){*v=0;return true;}
bool fpga_live_write(uint16_t b,uint32_t v){(void)b;(void)v;return true;}
#include "engine_link.c"

static engine_reverb_state_t state(void)
{engine_reverb_state_t s;engine_link_reverb_state(&s);return s;}
static void fixture(bool supported)
{
    memset(regs,0,sizeof(regs));memset(&s_local,0,sizeof(s_local));
    memset(&raw_state,0,sizeof(raw_state));
    reverb_reads=reverb_writes=other_writes=commits=raw_services=0;
    auto_ack=true;torn=fail_read=ignore_commit=shadow_valid=false;
    fake_time=1000000;reverb_init();
    if(supported) {
        regs[REVERB_REG_MAGIC]=REVERB_MAGIC;regs[REVERB_REG_ABI]=REVERB_ABI;
        regs[REVERB_REG_STATUS]=REVERB_STATUS_READY;
        regs[REVERB_REG_APPLIED_PARAMS]=(8192u<<16)|8192u;
        regs[REVERB_REG_APPLIED_SEQUENCE]=41;regs[REVERB_REG_GENERATION]=10;
    }
}
static void tick(void)
{
    fake_time+=1000;unsigned before=reverb_reads+reverb_writes;
    reverb_service(fake_time+700);
    assert(reverb_reads+reverb_writes-before<=1 && "one background SPI frame per service");
}
static void pump(unsigned n) {for(unsigned i=0;i<n;i++)tick();}
static void ready(void)
{
    for(unsigned i=0;i<50 && !state().valid;i++)tick();
    assert(state().available && state().valid && !state().pending && !state().fault);
}
static void committed(void)
{
    for(unsigned i=0;i<20 && !commits;i++)tick();
    assert(commits==1 && state().pending && !state().completed_request_id);
}
static void retired(void)
{
    for(unsigned i=0;i<50 && state().pending;i++)tick();
    assert(!state().pending);
}
static void old_capability(void)
{
    fixture(false);engine_link_init();
    for(unsigned i=0;i<80;i++){fake_time+=1000;engine_service();}
    assert(reverb_reads && !reverb_writes && !state().available);
    assert(!engine_link_reverb_request(1,2,1));
    fixture(true);regs[REVERB_REG_ABI]++;
    pump(50);assert(!state().available && !reverb_writes);
    fixture(true);regs[REVERB_REG_MAGIC]=0;
    pump(50);assert(!state().available && !reverb_writes);
    fixture(true);ready();assert(engine_link_reverb_request(1,2,1));
    regs[REVERB_REG_MAGIC]=0;tick();
    assert(!state().available && !state().pending && !reverb_writes);
}
static void atomic_pair_and_admission(void)
{
    fixture(true);ready();unsigned frames=reverb_reads;
    assert(!engine_link_reverb_request(REVERB_WET_MAX+1,0,1));
    assert(!engine_link_reverb_request(0,REVERB_DAMP_MAX+1,1));
    assert(!engine_link_reverb_request(UINT32_MAX,UINT32_MAX,1));
    assert(!engine_link_reverb_request(0,0,0));
    assert(engine_link_reverb_request(REVERB_WET_MAX,REVERB_DAMP_MAX,71));
    assert(!engine_link_reverb_request(0,0,72));
    assert(reverb_reads==frames && !reverb_writes); // public APIs are RAM only
    auto_ack=false;committed();
    assert(reverb_writes==2 && commit_sequence==42);
    assert(captured_pair==((REVERB_WET_MAX<<16)|REVERB_DAMP_MAX));
    pump(5);assert(state().pending && !state().completed_request_id);
    assert(state().wet_q15==8192); // shadow and COMMIT are not completion
    hardware_ack();retired();
    assert(state().completed_request_id==71 && !state().fault);
    assert(state().wet_q15==REVERB_WET_MAX && state().damp_q15==REVERB_DAMP_MAX);
    assert(state().applied_sequence==42 && !other_writes);
}
static void coherent_and_invalid_reads(void)
{
    fixture(true);torn=true;pump(12);
    assert(!state().valid && !reverb_writes);
    torn=false;ready();
    assert(engine_link_reverb_request(321,654,1));auto_ack=false;committed();
    hardware_ack();torn=true;pump(12);
    assert(!state().completed_request_id && state().wet_q15==8192);
    torn=false;retired();assert(state().completed_request_id==1);
    const uint32_t invalid_status[]={0,REVERB_STATUS_READY|REVERB_STATUS_FAULT,0x80000001u};
    for(unsigned i=0;i<sizeof(invalid_status)/sizeof(invalid_status[0]);i++) {
        fixture(true);regs[REVERB_REG_STATUS]=invalid_status[i];pump(30);
        assert(!state().valid && !engine_link_reverb_request(0,0,1) && !reverb_writes);
    }
    fixture(true);regs[REVERB_REG_APPLIED_PARAMS]=(32769u<<16);pump(30);
    assert(!state().valid && !reverb_writes);
    fixture(true);regs[REVERB_REG_APPLIED_SEQUENCE]=UINT32_MAX;pump(30);
    assert(!state().valid && !reverb_writes);
    fixture(true);regs[REVERB_REG_STATUS]|=REVERB_STATUS_BUSY;pump(6);
    assert(state().valid && state().pending && !engine_link_reverb_request(1,1,1));
}
static void cancellation_and_late_ack(void)
{
    fixture(true);ready();assert(engine_link_reverb_request(1,2,1));
    engine_link_reverb_cancel(99);tick();assert(state().pending);
    engine_link_reverb_cancel(1);tick();assert(!state().pending && !reverb_writes);
    fixture(true);ready();assert(engine_link_reverb_request(1,2,2));
    while(!reverb_writes)tick(); // shadow written, COMMIT still not sent
    s_local.generation++;pump(3);
    assert(!commits && !state().pending && !state().completed_request_id);
    fixture(true);ready();auto_ack=false;assert(engine_link_reverb_request(7,8,3));committed();
    s_local.generation++;pump(5);
    assert(state().pending && !engine_link_reverb_request(9,10,4));
    hardware_ack();retired();assert(!state().completed_request_id && reverb_writes==2);
    fixture(true);ready();auto_ack=false;assert(engine_link_reverb_request(7,8,5));committed();
    engine_link_reverb_cancel(0);hardware_ack();retired();
    assert(!state().completed_request_id && state().fault==REVERB_LINK_CANCELLED);
}
static void timeout_io_reset_wrap(void)
{
    fixture(true);ready();auto_ack=false;ignore_commit=true;
    assert(engine_link_reverb_request(11,22,6));committed();pump(110);
    assert(state().pending && !state().valid && state().fault==REVERB_LINK_TIMEOUT);
    assert(commits==1 && reverb_writes==2 && !state().completed_request_id);
    hardware_ack();retired();assert(!state().completed_request_id);
    pump(30);assert(engine_link_reverb_request(33,44,7));
    fixture(true);ready();auto_ack=false;assert(engine_link_reverb_request(1,2,8));committed();
    fail_read=true;pump(4);assert(!state().available && !state().valid && state().pending);
    fail_read=false;hardware_ack();retired();assert(!state().completed_request_id && commits==1);
    fixture(true);ready();auto_ack=false;assert(engine_link_reverb_request(1,2,9));committed();
    regs[REVERB_REG_APPLIED_SEQUENCE]=regs[REVERB_REG_GENERATION]=0;
    regs[REVERB_REG_STATUS]=REVERB_STATUS_READY;retired();
    assert(state().fault==REVERB_LINK_RESET && !state().completed_request_id && commits==1);
    fixture(true);ready();assert(engine_link_reverb_request(123,456,9));
    while(!reverb_writes)tick();
    shadow_valid=false; // hardware reset between shadow and COMMIT
    regs[REVERB_REG_APPLIED_SEQUENCE]=regs[REVERB_REG_GENERATION]=0;
    tick();retired();
    assert(state().fault==REVERB_LINK_RESET && !state().completed_request_id);
    assert(regs[REVERB_REG_APPLIED_PARAMS]==((8192u<<16)|8192u));
    fixture(true);regs[REVERB_REG_APPLIED_SEQUENCE]=regs[REVERB_REG_GENERATION]=0;ready();
    assert(engine_link_reverb_request(8192,8192,25));
    while(!reverb_writes)tick();
    shadow_valid=false; // reset is otherwise indistinguishable at boot defaults
    pump(110);
    assert(state().pending && state().fault==REVERB_LINK_TIMEOUT && !state().completed_request_id);
    assert(regs[REVERB_REG_APPLIED_SEQUENCE]==0 && commits==1);
    fixture(true);regs[REVERB_REG_APPLIED_SEQUENCE]=REVERB_SEQUENCE_MAX;
    regs[REVERB_REG_GENERATION]=UINT32_MAX-1;ready();
    assert(engine_link_reverb_request(0,0,10));committed();retired();
    assert(commit_sequence==1 && state().completed_request_id==10 && state().generation==0);
    fixture(true);ready();auto_ack=false;assert(engine_link_reverb_request(1,2,11));committed();
    hardware_ack();regs[REVERB_REG_APPLIED_PARAMS]++;retired();
    assert(!state().valid && !state().completed_request_id && state().fault==REVERB_LINK_PROTOCOL);
}
static void local_owner_and_stop_priority(void)
{
    fixture(true);
    regs[0x070b]=LOCAL_MAGIC;regs[0x070c]=(1u<<6)|(1u<<9)|(1u<<16);
    regs[0x070d]=regs[0x070e]=1;
    engine_link_init();engine_link_local_ack(s_local.generation);
    for(unsigned i=0;i<8;i++){fake_time+=1000;engine_service();}
    assert(s_local.local_active && state().valid && !other_writes);
    assert(engine_link_reverb_request(99,88,12));
    raw_state.stop_pending=true;unsigned before=reverb_reads;
    for(unsigned i=0;i<5;i++){fake_time+=1000;engine_service();}
    assert(raw_services && reverb_reads==before && !reverb_writes);
    raw_state.stop_pending=false;
    for(unsigned i=0;i<20 && !state().completed_request_id;i++){fake_time+=1000;engine_service();}
    assert(state().completed_request_id==12 && s_local.local_active && !other_writes);
    assert(!s_local.remote_pending && !s_local.remote_ready);
    unsigned total=reverb_reads+reverb_writes;reverb_service(fake_time);
    assert(reverb_reads+reverb_writes==total);
}
int main(void)
{
    old_capability();atomic_pair_and_admission();coherent_and_invalid_reads();
    cancellation_and_late_ack();timeout_io_reset_wrap();local_owner_and_stop_priority();
    puts("reverb link: old/unknown ABI, atomic pair, coherent ACK, bounds, cancellation, timeout/IO/reset/wrap, local ownership and STOP priority PASS");
}
