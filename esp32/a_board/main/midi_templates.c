// Copyright (c) 2026 ICECREAM1230
/* Static performance compiler for the FPGA-owned MIDI runtime.
 * DDS arithmetic adapted from voice_alloc.c (voice_program, build_voicing,
 * amp_depth_for, mod_depth_for), Copyright 2026 Thor H. Linloekken <thj@thj.no>,
 * License: MIT. Layout: interfaces/midi_control/dds-template.md and
 * ks-fm-template.md, frozen 2026-10-16. KS numerical source: ks_params.c;
 * FM expression conversion: fm_voice.c fm_web_volume/fm_web_pan. */
#include "midi_templates.h"
#include "ks_params.h"
#include "sub_patch1.h"

#include <string.h>

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

static uint32_t velocity_gain(unsigned velocity, unsigned amount)
{
    uint32_t curve = velocity * 65536u / 127u;
    return 65536u - amount * (65536u - curve) / 127u;
}

bool midi_templates_compile(const patch_t *p, unsigned decay_ms,
                            midi_template_image_t *out)
{
    if (!p || !out || decay_ms < 200 || decay_ms > 8000 ||
        (p->voice_struct != VOICE_2_PLAIN && p->voice_struct != VOICE_7_PLUS_1 &&
         p->voice_struct != VOICE_4_PLUS_4)) return false;
    memset(out,0,sizeof(*out));

    const int8_t spread7[] = {-3,-2,-1,0,1,2,3};
    const int8_t spread4[] = {-3,-1,1,3};
    unsigned active = p->voice_struct == VOICE_2_PLAIN ? 2 : 8;
    uint32_t mode = ((uint32_t)(p->filter.dual & 1) << 16)
                  | ((uint32_t)(p->filter.type & 3) << 17);
    for (unsigned u=0;u<active;u++) {
        unsigned osc;
        int16_t detune = 0;
        bool lean = false;
        switch (p->voice_struct) {
        case VOICE_2_PLAIN: osc = u; break;
        case VOICE_7_PLUS_1:
            osc = u == 7; lean = u != 7;
            if (lean) detune = (int16_t)(spread7[u] * p->unison_detune);
            break;
        default:
            osc = u >= 4; lean = true;
            detune = (int16_t)(spread4[u % 4] * p->unison_detune);
            break;
        }
        const osc_t *o = &p->osc[osc];
        /* Do not subtract a rendered note60 value: its original pitch clamp
         * loses negative/large offsets, poisoning the other MIDI notes. */
        int32_t offset = (int32_t)o->coarse * 1024 / 12 + o->fine + detune;
        if (offset < -131072 || offset > 131071) return false;
        int volume = 207 + (active <= 2 ? 8 : active <= 4 ? 4 : 0);
        if (!osc && p->osc_mix > 0) volume -= p->osc_mix;
        if (osc && p->osc_mix < 0) volume += p->osc_mix;
        bool balance_mute = (!osc && p->osc_mix >= 63) || (osc && p->osc_mix <= -63);
        int left = clamp(volume - (p->pan > 0 ? p->pan : 0),1,255);
        int right = clamp(volume - (p->pan < 0 ? -p->pan : 0),1,255);
        unsigned spread = p->unison_stereo & 127;
        bool left_enabled = !lean || !(u & 1) || spread < 127;
        bool right_enabled = !lean || (u & 1) || spread < 127;
        if (lean && spread < 127) {
            if (u & 1) left -= spread >> 1;
            else right -= spread >> 1;
            if (left < 1) left = 1;
            if (right < 1) right = 1;
        }
        uint32_t l = left_enabled && !balance_mute && p->pan < 63 ? (uint32_t)left : 0;
        uint32_t r = right_enabled && !balance_mute && p->pan > -63 ? (uint32_t)right : 0;
        out->elements[u][0] = (uint32_t)offset;
        out->elements[u][1] = (uint32_t)o->duty & 0xffffffu;
        out->elements[u][2] = p->filter.resonance & 0x3fffu;
        out->elements[u][3] = l | (r << 8) | mode;
        out->elements[u][4] = 0x100u | ((uint32_t)o->wave & 3u);
    }
    out->key_track = (uint32_t)p->filter.key_track;
    out->bend_range = p->bend_range & 127u;
    /* Optional expression extension in element 0's reserved words. Zero
     * disables each route; the old image size and SPI upload are unchanged. */
    out->elements[0][5] = (uint32_t)(int32_t)p->wheel_vibrato_depth;
    out->elements[0][6] = (uint32_t)(int32_t)p->touch_cutoff_depth;
    out->elements[0][7] = (uint32_t)(int32_t)p->touch_resonance_depth;
    for (unsigned value=0;value<128;value++) {
        out->amp_depth[value] = (uint32_t)(((int64_t)0x2800 * velocity_gain(value,p->vel_amp_amt)) >> 16);
        int64_t mod = ((int64_t)p->env1_depth * velocity_gain(value,p->vel_mod_amt)) >> 16;
        out->mod_depth[value] = (uint32_t)(int32_t)mod & 0x3ffffu;
        engine_ks_note_t note;
        if (ks_params_note(value,127,decay_ms,p->volume,p->pan,1,&note)) {
            out->ks_notes[value][0] = 0x80000000u | note.length;
            out->ks_notes[value][1] = ((uint32_t)note.loss << 16) | (uint16_t)note.tuning;
        }
        uint16_t left,right;
        int pan = clamp((int)p->pan + (int)value - 64,-64,63);
        ks_params_gains(p->volume,(int8_t)pan,&left,&right);
        out->ks_pan[value] = (uint32_t)left | ((uint32_t)right << 16);
    }
    unsigned fm_volume = p->volume / 2;
    out->fm_expression = (fm_volume > 127 ? 127 : fm_volume)
                       | ((uint32_t)clamp(64+p->pan,0,127) << 8);
    return true;
}

bool midi_templates_compile_snapshot(const midi_patch_snapshot_t *s,unsigned decay_ms,
                                    bool full_sub_patch,midi_template_image_t *out)
{
    if(!s || !out)return false;
    const patch_t *p=&s->patch;
    if(full_sub_patch && (s->cutoff_offset< -131072 || s->cutoff_offset>131071 ||
       s->resonance_offset< -131072 || s->resonance_offset>131071 || p->env1_dest ||
       p->lfo[0].shape>3 || p->lfo[1].shape>3 || p->lfo[1].dest>3 ||
       p->lfo[0].depth<0 || p->lfo[1].depth<0))return false;
    if(!midi_templates_compile(p,decay_ms,out))return false;
    if(!full_sub_patch)return true;
    out->elements[1][5]=SUB_PATCH1_MAGIC;
    out->elements[1][6]=SUB_PATCH1_VERSION;
    out->elements[1][7]=SUB_PATCH1_FIELD_MASK;
    out->elements[2][5]=patch_adsr_rate1(&p->env[0]);
    out->elements[2][6]=patch_adsr_rate2(&p->env[0]);
    out->elements[2][7]=patch_adsr_rate1(&p->env[1]);
    out->elements[3][5]=patch_adsr_rate2(&p->env[1]);
    out->elements[3][6]=14u|((uint32_t)p->lfo[0].shape<<4)|(2u<<6)|((uint32_t)p->lfo[0].rate<<16);
    out->elements[3][7]=(uint32_t)(int32_t)p->lfo[0].depth;
    static const uint8_t bus[]={1,3,2,4};
    out->elements[4][5]=14u|((uint32_t)p->lfo[1].shape<<4)|((uint32_t)bus[p->lfo[1].dest]<<6)|((uint32_t)p->lfo[1].rate<<16);
    out->elements[4][6]=(uint32_t)(int32_t)p->lfo[1].depth;
    out->elements[4][7]=(uint32_t)(-10240+((int32_t)p->volume-207)*64);
    out->elements[5][5]=(uint32_t)s->cutoff_offset;
    out->elements[5][6]=p->env1_dest;
    out->elements[5][7]=(uint32_t)s->resonance_offset;
    return true;
}
