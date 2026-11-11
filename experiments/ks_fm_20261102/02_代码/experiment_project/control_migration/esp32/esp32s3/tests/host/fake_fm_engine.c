#include "fm_engine.h"
#include "engine_link.h"
#include "reverb_control.h"
#include <string.h>
engine_fm_state_t fake_fm_state;
unsigned fake_fm_on[ENGINE_FM_VOICES],fake_fm_off[ENGINE_FM_VOICES],fake_fm_panics;
engine_local_state_t fake_local_state;
unsigned fake_remote_requests;
void engine_link_local_state(engine_local_state_t *s){*s=fake_local_state;}
void engine_link_local_ack(uint32_t generation){(void)generation;}
void engine_link_remote_request(void){fake_remote_requests++;fake_local_state.remote_pending=true;}
uint32_t fake_fm_pitch[ENGINE_FM_VOICES];
uint8_t fake_fm_volume[ENGINE_FM_VOICES],fake_fm_pan[ENGINE_FM_VOICES];
void engine_link_fm_status(engine_fm_status_t *s){*s=fake_fm_state.status;}
void engine_link_fm_state(engine_fm_state_t *s){*s=fake_fm_state;}
bool engine_link_fm_mode(uint32_t b){if(!engine_fm_selectable(&fake_fm_state.status))return false;fake_fm_state.status.requested_backend=b;fake_fm_state.status.switching=true;return true;}
bool engine_link_fm_note_on(unsigned v,uint32_t p,uint8_t vel,uint8_t volume,uint8_t pan)
{if(v>=fake_fm_state.status.voice_count||!vel||!engine_fm_playable(&fake_fm_state.status))return false;fake_fm_on[v]++;fake_fm_pitch[v]=p;fake_fm_volume[v]=volume;fake_fm_pan[v]=pan;return true;}
void engine_link_fm_note_off(unsigned v){fake_fm_off[v]++;}
void engine_link_fm_pitch(unsigned v,uint32_t p){if(engine_fm_playable(&fake_fm_state.status))fake_fm_pitch[v]=p;}
void engine_link_fm_gain(unsigned v,uint8_t volume,uint8_t pan){if(engine_fm_playable(&fake_fm_state.status)){fake_fm_volume[v]=volume;fake_fm_pan[v]=pan;}}
uint32_t engine_link_fm_parameter(uint32_t id,uint32_t target,uint32_t value)
{if(!engine_fm_playable(&fake_fm_state.status))return FM_RESULT_BUSY;const fm_parameter_t *p=fm_parameter(fake_fm_state.status.profile_id,id);return p&&fm_validate_parameter(fake_fm_state.status.profile_id,id,p->scope_id,target,value)?0:4;}
uint32_t engine_link_fm_preset(uint32_t id){return fm_preset(fake_fm_state.status.profile_id,id)?0:4;}
void engine_link_fm_panic(void){fake_fm_panics++;}
void engine_link_fm_owner_ack(void){}

engine_reverb_state_t fake_reverb_state;
unsigned fake_reverb_requests,fake_reverb_cancels;
uint32_t fake_reverb_wet,fake_reverb_damp;
bool engine_link_reverb_request(uint32_t wet,uint32_t damp,uint32_t id)
{
    if(!id || wet>REVERB_WET_MAX || damp>REVERB_DAMP_MAX ||
       !fake_reverb_state.available || !fake_reverb_state.valid || fake_reverb_state.pending)return false;
    fake_reverb_requests++;fake_reverb_wet=wet;fake_reverb_damp=damp;
    fake_reverb_state.pending=true;fake_reverb_state.request_id=id;
    fake_reverb_state.completed_request_id=0;fake_reverb_state.fault=REVERB_LINK_OK;return true;
}
void engine_link_reverb_cancel(uint32_t id)
{
    if(id && fake_reverb_state.request_id!=id)return;
    fake_reverb_cancels++;fake_reverb_state.pending=false;
    fake_reverb_state.completed_request_id=0;fake_reverb_state.fault=REVERB_LINK_CANCELLED;
}
void engine_link_reverb_state(engine_reverb_state_t *state){*state=fake_reverb_state;}

engine_flute_state_t fake_flute_state;
void engine_link_flute_state(engine_flute_state_t *s){*s=fake_flute_state;}
bool engine_link_flute_mode(bool bind){if(bind && !fake_flute_state.available)return false;fake_flute_state.requested_mode=bind;return true;}
