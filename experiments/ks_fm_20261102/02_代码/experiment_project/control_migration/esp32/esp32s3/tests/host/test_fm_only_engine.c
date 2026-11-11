/* A missing DDS/KS must neither block the validation firmware nor bypass the
 * real FM identity, SELECT/READY, template ACK or raw disable barrier. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fm_engine.h"
#include "spi_regs.h"
#include "raw_midi.h"
#include "midi_template_link.h"

extern int64_t fake_time;
static uint32_t regs[0x800];
static raw_midi_state_t raw;
static unsigned legacy_writes, swaps, enables, disables, expression_writes, fm_writes;
static bool identity_io=true;
static bool tear_status, status_io_fail;
static uint32_t pending_expression;
void raw_midi_state(raw_midi_state_t *out) { *out=raw; }
void raw_midi_request_enable(bool enable)
{ if(enable)enables++;else disables++;raw.enabled=enable; }
// The FMV1 fixture has no ENG1 local route; it only observes remote enable.
void raw_midi_pause(bool paused) { assert(!paused); }
void raw_midi_service(int64_t deadline) { assert(deadline>=fake_time);fake_time+=10; }
bool fpga_word_read_burst(uint16_t a,uint32_t *w,size_t n)
{
    fake_time+=5;
    if(a==0x07f0 && !identity_io)return false;
    if(a==FM_REG_EPOCH && status_io_fail)return false;
    for(size_t i=0;i<n;i++)w[i]=a+i<0x800?regs[a+i]:0;
    // Independent FPGA-local music may update status between SPI frames.
    if(tear_status && a==FM_REG_STATUS_GENERATION)regs[a]++;
    if(tear_status && a==FM_REG_EPOCH && n==10) {
        w[0]++;w[1]=FM_PHASE_RECOVERING;w[8]=FM_FLAG_READY|FM_FLAG_MUTED;
    }
    return true;
}
bool fpga_word_read_burst_observe(uint16_t a,uint32_t *w,size_t n)
{ return fpga_word_read_burst(a,w,n); }
bool fpga_control_status(uint32_t *s) { *s=0;return true; }
bool fpga_live_write(uint16_t b,uint32_t v) { (void)b;(void)v;legacy_writes++;return true; }
bool fpga_swap_checked(void) { swaps++;return true; }
void fpga_word_write(uint16_t a,uint32_t v)
{
    fake_time+=5;
    if(a==0xb400) { pending_expression=v;expression_writes++;return; }
    if(a<0x600 || a>=0x700) { legacy_writes++;return; }
    fm_writes++;
    regs[a]=v;
    if(a==FM_REG_CMD_COMMIT) {
        fm_command_t command;
        assert(fm_deserialize_command(&regs[0x610],12,&command));
        assert(command.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND);
        regs[0x608]=regs[0x609]=regs[0x60a]=command.sequence_id;
        regs[0x620]=regs[0x622]=regs[0x624]=command.session_id;
        regs[0x621]=regs[0x623]=regs[0x625]=command.engine_epoch;
        regs[0x60b]=FM_RESULT_OK;regs[0x60f]=command.arg0_u32;regs[0x606]++;
        regs[0x60e]=FM_FLAG_READY|FM_FLAG_SELECTED;regs[0x60d]++;
    }
    if(a==FM_REG_VOICE_QUERY_INDEX) { regs[0x648]=0;regs[0x649]=4; }
}
void fpga_word_write_burst(uint16_t a,const uint32_t *w,size_t n)
{ for(size_t i=0;i<n;i++)fpga_word_write(a+i,w[i]); }
#ifndef ENGINE_SOURCE
#define ENGINE_SOURCE "engine_link.c"
#endif
#include ENGINE_SOURCE

#if defined(CONFIG_FPGA_FM_ONLY) && CONFIG_FPGA_FM_ONLY
static void ticks(unsigned n) { while(n--) { fake_time+=1000;engine_service(); } }
#endif
static void fixture(void)
{
    regs[0x600]=0x464d4331;regs[0x601]=0x10001;regs[0x602]=1;
    regs[0x603]=8;regs[0x604]=4;regs[0x605]=0x3ff;
    regs[0x606]=1;regs[0x607]=2;regs[0x60c]=1;regs[0x60d]=1;regs[0x60e]=5;
    regs[0x640]=3579545;regs[0x641]=64;regs[0x642]=96000;regs[0x643]=1;
    regs[0x644]=0x10000;regs[0x645]=13;regs[0x646]=3;
    memcpy(&regs[0x660],fm_profiles_sha256_words,32);
    regs[0x7f0]=0x464d5631;regs[0x7f1]=0x10000;regs[0x7f2]=0x407f;
    raw.available=raw.initialized=true;
}
#if defined(CONFIG_FPGA_FM_ONLY) && CONFIG_FPGA_FM_ONLY
static void start_fm_performance(void)
{
    midi_template_image_t image={.fm_expression=0x4055};
    midi_template_link_publish(&image);engine_link_performance_ready(true);
    ticks(5);assert(engine_link_fm_mode(1));ticks(5);
    regs[0x7f2]=pending_expression;regs[0x7f3]=1;ticks(3);
    raw.ready=true;
    midi_template_link_state_t link;midi_template_link_state(&link);
    assert(engine_fm_playable(&s_fm.status) && s_raw_enable_requested && raw.enabled);
    assert(link.available && link.ready && !link.fault && enables==1 && !disables);
}
static void readback_regression(const char *mode)
{
    if(!strcmp(mode,"readback-first")) {
        tear_status=true;
        int64_t started=fake_time;
        while(!s_fm.status.needs_select && fake_time-started<125000)ticks(1);
        assert(s_fm.status.needs_select && s_fm.status.recovering);
        assert(s_fm.status.reason==FM_LINK_TIMEOUT && !s_fm.status.valid && !enables);
        puts("FM readback: first snapshot continuously torn fails closed within 100 ms plus the 20 ms polling interval PASS");
        return;
    }
    start_fm_performance();
    if(!strcmp(mode,"readback-once")) {
        engine_fm_state_t before=s_fm;
        midi_template_link_state_t old_link,link;midi_template_link_state(&old_link);
        int64_t coherent=s_fm_coherent_at;
        unsigned writes=fm_writes;
        tear_status=true;fake_time+=25000;engine_service();
        midi_template_link_state(&link);
        assert(!memcmp(&s_fm,&before,sizeof(before)) && s_fm_coherent_at==coherent);
        assert(s_raw_enable_requested && raw.enabled && !disables);
        assert(!memcmp(&link,&old_link,sizeof(link)) && fm_writes==writes);
        tear_status=false;ticks(25);
        assert(engine_fm_playable(&s_fm.status) && raw.enabled && enables==1 && !disables);
        puts("FM readback: one torn snapshot preserves the complete verified cache, raw ENABLE and template ACK; no mixed-field dispatch PASS");
        return;
    }
    if(!strcmp(mode,"readback-timeout")) {
        int64_t coherent=s_fm_coherent_at;
        tear_status=true;
        for(unsigned step=1;step<=4;step++) {
            fake_time=coherent+step*20000;engine_service();
            assert(engine_fm_playable(&s_fm.status) && raw.enabled && !disables);
        }
        fake_time=coherent+FM_CONFIRM_TIMEOUT_US+1000;engine_service();
        assert(s_fm.status.needs_select && s_fm.status.recovering);
        assert(s_fm.status.reason==FM_LINK_TIMEOUT && !s_fm.status.valid);
        assert(!s_raw_enable_requested && !raw.enabled && disables==1);
        puts("FM readback: repeated torn snapshots cannot refresh the 100 ms deadline and revoke raw ingress on timeout PASS");
        return;
    }
    if(!strcmp(mode,"readback-io"))status_io_fail=true;
    else if(!strcmp(mode,"readback-reset")) {
        regs[FM_REG_EPOCH]++;regs[FM_REG_SESSION]=0;
        regs[FM_REG_FLAGS]=FM_FLAG_READY|FM_FLAG_MUTED;
    } else if(!strcmp(mode,"readback-fault"))regs[FM_REG_FLAGS]|=FM_FLAG_FAULT;
    else if(!strcmp(mode,"readback-muted"))regs[FM_REG_FLAGS]|=FM_FLAG_MUTED;
    else if(!strcmp(mode,"readback-panic"))engine_link_fm_panic();
    else assert(!"unknown readback regression");
    regs[FM_REG_STATUS_GENERATION]++;fake_time+=25000;engine_service();
    assert(!engine_fm_playable(&s_fm.status));
    assert(!s_raw_enable_requested && !raw.enabled && disables==1);
    if(!strcmp(mode,"readback-io"))assert(s_fm.status.reason==FM_LINK_READBACK && s_fm.status.needs_select);
    if(!strcmp(mode,"readback-reset") || !strcmp(mode,"readback-fault"))assert(s_fm.status.needs_select);
    if(!strcmp(mode,"readback-muted"))assert(s_fm.status.valid && (s_fm.status.flags&FM_FLAG_MUTED));
    if(!strcmp(mode,"readback-panic"))assert(s_fm.status.switching);
    printf("FM readback: %s still immediately revokes performance permission PASS\n",mode+9);
}
#endif
int main(int argc,char **argv)
{
    assert(argc==2);fixture();
#if defined(CONFIG_FPGA_FM_ONLY) && CONFIG_FPGA_FM_ONLY
    if(!strncmp(argv[1],"readback-",9)) {
        engine_link_init();readback_regression(argv[1]);return 0;
    }
#endif
    if(!strcmp(argv[1],"main-image"))regs[0x7f0]=0;
    if(!strcmp(argv[1],"bad-version"))regs[0x7f1]=0x20000;
    if(!strcmp(argv[1],"identity-io"))identity_io=false;
    if(!strcmp(argv[1],"bad-fm-digest"))regs[0x660]^=1;
    engine_link_init();
#if defined(CONFIG_FPGA_FM_ONLY) && CONFIG_FPGA_FM_ONLY
    if(strcmp(argv[1],"valid") && strcmp(argv[1],"bad-fm-digest")) {
        assert(!s_ready && !legacy_writes && !swaps && !enables);
        puts("FM_ONLY rejects absent/wrong/unreadable validation identity PASS");return 0;
    }
    assert(s_ready && !legacy_writes && !swaps);
    /* Legacy static configuration may still update RAM. It cannot reach
     * absent engines or keep the FM-only owner permanently non-idle. */
    engine_cmd_t edit={.elem=0,.word=1,.value=55};assert(engine_link_send(&edit));
    assert(engine_link_bus_write(80,0));assert(engine_link_prod_write(0,0,7));
    midi_template_image_t image={.fm_expression=0x4055};
    midi_template_link_publish(&image);engine_link_performance_ready(true);
    if(!strcmp(argv[1],"bad-fm-digest")) {
        ticks(25);assert(!enables && !expression_writes && !engine_link_fm_mode(1));
        puts("FM_ONLY still requires the production FM capability/profile digest PASS");return 0;
    }
    ticks(5);assert(!enables && !raw.enabled && !legacy_writes && !swaps);
    assert(engine_link_fm_mode(1));ticks(5);
    assert(expression_writes==1 && pending_expression==0x4055 && !enables);
    /* Value readback alone is not a completion for a repeated-value write. */
    regs[0x7f2]=pending_expression;ticks(3);assert(!enables);
    regs[0x7f3]=1;ticks(3);assert(enables==1 && raw.enabled && engine_link_control_idle());
    assert(!legacy_writes && !swaps && !regs[0x707] && !regs[0x708]);
    /* READY disappearing revokes raw ingress even with a prior template ACK. */
    regs[0x60e]&=~FM_FLAG_READY;regs[0x60d]++;ticks(25);
    assert(disables==1 && !raw.enabled);
    regs[0x60e]|=FM_FLAG_READY;regs[0x60d]++;ticks(25);
    assert(enables==2 && raw.enabled);
    /* RESET invalidates the template; held bytes cannot re-enable it. */
    raw.reset_count++;ticks(2);assert(disables==2 && !raw.enabled);
    ticks(3);assert(enables==2);
    midi_template_link_publish(&image);ticks(25);
    assert(expression_writes==2 && enables==2);
    regs[0x7f3]=2;ticks(2);assert(enables==3);
    engine_link_performance_ready(false);raw.inflight=true;raw.source[0].flushing=true;
    unsigned sent=expression_writes;midi_template_link_publish(&image);ticks(3);
    assert(expression_writes==sent && !raw.enabled);
    raw.inflight=false;raw.source[0].flushing=false;ticks(25);
    assert(expression_writes==sent+1 && !legacy_writes && !swaps);
    puts("FM_ONLY: no DDS/KS transactions, real FM ready/SELECT/template ACK, reset and raw disable fences PASS");
#else
    if(!strcmp(argv[1],"valid")) {
        assert(!s_ready && !legacy_writes && !swaps && !enables);
        puts("production firmware refuses FM_ONLY validation image PASS");
    } else {
        assert(s_ready && legacy_writes && swaps==2);
        puts("production firmware retains acknowledged DDS double-bank initialization PASS");
    }
#endif
    return 0;
}
