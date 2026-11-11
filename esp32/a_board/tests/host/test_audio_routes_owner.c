#define main previous_raw_web_main
#include "test_raw_web_owner.c"
#undef main
static engine_cpu_mode_state_t mode_view;
static engine_audio_routes_state_t audio_view;
void engine_link_cpu_mode_state(engine_cpu_mode_state_t *out){*out=mode_view;}
bool engine_link_cpu_mode_request(uint32_t mode,uint32_t id){
    mode_view.desired=mode;mode_view.request_id=id;mode_view.pending=true;mode_view.busy=true;mode_view.ready=false;return true;
}
void engine_link_audio_routes_state(engine_audio_routes_state_t *out){*out=audio_view;}
int main(void)
{
    reset_owner();observed.cpu_control=true;
    mode_view=(engine_cpu_mode_state_t){.supported=true,.valid=true,.ready=true,.applied=0};
    audio_view=(engine_audio_routes_state_t){.available=true,.valid=true,.ready=true,.pcm_a=1};
    owner_cpu_mode_sync(); // Cold handover may clear stale ownership.
    web_v2_event_t e=keys(0,111,1,0);handle_web_v2(&e,fake_time);
    e=keys(0,111,2,1);handle_web_v2(&e,fake_time);assert(s_web[0].keys);
    uint32_t before=global_flushes;uint64_t held=s_web[0].keys;
    e=(web_v2_event_t){.client=0,.token=111,.sequence=3,.context_epoch=s_control_epoch,
        .op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=FM_ENGINE_FM,.expires=fake_time+1000000};
    assert(owner_remote_begin(&e,fake_time));
    assert(global_flushes==before && s_web[0].keys==held);
    owner_cpu_mode_sync();assert(global_flushes==before && s_web[0].keys==held);
    mode_view.pending=mode_view.busy=false;mode_view.ready=true;mode_view.applied=2;
    mode_view.completed_request_id=mode_view.request_id;mode_view.result=FM_RESULT_OK;
    owner_cpu_mode_sync();owner_remote_poll(fake_time);
    assert(global_flushes==before && s_web[0].keys==held && !s_remote_mode.active);
    mode_view.reset_count++;owner_cpu_mode_sync();assert(global_flushes>before && !s_web[0].keys);
    before=global_flushes;all_sound_off();assert(global_flushes>before);
    puts("PASS parallel owner: editor begin/busy/ACK preserves WEB keys and sends no ALL flush; reset/Panic retain cleanup");
    return 0;
}
