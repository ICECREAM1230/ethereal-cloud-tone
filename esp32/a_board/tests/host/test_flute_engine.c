#define FIFTH_ENGINE_TEST
#define main previous_three_engine_main
#include "test_three_engine.c"
#undef main
static void flute_fixture(unsigned initial)
{
    regs[0x07b0]=ENGINE_FLUTE_CAPABILITY;regs[0x07b1]=ENGINE_FLUTE_VERSION;
    regs[0x07b4]=1u|(initial==4?2u:0u);regs[0x07b5]=1;
    fixture(initial);
    assert(s_flute.available && s_local.engine_mask==23);
    assert(s_local.requested==initial && s_local.applied==initial);
}
static bool done(unsigned target)
{return routes_settled(target) && !flute_blocked_locked() && s_flute.applied_mode==(target==4);}
int main(int argc,char **argv)
{
    assert(argc==3 || argc==4);if(argc==4)io_setup=strtoul(argv[3],0,10);
    unsigned initial=strtoul(argv[1],0,10),target=strtoul(argv[2],0,10);
    flute_fixture(initial);
    if(initial==4 && target==0) {
        regs[0x07b4]|=16u;assert(flute_readback() && s_flute.fault);
    }
    engine_link_performance_ready(false);raw_midi_flush(MIDI_RAW_SOURCE_ALL);
    assert(engine_link_bus_write(1023,0));
    if(initial==0 && target==4)s_flute_sequence=0x7fffffffu;
    assert(engine_link_flute_mode(target==4));
    if(target==2 || engine_fm_control_active(&s_fm.status))assert(engine_link_fm_mode(target==2?1:0));
    assert(engine_link_ks_mode(target==1));
    if(target==4 && initial!=4)hold_flute=true;
    for(unsigned n=0;n<3000;n++) {
        tick();
        if(hold_flute && s_flute_pending_word)break;
        if(done(target) && engine_link_control_idle())break;
    }
    if(hold_flute) {
        uint32_t sent=s_flute_pending_word;
        if(!sent)fprintf(stderr,"FLU pending absent: %u->%u setup%u fake=%lld flags=%u fault=%u ks=%u/%u/%u fm=%u/%u silent=%u flush=%u swap=%u wait=%u\n",initial,target,io_setup,(long long)fake_time,s_flute.flags,s_flute.fault,s_ks.applied_mode,s_ks.switching,s_ks.fault,s_fm.status.selected,s_fm.status.switching,ks_dds_silent(),s_flushing,s_swap_requested,s_ks.wait_reason);
        assert(sent && (sent>>1));
        if(initial==0)assert((sent>>1)==1u);
        regs[0x07b3]=sent^1u;regs[0x07b4]=3;
        for(unsigned n=0;n<5;n++)tick();
        raw_midi_state_t raw;raw_midi_state(&raw);assert(s_flute.switching && !raw.enabled);
        regs[0x07b3]=sent;hold_flute=false;
    }
    for(unsigned n=0;n<3000;n++){tick();if(done(target) && engine_link_control_idle())break;}
    if(!done(target))fprintf(stderr,"flute failed %u->%u f=%u/%u/%u fault=%u ks=%u/%u/%u fm=%u/%u pending=%u\n",initial,target,s_flute.requested_mode,s_flute.applied_mode,s_flute.switching,s_flute.fault,s_ks.applied_mode,s_ks.switching,s_ks.fault,s_fm.status.selected,s_fm.status.switching,s_flute_pending_word);
    assert(done(target) && engine_link_control_idle());
    midi_template_image_t image={0};midi_template_link_publish(&image);engine_link_performance_ready(true);
    raw_midi_state_t raw;
    for(unsigned n=0;n<3000;n++){tick();raw_midi_state(&raw);if(raw.enabled && raw.ready)break;}
    assert(raw.enabled && raw.ready);
    assert(raw_midi_claim(MIDI_RAW_SOURCE_WEB,123));
    uint8_t notes[]={0x90,60,100,0x80,60,0,0x90,84,100,0xb0,64,127,0x80,84,0,0xb0,64,0};
    assert(raw_midi_submit(MIDI_RAW_SOURCE_WEB,123,notes,sizeof(notes),fake_time+1000000));
    for(unsigned n=0;n<100;n++)tick();
    raw_midi_state(&raw);assert(!raw.inflight && !raw.fault && !additive_frames);
    if(target==0) {
        regs[0x07b4]=17;assert(flute_readback());fake_time+=20000;tick();raw_midi_state(&raw);
        assert(s_flute.fault && !flute_blocked_locked() && raw.enabled && engine_link_control_idle());
        assert(engine_link_flute_mode(false) && !s_flute.switching);
    }
    if(target==4) {
        assert(engine_link_flute_mode(false));
        s_flute.fault=3;
        s_flute_pending_word=0x1234; // Unknown pending remote command loses to a physical revision.
        regs[0x070c]=(1u<<10)|(1u<<11)|(1u<<6)|(1u<<9)|(23u<<16);
        regs[0x070d]=regs[0x070e]=2;regs[0x07b4]=3;
        tick();assert(s_local.applied==4 && s_local.adopted && !s_flute_pending_word);
    }
    printf("FLU1 %u -> %u: coherent ACK/RAW readiness, MIDI60/84, local priority PASS\n",initial,target);
}
