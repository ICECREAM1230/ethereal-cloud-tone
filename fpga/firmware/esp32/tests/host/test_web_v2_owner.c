#define main legacy_web_owner_test_entry
#include "test_web_control.c"
#undef main

static void v2(unsigned client, uint32_t token, uint32_t seq, uint32_t epoch,
               unsigned op, uint32_t a, uint32_t b, uint32_t c)
{
    web_v2_event_t e={.expires=fake_time+WEB_LEASE_US,.client=client,.token=token,
        .sequence=seq,.context_epoch=epoch,.op=op,.a=a,.b=b,.c=c};
    handle_web_v2(&e,fake_time);
}
int main(void)
{
    reset(); s_control_epoch=1;
    apply(command(0,123,0,WEB_START,0,0)); apply(command(1,456,0,WEB_START,0,0));
    v2(0,123,1,1,FM_HTTP_OPCODE_KEYS_UPDATE,1u<<12,100,0);web_advance(fake_time+250);
    v2(1,456,1,1,FM_HTTP_OPCODE_KEYS_UPDATE,1u<<12,100,0);web_advance(fake_time+250);
    assert(held(123)==1 && held(456)==1);
    v2(0,123,2,1,FM_HTTP_OPCODE_KEYS_UPDATE,0,100,0);web_advance(fake_time+250);
    assert(!held(123) && held(456)==1);
    v2(1,456,2,1,FM_HTTP_OPCODE_CC,7,99,0);
    assert(g_patch.volume==198 && s_web[1].parameter_sequence==2);
    handle_cc(119,127);
    v2(0,123,3,1,FM_HTTP_OPCODE_STOP,0,0,0);
    assert(s_control_epoch==2 && !held(456) && !s_test_tone_requested);
    unsigned stopped=notes;
    v2(1,456,3,1,FM_HTTP_OPCODE_KEYS_UPDATE,1,100,0);web_advance(fake_time+250);
    assert(notes==stopped && s_v2_result_code[1]==FM_RESULT_STALE_EPOCH);
    apply(command(0,789,0,WEB_START,0,0));
    v2(0,789,1,2,FM_HTTP_OPCODE_KEYS_UPDATE,1,100,0);web_advance(fake_time+250);
    assert(held(789)==1);
    v2(0,123,4,2,FM_HTTP_OPCODE_KEYS_UPDATE,0,100,0);assert(held(789)==1);
    unsigned writes=costs;
    v2(0,789,2,2,FM_HTTP_OPCODE_FM_PARAMETER,18,4294901760u,31);
    assert(s_v2_result_code[0]==FM_RESULT_UNSUPPORTED && costs==writes);
    v2(0,789,3,2,FM_HTTP_OPCODE_ENGINE_SELECT,FM_ENGINE_FM,FM_BACKEND_JT51_OPM,0);
    assert(s_v2_result_code[0]==FM_RESULT_UNSUPPORTED && held(789)==1);
    web_control_state_t state;publish_web_state();web_control_read(&state);
    assert(state.context_epoch==2 && state.v2_result_sequence[0]==3 && state.v2_result_context[0]==2);
    fake_time=s_web[0].expires+1; assert(web_expire(fake_time) && !held(789));
    s_control_epoch=UINT32_MAX;all_sound_off();assert(s_control_epoch==1);
    puts("Web v2 owner: actual patch, source isolation, panic fence, stale On/token rejection, unavailable FM preserves DDS, state result and lease PASS");
    return 0;
}
