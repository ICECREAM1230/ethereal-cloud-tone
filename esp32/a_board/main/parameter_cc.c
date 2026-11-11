#include "parameter_cc.h"

#include <limits.h>
#include <math.h>

static int32_t duty_from_cc(uint8_t value)
{
    double duty = 0.5 * pow(10.0, -(double)value / 127.0);
    return (int32_t)((0.5-duty)*16777216.0);
}

static uint16_t lfo_rate_from_cc(uint8_t value)
{
    double hz = 0.03 * pow(1000.0, (double)value / 127.0);
    long increment = (long)(hz*16777216.0/48000.0+0.5);
    return (uint16_t)(increment < 1 ? 1 : increment > 0xffff ? 0xffff : increment);
}

typedef int32_t (*law_fn)(uint8_t value, const patch_t *patch);
static uint8_t nearest(int32_t target, law_fn law, const patch_t *patch)
{
    uint8_t best = 0;
    int64_t error = INT64_MAX;
    for (unsigned value = 0; value < 128; ++value) {
        int64_t delta = (int64_t)law((uint8_t)value, patch) - target;
        if (delta < 0) delta = -delta;
        if (delta < error) { error = delta; best = (uint8_t)value; }
    }
    return best;
}

static int32_t coarse_law(uint8_t v, const patch_t *p)
{ (void)p; return (v * 25) / 128 - 12; }
static int32_t fine_law(uint8_t v, const patch_t *p)
{ (void)p; return (((int)v - 64) * 2) / 3; }
static int32_t duty_law(uint8_t v, const patch_t *p)
{ (void)p; return duty_from_cc(v); }
static int32_t three_law(uint8_t v, const patch_t *p)
{ (void)p; int n=(v*3)>>7; return n>2?2:n; }
static int32_t detune_law(uint8_t v, const patch_t *p)
{ (void)p; return v>>2; }
static int32_t resonance_law(uint8_t v, const patch_t *p)
{ (void)p; return ((int32_t)v*5632)/127; }
static int32_t rate_law(uint8_t v, const patch_t *p)
{ (void)p; return lfo_rate_from_cc(v); }
static int32_t env_depth_law(uint8_t v, const patch_t *p)
{ (void)p; int d=(int)v-64; return d*(d<0?-d:d)*4; }
static int32_t lfo2_depth_law(uint8_t v, const patch_t *p)
{
    return p->lfo[1].dest==1 ? (int32_t)v<<5 :
           p->lfo[1].dest==2 ? (int32_t)v<<2 :
           p->lfo[1].dest==3 ? (int32_t)v<<5 : (int32_t)v<<4;
}

parameter_cc_state_t parameter_cc_state_from_patch(const patch_t *patch,
                                                    int32_t cutoff_offset,
                                                    int32_t resonance_offset)
{
    (void)patch;
    int32_t code = cutoff_offset + 8192;
    if (code < 0) code = 0;
    if (code > 16383) code = 16383;
    return (parameter_cc_state_t){
        .cutoff_coarse=(uint8_t)(code>>7), .cutoff_fine=(uint8_t)(code&127),
        .cutoff_offset=cutoff_offset, .resonance_offset=resonance_offset};
}

uint32_t parameter_cc_apply(patch_t *p, parameter_cc_state_t *s,
                            uint8_t cc, uint8_t v)
{
    switch (cc) {
    case 7: p->volume=(uint8_t)(v<127?v<<1:0xfe); return PARAM_CC_D_GAIN;
    case 10:p->pan=(int8_t)((int)v-64); return PARAM_CC_D_RENDER;
    case 14:p->osc[0].coarse=(int16_t)coarse_law(v,p); return PARAM_CC_D_RENDER;
    case 15:p->osc[0].fine=(int16_t)fine_law(v,p); return PARAM_CC_D_RENDER;
    case 20:p->osc[0].wave=(waveform_t)(v>>5); return PARAM_CC_D_RENDER;
    case 21:p->osc[1].wave=(waveform_t)(v>>5); return PARAM_CC_D_RENDER;
    case 22:p->osc[1].coarse=(int16_t)coarse_law(v,p); return PARAM_CC_D_RENDER;
    case 23:p->osc[1].fine=(int16_t)fine_law(v,p); return PARAM_CC_D_RENDER;
    case 24:p->osc_mix=(int8_t)((int)v-64); return PARAM_CC_D_RENDER;
    case 25:p->osc[0].duty=duty_from_cc(v); return PARAM_CC_D_RENDER;
    case 26:p->voice_struct=(voice_struct_t)three_law(v,p); return PARAM_CC_D_RENDER;
    case 27:p->unison_detune=(int16_t)(v>>2); return PARAM_CC_D_RENDER;
    case 28:p->unison_stereo=(int16_t)v; return PARAM_CC_D_RENDER;
    case 29:p->filter.type=(uint8_t)three_law(v,p); return PARAM_CC_D_RENDER;
    case 30:p->filter.dual=v>=64; return PARAM_CC_D_RENDER;
    case 31:p->filter.key_track=(int16_t)v; return PARAM_CC_D_RENDER;
    case 71:s->resonance_offset=resonance_law(v,p)-(int32_t)p->filter.resonance;
            return PARAM_CC_RESONANCE_WRITE;
    case 72:p->env[0].release=(uint8_t)((127-v)<<1); return PARAM_CC_D_ENV;
    case 73:p->env[0].attack=(uint8_t)((127-v)<<1); return PARAM_CC_D_ENV;
    case 74:s->cutoff_coarse=v; s->cutoff_offset=(((int32_t)v<<7)|s->cutoff_fine)-8192;
            return PARAM_CC_D_CUT;
    case 75:p->env[0].decay=(uint8_t)((127-v)<<1); return PARAM_CC_D_ENV;
    case 76:p->lfo[0].rate=lfo_rate_from_cc(v); return PARAM_CC_D_LFO;
    case 77:p->lfo[0].depth=(int16_t)(v<<2); return PARAM_CC_D_LFO;
    case 79:p->env[0].sustain=(uint8_t)(v<<1); return PARAM_CC_D_ENV;
    case 85:p->osc[1].duty=duty_from_cc(v); return PARAM_CC_D_RENDER;
    case 86:p->vel_amp_amt=v; return PARAM_CC_D_ENV;
    case 87:p->vel_mod_amt=v; return PARAM_CC_D_ENV2;
    case 102:p->env[1].attack=(uint8_t)((127-v)<<1); return PARAM_CC_D_ENV2;
    case 103:p->env[1].decay=(uint8_t)((127-v)<<1); return PARAM_CC_D_ENV2;
    case 104:p->env[1].sustain=(uint8_t)(v<<1); return PARAM_CC_D_ENV2;
    case 105:p->env[1].release=(uint8_t)((127-v)<<1); return PARAM_CC_D_ENV2;
    case 106:s->cutoff_fine=v; s->cutoff_offset=(((int32_t)s->cutoff_coarse<<7)|v)-8192;
             return PARAM_CC_D_CUT;
    case 107:p->env1_depth=(int16_t)env_depth_law(v,p); return PARAM_CC_D_ENV2;
    case 109:p->lfo[1].rate=lfo_rate_from_cc(v); return PARAM_CC_D_LFO2;
    case 110:p->lfo[1].depth=(int16_t)lfo2_depth_law(v,p); return PARAM_CC_D_LFO2;
    case 111:p->lfo[1].shape=(uint8_t)(v>>5); return PARAM_CC_D_LFO2;
    case 112:p->lfo[1].dest=(uint8_t)((v*4)>>7); return PARAM_CC_D_LFO2;
    case 113:p->lfo[0].shape=(uint8_t)(v>>5); return PARAM_CC_D_LFO;
    default:return 0;
    }
}

bool parameter_cc_read(const patch_t *p, const parameter_cc_state_t *s,
                       uint8_t cc, uint8_t *v)
{
    int32_t actual;
    switch (cc) {
    case 7:*v=p->volume>=0xfe?127:(uint8_t)(p->volume>>1); return true;
    case 10:actual=p->pan+64; break;
    case 14:*v=nearest(p->osc[0].coarse,coarse_law,p); return true;
    case 15:*v=nearest(p->osc[0].fine,fine_law,p); return true;
    case 20:actual=(int32_t)p->osc[0].wave<<5; break;
    case 21:actual=(int32_t)p->osc[1].wave<<5; break;
    case 22:*v=nearest(p->osc[1].coarse,coarse_law,p); return true;
    case 23:*v=nearest(p->osc[1].fine,fine_law,p); return true;
    case 24:actual=p->osc_mix+64; break;
    case 25:*v=nearest(p->osc[0].duty,duty_law,p); return true;
    case 26:*v=nearest(p->voice_struct,three_law,p); return true;
    case 27:*v=nearest(p->unison_detune,detune_law,p); return true;
    case 28:actual=p->unison_stereo; break;
    case 29:*v=nearest(p->filter.type,three_law,p); return true;
    case 30:*v=p->filter.dual?127:0; return true;
    case 31:actual=p->filter.key_track; break;
    case 71:*v=nearest((int32_t)p->filter.resonance+s->resonance_offset,resonance_law,p); return true;
    case 72:actual=127-(p->env[0].release>>1); break;
    case 73:actual=127-(p->env[0].attack>>1); break;
    case 74:*v=s->cutoff_coarse; return true;
    case 75:actual=127-(p->env[0].decay>>1); break;
    case 76:*v=nearest(p->lfo[0].rate,rate_law,p); return true;
    case 77:actual=p->lfo[0].depth>>2; break;
    case 79:actual=p->env[0].sustain>>1; break;
    case 85:*v=nearest(p->osc[1].duty,duty_law,p); return true;
    case 86:actual=p->vel_amp_amt; break;
    case 87:actual=p->vel_mod_amt; break;
    case 102:actual=127-(p->env[1].attack>>1); break;
    case 103:actual=127-(p->env[1].decay>>1); break;
    case 104:actual=p->env[1].sustain>>1; break;
    case 105:actual=127-(p->env[1].release>>1); break;
    case 106:*v=s->cutoff_fine; return true;
    case 107:*v=nearest(p->env1_depth,env_depth_law,p); return true;
    case 109:*v=nearest(p->lfo[1].rate,rate_law,p); return true;
    case 110:*v=nearest(p->lfo[1].depth,lfo2_depth_law,p); return true;
    case 111:actual=(int32_t)p->lfo[1].shape<<5; break;
    case 112:actual=(int32_t)p->lfo[1].dest<<5; break;
    case 113:actual=(int32_t)p->lfo[0].shape<<5; break;
    default:return false;
    }
    if (actual < 0) actual = 0;
    if (actual > 127) actual = 127;
    *v = (uint8_t)actual;
    return true;
}

size_t parameter_cc_dump(const patch_t *p, const parameter_cc_state_t *s,
                         parameter_cc_emit_fn emit, void *user)
{
    static const uint8_t controls[]={7,10,14,15,20,21,22,23,24,25,26,27,28,
        29,30,31,71,72,73,74,75,76,77,79,85,86,87,102,103,104,105,106,107,
        109,111,112,110,113};
    for(size_t i=0;i<sizeof controls;i++){uint8_t v;parameter_cc_read(p,s,controls[i],&v);emit(controls[i],v,user);}
    return sizeof controls;
}
