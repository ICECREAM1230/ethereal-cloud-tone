#define main previous_ae_engine_entry
#include "test_ae350_engine.c"
#undef main
static control_source_ref_t claim_source(void)
{control_source_ref_t source;assert(control_source_claim(CONTROL_SOURCE_WIFI,fake_time,&source));return source;}
int main(void)
{
    control_source_init();fake_time=1000;
    regs[0x700]=MIDI_RAW_ABI_MAGIC;regs[0x70a]=7;regs[0x718]=AE350_CPU_MAGIC;regs[0x719]=2;regs[0x71c]=64;
    raw_midi_init();engine_link_init();ticks(30);
    regs[0x07b0]=UINT32_C(0x414f5231);regs[0x07b1]=0x10000;
    regs[0x07b2]=1;regs[0x07b3]=4;audio_routes_init();ticks(30);
    control_source_ref_t source=claim_source(),other;
    assert(engine_link_audio_routes_request_source(source,2,0));unsigned before=audio_writes;
    assert(!control_source_claim(CONTROL_SOURCE_BLE_CTRL,fake_time,&other));
    control_source_revoke(source);audio_routes_service(fake_time+1000,true);
    assert(!s_audio_routes.pending && s_audio_routes.error && audio_writes==before);
    source=claim_source();assert(engine_link_audio_routes_request_source(source,3,1));
    audio_routes_service(fake_time+1000,true);audio_routes_service(fake_time+2000,true);
    assert(audio_writes==before+1);
    fake_time+=2000;audio_routes_service(fake_time+2000,true);
    assert(!s_audio_routes.pending && !s_audio_routes.error);
    assert(control_source_claim(CONTROL_SOURCE_BLE_CTRL,fake_time,&other));
    source=claim_source();s_cpu_mode=(engine_cpu_mode_state_t){.supported=true,.valid=true,.ready=true};
    regs[CPU_MODE_STATUS_REG]=0x100000a0;
    assert(engine_link_cpu_mode_request_source(source,2,77));before=writes;
    control_source_revoke(source);cpu_mode_service(fake_time+1000);
    assert(!s_cpu_mode.pending && s_cpu_mode.result==FM_RESULT_CANCELLED && writes==before);
    source=claim_source();s_fdc=(engine_fdc_state_t){.available=true,.valid=true,.fresh=true,
        .word=FDC_STATUS_TAG|3,.observed_us=fake_time};s_fdc_changed=fake_time;
    regs[FDC_STATUS_REG]=FDC_STATUS_TAG|3;
    fdc_request_t fdc={.op=FDC_ENABLE,.event={.c=81,.expires=fake_time+1000000}};
    assert(engine_link_fdc_request_source(source,&fdc));before=writes;
    control_source_revoke(source);fdc_service(fake_time+1000,true);
    assert(!s_fdc.pending && s_fdc.result==FDC_CANCELLED && !s_fdc.disable_pending && writes==before);
    source=claim_source();fdc.op=FDC_DISABLE;fdc.event.c++;
    assert(engine_link_fdc_request_source(source,&fdc));assert(s_fdc.disable_pending);
    control_source_revoke(source);fdc_service(fake_time+1000,true);
    assert(!s_fdc.pending && !s_fdc.disable_pending && writes==before);
    source=claim_source();reverb_init();
    s_reverb=(engine_reverb_state_t){.available=true,.valid=true};s_reverb_status=REVERB_STATUS_READY;
    assert(engine_link_reverb_request_source(source,123,456,91));
    s_reverb_job.verified=true;s_reverb_phase=RV_COMMIT;
    control_source_revoke(source);reverb_service(fake_time+1000);
    assert(!s_reverb_job.active && !s_reverb_job.committed && writes==before);
    puts("PASS source native jobs: audio/CPU/FDC/RVR unsent fences, anonymous completion, no sensor disable or protocol write");
    return 0;
}
