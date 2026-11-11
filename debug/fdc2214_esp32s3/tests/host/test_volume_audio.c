#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "volume_audio.h"

static unsigned checks;
#define CHECK(x) do {checks++;if (!(x)) {fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);abort();}} while (0)
static vc_state_t make(void)
{
    vc_config_t c=vc_default_config();c.raw_loud=2000;c.raw_mute=1000;c.noise_sigma=0;c.smoothing_ms=0;
    vc_state_t s;CHECK(vc_init(&s,&c));return s;
}
static void command(vc_state_t *s,vc_command_kind_t kind,uint32_t a,uint32_t b,uint64_t now)
{
    vc_command_t c={kind,a,b};CHECK(vc_execute(s,&c,now));
}
static void feed(vc_state_t *s,uint64_t *clock,uint32_t raw,uint8_t flags,unsigned count)
{
    for (unsigned i=0;i<count;i++) {
        *clock+=10000;vc_sample_t sample={.time_us=*clock,.raw=raw,.flags=flags};
        CHECK(vc_update(s,*clock,&sample));
    }
}
static void configuration(void)
{
    vc_config_t c=vc_default_config();
    CHECK(c.channel==1 && c.curve==2 && c.level_percent==25);
    CHECK(c.raw_loud==13246586 && c.raw_mute==13174639 && vc_config_valid(&c));
    vc_state_t s=make(),before=s;
    c.raw_mute=c.raw_loud;CHECK(!vc_init(&s,&c));CHECK(!memcmp(&s,&before,sizeof(s)));
    c=vc_default_config();c.smoothing_ms=NAN;CHECK(!vc_config_valid(&c));
    c=vc_default_config();c.noise_sigma=INFINITY;CHECK(!vc_config_valid(&c));
    CHECK(s.mode==VC_OFF && s.gain==0);
}
static void mapping(void)
{
    vc_state_t s=make();uint64_t t=0;
    command(&s,VC_MODE,VC_SENSOR,0,t);
    feed(&s,&t,2000,0,20);CHECK(fabsf(s.gain-1)<.0001f);
    feed(&s,&t,1500,1,20);CHECK(fabsf(s.gain-.25f)<.0001f);CHECK(s.quality==VC_AW);
    command(&s,VC_CURVE,1,0,t);feed(&s,&t,1500,0,20);CHECK(fabsf(s.gain-.5f)<.0001f);
    feed(&s,&t,1000,0,20);CHECK(s.gain==0);
    command(&s,VC_RANGE,1000,2000,t);command(&s,VC_APPLY,0,0,t);
    feed(&s,&t,1000,0,20);CHECK(fabsf(s.gain-1)<.0001f);
    feed(&s,&t,2000,0,20);CHECK(s.gain==0);
    vc_status_t status=vc_status(&s,t);CHECK(status.gain_q16==0 && status.calibrated);
}
static void freshness(void)
{
    vc_state_t s=make();uint64_t t=0;
    command(&s,VC_MODE,VC_SENSOR,0,t);feed(&s,&t,2000,0,20);
    uint64_t accepted=s.sample_us;vc_sample_t bad={.time_us=t+10000,.raw=2000,.flags=2};
    t+=10000;CHECK(vc_update(&s,t,&bad));CHECK(s.sample_us==accepted && s.quality==VC_WATCHDOG);
    bad=(vc_sample_t){.time_us=t+2000000,.raw=2000};t+=10000;
    CHECK(vc_update(&s,t,&bad));CHECK(s.sample_us==accepted);
    vc_state_t before=s;CHECK(!vc_update(&s,t,NULL));CHECK(!memcmp(&s,&before,sizeof(s)));
    t+=s.config.timeout_us+1;CHECK(vc_update(&s,t,NULL));CHECK(s.gain==0 && s.quality==VC_STALE);
    command(&s,VC_MODE,VC_REFERENCE,0,t);
    for (unsigned i=0;i<20;i++) {t+=10000;CHECK(vc_update(&s,t,NULL));}
    CHECK(fabsf(s.gain-1)<.0001f); /* reference checks the DAC without FDC */
    command(&s,VC_LEVEL,100,0,t);CHECK(fabsf(vc_status(&s,t).signal_gain-1)<.0001f);
    command(&s,VC_MODE,VC_OFF,0,t);
    for (unsigned i=0;i<20;i++) {t+=10000;CHECK(vc_update(&s,t,NULL));}
    CHECK(s.gain==0);
}
static void calibration(void)
{
    vc_state_t s=make();uint64_t t=0;
    command(&s,VC_LOUD,0,0,t);
    feed(&s,&t,2000,1,127);feed(&s,&t,999999,1,1);
    CHECK(s.capture_result==VC_CAPTURE_DONE && s.pending[0]==2000 && s.capture_aw==128);
    command(&s,VC_MUTE,0,0,t);feed(&s,&t,1000,0,128);
    CHECK(s.pending_mask==3 && s.pending[1]==1000);
    command(&s,VC_APPLY,0,0,t);CHECK(s.calibrated && s.config.raw_loud==2000 && s.config.raw_mute==1000);
    vc_state_t before=s;vc_command_t bad={VC_RANGE,1000,1010};
    CHECK(!vc_execute(&s,&bad,t));CHECK(!memcmp(&s,&before,sizeof(s)));
    command(&s,VC_LOUD,0,0,t);
    t+=10000;vc_sample_t sample={.time_us=t,.raw=1800};CHECK(vc_update(&s,t,&sample));
    for (unsigned i=0;i<10;i++) {t+=10000;CHECK(vc_update(&s,t,&sample));}
    CHECK(s.capture_count==1);
    t+=5000001;CHECK(vc_update(&s,t,NULL));CHECK(s.capture_result==VC_CAPTURE_FAILED);
    before=s;vc_command_t apply={VC_APPLY,0,0};CHECK(!vc_execute(&s,&apply,t));CHECK(!memcmp(&s,&before,sizeof(s)));
    command(&s,VC_CHANNEL,2,0,t);CHECK(s.mode==VC_OFF && !s.calibrated && !s.have_sample);
}
static void capture_noise_and_fault(void)
{
    vc_state_t s=make();uint64_t t=0;
    command(&s,VC_LOUD,0,0,1000000);
    vc_sample_t old={.time_us=950000,.raw=2000};
    CHECK(vc_update(&s,1010000,&old));CHECK(s.capture_count==0);
    s=make();
    command(&s,VC_LOUD,0,0,t);
    for (unsigned i=0;i<128;i++) feed(&s,&t,2000+(i%2)*200,0,1);
    command(&s,VC_MUTE,0,0,t);
    for (unsigned i=0;i<128;i++) feed(&s,&t,1000+(i%2)*200,0,1);
    vc_state_t before=s;vc_command_t apply={VC_APPLY,0,0};
    CHECK(!vc_execute(&s,&apply,t));CHECK(!memcmp(&before,&s,sizeof(s)));
    command(&s,VC_LOUD,0,0,t);
    vc_sample_t fault={.hard_error=true};t+=10000;CHECK(vc_update(&s,t,&fault));
    CHECK(s.capture_result==VC_CAPTURE_FAILED && s.transport_fault);
}
static void parsing_and_pins(void)
{
    const char *good[][4]={{"audio","reference"},{"audio","level","100"},{"audio","tone","440"},
        {"vol","capture","loud"},{"vol","range","2000","1000"},{"vol","curve","square"},{"vol","channel","3"},{"vol","export"}};
    const int counts[]={2,3,3,3,4,3,3,2};
    vc_command_t c;
    for (unsigned i=0;i<8;i++) CHECK(vc_parse_command(counts[i],good[i],&c));
    const char *bad[][4]={{"audio","level","-1"},{"audio","level","101"},{"audio","tone","nan"},
        {"vol","channel","4"},{"vol","range","1000","1010"},{"vol","range","429496729600","1000"}};
    for (unsigned i=0;i<6;i++) {
        c=(vc_command_t){VC_TONE,123,456};vc_command_t copy=c;
        CHECK(!vc_parse_command(i>=4 ? 4:3,bad[i],&c));CHECK(!memcmp(&c,&copy,sizeof(c)));
    }
    int pins[]={15,5,6,7},fdc[]={18,8,4,17};CHECK(vc_s3_pins_valid(pins,fdc,43,44));
    pins[0]=-1;CHECK(vc_s3_pins_valid(pins,fdc,43,44));
    pins[0]=15;pins[1]=8;CHECK(!vc_s3_pins_valid(pins,fdc,43,44));
    pins[1]=16;CHECK(!vc_s3_pins_valid(pins,fdc,43,44));
    pins[1]=5;pins[2]=5;CHECK(!vc_s3_pins_valid(pins,fdc,43,44));
    pins[2]=6;pins[3]=26;CHECK(!vc_s3_pins_valid(pins,fdc,43,44));
}
static void pcm(void)
{
    static int16_t data[VC_SAMPLE_RATE*2];vc_oscillator_t osc={0};
    CHECK(vc_render(&osc,data,VC_SAMPLE_RATE,VC_SAMPLE_RATE,440,1));
    int peak=0,crossings=0;double squares=0;
    for (unsigned i=0;i<VC_SAMPLE_RATE;i++) {
        CHECK(data[2*i]==data[2*i+1]);
        int v=abs(data[2*i]);if(v>peak)peak=v;
        if (i>VC_SAMPLE_RATE/10) {
            squares+=(double)data[2*i]*data[2*i];
            if(data[2*i-2]<=0 && data[2*i]>0) crossings++;
        }
    }
    CHECK(peak<=26214 && peak>=26200);CHECK(abs(crossings-396)<=1);
    CHECK(fabs(sqrt(squares/(VC_SAMPLE_RATE*.9-1))-26214/sqrt(2))<100);
    CHECK(vc_render(&osc,data,VC_SAMPLE_RATE,VC_SAMPLE_RATE,440,0));
    for (unsigned i=VC_SAMPLE_RATE/5;i<VC_SAMPLE_RATE;i++) CHECK(data[2*i]==0 && data[2*i+1]==0);
    CHECK(!vc_render(&osc,data,10,VC_SAMPLE_RATE,440,NAN));
}
int main(void)
{
    configuration();mapping();freshness();calibration();capture_noise_and_fault();parsing_and_pins();pcm();
    printf("PASS 7 behavior groups, %u checks\n",checks);return 0;
}
