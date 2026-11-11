#define main legacy_owner_regression
#include "test_web_control.c"
#undef main

extern engine_fm_state_t fake_fm_state;
static engine_ks_state_t ks;
static bool reject_mode;
static unsigned mode_calls;
void engine_link_ks_state(engine_ks_state_t *s){*s=ks;}
bool engine_link_ks_note_on(unsigned v,const engine_ks_note_t *n){(void)v;(void)n;return false;}
void engine_link_ks_note_off(unsigned v){(void)v;}
void engine_link_ks_panic(void){}
bool engine_link_ks_mode(bool mode)
{
    mode_calls++;
    if(reject_mode || ks.recovering)return false;
    ks.requested_mode=mode;ks.switching=true;return true;
}
void engine_link_ks_gain(unsigned v,uint16_t l,uint16_t r){(void)v;(void)l;(void)r;}
void engine_link_ks_loss(unsigned v,uint16_t loss){(void)v;(void)loss;}
void engine_link_owner_panic_ack(void){}
static void fixture(bool recovering)
{
    reset();s_requested_engine=FM_ENGINE_DDS;s_control_epoch=1;
    ks=(engine_ks_state_t){.available=true,.valid=true,.recovering=recovering};
    memset(&fake_fm_state,0,sizeof(fake_fm_state));
    fake_fm_state.status=(engine_fm_status_t){.available=true,.valid=true,.ready=true,
        .flags=FM_FLAG_READY|FM_FLAG_MUTED,.backend_id=1,.profile_id=1,.voice_count=8,
        .engine_epoch=1,.topology_generation=1};
    reject_mode=false;mode_calls=0;
    apply(command(0,123,0,WEB_START,0,0));
}
static void select_v2(unsigned engine)
{
    web_v2_event_t e={.client=0,.token=123,.sequence=1,.context_epoch=s_control_epoch,
        .op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=engine,.b=engine==FM_ENGINE_FM?1:0};
    handle_web_v2(&e,fake_time);
}
int main(void)
{
    fixture(true);
    apply(command(0,123,1,WEB_ENGINE,1,0));
    assert(s_requested_engine==FM_ENGINE_DDS && !s_web[0].parameter_sequence &&
           "legacy mode refusal must not publish an adopted mode");

    for(unsigned engine=FM_ENGINE_DDS;engine<=FM_ENGINE_FM;engine++) {
        fixture(true);select_v2(engine);
        assert(s_v2_result_code[0]==FM_RESULT_BUSY && s_requested_engine==FM_ENGINE_DDS);
        assert(!s_web[0].parameter_sequence && !fake_fm_state.status.requested_backend);
        assert(s_control_epoch==1); // Refusal must not clear held keys or change context.
    }
    fixture(false);reject_mode=true;select_v2(FM_ENGINE_KS);
    assert(mode_calls==1 && s_v2_result_code[0]==FM_RESULT_BUSY);
    assert(s_requested_engine==FM_ENGINE_DDS && !s_web[0].parameter_sequence);

    fixture(false);select_v2(FM_ENGINE_KS);
    assert(s_v2_result_code[0]==FM_RESULT_OK && s_requested_engine==FM_ENGINE_KS);
    assert(mode_calls==1 && ks.requested_mode && s_web[0].parameter_sequence==1);

    fixture(false);select_v2(FM_ENGINE_FM);
    assert(s_v2_result_code[0]==FM_RESULT_OK && s_requested_engine==FM_ENGINE_FM);
    assert(!ks.requested_mode && fake_fm_state.status.requested_backend==1);
    puts("Engine selection owner: recovery/refused mode returns BUSY, successful KS/FM adopted PASS");
}
