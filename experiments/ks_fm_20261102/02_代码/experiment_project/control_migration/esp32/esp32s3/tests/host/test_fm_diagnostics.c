#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fm_diagnostics.h"
#include "fm_engine.h"
#include "midi_template_link.h"
#include "spi_regs.h"
extern int64_t fake_time;
static raw_midi_state_t raw;
static uint32_t registers[0x800];
static unsigned reads, writes, enable_changes, reads_this_tick;
static uint16_t fail_address;
static bool owner_running, raw_served;
void raw_midi_pause(bool paused) { (void)paused; }
void raw_midi_state(raw_midi_state_t *out) { *out=raw; }
void raw_midi_request_enable(bool value) { if(value!=raw.enabled)enable_changes++;raw.enabled=value; }
void raw_midi_service(int64_t deadline) { assert(fake_time<deadline);raw_served=true;fake_time+=20; }
void midi_template_link_init(void) { }
void midi_template_link_invalidate(void) { assert(!"diagnostics must not invalidate templates"); }
void midi_template_link_state(midi_template_link_state_t *out)
{ *out=(midi_template_link_state_t){.available=true,.ready=true}; }
void midi_template_link_service(int64_t deadline,bool idle,bool available)
{ (void)deadline;(void)idle;(void)available; }
bool fpga_word_read_burst(uint16_t address,uint32_t *out,size_t count)
{
    reads++;reads_this_tick++;
    if(owner_running)assert(raw_served);
    assert(count<=11 && address+count<=0x800);
    fake_time+=120;
    if(address==fail_address)return false;
    memcpy(out,&registers[address],count*sizeof(*out));return true;
}
bool fpga_word_read_burst_observe(uint16_t address,uint32_t *out,size_t count)
{ return fpga_word_read_burst(address,out,count); }
void fpga_word_write_burst(uint16_t address,const uint32_t *words,size_t count)
{ (void)address;(void)words;(void)count;writes++; }
void fpga_word_write(uint16_t address,uint32_t word) { fpga_word_write_burst(address,&word,1); }
bool fpga_control_status(uint32_t *status) { *status=0;return true; }
bool fpga_live_write(uint16_t address,uint32_t value) { (void)address;(void)value;writes++;return true; }
bool fpga_swap_checked(void) { writes++;return true; }
#include "engine_link.c"

static engine_fm_diagnostics_t state(void)
{ engine_fm_diagnostics_t out;engine_link_fm_diagnostics_state(&out);return out; }
static void owner_tick(void)
{
    fake_time+=1000;int64_t start=fake_time;reads_this_tick=0;raw_served=false;owner_running=true;
    engine_service();owner_running=false;
    assert(reads_this_tick<=1 && fake_time-start<=700);
}
static void setup(void)
{
    // Stable, already-running music; diagnostics may not disturb these fences.
    s_ready=true;s_control_idle=true;s_performance_ready=s_raw_enable_requested=true;
    s_fm.status=(engine_fm_status_t){.available=true,.valid=true,.ready=true,.selected=true,
        .backend_id=1,.requested_backend=1,.applied_backend=1,.session_id=123,
        .engine_epoch=4,.topology_generation=1,.flags=FM_FLAG_READY|FM_FLAG_SELECTED};
    s_fm_probe_at=s_fm_poll_at=fake_time+10000000;s_fm_coherent_at=fake_time;
    raw=(raw_midi_state_t){.available=true,.initialized=true,.enabled=true,.ready=true,
        .accepted=91,.rejected=7,.events=44,.usb_status=3};
    raw.source[1]=(raw_midi_source_state_t){.session=UINT64_C(0x123456789abcdef0),
        .sent_bytes=33,.dropped_bytes=2,.flushes=8,.fault=RAW_MIDI_EXPIRED,.flushing=true};
    registers[0x7f0]=0x464d5631;registers[0x7f1]=0x10000;
    registers[0x7f2]=0x4000;registers[0x7f3]=99;
    for(unsigned i=4;i<11;i++)registers[0x7f0+i]=100+i;
    registers[0x670]=0x80000004;registers[0x671]=0x80000003;registers[0x672]=FM_RESULT_CANCELLED;
    registers[0x700]=MIDI_RAW_ABI_MAGIC;registers[0x702]=2;
    registers[0x703]=91;registers[0x704]=7;registers[0x705]=44;registers[0x706]=3;
}
int main(int argc,char **argv)
{
    assert(argc==2);fake_time=1000000;setup();engine_fm_status_t before=s_fm.status;
    unsigned old_reads=reads;
    uint32_t id=engine_link_fm_diagnostics_request();engine_fm_diagnostics_t d=state();
#if !CONFIG_FPGA_FM_ONLY
    (void)argv;(void)before;(void)owner_tick;
    fm_diagnostics_service(fake_time+700,true);
    assert(!id && !d.supported && !d.pending);assert(reads==old_reads && !writes);
    puts("diagnostics: production requests never read experimental registers PASS");return 0;
#else
    assert(id && d.pending && !d.valid && reads==old_reads && !writes);
    assert(engine_link_fm_diagnostics_request()==id);
    if(!strcmp(argv[1],"request")) {
        assert(state().request_id==id && reads==old_reads);
        puts("diagnostics: RAM request/get, in-flight dedup, no SPI or musical mutation PASS");return 0;
    }
    if(!strcmp(argv[1],"budget")) {
        raw.inflight=true;owner_tick();assert(reads==old_reads && state().pending);
        raw.inflight=false;raw.source[1].queued_bytes=3;owner_tick();assert(reads==old_reads);
        raw.source[1].queued_bytes=0;
        fm_diagnostics_service(fake_time+ENGINE_FM_DIAGNOSTIC_RESERVE_US-10,true);
        assert(reads==old_reads && state().pending);
    }
    if(!strcmp(argv[1],"io"))fail_address=0x670;
    if(!strcmp(argv[1],"identity"))registers[0x7f0]^=1;
    if(!strcmp(argv[1],"version"))registers[0x7f1]=0x20000;
    if(!strcmp(argv[1],"mid1"))registers[0x700]=0;
    if(!strcmp(argv[1],"timeout"))fake_time+=ENGINE_FM_DIAGNOSTIC_TIMEOUT_US+1;
    owner_tick();assert(state().sample_request_id==0 || !state().valid);
    if(!strcmp(argv[1],"context"))s_fm.status.engine_epoch++;
    if(!strcmp(argv[1],"end-identity"))registers[0x7f0]^=1;
    for(unsigned i=0;i<4 && state().pending;i++)owner_tick();
    d=state();assert(!d.pending && d.sample_request_id==id);
    unsigned expected=FM_DIAGNOSTIC_OK;
    if(!strcmp(argv[1],"io"))expected=FM_DIAGNOSTIC_IO;
    if(!strcmp(argv[1],"identity") || !strcmp(argv[1],"version") || !strcmp(argv[1],"mid1") || !strcmp(argv[1],"end-identity"))expected=FM_DIAGNOSTIC_IDENTITY;
    if(!strcmp(argv[1],"context"))expected=FM_DIAGNOSTIC_CONTEXT;
    if(!strcmp(argv[1],"timeout"))expected=FM_DIAGNOSTIC_TIMEOUT;
    assert(d.error==expected && d.valid==(expected==FM_DIAGNOSTIC_OK));
    if(d.valid) {
        assert(reads-old_reads==4 && d.raw_cache_valid);
        assert(d.blocks[0].words[2]==0x4000 && d.blocks[0].words[3]==99);
        assert(d.blocks[1].words[2]==FM_RESULT_CANCELLED && d.blocks[2].words[4]==7);
        assert(d.raw_cache.source[1].session==UINT64_C(0x123456789abcdef0));
        assert(d.raw_cache.source[1].flushes==8 && d.raw_cache.usb_status==3);
        for(unsigned i=0;i<4;i++)assert(d.blocks[i].read_ok && d.blocks[i].completed_at_us>=d.blocks[i].started_at_us);
    }
    if(!strcmp(argv[1],"context"))before.engine_epoch++;
    assert(!memcmp(&before,&s_fm.status,sizeof(before)) && !writes && !enable_changes && raw.enabled);
    if(fake_time-d.requested_at_us<ENGINE_FM_DIAGNOSTIC_INTERVAL_US) {
        assert(engine_link_fm_diagnostics_request()==id); // completion does not bypass rate limit
        fake_time=d.requested_at_us+ENGINE_FM_DIAGNOSTIC_INTERVAL_US+100;
    }
    assert(engine_link_fm_diagnostics_request()!=id);
    engine_fm_diagnostics_t pending=state();
    assert(pending.pending && pending.sample_request_id==id && pending.valid==d.valid);
    assert(!memcmp(pending.blocks,d.blocks,sizeof(d.blocks)));
    fail_address=0;registers[0x7f0]=0x464d5631;registers[0x7f1]=0x10000;
    registers[0x700]=MIDI_RAW_ABI_MAGIC;registers[0x7f2]=0x4055;
    owner_tick();pending=state();
    assert(pending.pending && pending.sample_request_id==id &&
        !memcmp(pending.blocks,d.blocks,sizeof(d.blocks))); // never publish a mixed new/old capture
    printf("diagnostics: %s, one burst/tick, timestamps, rate limit, music unchanged PASS\n",argv[1]);
#endif
}
