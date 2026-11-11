#include "ks_timbre_params.h"
#include "ks_timbre_contract.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846
static double freq(unsigned n) { return 440.0*exp2(((double)n-69.0)/12.0); }
static long qr(double x, long lo, long hi) {
    long v=lround(x); return v<lo?lo:v>hi?hi:v;
}
static bool range(double x,double lo,double hi) { return isfinite(x)&&x>=lo&&x<=hi; }
static double ap_delay(double a,double w) { return 2.0*atan2((1.0-a)*sin(w*.5),(1.0+a)*cos(w*.5))/w; }
static double loss_delay(double b,double w) { return atan2(b*sin(w),1.0-b+b*cos(w))/w; }
static double loss_mag(double b,double w) { return hypot(1.0-b+b*cos(w),b*sin(w)); }
bool ks_timbre_factory(unsigned id,ks_timbre_preset_t *out) {
    if (!out || id>2) return false;
    ks_timbre_preset_t p={.instrument_id=id,.preset_schema=1,.model_version=1,.factory_revision=1,
        .pluck_position=.28,.noise_mix=.04,.noise_lowpass=.10,.velocity_brightness=.12,
        .loss_weight=.40,.dispersion=-.10,.release_ms=300,.dry=.70,
        .modes={{110,.18,.12},{240,.22,.08},{490,.13,.06},{1120,.08,.04}}};
    if(id==2) {
        p.pluck_position=.12;p.noise_mix=.28;p.noise_lowpass=.50;p.velocity_brightness=.40;
        p.loss_weight=.15;p.dispersion=-.65;p.release_ms=80;p.dry=.75;
        const ks_timbre_mode_t modes[4]={{180,.10,.10},{430,.08,.07},{1120,.05,.05},{2600,.035,.03}};
        memcpy(p.modes,modes,sizeof modes);
    }
    if(id==0) { p.loss_weight=.5;p.dispersion=0;p.release_ms=1000.0*960/KS_TIMBRE_FS; }
    *out=p;return true;
}
bool ks_timbre_validate(const ks_timbre_preset_t *p) {
    if(!p || p->instrument_id>2 || p->preset_schema!=1 || p->model_version!=1 || !p->factory_revision ||
       !range(p->pluck_position,.05,.45)||!range(p->noise_mix,0,1)||!range(p->noise_lowpass,0,1)||
       !range(p->velocity_brightness,0,1)||!range(p->loss_weight,0,.5)||!range(p->dispersion,-.8,.8)||
       !range(p->release_ms,5,2000)||!range(p->dry,0,1)) return false;
    double sum=p->dry;
    for(unsigned j=0;j<4;j++) {
        const ks_timbre_mode_t *m=&p->modes[j];
        if(!range(m->frequency_hz,20,20000)||!range(m->t60_seconds,.005,10)||!range(m->weight,0,1))return false;
        sum+=m->weight;
    }
    return sum<=1.0+1e-12;
}
static uint32_t crc_feed(uint32_t crc,uint32_t word) {
    for(int j=24;j>=0;j-=8) {
        crc^=(word>>j)&255u;
        for(unsigned b=0;b<8;b++) crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return crc;
}
uint32_t ks_timbre_crc32_words(const uint32_t *words,size_t count) {
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<count;i++)crc=crc_feed(crc,words[i]);
    return crc^0xffffffffu;
}
static uint32_t model_crc(const ks_timbre_image_t *im) {
    uint32_t crc=0xffffffffu;
    for(unsigned i=0;i<64;i++)crc=crc_feed(crc,(i==3||i==22||i==60||i==61)?0:im->header[i]);
    for(unsigned i=0;i<128;i++) {
        crc=crc_feed(crc,im->notes[i][0]);crc=crc_feed(crc,im->notes[i][1]&65535u);
    }
    return crc^0xffffffffu;
}
void ks_timbre_image_finalize(ks_timbre_image_t *im,uint32_t gen) {
    if(!im)return;
    uint32_t crc=0xffffffffu;
    for(unsigned i=0;i<128;i++)for(unsigned j=0;j<2;j++)crc=crc_feed(crc,im->notes[i][j]);
    for(unsigned i=0;i<128;i++)crc=crc_feed(crc,im->pan[i]);
    for(unsigned i=0;i<64;i++)crc=crc_feed(crc,i==61?0:im->header[i]);
    im->header[61]=crc_feed(crc,gen)^0xffffffffu;
}
bool ks_timbre_compile(const ks_timbre_preset_t *p,unsigned decay_ms,uint8_t volume,int8_t pan,uint32_t gen,ks_timbre_image_t *out) {
    if(!out||!gen||decay_ms<200||decay_ms>8000||!ks_timbre_validate(p))return false;
    ks_timbre_image_t im={0};uint32_t *h=im.header;
    h[0]=KST_MAGIC;h[1]=KST_VERSION;h[2]=p->instrument_id;h[3]=gen;h[4]=p->factory_revision;
    h[5]=KST_FS_NUMERATOR;h[6]=KST_FS_DENOMINATOR;h[21]=(uint32_t)lround(KS_TIMBRE_FS*.005);h[22]=decay_ms;h[63]=64;
    if(p->instrument_id) {
        h[7]=(uint32_t)lround(p->pluck_position*2147483648.0);
        h[8]=(uint32_t)lround(67108864.0/p->pluck_position);h[9]=(uint32_t)lround(67108864.0/(1.0-p->pluck_position));
        h[10]=(uint32_t)qr(p->noise_mix*32768,0,32768);h[11]=(uint32_t)qr(p->noise_lowpass*32768,0,32767);
        h[12]=(uint32_t)qr(p->velocity_brightness*32768,0,32768);h[13]=(uint32_t)qr(p->loss_weight*32768,0,16384);
        h[14]=(uint32_t)(int32_t)qr(p->dispersion*32768,-26214,26214);
        h[15]=(uint32_t)lround(p->release_ms*KS_TIMBRE_FS/1000);h[16]=65536/h[15];h[17]=65536%h[15];
        h[18]=(uint32_t)qr(p->dry*32768,0,32768);h[20]=KST_FLAG_BODY|KST_FLAG_SHAPED|(h[14]?KST_FLAG_DISPERSION:0);
        uint32_t remaining=32768-h[18];
        for(unsigned j=0;j<4;j++) {
            double theta=2*PI*p->modes[j].frequency_hz/KS_TIMBRE_FS;
            double r=pow(10,-3.0/(KS_TIMBRE_FS*p->modes[j].t60_seconds));
            int32_t c=(int32_t)lround(r*cos(theta)*131072),s=(int32_t)lround(r*sin(theta)*131072);
            double radius=hypot(c,s);if(radius>=131072)return false;
            h[24+j*4]=(uint32_t)c;h[25+j*4]=(uint32_t)s;h[26+j*4]=(uint32_t)floor(131072-radius);
            uint32_t weight=(uint32_t)qr(p->modes[j].weight*32768,0,32768);
            if(weight>remaining)weight=remaining;
            h[27+j*4]=weight;remaining-=weight;
        }
    } else { h[4]=1;h[13]=16384;h[15]=960;h[16]=68;h[17]=256;h[18]=32768; }
    double b=h[13]/32768.0,d=(int32_t)h[14]/32768.0;
    for(unsigned n=0;n<128;n++) {
        int pp=(int)pan+(int)n; if(pp<0)pp=0;if(pp>127)pp=127;
        double gain=volume/16.0*4096;
        uint32_t l=(uint32_t)qr(gain*sqrt((127.0-pp)/127),0,65535),r=(uint32_t)qr(gain*sqrt(pp/127.0),0,65535);
        im.pan[n]=l|(r<<16);
        if(n<19)continue;
        double f=freq(n),w=2*PI*f/KS_TIMBRE_FS,T=KS_TIMBRE_FS/f;
        double td=(h[20]&KST_FLAG_DISPERSION)?ap_delay(d,w):0;
        long N=(long)floor(T-loss_delay(b,w)-td-.5);
        if(!p->instrument_id)N=(long)floor(T-1);
        if(N<2||N>4096)continue;
        double delta=T-loss_delay(b,w)-td-N;
        if(!p->instrument_id)delta=T-.5-N;
        double aa=tan(w*.5),bb=tan(w*delta*.5);
        long tuning=qr((aa-bb)/(aa+bb)*32768,-32767,32767);
        /* Choose nearest quantized allpass around analytic solution. */
        if(p->instrument_id) {
            double best=1e100;long chosen=tuning;
            for(long candidate=tuning-2;candidate<=tuning+2;candidate++)if(candidate>=-32767&&candidate<=32767) {
                double error=fabs(N+loss_delay(b,w)+td+ap_delay(candidate/32768.0,w)-T);
                if(error<best){best=error;chosen=candidate;}
            }
            tuning=chosen;
        }
        double mag=p->instrument_id?loss_mag(b,w):cos(w*.5);
        double rho=pow(10,-3.0/(f*decay_ms/1000.0))/mag;
        uint32_t loss=(uint32_t)qr(fmin(32767.0/32768.0,rho)*32768,0,32767);
        im.notes[n][0]=0x80000000u|(uint32_t)N;im.notes[n][1]=(loss<<16)|(uint16_t)(int16_t)tuning;
        h[40+n/32]|=1u<<(n%32);
    }
    h[60]=model_crc(&im);*out=im;return true;
}
bool ks_timbre_note_metrics(const ks_timbre_image_t *im,unsigned n,double *cents,double *t60) {
    if(!im||n>127||!(im->notes[n][0]&0x80000000u)||!cents||!t60)return false;
    double target=freq(n),a=(int16_t)im->notes[n][1]/32768.0,b=im->header[13]/32768.0;
    double d=(int32_t)im->header[14]/32768.0,N=im->notes[n][0]&65535u;
    double lo=target*.8,hi=target*1.2;
    for(unsigned k=0;k<64;k++) {
        double f=(lo+hi)*.5,w=2*PI*f/KS_TIMBRE_FS;
        double delay=N+loss_delay(b,w)+ap_delay(a,w)+((im->header[20]&1)?ap_delay(d,w):0);
        if(delay*f<KS_TIMBRE_FS)lo=f;else hi=f;
    }
    double f=(lo+hi)*.5,w=2*PI*f/KS_TIMBRE_FS,rho=(im->notes[n][1]>>16)/32768.0;
    *cents=1200*log2(f/target);*t60=log(.001)/(f*log(rho*loss_mag(b,w)));return true;
}
