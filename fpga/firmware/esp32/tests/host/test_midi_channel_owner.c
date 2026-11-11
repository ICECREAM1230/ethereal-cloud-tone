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
static void select_channel(unsigned client,unsigned sequence,unsigned engine) {
 web_v2_event_t e={.client=(uint8_t)client,.token=s_web[client].token,.sequence=sequence,
  .context_epoch=s_control_epoch,.op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=engine,.expires=fake_time+1000000};
 assert(owner_remote_begin(&e,fake_time));
 assert(s_v2_result_code[client]==FM_RESULT_OK && s_v2_result_sequence[client]==sequence);
}
int main(void) {
 reset_owner();observed.cpu_control=true;
 mode_view=(engine_cpu_mode_state_t){.supported=true,.valid=true,.ready=true,.applied=0,.channel_routes=true};
 audio_view=(engine_audio_routes_state_t){.available=true,.valid=true,.ready=true,.pcm_a=1};
 owner_cpu_mode_sync();
 web_v2_event_t e=keys(0,111,1,0);handle_web_v2(&e,fake_time);
 e=keys(0,111,2,1);handle_web_v2(&e,fake_time);
 assert(stream_length==3 && stream[0]==0x91); /* default KS */
 unsigned flushes=global_flushes;uint32_t epoch=s_control_epoch;
 select_channel(0,3,2); /* new notes FM, original KS still held */
 assert(s_web[0].keys==1 && !s_remote_mode.active && !mode_view.pending);
 e=keys(0,111,4,3);handle_web_v2(&e,fake_time);assert(stream[3]==0x92 && stream[4]==49);
 select_channel(0,5,0); /* SUB future notes */
 e=keys(0,111,6,6);handle_web_v2(&e,fake_time);
 assert(stream[6]==0x81 && stream[7]==48 && stream[9]==0x90 && stream[10]==50);
 e=keys(0,111,7,0);handle_web_v2(&e,fake_time);
 assert(stream[12]==0x82 && stream[13]==49 && stream[15]==0x80 && stream[16]==50);
 assert(global_flushes==flushes && s_control_epoch==epoch && !source_flushes[1]);
 /* A second browser client defaults independently and cannot rewrite the first. */
 select_channel(1,1,2);e=keys(0,111,8,1);handle_web_v2(&e,fake_time);assert(stream[18]==0x90);
 e=keys(0,111,9,0);handle_web_v2(&e,fake_time);
 /* Explicit RAW channels are never rewritten. */
 const uint8_t raw[]={0x92,67,100,0x82,67,0};e=raw_event(0,111,10,raw,sizeof(raw));handle_web_raw(&e,fake_time);
 assert(!memcmp(stream+24,raw,sizeof(raw)));
 /* Expired selection cannot change current channel or resurrect voices. */
 web_v2_event_t expired={.client=0,.token=111,.sequence=11,.context_epoch=s_control_epoch,
  .op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=2,.expires=fake_time};
 assert(owner_remote_begin(&expired,fake_time));assert(s_v2_result_code[0]==FM_RESULT_CANCELLED);
 e=keys(0,111,12,1);handle_web_v2(&e,fake_time);assert(stream[30]==0x90);
 mode_view.reset_count++;owner_cpu_mode_sync();assert(global_flushes>flushes && !s_web[0].keys && !s_web[0].armed);
 puts("PASS channel owner: KS default, per-key original-channel Off, independent selection, no flush/context/editor write, RAW untouched, expiry/reset");return 0;
}
