/* Actual SPI-owner state machines; only the wire and RTOS are fake. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine_link.h"
#include "fm_engine.h"
#include "raw_midi.h"
#include "midi_template_link.h"
#include "spi_regs.h"
extern int64_t fake_time;
static uint32_t regs[0x800];
static bool fail_ks,fail_fm,fail_all;
static unsigned writes,disables,pauses;
static raw_midi_state_t raw;
static midi_template_link_state_t image;
void raw_midi_state(raw_midi_state_t *s) {*s=raw;}
void raw_midi_request_enable(bool enabled) {if(!enabled)disables++;raw.enabled=enabled;}
void raw_midi_pause(bool paused) {if(paused)pauses++;}
void raw_midi_service(int64_t deadline) {(void)deadline;fake_time+=5;}
void midi_template_link_init(void) {memset(&image,0,sizeof(image));}
void midi_template_link_invalidate(void) {image.ready=false;}
void midi_template_link_state(midi_template_link_state_t *s) {*s=image;}
void midi_template_link_service(int64_t deadline,bool idle,bool available)
{(void)deadline;(void)idle;(void)available;}
bool fpga_word_read_burst(uint16_t a,uint32_t *v,size_t n)
{
    fake_time+=5;
    if(fail_all || (fail_ks && a>=0x0500 && a<0x0600) ||
       (fail_fm && a>=0x0600 && a<0x0700))return false;
    for(size_t i=0;i<n;i++)v[i]=a+i<0x800?regs[a+i]:0;
    return true;
}
void fpga_word_write(uint16_t a,uint32_t v) {(void)a;(void)v;fake_time+=5;writes++;}
void fpga_word_write_burst(uint16_t a,const uint32_t *v,size_t n)
{(void)a;(void)v;(void)n;fake_time+=5;writes++;}
bool fpga_control_status(uint32_t *v) {*v=0;return !fail_all;}
bool fpga_live_write(uint16_t b,uint32_t v) {(void)b;(void)v;return !fail_all;}
bool fpga_swap_checked(void) {return !fail_all;}
#include "engine_link.c"
static int subscriber;
static bool panic(void)
{evt_t e;return event_bus_receive(subscriber,&e,0)==pdTRUE && e.kind==EVT_PANIC;}
static void fixture(void)
{
    fake_time=1000000;raw=(raw_midi_state_t){.available=true,.initialized=true,.enabled=true,
        .ready=true,.input_capabilities_known=true,.independent_usb=true};
    regs[0x0700]=MIDI_RAW_ABI_MAGIC;engine_link_init();
    memset(&s_ks,0,sizeof(s_ks));memset(s_ks_desired,0,sizeof(s_ks_desired));
    memset(&s_ks_inflight,0,sizeof(s_ks_inflight));
    s_ks.available=s_ks.valid=true;s_ks.accepted_sequence=s_ks.mode_sequence=4;s_ks.frame_count=100;
    s_ks_mode_generation=s_ks_mode_sent=4;s_ks_panic_pending=s_ks_owner_pending=s_ks_need_probe=false;
    s_ks_wait_bank_reset=false;s_ks_seen=true;
    regs[0x0500]=ENGINE_KS_CAPABILITY;regs[0x0501]=ENGINE_KS_VERSION;
    regs[0x0503]=regs[0x0505]=4;regs[0x0506]=100;
    s_fm.status=(engine_fm_status_t){.available=true,.valid=true,.ready=true,
        .flags=FM_FLAG_READY|FM_FLAG_MUTED,.backend_id=1,.profile_id=1,.profile_version=0x10000,
        .voice_count=8,.session_id=13,.engine_epoch=7,.topology_generation=1,.phase=FM_PHASE_READY};
    regs[0x0600]=FM_CAP_MAGIC;regs[0x0601]=0x00010001;regs[0x0602]=1;
    regs[0x0603]=8;regs[0x0604]=4;regs[0x0605]=0x3ff;
    regs[0x0606]=7;regs[0x0607]=FM_PHASE_READY;regs[0x060c]=1;
    regs[0x060d]=1;regs[0x060e]=FM_FLAG_READY|FM_FLAG_MUTED;regs[0x060f]=13;
    regs[0x0640]=3579545;regs[0x0641]=64;regs[0x0642]=96000;regs[0x0643]=1;
    regs[0x0644]=0x10000;regs[0x0645]=13;regs[0x0646]=3;
    memcpy(&regs[0x0660],fm_profiles_sha256_words,32);
    memset(s_pending,0,sizeof(s_pending));memset(s_ppending,0,sizeof(s_ppending));
    memset(s_dirty_prev,0,sizeof(s_dirty_prev));memset(s_pdirty_prev,0,sizeof(s_pdirty_prev));
    memset(s_bus_dirty,0,sizeof(s_bus_dirty));
    s_flushing=s_swap_requested=false;s_control_idle=s_performance_ready=s_raw_enable_requested=true;
    s_notes[0].on=true;s_desired[0][4]=s_image[0][4]=1;s_bus[GATE_BASE]=1;
    image=(midi_template_link_state_t){.available=true,.ready=true,.applied_generation=1};
    event_bus_init();subscriber=event_bus_subscribe_reliable(xQueueCreate(64,sizeof(evt_t)));
    writes=disables=pauses=0;
}
static void kept(void)
{
    assert(s_notes[0].on && s_desired[0][4]==1 && s_bus[GATE_BASE]==1);
    assert(!panic());performance_enable(&raw,&image);
    assert(raw.enabled && !disables);
}
static void local_fixture(unsigned mode)
{
    s_local=(engine_local_state_t){.supported=true,.valid=true,.local_active=true,.ready=true,
        .adopted=true,.requested=mode,.applied=mode,.revision=1,.completed=1,.generation=1,.engine_mask=15};
    s_local_owner_ack=1;s_local_health_at=0;
    regs[0x070b]=LOCAL_MAGIC;regs[0x070c]=mode|(mode<<2)|(1u<<6)|(1u<<9)|(15u<<16);
    if(mode==2)regs[0x070c]|=1u<<8;
    regs[0x070d]=regs[0x070e]=1;
    if(mode==2) {
        s_fm.status.selected=true;s_fm.status.requested_backend=s_fm.status.applied_backend=1;
        s_fm.status.flags=regs[0x060e]=FM_FLAG_READY|FM_FLAG_SELECTED;
    }
    if(mode==1) {s_ks.requested_mode=s_ks.applied_mode=true;regs[0x0502]=1;}
}
int main(int argc,char **argv)
{
    assert(argc==2);fixture();const char *name=argv[1];
    if(!strcmp(name,"ks-idle") || !strcmp(name,"ks-during-fm")) {
        if(!strcmp(name,"ks-during-fm")) {
            s_fm.status.selected=true;s_fm.status.requested_backend=s_fm.status.applied_backend=1;
            s_fm.status.flags=FM_FLAG_READY|FM_FLAG_SELECTED;
        }
        fail_ks=true;assert(!ks_readback());kept();
        assert(!s_ks.available && !s_ks.valid && !s_ks.recovering && !s_ks.switching);
        assert(engine_link_ks_mode(false)); // unrelated routing must be idempotent
        fail_ks=false;fake_time+=21000;ks_service();
        assert(s_ks.available && s_ks.valid && !s_ks.requested_mode && !writes);
        fail_ks=true;assert(!ks_readback());kept();
    } else if(!strcmp(name,"fm-idle") || !strcmp(name,"fm-during-ks")) {
        if(!strcmp(name,"fm-during-ks"))s_ks.requested_mode=s_ks.applied_mode=true;
        fail_fm=true;assert(!fm_readback());kept();
        assert(!s_fm.status.available && !s_fm.status.valid && !s_fm.status.recovering && !s_fm.status.needs_select);
        assert(engine_link_fm_mode(FM_BACKEND_UNBOUND));
        fail_fm=false;fake_time+=21000;fm_service(false);
        assert(s_fm.status.available && s_fm.status.valid && !s_fm.status.selected && !writes);
    } else if(!strcmp(name,"fm-idle-not-ready")) {
        s_fm.status.ready=false;s_fm.status.flags=FM_FLAG_MUTED;
        performance_enable(&raw,&image);assert(raw.enabled && !disables);
    } else if(!strcmp(name,"fm-active-unavailable")) {
        s_fm.status.selected=true;s_fm.status.applied_backend=1;s_fm.status.available=false;
        assert(!control_stable_locked());performance_enable(&raw,&image);assert(!raw.enabled && disables==1);
    } else if(!strcmp(name,"ks-shared") || !strcmp(name,"ks-flight") || !strcmp(name,"ks-active")) {
        if(!strcmp(name,"ks-shared"))fail_all=true;
        else if(!strcmp(name,"ks-flight"))s_ks_inflight.sequence=9,s_ks_inflight.op=KS_MODE,s_ks_inflight.mode=true;
        else s_ks.applied_mode=s_ks.requested_mode=true;
        fail_ks=true;assert(!ks_readback());assert(panic() && !s_notes[0].on && s_ks.recovering);
        performance_enable(&raw,&image);assert(!raw.enabled && disables==1);
    } else if(!strcmp(name,"fm-shared") || !strcmp(name,"fm-flight") || !strcmp(name,"fm-active")) {
        if(!strcmp(name,"fm-shared"))fail_all=true;
        else if(!strcmp(name,"fm-flight"))s_fm_inflight.active=true,s_fm_inflight.command.opcode_id=FM_SPI_OPCODE_SELECT_BACKEND;
        else s_fm.status.selected=true,s_fm.status.applied_backend=s_fm.status.requested_backend=1;
        fail_fm=true;assert(!fm_readback());assert(panic() && s_fm.status.recovering);
        performance_enable(&raw,&image);assert(!raw.enabled && disables==1);
    } else if(!strcmp(name,"local-idle-fm") || !strcmp(name,"local-idle-ks")) {
        local_fixture(!strcmp(name,"local-idle-ks")?2:0);
        if(s_local.applied==2)regs[0x0506]=99;else regs[0x0606]++;
        local_mode_poll();engine_local_state_t s;engine_link_local_state(&s);
        assert(s.valid && !s.blocked && s.adopted && !s_local_health_fault && !writes && !pauses);
        assert(!panic());
    } else if(!strcmp(name,"local-leave-fm-offline") || !strcmp(name,"local-leave-ks-offline")) {
        local_fixture(!strcmp(name,"local-leave-fm-offline")?2:1);
        regs[0x070c]=(1u<<6)|(1u<<9)|(15u<<16);
        regs[0x070d]=regs[0x070e]=2;
        if(s_local.applied==2)fail_fm=true;else fail_ks=true;
        local_mode_poll();engine_link_local_ack(s_local.generation);
        engine_local_state_t s;engine_link_local_state(&s);
        assert(s.valid && s.adopted && !s.blocked && !s_local_health_fault && s.applied==FM_ENGINE_DDS);
        assert((fail_fm?s_fm_quarantined:s_ks_quarantined) && !writes && !panic());
    } else if(!strcmp(name,"local-active-fm") || !strcmp(name,"local-active-ks") ||
              !strcmp(name,"local-contradiction") || !strcmp(name,"local-invalid-selected")) {
        unsigned mode=!strcmp(name,"local-active-ks")?1:!strcmp(name,"local-active-fm")?2:0;
        local_fixture(mode);
        if(mode==1)regs[0x0506]=99;
        else if(mode==2)regs[0x0606]++;
        else regs[0x060e]=FM_FLAG_READY|FM_FLAG_SELECTED|
            (!strcmp(name,"local-invalid-selected")?0x80000000u:0);
        local_mode_poll();engine_local_state_t s;engine_link_local_state(&s);
        assert(s.blocked && !s.valid && s_local_health_fault);
    } else assert(!"unknown test case");
    printf("engine isolation: %s PASS\n",name);
}
