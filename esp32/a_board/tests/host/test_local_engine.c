#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine_link.h"
#include "fm_engine.h"
#include "spi_regs.h"
#include "raw_midi.h"
extern int64_t fake_time;
static uint32_t regs[0xb600];
static unsigned writes, mode_writes, panics, swaps;
static bool torn,fail_magic;
bool fpga_word_read_burst(uint16_t a,uint32_t *w,size_t n)
{
    assert(a!=0x0709 && (a<0xa000 || a>0xa1ff));
    fake_time+=2;
    if(fail_magic && a==0x070b)return false;
    for(size_t i=0;i<n;i++)w[i]=regs[a+i];
    if(torn && a==0x070c)regs[0x070d]++;
    return true;
}
void fpga_word_write(uint16_t a,uint32_t v)
{
    assert(a!=0x0709 && (a<0xa000 || a>0xa1ff));
    (void)v;fake_time+=2;writes++;
#ifdef REAL_RAW_MIDI
    if(a==MIDI_RAW_REG_COMMAND) {
        regs[MIDI_RAW_REG_ACCEPT_COUNT]++;
        if((v>>24)==MIDI_RAW_OP_ENABLE)regs[MIDI_RAW_REG_STATUS]=(v&1u)?MIDI_RAW_STATUS_ENABLED:0;
    }
#endif
    if(a==0x0516)mode_writes++;
    if(a==FM_REG_PANIC_SEQUENCE)panics++;
}
void fpga_word_write_burst(uint16_t a,const uint32_t *w,size_t n)
{for(size_t i=0;i<n;i++)fpga_word_write(a+i,w[i]);}
bool fpga_swap_checked(void){swaps++;return true;}
bool fpga_control_status(uint32_t *s){*s=0;return true;}
bool fpga_live_write(uint16_t b,uint32_t v){fpga_word_write(0x0800+b,v);return true;}
#ifndef ENGINE_SOURCE
#define ENGINE_SOURCE "engine_link.c"
#endif
#include ENGINE_SOURCE
static void fixture(unsigned mode)
{
    memset(regs,0,sizeof(regs));writes=mode_writes=panics=swaps=0;torn=fail_magic=false;
    regs[MIDI_RAW_REG_MAGIC]=MIDI_RAW_ABI_MAGIC;
#ifdef REAL_RAW_MIDI
    raw_midi_init();
    regs[MIDI_RAW_REG_MAGIC]=MIDI_RAW_ABI_MAGIC;
    regs[MIDI_RAW_REG_INPUT_CAPABILITIES]=MIDI_RAW_CAP_INDEPENDENT_USB;
#endif
    memset(&s_ks,0,sizeof(s_ks));s_ks_seen=false;
    regs[0x070b]=0x454e4731;regs[0x070c]=mode|(mode<<2)|(1u<<6)|(1u<<9)|(15u<<16);
    if(mode==2)regs[0x070c]|=1u<<8;
    regs[0x070d]=regs[0x070e]=3;
    regs[0x0500]=ENGINE_KS_CAPABILITY;regs[0x0501]=ENGINE_KS_VERSION;
    regs[0x0502]=mode==1;regs[0x0503]=17;regs[0x0505]=0x80000002;
    regs[0x0506]=100;
    regs[0x0600]=FM_CAP_MAGIC;regs[0x0601]=0x00010001u;
    regs[0x0602]=FM_BACKEND_JT51_OPM;regs[0x0603]=8;regs[0x0604]=4;regs[0x0605]=0x3ff;
    regs[0x0606]=7;regs[0x0607]=FM_PHASE_READY;regs[0x060c]=1;regs[0x060d]=1;
    regs[0x060e]=FM_FLAG_READY|(mode==2?FM_FLAG_SELECTED:FM_FLAG_MUTED);regs[0x060f]=13;
    regs[0x0640]=3579545;regs[0x0641]=64;regs[0x0642]=96000;regs[0x0643]=1;
    regs[0x0644]=0x10000;regs[0x0645]=13;regs[0x0646]=3;
    memcpy(&regs[0x0660],fm_profiles_sha256_words,32);
    fake_time=1000;
}
static void tick(void) {fake_time+=1000;engine_service();}
static engine_local_state_t local(void) {engine_local_state_t s;engine_link_local_state(&s);return s;}
static void late_boot(void)
{
    for(unsigned mode=0;mode<3;mode++) {
        fixture(mode);engine_link_init();
        assert(!s_flute.available && !engine_link_flute_mode(true));
        assert(!writes && !swaps && "late ESP boot must not overwrite FPGA local performance");
        assert(local().supported && local().adopted && local().applied==mode && local().blocked);
        engine_link_local_ack(local().generation);
        for(unsigned n=0;n<5;n++)tick();
        assert(!mode_writes && !panics && !local().blocked);
        regs[0x070c]&=~(1u<<9);tick();assert(local().blocked);
        regs[0x070c]|=1u<<9;tick();assert(!local().blocked);
        assert(s_ks.requested_mode==(mode==1) && s_ks.applied_mode==(mode==1));
        assert(s_fm.status.session_id==13 && s_fm.status.engine_epoch==7);
        assert(s_fm.status.applied_backend==(mode==2?FM_BACKEND_JT51_OPM:0));
        assert(local().engine_mask==15 && "ENG1 mask retains the physical five-bit capability field");
    }
}
static void switching_and_faults(void)
{
    fixture(2);engine_link_init();engine_link_local_ack(local().generation);
    s_fm_inflight.active=true;s_fm_notes[0].on=true;s_ks_desired[0].on=true;
    regs[0x070d]=4;regs[0x070c]=(2u<<2)|(1u<<4)|(1u<<6)|(15u<<16);
    tick();assert(local().blocked && !local().adopted && !s_fm_inflight.active);
    assert(!s_fm_notes[0].on && !s_ks_desired[0].on && !writes);
    regs[0x070c]=(1u<<6)|(1u<<9)|(15u<<16);
    regs[0x060e]=FM_FLAG_READY|FM_FLAG_MUTED;regs[0x060f]=14;regs[0x0606]=8;
    tick();assert(!local().adopted); // old E never confirms the latest target
    regs[0x070e]=4;regs[0x070c]|=1u<<8;
    tick();assert(!local().adopted); // wrong real DAC gate
    regs[0x070c]&=~(1u<<8);tick();
    assert(local().adopted && local().applied==FM_ENGINE_DDS && !panics && !mode_writes);
    engine_link_local_ack(local().generation);assert(!local().blocked);
    regs[0x0606]++;regs[0x060d]++;fake_time+=20000;tick();
    assert(!local().blocked && local().valid && !panics); // inactive FM stays isolated
    regs[0x070d]=regs[0x070e]=5;
    regs[0x070c]=(1u<<6)|(1u<<9)|(15u<<16);tick();
    assert(local().adopted && local().applied==0);
    unsigned gen=local().generation;
    regs[0x070f]=5;regs[0x070c]=(1u<<5)|(1u<<6)|(15u<<16);tick();
    assert(local().generation!=gen && local().blocked && local().fault==5 && !panics);
    gen=local().generation;tick();assert(local().generation==gen);
    engine_link_remote_request();tick();assert(writes==1); // only REMT
    tick();assert(writes==1 && local().remote_pending);
    regs[0x070d]=6;tick();assert(!local().remote_pending && writes==1); // new key wins
    engine_link_remote_request();tick();assert(writes==2);
    regs[0x070f]=0;regs[0x070c]=(1u<<9)|(15u<<16);tick();
    assert(!local().local_active && local().remote_ready && !local().remote_pending);
    assert(local().completed==5 && local().revision==6); // release is not local success
    fixture(2);engine_link_init();engine_link_local_ack(local().generation);
    regs[0x0606]++;regs[0x060d]++;fake_time+=20000;tick();
    assert(local().blocked && !local().valid && !panics); // active FM still fences
}
static void torn_and_legacy(void)
{
    fixture(2);torn=true;engine_link_init();
    assert(!writes && !swaps && local().blocked && !local().valid);
    for(unsigned n=0;n<3;n++)tick();
    assert(!writes && local().blocked);
    torn=false;regs[0x070e]=regs[0x070d];tick();assert(local().adopted);
    fixture(0);regs[0x070c]=(1u<<9)|(15u<<16);regs[0x070d]=regs[0x070e]=0;
    fail_magic=true;engine_link_init();assert(!writes && !swaps && !s_ks.available);
    fail_magic=false;tick();assert(s_ks.available && !s_backend_boot_pending);
    fixture(0);regs[0x070c]=(1u<<9)|(15u<<16);regs[0x070d]=regs[0x070e]=0;
    torn=true;engine_link_init();torn=false;regs[0x070d]=regs[0x070e]=0;
    tick();assert(s_ks.available && !s_backend_boot_pending);
    fixture(0);regs[0x070b]=0;engine_link_init();
    assert(writes && swaps==2 && !local().supported);
}
static void ks_namespace(void)
{
    fixture(0);engine_link_init();
    s_local.local_active=false;s_local.blocked=false;s_local.adopted=false;
    s_ks_seen=false;s_ks.requested_mode=true;s_ks.applied_mode=false;s_ks.switching=true;
    s_ks_mode_generation=2;s_ks_mode_sent=1;s_ks_sequence=0x7fffffffu;s_ks_prepare_at=fake_time;
    s_ks_inflight.sequence=0;s_ks_panic_pending=s_ks_owner_pending=false;
    ks_service();assert(s_ks_inflight.op==KS_MODE && s_ks_inflight.sequence==1);
    regs[0x0502]=1;regs[0x0503]=regs[0x0505]=1;regs[0x0506]++;
    assert(ks_readback() && !s_ks.recovering && !s_ks.switching);
    regs[0x0506]=0;ks_readback();assert(s_ks.recovering && s_ks.fault==KS_FAULT_RESET);
}
static void retired_local_mode_can_release(void)
{
    fixture(3);engine_link_init();
    assert(local().supported && local().valid && local().engine_mask==15);
    assert(local().blocked && !local().adopted && !writes && !panics);
    engine_link_remote_request();tick();
    assert(local().remote_pending && writes==1); // REMT may escape an old mode3.
    regs[0x070c]=(1u<<9)|(15u<<16);
    tick();
    assert(!local().local_active && local().remote_ready && !local().remote_pending);
    assert(local().engine_mask==15);
}
int main(void)
{
    event_bus_init();late_boot();switching_and_faults();torn_and_legacy();ks_namespace();
    retired_local_mode_can_release();
    puts("local engine: three late-boot modes, coherent completion/DAC, cancellation, faults, REMT/key arbitration, legacy mask15/mode3 escape and KS namespace PASS");
    return 0;
}
