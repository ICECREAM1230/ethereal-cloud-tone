#include <assert.h>
#include <stdio.h>
#if !__has_include("fm_voice.h")
int main(void){assert(!"FM source voice allocator is absent");}
#else
#include "fm_voice.h"
#include "patch.h"
extern engine_fm_state_t fake_fm_state;
extern unsigned fake_fm_on[ENGINE_FM_VOICES],fake_fm_off[ENGINE_FM_VOICES],fake_fm_panics;
extern uint32_t fake_fm_pitch[ENGINE_FM_VOICES];
extern uint8_t fake_fm_volume[ENGINE_FM_VOICES],fake_fm_pan[ENGINE_FM_VOICES];
int main(void)
{
 fake_fm_state.status=(engine_fm_status_t){.available=true,.valid=true,.ready=true,.selected=true,
     .requested_backend=1,.applied_backend=1,.backend_id=1,.voice_count=8,.profile_id=1,
     .flags=FM_FLAG_READY|FM_FLAG_SELECTED,.session_id=42,.engine_epoch=4,.topology_generation=1};
 patch_default(&g_patch);
 assert(fm_voice_on(1,0,0,60,100));assert(fm_voice_on(2,0,0,60,100));
 fm_voice_off(2,0,0,60);assert(fake_fm_off[1]==1&&!fake_fm_off[0]);
 fm_voice_sustain(1,0,true);fm_voice_off(1,0,0,60);assert(!fake_fm_off[0]);
 fm_voice_sustain(1,0,false);assert(fake_fm_off[0]==1);
 assert(fm_voice_on(1,1,0,64,100));assert(fm_voice_on(2,1,0,64,100));
 fm_voice_bend(1,1,16383);assert(fake_fm_pitch[2]>64u*65536&&fake_fm_pitch[3]==64u*65536);
 fm_voice_cc(1,1,7,32);fm_voice_cc(1,1,10,127);
 assert(fake_fm_volume[2]==32&&fake_fm_pan[2]==127&&fake_fm_volume[3]==127);
 assert(fm_voice_on(0,0,111,67,80));assert(fm_voice_on(0,0,222,67,80));
 fm_voice_release_owner(111);assert(fake_fm_off[4]==1&&!fake_fm_off[5]);
 g_patch.pan=63;fm_voice_web_expression();assert(fake_fm_pan[5]==127);
 fm_voice_all_notes_off(1,1);assert(fake_fm_off[2]==1&&!fake_fm_off[3]);
 // Do not synthesize extra NoteOn on bend, volume, sustain or key release.
 assert(fake_fm_on[2]==1&&fake_fm_on[3]==1);
 fm_voice_panic();assert(fake_fm_panics==1);
 fake_fm_state.status.switching=true;assert(!fm_voice_on(1,0,0,60,100));
 fake_fm_state.status.switching=false;fake_fm_state.status.flags|=FM_FLAG_MUTED;
 assert(!fm_voice_on(1,0,0,60,100));
 puts("FM voice owner: BLE/DIN/channel/token isolation, sustain, absolute pitch bend, source gain/pan, lease release, source AllNotesOff, Panic and switching gate PASS");
}
#endif
