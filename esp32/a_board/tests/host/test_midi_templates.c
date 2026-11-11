// Copyright (c) 2026 ICECREAM1230
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "midi_templates.h"
#include "ks_params.h"

/* Optional GNU ld --wrap run exercises an unavailable KS note without
 * changing the real numerical helper (all 128 notes currently fit its RAM). */
#ifdef MIDI_TEMPLATES_WRAP_KS
static bool reject_ks_note;
bool __real_ks_params_note(uint8_t, uint8_t, unsigned, uint8_t, int8_t, uint32_t, engine_ks_note_t *);
bool __wrap_ks_params_note(uint8_t note, uint8_t vel, unsigned decay,
                           uint8_t volume, int8_t pan, uint32_t seed, engine_ks_note_t *out)
{
    return !(reject_ks_note && note == 57) && __real_ks_params_note(note,vel,decay,volume,pan,seed,out);
}
#endif

/* Independent renderer oracle, transcribed from voice_alloc.c voice_program:
 * Copyright 2026 Thor H. Linloekken; MIT. Compute the old per-note
 * result before comparing it to the note-independent performance words. */
static int clamp(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
static int osc_for(unsigned shape, unsigned u)
{
    return shape == VOICE_2_PLAIN ? (int)u : shape == VOICE_7_PLUS_1 ? u == 7 : u >= 4;
}
static int spread_position(unsigned shape, unsigned u)
{
    static const int seven[] = {-3,-2,-1,0,1,2,3,0};
    static const int four[] = {-3,-1,1,3};
    return shape == VOICE_2_PLAIN ? 0 : shape == VOICE_7_PLUS_1 ? seven[u] : four[u % 4];
}
static uint32_t old_gain(const patch_t *p, unsigned u)
{
    int osc = osc_for(p->voice_struct,u);
    int vol = 207 + (p->voice_struct == VOICE_2_PLAIN ? 8 : 0);
    if (!osc && p->osc_mix > 0) vol -= p->osc_mix;
    if (osc && p->osc_mix < 0) vol += p->osc_mix;
    int l = clamp(vol - (p->pan > 0 ? p->pan : 0),1,255);
    int r = clamp(vol + (p->pan < 0 ? p->pan : 0),1,255);
    bool lean = p->voice_struct != VOICE_2_PLAIN && !(p->voice_struct == VOICE_7_PLUS_1 && u == 7);
    int spread = p->unison_stereo & 127;
    if (lean && spread < 127) {
        if (u & 1) l = clamp(l - spread / 2,1,255);
        else r = clamp(r - spread / 2,1,255);
    }
    if (p->pan >= 63 || (lean && spread == 127 && (u & 1))) l = 0;
    if (p->pan <= -63 || (lean && spread == 127 && !(u & 1))) r = 0;
    if ((!osc && p->osc_mix >= 63) || (osc && p->osc_mix <= -63)) l = r = 0;
    return (uint32_t)l | ((uint32_t)r << 8) | ((p->filter.dual & 1u) << 16) | ((p->filter.type & 3u) << 17);
}
static void test_dds(void)
{
    const int mixes[] = {-64,-63,-31,0,31,63};
    const int pans[] = {-64,-63,-17,0,23,63};
    const int spreads[] = {0,1,64,126,127};
    patch_t p; patch_default(&p);
    p.osc[0] = (osc_t){WAVE_PULSE,-120,-1100,-1};
    p.osc[1] = (osc_t){WAVE_SINE,120,1700,0x1234567};
    p.unison_detune = 83;
    p.filter.resonance = 0x3ffe; p.filter.type = 3; p.filter.dual = 1;
    p.filter.key_track = 128; p.bend_range = 12;
    midi_template_image_t t;
    for (unsigned shape=0; shape<3; shape++)
    for (unsigned mix=0; mix<sizeof(mixes)/sizeof(*mixes); mix++)
    for (unsigned pan=0; pan<sizeof(pans)/sizeof(*pans); pan++)
    for (unsigned spread=0; spread<sizeof(spreads)/sizeof(*spreads); spread++) {
        p.voice_struct = (voice_struct_t)shape; p.osc_mix = mixes[mix];
        p.pan = pans[pan]; p.unison_stereo = spreads[spread];
        memset(&t,0xa5,sizeof(t));
        assert(midi_templates_compile(&p,3000,&t));
        assert(t.key_track == 128 && t.bend_range == 12);
        for (unsigned u=0;u<8;u++) {
            if (shape == VOICE_2_PLAIN && u>=2) {
                for (unsigned w=0;w<8;w++) assert(t.elements[u][w] == 0);
                continue;
            }
            const osc_t *o = &p.osc[osc_for(shape,u)];
            int offset = o->coarse * 1024 / 12 + o->fine + spread_position(shape,u)*p.unison_detune;
            assert((int32_t)t.elements[u][0] == offset);
            /* Two extremes expose any note60 clamp/subtract contamination. */
            for (unsigned note=0;note<=127;note+=127) {
                int pitch = (int)note * 1024 / 12;
                assert(clamp(pitch+(int32_t)t.elements[u][0],0,16383) == clamp(pitch+offset,0,16383));
            }
            assert(t.elements[u][1] == ((uint32_t)o->duty & 0xffffff));
            assert(t.elements[u][2] == 0x3ffe);
            assert(t.elements[u][3] == old_gain(&p,u));
            assert(t.elements[u][4] == (0x100u | (unsigned)o->wave));
            if(u==0) {
                assert((int32_t)t.elements[u][5]==p.wheel_vibrato_depth);
                assert((int32_t)t.elements[u][6]==p.touch_cutoff_depth);
                assert((int32_t)t.elements[u][7]==p.touch_resonance_depth);
            } else for (unsigned w=5;w<8;w++) assert(t.elements[u][w] == 0);
        }
    }
}
static void test_depths(void)
{
    patch_t p; patch_default(&p);
    const int depths[] = {-32768,-4096,-1,0,1,4096,32767};
    midi_template_image_t t;
    for (unsigned amt=0;amt<128;amt++)
    for (unsigned d=0;d<sizeof(depths)/sizeof(*depths);d++) {
        p.vel_amp_amt = amt; p.vel_mod_amt = 127-amt; p.env1_depth = depths[d];
        assert(midi_templates_compile(&p,3000,&t));
        for (unsigned vel=0;vel<128;vel++) {
            unsigned curve = vel * 65536u / 127u;
            unsigned a = 65536u - amt*(65536u-curve)/127u;
            unsigned m = 65536u - (127u-amt)*(65536u-curve)/127u;
            int64_t scaled = (int64_t)depths[d]*m;
            int32_t rounded_down = scaled >= 0 ? scaled/65536 : -((-scaled+65535)/65536);
            assert(t.amp_depth[vel] == 10240u*a/65536u);
            assert(t.mod_depth[vel] == ((uint32_t)rounded_down & 0x3ffff));
        }
    }
}
static void test_ks(const char *golden)
{
    patch_t p; patch_default(&p);
    midi_template_image_t t;
    assert(midi_templates_compile(&p,3000,&t));
    FILE *f = fopen(golden,"r"); assert(f);
    char line[128]; assert(fgets(line,sizeof(line),f));
    for (unsigned n=0;n<128;n++) {
        unsigned note,length,loss; int tuning;
        assert(fscanf(f,"%u,%u,%d,%u\n",&note,&length,&tuning,&loss) == 4 && note == n);
        assert(t.ks_notes[n][0] == (n>=19 ? 0x80000000u | length : 0));
        assert(t.ks_notes[n][1] == (n>=19 ? (uint32_t)loss<<16 | (uint16_t)tuning : 0));
    }
    fclose(f);
    const int pans[] = {-64,-63,0,63};
    const unsigned volumes[] = {0,1,207,255};
    const unsigned decays[] = {200,8000};
    for (unsigned v=0;v<4;v++) for (unsigned pan=0;pan<4;pan++) for(unsigned d=0;d<2;d++) {
        p.volume = volumes[v]; p.pan = pans[pan];
        assert(midi_templates_compile(&p,decays[d],&t));
        for (unsigned n=0;n<128;n++) {
            engine_ks_note_t k;
            bool valid = ks_params_note(n,127,decays[d],p.volume,p.pan,1,&k);
            assert(t.ks_notes[n][0] == (valid ? 0x80000000u | k.length : 0));
            assert(t.ks_notes[n][1] == (valid ? (uint32_t)k.loss<<16 | (uint16_t)k.tuning : 0));
            int position = clamp(p.pan+(int)n-64,-64,63)+64;
            unsigned left = lround(volumes[v]*256.0*sqrt((127.0-position)/127.0));
            unsigned right = lround(volumes[v]*256.0*sqrt(position/127.0));
            assert(t.ks_pan[n] == (left | right<<16));
        }
    }
}
static void test_fm_and_invalid(void)
{
    patch_t p; patch_default(&p); midi_template_image_t t;
    const unsigned volumes[] = {0,1,2,207,254,255};
    const int pans[] = {-128,-64,-63,0,63,127};
    for(unsigned v=0;v<6;v++) for(unsigned pan=0;pan<6;pan++) {
        p.volume=volumes[v]; p.pan=pans[pan];
        assert(midi_templates_compile(&p,3000,&t));
        assert(t.fm_expression == (volumes[v]/2 | (unsigned)clamp(64+pans[pan],0,127)<<8));
    }
    assert(!midi_templates_compile(NULL,3000,&t));
    assert(!midi_templates_compile(&p,3000,NULL));
    assert(!midi_templates_compile(&p,199,&t));
    assert(!midi_templates_compile(&p,8001,&t));
    p.voice_struct=(voice_struct_t)99; assert(!midi_templates_compile(&p,3000,&t));
    p.voice_struct=VOICE_2_PLAIN; p.osc[0].coarse=32767;
    assert(!midi_templates_compile(&p,3000,&t));
    p.osc[0].coarse=1536; p.osc[0].fine=-1;
    assert(midi_templates_compile(&p,3000,&t) && t.elements[0][0] == 131071);
    p.osc[0].fine=0; assert(!midi_templates_compile(&p,3000,&t));
    p.osc[0].coarse=-1536;
    assert(midi_templates_compile(&p,3000,&t) && (int32_t)t.elements[0][0] == -131072);
    p.osc[0].fine=-1; assert(!midi_templates_compile(&p,3000,&t));
    patch_default(&p); p.voice_struct=VOICE_2_PLAIN;
    for (unsigned mode=0;mode<8;mode++) for (unsigned wave=0;wave<4;wave++) {
        p.filter.dual=mode & 1; p.filter.type=mode >> 1; p.osc[0].wave=(waveform_t)wave;
        assert(midi_templates_compile(&p,3000,&t));
        assert((t.elements[0][3] >> 16) == mode);
        assert(t.elements[0][4] == (0x100u | wave));
    }
#ifdef MIDI_TEMPLATES_WRAP_KS
    reject_ks_note=true;
    memset(&t,0xa5,sizeof(t));
    assert(midi_templates_compile(&p,3000,&t));
    assert(!t.ks_notes[57][0] && !t.ks_notes[57][1]);
    assert(t.ks_notes[56][0] & 0x80000000u);
    assert(t.ks_notes[58][0] & 0x80000000u);
    reject_ks_note=false;
#endif
}
int main(int argc,char **argv)
{
    assert(argc==2);
    test_dds(); test_depths(); test_ks(argv[1]); test_fm_and_invalid();
    puts("MIDI templates: DDS 3 voicings/mix/pan/spread/unclamped offsets, all velocity depths, 128-note KS golden/pan, FM boundaries PASS");
    return 0;
}
