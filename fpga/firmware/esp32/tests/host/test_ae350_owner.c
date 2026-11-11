#define main original_raw_web_owner_main
#include "test_raw_web_owner.c"
#undef main
int main(void)
{
    reset_owner();observed.cpu_control=true;
    assert(owner_raw_ready());
    web_v2_event_t e=keys(0,111,1,1);handle_web_v2(&e,fake_time);
    assert(stream_length==3 && stream[0]==0x90 && stream[1]==WEB_FIRST_NOTE);
    const uint8_t bend[]={0xe0,0,96};e=raw_event(0,111,2,bend,sizeof bend);handle_web_raw(&e,fake_time);
    assert(stream_length==6 && stream[3]==0xe0);
    observed.cpu_control=false;assert(!owner_raw_ready());observed.cpu_control=true;
    template_state.ready=false;template_state.applied_generation=1;
    assert(owner_raw_ready());template_state.fault=MIDI_TEMPLATE_LINK_IO;assert(!owner_raw_ready());
    template_state.fault=0;template_state.ready=true;
    e=(web_v2_event_t){.client=0,.token=111,.op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=FM_ENGINE_KS};
    assert(owner_mode_apply(&e)==FM_RESULT_UNSUPPORTED);
    owner_templates_update();assert(publishes==1 && performance_true);
    unsigned before=publishes;g_patch.pan=17;fake_time+=30000;owner_templates_update();assert(publishes==before+1);
    all_sound_off();assert(global_flushes && !s_web[0].keys);
    puts("PASS ae350_owner: actual Web RAW order, capability and template readiness, hot patch publish, RAW panic; engine routing stays at AE350");
}
