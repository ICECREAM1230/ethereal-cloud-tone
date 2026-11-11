#include "ks_voice.h"
#include "ks_params.h"
#include <string.h>
typedef enum { K_IDLE, K_HELD, K_RELEASE } ks_lifecycle_t;
static struct {
    ks_lifecycle_t state;
    uint64_t owner;
    uint32_t stamp;
    uint8_t note;
    bool key_down;
} s_strings[ENGINE_KS_VOICES];
static bool s_sustain[5][16];
static uint32_t s_ks_stamp;
static unsigned s_decay_ms=3000;
static uint64_t owner_key(uint8_t source, uint8_t channel, uint32_t token)
{ return token ? (1ULL<<32)|token : ((uint64_t)source<<8)|channel; }
void ks_voice_poll(void)
{
    engine_ks_state_t state;engine_link_ks_state(&state);
    if(!state.valid)return;
    for(unsigned v=0;v<ENGINE_KS_VOICES;v++)if(s_strings[v].state==K_RELEASE &&
       !((state.active_mask|state.init_mask|state.release_mask|state.pending_mask)&(1u<<v)))
        s_strings[v].state=K_IDLE;
}
void ks_voice_on(uint8_t source, uint8_t channel, uint32_t token, uint8_t note, uint8_t velocity)
{
    if(source>=5 || channel>=16 || note<KS_MIDI_MIN || note>127)return;
    ks_voice_poll();
    unsigned pick=ENGINE_KS_VOICES;uint32_t oldest=UINT32_MAX;
    for(unsigned rank=K_IDLE;rank<=K_RELEASE && pick==ENGINE_KS_VOICES;rank++) {
        // Allocation order: idle, release, then oldest held (incl. sustain).
        unsigned wanted=rank==1 ? K_RELEASE : rank==2 ? K_HELD : K_IDLE;
        for(unsigned v=0;v<ENGINE_KS_VOICES;v++)if(s_strings[v].state==wanted && s_strings[v].stamp<oldest)
            {oldest=s_strings[v].stamp;pick=v;}
    }
    engine_ks_note_t parameters;
    uint32_t stamp=s_ks_stamp+1;
    if(!ks_params_note(note,velocity,s_decay_ms,g_patch.volume,g_patch.pan,
       0x9e3779b9u^stamp^((uint32_t)note<<16),&parameters) ||
       !engine_link_ks_note_on(pick,&parameters))return;
    s_ks_stamp=stamp;
    s_strings[pick].state=K_HELD;s_strings[pick].key_down=true;
    s_strings[pick].owner=owner_key(source,channel,token);
    s_strings[pick].stamp=stamp;s_strings[pick].note=note;
}
static void release(unsigned voice)
{
    s_strings[voice].key_down=false;s_strings[voice].state=K_RELEASE;
    engine_link_ks_note_off(voice);
}
void ks_voice_off(uint8_t source, uint8_t channel, uint32_t token, uint8_t note)
{
    unsigned pick=ENGINE_KS_VOICES;uint32_t oldest=UINT32_MAX;
    uint64_t owner=owner_key(source,channel,token);
    for(unsigned v=0;v<ENGINE_KS_VOICES;v++)if(s_strings[v].state==K_HELD && s_strings[v].key_down &&
       s_strings[v].owner==owner && s_strings[v].note==note && s_strings[v].stamp<oldest)
        {oldest=s_strings[v].stamp;pick=v;}
    if(pick==ENGINE_KS_VOICES)return;
    s_strings[pick].key_down=false;
    if(token || source>=5 || channel>=16 || !s_sustain[source][channel])release(pick);
}
void ks_voice_sustain(uint8_t source, uint8_t channel, bool down)
{
    if(source>=5 || channel>=16)return;
    s_sustain[source][channel]=down;
    if(!down)for(unsigned v=0;v<ENGINE_KS_VOICES;v++)if(s_strings[v].state==K_HELD &&
        !s_strings[v].key_down && s_strings[v].owner==owner_key(source,channel,0))release(v);
}
void ks_voice_release_owner(uint32_t token)
{
    if(!token)return;
    for(unsigned v=0;v<ENGINE_KS_VOICES;v++)if(s_strings[v].state==K_HELD &&
        s_strings[v].owner==owner_key(0,0,token))release(v);
}
void ks_voice_all_notes_off(void)
{for(unsigned v=0;v<ENGINE_KS_VOICES;v++)if(s_strings[v].state==K_HELD)release(v);memset(s_sustain,0,sizeof(s_sustain));}
void ks_voice_panic(void)
{memset(s_strings,0,sizeof(s_strings));memset(s_sustain,0,sizeof(s_sustain));engine_link_ks_panic();}
void ks_voice_expression(void)
{
    uint16_t left,right;ks_params_gains(g_patch.volume,g_patch.pan,&left,&right);
    for(unsigned v=0;v<ENGINE_KS_VOICES;v++)if(s_strings[v].state!=K_IDLE)engine_link_ks_gain(v,left,right);
}
void ks_voice_decay(unsigned milliseconds)
{
    if(milliseconds<200 || milliseconds>8000)return;
    s_decay_ms=milliseconds;
    for(unsigned v=0;v<ENGINE_KS_VOICES;v++)if(s_strings[v].state!=K_IDLE)
        engine_link_ks_loss(v,ks_params_loss(s_strings[v].note,s_decay_ms));
}
unsigned ks_voice_decay_ms(void) {return s_decay_ms;}
