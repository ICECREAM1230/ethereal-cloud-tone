#include "theremin_control.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#expr); exit(1); } } while(0)

static theremin_t make_controller(void)
{
    theremin_t state;
    theremin_config_t config=theremin_default_config();
    config.pitch=(theremin_calibration_t){20000,10000,1.0f};
    config.volume=(theremin_calibration_t){40000,30000,1.0f};
    CHECK(theremin_init(&state,&config));
    return state;
}

static theremin_output_t step(theremin_t *state, uint64_t time,
                              uint32_t pitch, uint32_t volume,
                              uint8_t pf, uint8_t vf, bool ph, bool vh)
{
    theremin_sample_t p={pitch,pf,true,ph},v={volume,vf,true,vh};
    theremin_output_t out;
    CHECK(theremin_update(state,time,&p,&v,&out));
    return out;
}

static theremin_output_t settle(theremin_t *state, uint64_t start,
                                uint32_t pitch, uint32_t volume)
{
    theremin_output_t out;
    for (unsigned i=0;i<=200;i++)out=step(state,start+(uint64_t)i*10000,pitch,volume,0,0,false,false);
    return out;
}

static void test_acceptance(void)
{
    theremin_t s=make_controller();
    theremin_output_t out=step(&s,0,15000,35000,1,1,false,false);
    CHECK(out.pitch_accepted && out.volume_accepted);
    CHECK(out.pitch_quality==THEREMIN_AMPLITUDE_WARNING && out.pitch_code==5888);
    out=step(&s,10000,15000,35000,3,1,false,false);
    CHECK(!out.pitch_accepted && out.volume_accepted);
    CHECK(out.pitch_quality==THEREMIN_WATCHDOG);
    out=step(&s,20000,15000,35000,1,0,true,false);
    CHECK(!out.pitch_accepted && out.pitch_quality==THEREMIN_HARD_ERROR);
    s.config.accept_amplitude_warning=false;
    out=step(&s,30000,15000,35000,1,0,false,false);
    CHECK(!out.pitch_accepted && out.volume_accepted);
    CHECK(out.pitch_quality==THEREMIN_WARNING_REJECTED);
    theremin_sample_t failed={0,0,false,true};
    CHECK(theremin_update(&s,40000,&failed,NULL,&out));
    CHECK(out.pitch_quality==THEREMIN_HARD_ERROR && !out.pitch_accepted);
}

static void test_ranges(void)
{
    theremin_t s=make_controller();
    theremin_output_t out=settle(&s,0,20000,40000);
    CHECK(out.pitch_code==4864 && out.pitch_delta==-1024 && out.gain_q16==65536);
    out=settle(&s,2010000,10000,30000);
    CHECK(out.pitch_code>=6911 && out.pitch_code<=6912 && out.gain_q16==0);
    s=make_controller();out=settle(&s,0,15000,35000);
    CHECK(out.pitch_code==5888 && out.pitch_delta==0 && out.gain_q16==16384);
    s=make_controller();out=settle(&s,0,25000,45000);
    CHECK(out.pitch_code==4864 && out.gain_q16==65536);
    s=make_controller();out=settle(&s,0,5000,25000);
    CHECK(out.pitch_code==6912 && out.gain_q16==0);
    theremin_config_t cfg=s.config;
    cfg.pitch=(theremin_calibration_t){10000,20000,1.0f};
    CHECK(theremin_init(&s,&cfg));out=settle(&s,0,25000,40000);
    CHECK(out.pitch_code==6912);
}

static void test_spikes_and_independent_lanes(void)
{
    theremin_t s=make_controller();
    step(&s,0,20000,40000,0,0,false,false);
    step(&s,10000,20000,40000,0,0,false,false);
    theremin_output_t out=step(&s,20000,10000,40000,1,1,false,false);
    CHECK(out.pitch_code==4864);
    out=step(&s,30000,20000,40000,0,0,false,false);
    CHECK(out.pitch_code==4864);
    for (unsigned i=4;i<30;i++)out=step(&s,(uint64_t)i*10000,10000,40000,1,0,false,true);
    CHECK(out.pitch_code>6800 && out.pitch_accepted && !out.volume_accepted);
    CHECK(!out.volume_stale);
    for (unsigned i=30;i<70;i++)out=step(&s,(uint64_t)i*10000,10000,40000,1,0,false,true);
    CHECK(out.volume_stale && out.gain_q16==0);
}

static void test_bad_inputs_and_timeout(void)
{
    static const uint32_t bad[]={0,THEREMIN_RAW_LIMIT,UINT32_MAX};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        theremin_t s=make_controller();step(&s,0,15000,35000,0,0,false,false);
        theremin_output_t out=step(&s,200000,bad[i],35000,0,0,false,false);
        CHECK(!out.pitch_accepted && out.pitch_age_us==200000);
    }
    theremin_t s=make_controller();theremin_output_t out;
    step(&s,10000,15000,40000,0,0,false,false);
    CHECK(!theremin_update(&s,10000,NULL,NULL,&out));
    CHECK(!theremin_update(&s,9999,NULL,NULL,&out));
    out=settle(&s,20000,15000,40000);
    CHECK(out.gain_q16==65536);
    CHECK(theremin_update(&s,2600000,NULL,NULL,&out));
    CHECK(out.pitch_stale && out.volume_stale && out.gain_q16==0 && out.pitch_code==5888);
    out=step(&s,2700000,10000,30000,0,0,false,false);
    CHECK(out.pitch_u>.99f && out.volume_u>.99f);
    CHECK(!theremin_update(NULL,2800000,NULL,NULL,&out));
    CHECK(!theremin_update(&s,2800000,NULL,NULL,NULL));
    s=make_controller();out=settle(&s,0,15000,40000);
    CHECK(out.gain_q16==65536);
    out=step(&s,3000000,15000,40000,0,0,false,false);
    CHECK(out.gain_q16==0); /* Scheduler gap: restart fade rather than jumping loud. */
    out=step(&s,3010000,15000,40000,0,0,false,false);
    CHECK(out.gain_q16>0 && out.gain_q16<=6554);
}

static void test_config_and_rebaseline(void)
{
    theremin_t s=make_controller();theremin_config_t c=s.config;
    c.pitch.raw_near=c.pitch.raw_far;CHECK(!theremin_init(&s,&c));
    c=s.config;c.pitch.raw_near=c.pitch.raw_far-1;CHECK(!theremin_init(&s,&c));
    c=s.config;c.pitch.noise_sigma=1000.0f;CHECK(!theremin_init(&s,&c));
    c=s.config;c.min_cutoff_hz=0.0f;CHECK(!theremin_init(&s,&c));
    c=s.config;c.beta=NAN;CHECK(!theremin_init(&s,&c));
    c=s.config;c.beta=-1.0f;CHECK(!theremin_init(&s,&c));
    c=s.config;c.stale_timeout_us=0;CHECK(!theremin_init(&s,&c));
    c=s.config;c.pitch_min=c.pitch_max;CHECK(!theremin_init(&s,&c));
    c=s.config;c.mute_open=.99f;c.mute_close=.9f;CHECK(!theremin_init(&s,&c));
    CHECK(s.config.pitch.raw_far==20000);
    CHECK(!theremin_rebaseline(&s,1,40000));
    CHECK(s.config.pitch.raw_far==20000);
    CHECK(theremin_rebaseline(&s,22000,42000));
    CHECK(s.config.pitch.raw_near==12000 && s.config.volume.raw_near==32000);
    theremin_output_t out=settle(&s,0,17000,37000);
    CHECK(out.pitch_code==5888 && out.gain_q16==16384);
    CHECK(theremin_init(&s,&s.config)); /* Alias-safe reinitialization. */
    out=settle(&s,0,17000,37000);CHECK(out.pitch_code==5888);
}

static void test_jitter_held_note_and_time_rates(void)
{
    theremin_t s=make_controller();double sum=0.0,squares=0.0;unsigned n=0;
    for (unsigned i=0;i<1200;i++) {
        uint32_t raw=i%4<2 ? 15080u:14920u;
        theremin_output_t out=step(&s,(uint64_t)i*1000000/142,raw,35000,1,1,false,false);
        if(i>500){sum+=out.pitch_code;squares+=(double)out.pitch_code*out.pitch_code;n++;}
    }
    double mean=sum/n,variance=squares/n-mean*mean;
    CHECK(sqrt(variance)<8.2 && fabs(mean-5888.0)<25.0);
    s=make_controller();theremin_output_t out=settle(&s,0,15000,35000);
    for(unsigned i=201;i<=1200;i++)out=step(&s,(uint64_t)i*10000,15000,35000,0,0,false,false);
    CHECK(out.pitch_code==5888 && out.gain_q16==16384);
    for(unsigned rate=10;rate<=142;rate+=132) {
        s=make_controller();step(&s,0,20000,40000,0,0,false,false);
        for(unsigned i=1;i<=rate;i++)out=step(&s,(uint64_t)i*1000000/rate,10000,30000,0,0,false,false);
        CHECK(out.pitch_code>6900 && out.gain_q16==0);
    }
    s=make_controller();s.config.pitch_slew_octaves_per_s=1.0f;
    out=step(&s,0,20000,40000,0,0,false,false);
    for(unsigned i=1;i<20;i++) {
        uint16_t previous=out.pitch_code;
        out=step(&s,(uint64_t)i*10000,10000,40000,0,0,false,false);
        CHECK((int)out.pitch_code-(int)previous<=11);
    }
}

static void test_mute_and_long_timestamp(void)
{
    theremin_t s=make_controller();theremin_output_t out=settle(&s,0,15000,30100);
    CHECK(out.muted && out.gain_q16==0);
    out=settle(&s,2010000,15000,30300);CHECK(out.muted && out.gain_q16==0);
    out=settle(&s,4020000,15000,30600);CHECK(!out.muted && out.gain_q16>100);
    s=make_controller();uint64_t time=UINT64_C(0x100000000)+200;
    out=step(&s,time,15000,35000,0,0,false,false);
    out=step(&s,time+10000,15000,35000,0,0,false,false);
    CHECK(out.pitch_age_us==0 && out.pitch_code==5888);
}

int main(void)
{
    test_acceptance();test_ranges();test_spikes_and_independent_lanes();
    test_bad_inputs_and_timeout();test_config_and_rebaseline();
    test_jitter_held_note_and_time_rates();test_mute_and_long_timestamp();
    printf("PASS %u C checks\n",checks);
    return 0;
}
