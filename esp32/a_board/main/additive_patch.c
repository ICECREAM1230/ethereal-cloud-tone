// Copyright (c) 2026 ICECREAM1230
/* SPI layout and controlled defaults from additive/rtl/additive_control_bridge.sv
 * (MIT), current project 2026-10-19. No existing engine parameters
 * are reused: additive owns its envelopes, spectrum and complete shadow. */
#include "additive_patch.h"
#include <string.h>

void additive_patch_default(additive_patch_t *p)
{
    if(!p)return;
    memset(p,0,sizeof(*p));
    p->bypass_lowpass=true;p->master_gain=16384;p->master_pan=32768;p->bend_range=2;
    const uint16_t sustain[]={49152,32768,16384,8192},release[]={8,12,20,32};
    for(unsigned g=0;g<ADDITIVE_ENVELOPE_GROUPS;g++)
        p->env[g]=(additive_envelope_t){2048,8,sustain[g],release[g]};
    for(unsigned k=0;k<ADDITIVE_PARTIALS;k++)
        p->partials[k]=(additive_partial_t){(k+1)*65536u,(uint16_t)(65535u/(k+1)),(uint8_t)(k/8)};
}

bool additive_patch_validate(const additive_patch_t *p)
{
    if(!p || p->centroid_q8>8191 || p->comb_period>31 || p->bend_range>127)return false;
    for(unsigned k=0;k<ADDITIVE_PARTIALS;k++)
        if(p->partials[k].ratio_q16>0xffffffu || p->partials[k].group>3)return false;
    return true;
}

bool additive_patch_word(const additive_patch_t *p,bool selected,unsigned i,uint16_t *address,uint32_t *word)
{
    if(!p || !address || !word || i>=ADDITIVE_SHADOW_WORDS)return false;
    *address=(uint16_t)(i<32 ? 0xa000u+i : 0xa100u+i-32);
    *word=0;
    switch(i) {
    case 0:*word=(uint32_t)selected | ((uint32_t)p->bypass_lowpass<<1);break;
    case 1:*word=p->centroid_q8 | ((uint32_t)p->slope<<16);break;
    case 2:*word=p->comb_period | ((uint32_t)p->comb_depth<<16);break;
    case 3:*word=p->master_gain | ((uint32_t)p->master_pan<<16);break;
    case 4:*word=p->bend_range;break;
    default:
        if(i>=16 && i<20)*word=p->env[i-16].attack_inc | ((uint32_t)p->env[i-16].decay_inc<<16);
        else if(i>=20 && i<24)*word=p->env[i-20].sustain | ((uint32_t)p->env[i-20].release_inc<<16);
        else if(i>=32) {
            const additive_partial_t *partial=&p->partials[(i-32)/4];
            if((i-32)%4==0)*word=partial->ratio_q16;
            else if((i-32)%4==1)*word=partial->weight | ((uint32_t)partial->group<<16);
        }
    }
    return true;
}
