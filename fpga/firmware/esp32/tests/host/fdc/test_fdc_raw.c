#define main previous_link_main
#include "test_fdc_link.c"
#undef main
static void prepare_raw(void)
{
    reset();raw_supported=true;
    raw_peer[0]=0x12345678;raw_peer[1]=0xc1234567;raw_peer[2]=0xd3456789;
    raw_peer[3]=65536u|(1u<<17)|(0x42u<<24);
}
static void advance(unsigned n){for(unsigned i=0;i<n;i++)tick();}
int main(void)
{
    previous_link_main();
    reset();advance(8);assert(!state().raw.supported && !state().raw.valid && !writes);
    prepare_raw();advance(4);engine_fdc_raw_state_t t=state().raw;
    assert(t.supported && t.valid && t.fresh && t.sample_valid && t.sample_fresh);
    assert(t.revision==0x12345678 && t.sequence==0x42 && t.raw0==0x1234567 && t.raw1==0x3456789);
    assert(t.quality0==0 && t.quality1==1 && t.gain_q16==65536 && t.enabled && !writes);
    fake_time+=600000;assert(!state().raw.fresh);
    prepare_raw();raw_torn=true;advance(4);assert(!state().raw.valid && state().valid && !writes);
    prepare_raw();raw_data_error=true;advance(4);assert(!state().raw.valid && state().valid && !writes);
    prepare_raw();raw_peer[3]=65537;advance(4);assert(!state().raw.valid);
    prepare_raw();raw_peer[3]|=1u<<23;advance(4);assert(!state().raw.valid);
    prepare_raw();raw_peer[2]^=1u<<30;advance(4);assert(!state().raw.valid);
    prepare_raw();raw_peer[0]=0;advance(4);assert(!state().raw.valid);
    prepare_raw();raw_peer[1]=raw_peer[2]=0;advance(4);t=state().raw;
    assert(t.valid && !t.sample_valid && !t.sample_fresh && !t.fresh);
    prepare_raw();advance(2);assert(s_fdc_raw_phase==2);
    fake_time+=FDC_FRESH_US+1;status^=0x100;advance(2);assert(!state().raw.valid);
    prepare_raw();advance(4);assert(state().raw.valid);fdc_unavailable();assert(!state().raw.valid && !state().raw.supported);
    prepare_raw();fdc_request_t r=job(FDC_ENABLE,99);assert(engine_link_fdc_request(&r));tick();
    assert(writes==1 && address[0]==0xb500 && data[0][0]==1);
    puts("PASS FDC_RAW coherent/read-only/old-FPGA/torn/timeout/stale/range/reset/control-priority");return 0;
}
