#define main previous_raw_owner_main
#include "test_raw_web_owner.c"
#undef main
extern engine_flute_state_t fake_flute_state;
static web_v2_event_t select_flute(void)
{
    return (web_v2_event_t){.client=0,.token=111,.sequence=1,.context_epoch=s_control_epoch,
        .op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=ENGINE_MODE_FLUTE,.expires=fake_time+WEB_LEASE_US};
}
int main(void)
{
    reset_owner();memset(&fake_flute_state,0,sizeof(fake_flute_state));
    web_v2_event_t event=select_flute();uint32_t epoch=s_control_epoch;
    handle_web_v2(&event,fake_time);
    assert(s_v2_result_code[0]==FM_RESULT_UNSUPPORTED && s_control_epoch==epoch);
    reset_owner();fake_flute_state=(engine_flute_state_t){.available=true,.valid=true,.ready=true};
    event=select_flute();handle_web_v2(&event,fake_time);
    assert(s_v2_result_code[0]==FM_RESULT_OK && fake_flute_state.requested_mode && s_requested_engine==4);
    fake_flute_state.applied_mode=true;fake_flute_state.preset=1;
    publish_web_state();web_control_state_t state;web_control_read(&state);
    assert(state.requested_engine==4 && state.applied_engine==4 && !state.mode_switching && state.inputs.ready);
    assert(state.flute.available && state.flute.preset==1 && !dds_selected());
    start(0,111); // Fresh empty-key handshake after the mode context changed.
    uint8_t notes[]={0x90,60,100,0x80,60,0,0x90,84,100,0x80,84,0};
    event=raw_event(0,111,1,notes,sizeof(notes));handle_web_raw(&event,fake_time);
    assert(stream_length==sizeof(notes) && !memcmp(stream,notes,sizeof(notes)));
    event=(web_v2_event_t){.client=0,.token=111,.sequence=2,.context_epoch=s_control_epoch,
        .op=FM_HTTP_OPCODE_CC,.a=74,.b=100};
    handle_web_v2(&event,fake_time);assert(s_v2_result_code[0]==FM_RESULT_UNSUPPORTED);
    event.op=FM_HTTP_OPCODE_KS_DECAY;event.a=2000;event.b=0;event.sequence=3;
    handle_web_v2(&event,fake_time);assert(s_v2_result_code[0]==FM_RESULT_UNSUPPORTED);
    event=keys(0,111,4,1);handle_web_v2(&event,fake_time);
    assert(s_v2_result_code[0]==FM_RESULT_UNSUPPORTED); // MIDI48 is outside normal's 60..84.
    event=keys(0,111,5,0);handle_web_v2(&event,fake_time);
    event=keys(0,111,6,1u<<12);handle_web_v2(&event,fake_time);
    assert(s_v2_result_code[0]==FM_RESULT_OK && stream_length==sizeof(notes)+3 && stream[sizeof(notes)+1]==60);
    puts("FLU1 owner: old hardware refusal, mode4 snapshot/RAW60..84, DDS fence, unsupported parameters/notes PASS");
    return 0;
}
