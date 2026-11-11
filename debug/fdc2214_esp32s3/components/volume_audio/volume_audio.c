#include "volume_audio.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "sine_table.h"

static float clamp01(float x) { return x<0 ? 0 : (x>1 ? 1:x); }
static uint32_t distance_u32(uint32_t a,uint32_t b) { return a>b ? a-b:b-a; }

vc_config_t vc_default_config(void)
{
    return (vc_config_t){.raw_loud=13246586,.raw_mute=13174639,.timeout_us=343000,
        .noise_sigma=985.4551f,.smoothing_ms=30,.tone_hz=440,.channel=1,.curve=2,.level_percent=25};
}

bool vc_config_valid(const vc_config_t *c)
{
    return c && c->raw_loud>0 && c->raw_loud<VC_RAW_LIMIT && c->raw_mute>0 && c->raw_mute<VC_RAW_LIMIT
        && distance_u32(c->raw_loud,c->raw_mute)>=64
        && isfinite(c->noise_sigma) && c->noise_sigma>=0
        && distance_u32(c->raw_loud,c->raw_mute)>=20.0f*c->noise_sigma
        && isfinite(c->smoothing_ms) && c->smoothing_ms>=0 && c->smoothing_ms<=2000
        && c->timeout_us>=10000 && c->timeout_us<=10000000 && c->channel<4
        && (c->curve==1 || c->curve==2) && c->level_percent<=100 && c->tone_hz>=20 && c->tone_hz<=2000;
}

bool vc_init(vc_state_t *s,const vc_config_t *config)
{
    if (!s || !vc_config_valid(config)) return false;
    vc_config_t copy=*config;
    memset(s,0,sizeof(*s));s->config=copy;s->quality=VC_NO_SAMPLE;
    s->pending[0]=copy.raw_loud;s->pending[1]=copy.raw_mute;
    return true;
}

static uint32_t median(uint32_t *a,size_t n)
{
    for (size_t i=1;i<n;i++) {
        uint32_t v=a[i];size_t j=i;
        while (j && a[j-1]>v) {a[j]=a[j-1];j--;}
        a[j]=v;
    }
    return n&1 ? a[n/2]:(uint32_t)(((uint64_t)a[n/2-1]+a[n/2])/2);
}

static void finish_capture(vc_state_t *s)
{
    uint32_t value=median(s->capture,VC_CAPTURE_COUNT);
    for (unsigned i=0;i<VC_CAPTURE_COUNT;i++) s->capture[i]=distance_u32(s->capture[i],value);
    float sigma=1.4826f*(float)median(s->capture,VC_CAPTURE_COUNT);
    unsigned lane=s->capture_target-1;
    s->pending[lane]=value;s->pending_noise[lane]=sigma;
    s->pending_mask|=(uint8_t)(1u<<lane);s->capture_target=0;s->capture_result=VC_CAPTURE_DONE;
}

bool vc_update(vc_state_t *s,uint64_t now,const vc_sample_t *sample)
{
    if (!s || !vc_config_valid(&s->config) || (s->have_time && now<=s->update_us)) return false;
    float dt=s->have_time ? (float)(now-s->update_us)/1000000.0f:0;
    if (s->have_time && now-s->update_us>s->config.timeout_us) {s->gain=0;dt=0;}
    s->have_time=true;s->update_us=now;
    if (s->capture_target && now>s->capture_deadline) {
        s->capture_target=0;s->capture_result=VC_CAPTURE_FAILED;
    }
    if (sample) {
        if (sample->hard_error) {
            s->quality=VC_FAULT;s->transport_fault=true;
            if (s->capture_target) {s->capture_target=0;s->capture_result=VC_CAPTURE_FAILED;}
        } else if (sample->time_us>now || (s->have_seen && sample->time_us<=s->seen_us)) {
            /* A duplicate never refreshes sample age or calibration count. */
        } else {
            s->have_seen=true;s->seen_us=sample->time_us;
            if (sample->flags>3 || !sample->raw || sample->raw>=VC_RAW_LIMIT || now-sample->time_us>s->config.timeout_us) s->quality=VC_INVALID;
            else if (sample->flags&2) s->quality=VC_WATCHDOG;
            else {
                bool reset=!s->have_sample || sample->time_us-s->sample_us>s->config.timeout_us;
                float sample_dt=s->have_sample ? (float)(sample->time_us-s->sample_us)/1000000.0f:0;
                if (reset) {for (unsigned i=0;i<3;i++) s->median[i]=sample->raw;s->median_next=0;s->primed=false;}
                s->median[s->median_next]=sample->raw;s->median_next=(uint8_t)((s->median_next+1)%3);
                uint32_t values[3];memcpy(values,s->median,sizeof(values));
                uint32_t filtered=median(values,3);
                int64_t numerator=(int64_t)filtered-s->config.raw_loud;
                int64_t denominator=(int64_t)s->config.raw_mute-s->config.raw_loud;
                float u=clamp01((float)numerator/(float)denominator);
                if (!s->primed || s->config.smoothing_ms==0) s->position=u;
                else s->position+=(sample_dt/(s->config.smoothing_ms/1000+sample_dt))*(u-s->position);
                s->primed=true;s->raw=sample->raw;s->sample_us=sample->time_us;s->have_sample=true;s->transport_fault=false;
                s->quality=sample->flags&1 ? VC_AW:VC_OK;
                if (s->position>=0.985f) s->volume_muted=true;
                else if (s->position<=0.95f) s->volume_muted=false;
                if (s->capture_target && sample->time_us>=s->capture_deadline-UINT64_C(5000000)) {
                    s->capture[s->capture_count++]=sample->raw;
                    if (sample->flags&1) s->capture_aw++;
                    if (s->capture_count==VC_CAPTURE_COUNT) finish_capture(s);
                }
            }
        }
    }
    bool stale=!s->have_sample || now-s->sample_us>s->config.timeout_us;
    if (stale && s->have_sample) s->quality=VC_STALE;
    float target=0;
    if (s->mode==VC_REFERENCE) target=1;
    else if (s->mode==VC_SENSOR && !stale && !s->transport_fault && !s->volume_muted) {
        target=1-s->position;
        if (s->config.curve==2) target*=target;
    }
    float step=10*dt;
    s->gain+=fmaxf(-step,fminf(step,target-s->gain));s->gain=clamp01(s->gain);
    return true;
}

bool vc_execute(vc_state_t *s,const vc_command_t *c,uint64_t now)
{
    if (!s || !c) return false;
    switch(c->kind) {
    case VC_SHOW:case VC_EXPORT:return true;
    case VC_MODE:if (c->a>VC_SENSOR) return false;s->mode=(vc_mode_t)c->a;return true;
    case VC_LEVEL:if (c->a>100) return false;s->config.level_percent=(uint8_t)c->a;return true;
    case VC_TONE:if (c->a<20 || c->a>2000) return false;s->config.tone_hz=(uint16_t)c->a;return true;
    case VC_CURVE:if (c->a!=1 && c->a!=2) return false;s->config.curve=(uint8_t)c->a;return true;
    case VC_CHANNEL:
        if (c->a>3) return false;
        s->config.channel=(uint8_t)c->a;s->mode=VC_OFF;s->have_sample=false;s->have_seen=false;
        s->primed=false;s->calibrated=false;s->pending_mask=0;s->capture_target=0;s->capture_result=VC_CAPTURE_IDLE;
        return true;
    case VC_LOUD:case VC_MUTE:
        if (now>UINT64_MAX-UINT64_C(5000000)) return false;
        s->capture_target=c->kind==VC_LOUD ? 1:2;s->capture_count=0;s->capture_aw=0;
        s->pending_mask&=(uint8_t)~(1u<<(s->capture_target-1));
        s->capture_deadline=now+UINT64_C(5000000);s->capture_result=VC_CAPTURE_ACTIVE;return true;
    case VC_RANGE:
        if (!c->a || !c->b || c->a>=VC_RAW_LIMIT || c->b>=VC_RAW_LIMIT || distance_u32(c->a,c->b)<64) return false;
        s->pending[0]=c->a;s->pending[1]=c->b;s->pending_noise[0]=0;s->pending_noise[1]=0;
        s->pending_mask=3;s->capture_target=0;s->capture_result=VC_CAPTURE_IDLE;return true;
    case VC_APPLY: {
        if (s->pending_mask!=3 || s->capture_target) return false;
        vc_config_t next=s->config;
        next.raw_loud=s->pending[0];next.raw_mute=s->pending[1];next.noise_sigma=fmaxf(s->pending_noise[0],s->pending_noise[1]);
        if (!vc_config_valid(&next)) return false;
        s->config=next;s->have_sample=false;s->have_seen=false;s->primed=false;s->gain=0;
        s->calibrated=true;s->volume_muted=false;s->quality=VC_NO_SAMPLE;return true;
    }
    default:return false;
    }
}

static bool number(const char *s,uint32_t *out)
{
    if (!s || *s<'0' || *s>'9') return false;
    char *end=NULL;errno=0;unsigned long value=strtoul(s,&end,0);
    if (errno || !end || *end || value>UINT32_MAX) return false;
    *out=(uint32_t)value;return true;
}

bool vc_parse_command(int argc,const char *const argv[],vc_command_t *out)
{
    if (argc<2 || !argv || !out) return false;
    vc_command_t c={0};
    if (!strcmp(argv[0],"audio")) {
        if (argc==2) {
            c.kind=VC_MODE;
            if (!strcmp(argv[1],"off")) c.a=VC_OFF;
            else if (!strcmp(argv[1],"reference")) c.a=VC_REFERENCE;
            else if (!strcmp(argv[1],"sensor") || !strcmp(argv[1],"on")) c.a=VC_SENSOR;
            else if (!strcmp(argv[1],"show")) c.kind=VC_SHOW;
            else return false;
        } else if (argc==3 && number(argv[2],&c.a)) {
            if (!strcmp(argv[1],"level") && c.a<=100) c.kind=VC_LEVEL;
            else if (!strcmp(argv[1],"tone") && c.a>=20 && c.a<=2000) c.kind=VC_TONE;
            else return false;
        } else return false;
    } else if (!strcmp(argv[0],"vol")) {
        if (argc==2) {
            if (!strcmp(argv[1],"show")) c.kind=VC_SHOW;
            else if (!strcmp(argv[1],"export")) c.kind=VC_EXPORT;
            else if (!strcmp(argv[1],"apply")) c.kind=VC_APPLY;
            else return false;
        } else if (argc==3 && !strcmp(argv[1],"capture")) {
            if (!strcmp(argv[2],"loud")) c.kind=VC_LOUD;
            else if (!strcmp(argv[2],"mute")) c.kind=VC_MUTE;
            else return false;
        } else if (argc==3 && !strcmp(argv[1],"channel") && number(argv[2],&c.a) && c.a<4) c.kind=VC_CHANNEL;
        else if (argc==3 && !strcmp(argv[1],"curve")) {
            c.kind=VC_CURVE;
            if (!strcmp(argv[2],"linear")) c.a=1;
            else if (!strcmp(argv[2],"square")) c.a=2;
            else return false;
        } else if (argc==4 && !strcmp(argv[1],"range") && number(argv[2],&c.a) && number(argv[3],&c.b)
                   && c.a>0 && c.b>0 && c.a<VC_RAW_LIMIT && c.b<VC_RAW_LIMIT && distance_u32(c.a,c.b)>=64) c.kind=VC_RANGE;
        else return false;
    } else return false;
    *out=c;return true;
}

vc_status_t vc_status(const vc_state_t *s,uint64_t now)
{
    vc_status_t out={.config=s->config,.mode=s->mode,.quality=s->quality,.raw=s->raw,
        .pending_loud=s->pending[0],.pending_mute=s->pending[1],.pending_mask=s->pending_mask,
        .capture_result=s->capture_result,.capture_count=s->capture_count,.capture_aw=s->capture_aw,
        .calibrated=s->calibrated,.position=s->position,.gain_q16=(uint32_t)(s->gain*65536+0.5f),
        .age_us=s->have_sample && now>=s->sample_us ? now-s->sample_us:UINT64_MAX,
        .signal_gain=s->gain*(float)s->config.level_percent/100};
    return out;
}

const char *vc_quality_name(vc_quality_t q)
{
    static const char *const names[]={"missing","ok","amplitude_warning","watchdog","invalid","fault","stale"};
    return (unsigned)q<sizeof(names)/sizeof(names[0]) ? names[q]:"invalid";
}

bool vc_render(vc_oscillator_t *osc,int16_t *lr,size_t frames,uint32_t rate,uint16_t hz,float gain)
{
    if (!osc || !lr || !rate || hz<20 || hz>2000 || (uint32_t)hz*2>=rate || !isfinite(gain) || gain<0 || gain>1) return false;
    uint32_t step=(uint32_t)(((uint64_t)hz<<32)/rate);
    float slew=10.0f/(float)rate;
    for (size_t i=0;i<frames;i++) {
        osc->gain+=fmaxf(-slew,fminf(slew,gain-osc->gain));
        int16_t value=(int16_t)((float)vc_sine[osc->phase>>24]*osc->gain);
        lr[2*i]=value;lr[2*i+1]=value;osc->phase+=step;
    }
    return true;
}

bool vc_s3_pins_valid(const int pins[4],const int fdc[4],int tx,int rx)
{
    if (!pins || !fdc) return false;
    for (unsigned i=0;i<4;i++) {
        int p=pins[i];if (i==0 && p==-1) continue;
        if (p<0 || p>48 || p==0 || p==3 || p==16 || p==19 || p==20 || (p>=22 && p<=37) || p==43 || p==44 || p==45 || p==46 || p==tx || p==rx) return false;
        for (unsigned j=0;j<i;j++) if (pins[j]==p) return false;
        for (unsigned j=0;j<4;j++) if (fdc[j]>=0 && fdc[j]==p) return false;
    }
    return true;
}
