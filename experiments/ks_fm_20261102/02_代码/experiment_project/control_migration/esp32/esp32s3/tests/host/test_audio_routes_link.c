#define main previous_ae_engine_main
#include "test_ae350_engine.c"
#undef main
int main(void)
{
    previous_ae_engine_main();
    // The parent regression deliberately leaves RAW in reset/link-loss.
    // Start this independent AOR scenario from a fresh real owner lifecycle.
    memset(regs,0,sizeof regs);io_ok=true;hold_flush=false;auto_ack=true;raw_pending=0;
    audio_drop=false;audio_uncertain=false;
    regs[0x700]=MIDI_RAW_ABI_MAGIC;regs[0x70a]=7;regs[0x718]=AE350_CPU_MAGIC;regs[0x719]=2;regs[0x71c]=64;
    raw_midi_init();engine_link_init();
    regs[0x07b0]=UINT32_C(0x414f5231);regs[0x07b1]=UINT32_C(0x10000);
    regs[0x07b2]=1;regs[0x07b3]=4;regs[0x07b4]=0;regs[0x07b7]=0;
    audio_routes_init();ticks(30);
    engine_audio_routes_state_t state;engine_link_audio_routes_state(&state);
    assert(state.available&&state.valid&&state.ready&&state.pt8211==0&&state.pcm_a==1);
    for(unsigned hp=0;hp<4;hp++)for(unsigned a=0;a<4;a++) {
        unsigned before=audio_writes;
        if(!engine_link_audio_routes_request(hp,a)) {
            engine_link_audio_routes_state(&state);raw_midi_state_t rs=raw_state();
            fprintf(stderr,"request rejected hp=%u a=%u available=%u valid=%u ready=%u busy=%u pending=%u error=%u seq=%u gen=%u raw avail=%u init=%u enabled=%u fault=%u reset=%u\n",hp,a,state.available,state.valid,state.ready,state.busy,state.pending,state.error,state.sequence,state.generation,rs.available,rs.initialized,rs.enabled,rs.fault,rs.reset_count);
            assert(!"route request unexpectedly rejected");
        }
        assert(!engine_link_audio_routes_request(0,0));
        ticks(25);engine_link_audio_routes_state(&state);
        assert(!state.pending&&!state.error&&state.pt8211==hp&&state.pcm_a==a);
        assert(audio_writes==before+1);
    }
    assert(!engine_link_audio_routes_request(4,0));
    audio_uncertain=true;unsigned before=audio_writes;
    assert(engine_link_audio_routes_request(0,1));ticks(25);engine_link_audio_routes_state(&state);
    assert(!state.pending&&!state.error&&audio_writes==before+1);
    audio_drop=true;before=audio_writes;
    assert(engine_link_audio_routes_request(2,2));ticks(1100);engine_link_audio_routes_state(&state);
    assert(!state.pending&&state.error&&audio_writes==before+1);
    audio_drop=false;audio_uncertain=false;before=audio_writes;
    assert(engine_link_audio_routes_request(1,0));
    regs[0x07b3]=4;regs[0x07b4]=regs[0x07b7]=0; // Reset before the owner sends.
    ticks(3);engine_link_audio_routes_state(&state);
    assert(!state.pending&&state.error&&audio_writes==before);
    ticks(30);before=audio_writes;
    assert(engine_link_audio_routes_request(2,0));
    audio_routes_unavailable(); // RAW reset/identity loss at generation zero.
    ticks(3);engine_link_audio_routes_state(&state);
    assert(!state.pending&&state.error&&audio_writes==before);
    ticks(30);audio_drop=true;
    assert(engine_link_audio_routes_request(0,2));ticks(2);
    engine_link_audio_routes_state(&state);assert(state.pending);
    before=audio_writes;audio_routes_unavailable();ticks(3);engine_link_audio_routes_state(&state);
    assert(!state.pending&&state.error&&audio_writes==before);
    audio_drop=false;ticks(30);before=audio_writes;
    assert(engine_link_audio_routes_request(3,0));regs[0x07b0]=0;ticks(3);engine_link_audio_routes_state(&state);
    assert(!state.pending&&state.error&&audio_writes==before);
    io_ok=false;audio_routes_unavailable();assert(!engine_link_audio_routes_request(0,0));
    puts("PASS AOR1 link: 16 pairs; uncertain/no resend; timeout; unsent/sent/zero-generation reset and identity loss cancel");
    return 0;
}
