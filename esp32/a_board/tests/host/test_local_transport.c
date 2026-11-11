// Real RAW transport through the actual three-engine single-owner service.
#define main previous_local_engine_main
#include "test_local_engine.c"
#undef main
int main(void)
{
    fixture(2);engine_link_init();engine_link_local_ack(local().generation);
    for(unsigned n=0;n<20;n++)tick();
    raw_midi_state_t raw;raw_midi_state(&raw);
    assert(raw.enabled && raw.ready);
    engine_link_remote_request();tick();
    assert(local().remote_pending);
    regs[0x070c]=2|(2u<<2)|(1u<<8)|(1u<<9)|(15u<<16);
    for(unsigned n=0;n<20;n++)tick();
    raw_midi_state(&raw);
    assert(local().remote_ready && !local().remote_pending);
    assert(!raw.enabled && !s_raw_enable_requested && "REMT must actually disable RAW before remote initialization");
    assert(raw.initialized && !raw.inflight && !raw_sources_flushing(&raw));
    puts("local transport: late-boot RAW enable -> REMT -> actual disable/flush without removed-engine probe PASS");
}
