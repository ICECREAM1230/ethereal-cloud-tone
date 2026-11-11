#include "fm_voice.h"
#include "patch.h"
#include <string.h>
enum { F_IDLE,F_HELD,F_RELEASE };
static struct {
    uint64_t owner;
    uint32_t stamp;
    uint8_t note,source,channel,state;
    bool down;
} s_fm_voices[ENGINE_FM_VOICES];
static struct {uint16_t bend;uint8_t volume,pan;bool sustain,initialized;} s_fm_channels[5][16];
static uint32_t s_fm_stamp;
static uint64_t fm_owner(uint8_t source,uint8_t channel,uint32_t token)
{return token?(UINT64_C(1)<<32)|token:((uint64_t)source<<8)|channel;}
static void fm_channel(uint8_t source,uint8_t channel)
{
    if(!s_fm_channels[source][channel].initialized) {
        s_fm_channels[source][channel].bend=8192;s_fm_channels[source][channel].volume=127;
        s_fm_channels[source][channel].pan=64;s_fm_channels[source][channel].initialized=true;
    }
}
static uint8_t fm_web_volume(void){unsigned v=g_patch.volume/2;return v>127?127:v;}
static uint8_t fm_web_pan(void){int p=64+g_patch.pan;return p<0?0:p>127?127:(uint8_t)p;}
static uint32_t fm_pitch(uint8_t source,uint8_t channel,uint8_t note)
{
    int32_t value=(int32_t)note*65536+((int32_t)s_fm_channels[source][channel].bend-8192)*16;
    return value<0?0:value>0x7fffff?0x7fffff:(uint32_t)value;
}
bool fm_voice_on(uint8_t source,uint8_t channel,uint32_t token,uint8_t note,uint8_t velocity)
{
    if(source>=5 || channel>=16 || note>127)return false;
    if(!velocity){fm_voice_off(source,channel,token,note);return true;}
    engine_fm_status_t state;engine_link_fm_status(&state);
    if(!engine_fm_playable(&state))return false;
    fm_channel(source,channel);unsigned pick=ENGINE_FM_VOICES;
    for(unsigned rank=0;rank<3 && pick==ENGINE_FM_VOICES;rank++) {
        unsigned wanted=rank==0?F_IDLE:rank==1?F_RELEASE:F_HELD;uint32_t oldest=UINT32_MAX;
        for(unsigned v=0;v<state.voice_count && v<ENGINE_FM_VOICES;v++)
            if(s_fm_voices[v].state==wanted && s_fm_voices[v].stamp<oldest){oldest=s_fm_voices[v].stamp;pick=v;}
    }
    uint8_t volume=token?fm_web_volume():s_fm_channels[source][channel].volume;
    uint8_t pan=token?fm_web_pan():s_fm_channels[source][channel].pan;
    uint32_t pitch=token?(uint32_t)note*65536:fm_pitch(source,channel,note);
    if(pick==ENGINE_FM_VOICES || !engine_link_fm_note_on(pick,pitch,velocity,volume,pan))return false;
    s_fm_voices[pick].owner=fm_owner(source,channel,token);s_fm_voices[pick].stamp=++s_fm_stamp;
    s_fm_voices[pick].note=note;s_fm_voices[pick].source=source;s_fm_voices[pick].channel=channel;
    s_fm_voices[pick].down=true;s_fm_voices[pick].state=F_HELD;return true;
}
static void fm_release(unsigned voice)
{s_fm_voices[voice].down=false;s_fm_voices[voice].state=F_RELEASE;engine_link_fm_note_off(voice);}
void fm_voice_off(uint8_t source,uint8_t channel,uint32_t token,uint8_t note)
{
    if(source>=5 || channel>=16)return;
    unsigned pick=ENGINE_FM_VOICES;uint32_t oldest=UINT32_MAX;uint64_t owner=fm_owner(source,channel,token);
    for(unsigned v=0;v<ENGINE_FM_VOICES;v++)if(s_fm_voices[v].state==F_HELD && s_fm_voices[v].down &&
        s_fm_voices[v].owner==owner && s_fm_voices[v].note==note && s_fm_voices[v].stamp<oldest){oldest=s_fm_voices[v].stamp;pick=v;}
    if(pick==ENGINE_FM_VOICES)return;
    s_fm_voices[pick].down=false;
    if(token || !s_fm_channels[source][channel].sustain)fm_release(pick);
}
void fm_voice_sustain(uint8_t source,uint8_t channel,bool down)
{
    if(source>=5 || channel>=16)return;
    fm_channel(source,channel);s_fm_channels[source][channel].sustain=down;
    if(!down)for(unsigned v=0;v<ENGINE_FM_VOICES;v++)if(s_fm_voices[v].state==F_HELD && !s_fm_voices[v].down &&
        s_fm_voices[v].owner==fm_owner(source,channel,0))fm_release(v);
}
void fm_voice_bend(uint8_t source,uint8_t channel,uint16_t bend)
{
    if(source>=5 || channel>=16 || bend>16383)return;
    fm_channel(source,channel);s_fm_channels[source][channel].bend=bend;
    for(unsigned v=0;v<ENGINE_FM_VOICES;v++)if(s_fm_voices[v].state!=F_IDLE && s_fm_voices[v].owner==fm_owner(source,channel,0))
        engine_link_fm_pitch(v,fm_pitch(source,channel,s_fm_voices[v].note));
}
void fm_voice_cc(uint8_t source,uint8_t channel,uint8_t cc,uint8_t value)
{
    if(source>=5 || channel>=16 || value>127)return;
    if(cc==64){fm_voice_sustain(source,channel,value>=64);return;}
    if(cc==123){fm_voice_all_notes_off(source,channel);return;}
    if(cc!=7 && cc!=10)return;
    fm_channel(source,channel);
    if(cc==7)s_fm_channels[source][channel].volume=value;else s_fm_channels[source][channel].pan=value;
    for(unsigned v=0;v<ENGINE_FM_VOICES;v++)if(s_fm_voices[v].state!=F_IDLE && s_fm_voices[v].owner==fm_owner(source,channel,0))
        engine_link_fm_gain(v,s_fm_channels[source][channel].volume,s_fm_channels[source][channel].pan);
}
void fm_voice_web_expression(void)
{
    for(unsigned v=0;v<ENGINE_FM_VOICES;v++)if(s_fm_voices[v].state!=F_IDLE && s_fm_voices[v].owner>>32)
        engine_link_fm_gain(v,fm_web_volume(),fm_web_pan());
}
void fm_voice_release_owner(uint32_t token)
{
    if(!token)return;
    for(unsigned v=0;v<ENGINE_FM_VOICES;v++)if(s_fm_voices[v].state==F_HELD && s_fm_voices[v].owner==fm_owner(0,0,token))fm_release(v);
}
void fm_voice_all_notes_off(uint8_t source,uint8_t channel)
{
    if(source>=5 || channel>=16)return;
    s_fm_channels[source][channel].sustain=false;
    for(unsigned v=0;v<ENGINE_FM_VOICES;v++)if(s_fm_voices[v].state==F_HELD && s_fm_voices[v].owner==fm_owner(source,channel,0))fm_release(v);
}
void fm_voice_panic(void)
{
    memset(s_fm_voices,0,sizeof(s_fm_voices));
    for(unsigned s=0;s<5;s++)for(unsigned c=0;c<16;c++)s_fm_channels[s][c].sustain=false;
    engine_link_fm_panic();
}
