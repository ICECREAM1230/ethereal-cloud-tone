#include "ks_params.h"
#include <math.h>
#define KS_PI 3.14159265358979323846
static long clamp_round(double value, long low, long high)
{
    long v=lround(value);
    return v<low ? low : v>high ? high : v;
}
static double frequency(uint8_t note) { return 440.0*exp2(((double)note-69.0)/12.0); }
uint16_t ks_params_loss(uint8_t note, unsigned decay_ms)
{
    if (note<KS_MIDI_MIN || note>127 || decay_ms<200 || decay_ms>8000) return 0;
    double f=frequency(note), w=2.0*KS_PI*f/KS_SAMPLE_RATE;
    double rho=pow(10.0,-3.0/(f*(double)decay_ms/1000.0))/cos(w/2.0);
    return (uint16_t)clamp_round(fmin(32767.0/32768.0,rho)*32768.0,0,32767);
}
void ks_params_gains(uint8_t volume, int8_t pan, uint16_t *left, uint16_t *right)
{
    int p=(int)pan+64;
    if(p<0)p=0;
    if(p>127)p=127;
    double gain=(double)volume/16.0*4096.0;
    *left=(uint16_t)clamp_round(gain*sqrt((127.0-p)/127.0),0,65535);
    *right=(uint16_t)clamp_round(gain*sqrt((double)p/127.0),0,65535);
}
bool ks_params_note(uint8_t note, uint8_t velocity, unsigned decay_ms,
                    uint8_t volume, int8_t pan, uint32_t seed, engine_ks_note_t *out)
{
    if (!out || note<KS_MIDI_MIN || note>127 || !velocity || velocity>127 || decay_ms<200 || decay_ms>8000) return false;
    double f=frequency(note), period=KS_SAMPLE_RATE/f;
    long length=(long)floor(period-1.0);
    if(length<2 || length>KS_MAX_LENGTH) return false;
    double d=period-.5-(double)length, w=2.0*KS_PI*f/KS_SAMPLE_RATE;
    double a=tan(w/2.0), b=tan(w*d/2.0);
    *out=(engine_ks_note_t){.length=(uint16_t)length,
        .tuning=(int16_t)clamp_round((a-b)/(a+b)*32768.0,-32767,32767),
        .loss=ks_params_loss(note,decay_ms),.amplitude=(uint16_t)(8192u*velocity/127u),.seed=seed?seed:1};
    ks_params_gains(volume,pan,&out->gain_left,&out->gain_right);
    return true;
}
