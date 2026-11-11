/* Production three-engine service against hardware with no additive ACK.
 * Every additive register frame is observed; normal modes, hot parameters
 * and explicit two-scope STOP must finish without such a frame. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine_link.h"
#include "fm_engine.h"
#include "raw_midi.h"
#include "midi_template_link.h"
#include "spi_regs.h"
extern int64_t fake_time;
static uint32_t regs[0xc000];
static unsigned bank,pass,mode_commits,template_commits,ks_commands,last_ks_op,fm_expression_writes;
static unsigned io_setup,additive_frames,ks_health_frames;
static bool hold_mode,hold_template;
static uint32_t held_mode;
#ifdef FIFTH_ENGINE_TEST
static bool hold_flute;
#endif

bool fpga_word_read_burst(uint16_t address,uint32_t *words,size_t n)
{
    fake_time+=io_setup+(5+4*n)*8/10+1;assert((size_t)address+n<=0xc000);
    if(address==0x0502 || address==0x0508 || address==0x0540 || address==0x0548)ks_health_frames++;
    for(size_t i=0;i<n;i++) {
        unsigned word=address+i;
        if(word==0x0709 || (word>=0xa000 && word<=0xa1ff))additive_frames++;
        words[i]=regs[word];
    }
    return true;
}
bool fpga_control_status(uint32_t *word)
{fake_time+=io_setup+8;*word=(++pass<<16)|bank;return true;}
bool fpga_live_write(uint16_t address,uint32_t value)
{(void)address;(void)value;fake_time+=io_setup+6;return true;}
bool fpga_swap_checked(void){bank^=1;return true;}
static void apply_mode(uint32_t sequence)
{regs[0x0503]=regs[0x0505]=sequence;regs[0x0502]=(regs[0x0510]>>16)&1u;}
static void write_word(uint16_t address,uint32_t value)
{
    assert(address<0xc000);regs[address]=value;
    if(address==0x0709 || (address>=0xa000 && address<=0xa1ff))additive_frames++;
#ifdef FIFTH_ENGINE_TEST
    if(address==0x07b2 && regs[0x07b0]==ENGINE_FLUTE_CAPABILITY && !hold_flute) {
        regs[0x07b3]=value;regs[0x07b4]=1u|((value&1u)<<1)|(regs[0x07b4]&16u);
    }
#endif
    if(address==2 && value)bank^=1;
    if(address==MIDI_RAW_REG_COMMAND) {
        regs[MIDI_RAW_REG_ACCEPT_COUNT]++;
        if(value>>24==MIDI_RAW_OP_ENABLE)regs[MIDI_RAW_REG_STATUS]=(value&1u)?MIDI_RAW_STATUS_ENABLED:0;
    }
    // This FPGA contains no additive module; its ACK register stays zero.
    if(address==0x0516) {
        ks_commands++;last_ks_op=regs[0x0510]&7u;
        if((regs[0x0510]&7u)==KS_MODE) {
            mode_commits++;held_mode=value;
            if(!hold_mode)apply_mode(value);
        } else regs[0x0503]=value;
    }
    if(address==0x80ff || address==0xb3ff) {
        template_commits++;
        if(!hold_template)regs[address==0x80ff?0x0707:0x0708]=value;
    }
    if(address==0xb400)fm_expression_writes++;
    if(address==FM_REG_VOICE_QUERY_INDEX) {regs[0x0648]=0;regs[0x0649]=4;}
    if(address==FM_REG_CMD_COMMIT) {
        fm_command_t command;assert(fm_deserialize_command(&regs[0x0610],12,&command));
        regs[0x0608]=regs[0x0609]=regs[0x060a]=value;regs[0x060b]=FM_RESULT_OK;
        regs[0x0620]=regs[0x0622]=regs[0x0624]=command.session_id;
        regs[0x0621]=regs[0x0623]=regs[0x0625]=command.engine_epoch;
        if(command.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND) {
            regs[0x060f]=command.arg0_u32;regs[0x0606]++;
            regs[0x060e]=FM_FLAG_READY|(command.value_u32?FM_FLAG_SELECTED:FM_FLAG_MUTED);
        }
        regs[0x060d]++;
    }
    if(address==FM_REG_PANIC_SEQUENCE) {
        regs[0x0631]=regs[0x0632]=regs[0x0633]=value;regs[0x0634]=FM_RESULT_OK;
        regs[0x0635]=regs[0x0626];regs[0x0636]=regs[0x0627];
        regs[0x0606]++;regs[0x060d]++;
    }
}
void fpga_word_write(uint16_t address,uint32_t value)
{fake_time+=io_setup+6;write_word(address,value);}
void fpga_word_write_burst(uint16_t address,const uint32_t *words,size_t n)
{fake_time+=io_setup+(3+4*n)*8/10+1;for(size_t i=0;i<n;i++)write_word(address+i,words[i]);}
#include "engine_link.c"
static void tick(void)
{fake_time+=1000;regs[0x0506]++;engine_service();engine_link_owner_panic_ack();engine_link_fm_owner_ack();}
static bool routes_settled(unsigned target)
{
    return !s_ks.switching && !s_ks.recovering && s_ks.applied_mode==(target==1) &&
        !s_fm.status.switching && !s_fm.status.recovering && (s_fm.status.applied_backend!=0)==(target==2);
}
static void fixture(unsigned initial)
{
    regs[0x0700]=MIDI_RAW_ABI_MAGIC;regs[0x070a]=MIDI_RAW_CAP_INDEPENDENT_USB;
    regs[0x070b]=LOCAL_MAGIC;regs[0x070c]=(initial&3u)|((initial&3u)<<2)|((initial&4u)<<8)|((initial&4u)<<9)|(1u<<6)|(1u<<9)|(7u<<16);
#ifdef FIFTH_ENGINE_TEST
    if(regs[0x07b0])regs[0x070c]|=16u<<16;
#endif
    if(initial==2)regs[0x070c]|=1u<<8;
    regs[0x070d]=regs[0x070e]=1;
    regs[0x0500]=ENGINE_KS_CAPABILITY;regs[0x0501]=ENGINE_KS_VERSION;
    regs[0x0502]=initial==1;regs[0x0503]=17;regs[0x0505]=0x80000002u;regs[0x0506]=100;
    regs[0x0600]=FM_CAP_MAGIC;regs[0x0601]=0x10001;regs[0x0602]=1;
    regs[0x0603]=8;regs[0x0604]=4;regs[0x0605]=0x3ff;
    regs[0x0606]=7;regs[0x0607]=FM_PHASE_READY;regs[0x060c]=regs[0x060d]=1;
    regs[0x060e]=FM_FLAG_READY|(initial==2?FM_FLAG_SELECTED:FM_FLAG_MUTED);regs[0x060f]=13;
    regs[0x0640]=3579545;regs[0x0641]=64;regs[0x0642]=96000;regs[0x0643]=1;
    regs[0x0644]=0x10000;regs[0x0645]=13;regs[0x0646]=3;
    memcpy(&regs[0x0660],fm_profiles_sha256_words,32);
    event_bus_init();raw_midi_init();engine_link_init();engine_link_local_ack(s_local.generation);
    for(unsigned n=0;n<20;n++)tick();
    raw_midi_state_t raw;raw_midi_state(&raw);assert(raw.enabled && raw.ready && s_local.adopted);
    engine_link_remote_request();tick();assert(s_local.remote_pending);
    regs[0x070c]&=~(1u<<6);
    for(unsigned n=0;n<200;n++) {
        tick();raw_midi_state(&raw);
        if(s_local.remote_ready && raw.initialized && !raw.enabled && !raw.inflight &&
           !raw_sources_flushing(&raw))break;
    }
    raw_midi_state(&raw);assert(s_local.remote_ready && !s_local.remote_pending && !raw.enabled && raw.initialized);

    assert(!s_fm.status.needs_select && !s_fm.status.recovering && "fixture must reach remote acquisition without an earlier FM fault");
}
/* Match the measured per-frame cost while using the production service,
 * RAW mailbox and dual-template acknowledgement paths together. The deadline
 * is an owner-sized completion window, not a check of a polling constant. */
static void prepare_idle_route(unsigned target)
{
    /* Set up the measured stable-idle episode independently of boot rendering
     * cost. The budget regression begins only after all static work is done. */
    unsigned measured_setup=io_setup;io_setup=5;
    engine_link_performance_ready(false);raw_midi_flush(MIDI_RAW_SOURCE_ALL);
    assert(engine_link_bus_write(1023,0));
    if(target==2 || engine_fm_control_active(&s_fm.status))assert(engine_link_fm_mode(target==2?1:0));
    assert(engine_link_ks_mode(target==1));
    for(unsigned n=0;n<3000;n++) {
        tick();if(routes_settled(target) && engine_link_control_idle())break;
    }
    assert(routes_settled(target) && engine_link_control_idle() && ks_dds_silent());
    io_setup=measured_setup;
}
static unsigned template_updates(void) {return template_commits+fm_expression_writes;}
static void change_template(unsigned target,midi_template_image_t *image)
{
    if(target==0)image->elements[0][0]++;
    else if(target==1)image->ks_notes[60][1]++;
    else image->fm_expression=87u | (64u<<8);
}
static void idle_budget_case(unsigned target)
{
    prepare_idle_route(target);
    midi_template_image_t image={0};midi_template_link_publish(&image);engine_link_performance_ready(true);
    raw_midi_state_t raw;midi_template_link_state_t state;
    unsigned reads=ks_health_frames,commits=template_commits;
    int64_t started=fake_time;
    do {
        tick();raw_midi_state(&raw);midi_template_link_state(&state);
        if(state.ready && raw.enabled && raw.ready)break;
    } while(fake_time-started<500000);
    fprintf(stderr,"idle budget initial template: target=%u setup=%u elapsed=%lld us KS_read_frames=%u commits=%u ready=%u RAW=%u/%u fault=%u\n",
        target,io_setup,(long long)(fake_time-started),ks_health_frames-reads,template_commits-commits,
        state.ready,raw.enabled,raw.ready,state.fault);
    assert(state.ready && state.applied_generation && raw.enabled && raw.ready &&
        "idle KS health reads must leave time for initial template ACK and RAW enable");
    assert(!raw.fault && !raw.stop_pending && !additive_frames);
    /* A running voice path must also accept a changed template without
     * disabling ingress while stable KS health traffic is present. */
    change_template(target,&image);midi_template_link_publish(&image);
    reads=ks_health_frames;commits=template_commits;started=fake_time;
    unsigned updates=template_updates();
    do {
        tick();raw_midi_state(&raw);midi_template_link_state(&state);
        assert(raw.enabled && "a hot parameter template must preserve ingress");
        if(state.ready && template_updates()>updates)break;
    } while(fake_time-started<500000);
    fprintf(stderr,"idle budget hot template: target=%u setup=%u elapsed=%lld us KS_read_frames=%u commits=%u ready=%u\n",
        target,io_setup,(long long)(fake_time-started),ks_health_frames-reads,template_commits-commits,state.ready);
    assert(state.ready && template_updates()>updates && raw.ready && !additive_frames);
    puts("three-engine idle budget: initial/hot template ACK and RAW readiness complete within owner window PASS");
}

/* Cache hits are permitted only for stable idle health traffic. Every
 * operation below starts immediately after a coherent read, before another
 * health interval could elapse, and must still dispatch or observe an ACK. */
static void idle_urgent_case(void)
{
    prepare_idle_route(1);
    assert(ks_readback());
    unsigned commands=ks_commands,reads=ks_health_frames;
    engine_link_ks_note_off(0);ks_service();
    assert(ks_commands==commands+1 && last_ks_op==KS_NOTE_OFF && s_ks_inflight.sequence);
    reads=ks_health_frames;ks_service();
    assert(ks_health_frames==reads+4 && !s_ks_inflight.sequence &&
        "a fresh health cache cannot defer an unconfirmed Off acknowledgement");
    for(unsigned voice=0;voice<ENGINE_KS_VOICES;voice++) {
        engine_ks_note_t note={.length=512,.tuning=0,.loss=30000,.amplitude=12000,
            .gain_left=10000,.gain_right=11000,.seed=voice+1};
        commands=ks_commands;assert(engine_link_ks_note_on(voice,&note));ks_service();
        assert(ks_commands==commands+1 && last_ks_op==KS_NOTE_ON);ks_service();
        assert(!s_ks_inflight.sequence);
        commands=ks_commands;engine_link_ks_gain(voice,9000,10000);ks_service();
        assert(ks_commands==commands+1 && last_ks_op==KS_GAIN);ks_service();
        assert(!s_ks_inflight.sequence);
        commands=ks_commands;engine_link_ks_loss(voice,29000);ks_service();
        assert(ks_commands==commands+1 && last_ks_op==KS_LOSS);ks_service();
        assert(!s_ks_inflight.sequence);
    }
    commands=ks_commands;engine_link_ks_panic();ks_service();
    assert(ks_commands==commands+1 && last_ks_op==KS_PANIC && s_ks_inflight.sequence);
    ks_service();assert(!s_ks_inflight.sequence);
    /* Initialization and release are native work still awaiting completion,
     * even after mailbox acceptance cleared the host inflight command. */
    for(unsigned address=0x0509;address<=0x050a;address++) {
        regs[address]=0x8000;assert(ks_readback());
        regs[address]=0;reads=ks_health_frames;ks_service();
        assert(ks_health_frames==reads+4 && !s_ks.init_mask && !s_ks.release_mask &&
            "native initialization/release completion cannot wait for idle health");
    }
    /* A known busy mailbox is urgent even when its last read is fresh. */
    regs[0x0502]|=2u;assert(ks_readback() && s_ks.busy);
    regs[0x0502]&=~2u;reads=ks_health_frames;ks_service();
    assert(ks_health_frames==reads+4 && !s_ks.busy);
    /* MODE first emits its Panic, then must commit as soon as the DDS mute
     * fence is satisfied, and keep checking an unknown ACK immediately. */
    hold_mode=true;commands=ks_commands;
    assert(engine_link_ks_mode(false));ks_service();
    assert(ks_commands==commands+1 && last_ks_op==KS_PANIC);
    ks_service();
    assert(ks_commands==commands+2 && last_ks_op==KS_MODE && s_ks_inflight.sequence);
    reads=ks_health_frames;ks_service();
    assert(ks_health_frames==reads+4 && s_ks_inflight.sequence &&
        "an unknown MODE ACK cannot wait for a health interval");
    hold_mode=false;apply_mode(held_mode);reads=ks_health_frames;ks_service();
    assert(ks_health_frames==reads+4 && !s_ks_inflight.sequence && !s_ks.switching && !s_ks.applied_mode);
    assert(!additive_frames);
    puts("three-engine idle urgency: Off/Panic/MODE, BUSY and unconfirmed ACKs bypass fresh health cache PASS");
}

int main(int argc,char **argv)
{
    assert(argc==4 || argc==5);unsigned initial=(unsigned)atoi(argv[1]),target=(unsigned)atoi(argv[2]);
    assert(initial<3 && target<3);io_setup=argc==5?(unsigned)atoi(argv[4]):5;fixture(initial);
    if(!strcmp(argv[3],"idle-budget")) {idle_budget_case(target);return 0;}
    if(!strcmp(argv[3],"idle-urgent")) {idle_urgent_case();return 0;}
    hold_mode=!strcmp(argv[3],"hold-mode");hold_template=!strcmp(argv[3],"hold-template");
    engine_link_performance_ready(false);raw_midi_flush(MIDI_RAW_SOURCE_ALL);
    assert(engine_link_bus_write(1023,0));
    if(!strcmp(argv[3],"priority")) {
        assert(engine_link_ks_mode(true));ks_service();ks_service();
        assert(!s_ks_inflight.sequence && !s_ks_panic_pending && !ks_dds_silent());
        unsigned before=ks_commands;engine_link_ks_note_off(0);ks_service();
        assert(ks_commands==before+1 && last_ks_op==KS_NOTE_OFF);
        ks_service();before=ks_commands;engine_link_ks_panic();ks_service();
        assert(ks_commands==before+1 && last_ks_op==KS_PANIC);
        puts("three-engine completion: Off and Panic bypass the cached preparation-health interval PASS");return 0;
    }
    if(target==2 || engine_fm_control_active(&s_fm.status))assert(engine_link_fm_mode(target==2?1:0));
    assert(engine_link_ks_mode(target==1));
    for(unsigned n=0;n<3000;n++) {
        tick();raw_midi_state_t raw;raw_midi_state(&raw);assert(!raw.enabled);
        if(hold_mode && mode_commits)break;
        if(routes_settled(target))break;
    }
    if(hold_mode) {
        assert(mode_commits && s_ks.switching);for(unsigned n=0;n<5;n++)tick();
        raw_midi_state_t raw;raw_midi_state(&raw);assert(!raw.enabled && !template_commits);
        hold_mode=false;apply_mode(held_mode);
        for(unsigned n=0;n<3000 && !routes_settled(target);n++)tick();
    }
    engine_ks_state_t ks;engine_link_ks_state(&ks);
    if(ks.switching || ks.recovering || ks.applied_mode!=(target==1))fprintf(stderr,"switch stalled: requested=%u applied=%u fault=%u recoveries=%u wait=%u shadow=%u\n",ks.requested_mode,ks.applied_mode,ks.fault,ks.recovery_count,ks.wait_reason,ks.shadow_gate_pending);
    assert(!ks.switching && !ks.recovering && ks.applied_mode==(target==1));
    if(s_fm.status.switching || s_fm.status.recovering)fprintf(stderr,"FM stalled: reason=%u selected=%u requested=%u applied=%u switching=%u recovering=%u needs_select=%u mode_pending=%u panic=%u\n",s_fm.status.reason,s_fm.status.selected,s_fm.status.requested_backend,s_fm.status.applied_backend,s_fm.status.switching,s_fm.status.recovering,s_fm.status.needs_select,s_fm_mode_pending,s_fm_panic.sequence_id);
    assert(!s_fm.status.switching && !s_fm.status.recovering && (s_fm.status.applied_backend!=0)==(target==2));

    midi_template_image_t image={0};midi_template_link_publish(&image);engine_link_performance_ready(true);
    for(unsigned n=0;n<3000;n++) {
        tick();raw_midi_state_t raw;raw_midi_state(&raw);
        if(hold_template && template_commits==2) {
            assert(!raw.enabled);regs[0x0707]=regs[0x80ff];
            for(unsigned delay=0;delay<5;delay++) {tick();raw_midi_state(&raw);assert(!raw.enabled);}
            regs[0x0708]=regs[0xb3ff];hold_template=false;
        }
        if(raw.enabled && raw.ready)break;
    }
    raw_midi_state_t raw;raw_midi_state(&raw);midi_template_link_state_t image_state;midi_template_link_state(&image_state);
    assert(image_state.ready && image_state.applied_generation && raw.enabled && raw.ready);
    assert(!raw.stop_pending && !raw.fault);
    assert(!additive_frames && "three-engine build must never probe or write removed additive hardware");
    unsigned previous=template_updates();
    for(unsigned v=0;v<32;v++)assert(engine_link_bus_write(16+v,4000));
    change_template(target,&image);
    midi_template_link_publish(&image);
    for(unsigned n=0;n<3000;n++) {
        tick();midi_template_link_state(&image_state);raw_midi_state(&raw);
        assert(raw.enabled && "ordinary parameter changes must preserve performance enable");
        if(image_state.ready && template_updates()>previous)break;
    }
    assert(image_state.ready && template_updates()>previous && !additive_frames);
    uint32_t stop=raw_midi_request_stop_all();
    for(unsigned n=0;n<1000;n++){tick();raw_midi_state(&raw);if(raw.stop_completed==stop)break;}
    assert(raw.stop_completed==stop && !raw.stop_pending && !additive_frames);

    printf("three-engine completion: %u -> %u (%s), setup=%u us, MODE/SELECT + bank + dual template ACK + RAW re-enable PASS\n",initial,target,argv[3],io_setup);
    return 0;
}
