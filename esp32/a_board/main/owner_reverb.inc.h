/* Voice-owner identity only. The SPI owner independently confirms RVR1. */
#ifndef S3_LEGACY_HOST_TEST
static struct {bool active;control_source_ref_t source;web_v2_event_t event;} s_reverb_owner;
static web_effect_result_t s_reverb_result,s_reverb_applied;
static uint32_t s_reverb_applied_wet,s_reverb_applied_damp;

static void owner_reverb_result(const web_v2_event_t *e,uint32_t code,uint32_t applied_sequence)
{
    control_source_complete(s_reverb_owner.source,e->token,e->sequence,CONTROL_SOURCE_OUTPUT,code==FM_RESULT_OK);
    s_reverb_result=(web_effect_result_t){.token=e->token,.sequence=e->sequence,
        .context_epoch=e->context_epoch,.request_id=e->c,.code=code,.applied_sequence=applied_sequence};
    unsigned i=e->client;
    if(i<WEB_CLIENTS && s_web[i].token==e->token) {
        if(e->sequence>=s_v2_result_sequence[i]) {
            s_v2_result_sequence[i]=e->sequence;s_v2_result_context[i]=e->context_epoch;
            s_v2_result_code[i]=code;
        }
        if(code==FM_RESULT_OK && e->sequence>s_web[i].parameter_sequence)
            s_web[i].parameter_sequence=e->sequence;
    }
}
static void owner_reverb_cancel(void)
{
    if(!s_reverb_owner.active)return;
    const web_v2_event_t *e=&s_reverb_owner.event;
    engine_link_reverb_cancel(e->c);
    owner_reverb_result(e,FM_RESULT_CANCELLED,0);
    web_control_reverb_release(e->c);
    memset(&s_reverb_owner,0,sizeof(s_reverb_owner));
}
static bool owner_reverb_poll(int64_t now)
{
    engine_reverb_state_t link;engine_link_reverb_state(&link);
    if(s_reverb_applied.token && (!link.valid || link.applied_sequence!=s_reverb_applied.applied_sequence ||
       link.wet_q15!=s_reverb_applied_wet || link.damp_q15!=s_reverb_applied_damp))
        memset(&s_reverb_applied,0,sizeof(s_reverb_applied));
    if(!s_reverb_owner.active)return false;
    const web_v2_event_t *e=&s_reverb_owner.event;
    if((s_reverb_owner.source.epoch && !control_source_valid(s_reverb_owner.source)) ||
       s_web[e->client].token!=e->token || e->context_epoch!=s_control_epoch || now>=e->expires ||
       !web_control_reverb_matches(e)) {owner_reverb_cancel();return true;}
    if(link.fault!=REVERB_LINK_OK || !link.available) {
        uint32_t code=(link.fault==REVERB_LINK_CANCELLED || link.fault==REVERB_LINK_RESET)?
            FM_RESULT_CANCELLED:FM_RESULT_BACKEND_FAULT;
        engine_link_reverb_cancel(e->c);owner_reverb_result(e,code,0);
        web_control_reverb_release(e->c);memset(&s_reverb_owner,0,sizeof(s_reverb_owner));return true;
    }
    // A retired/cancelled driver identity stays retired even if a later
    // health snapshot has already cleared its transient fault code.
    if(link.request_id!=e->c || (!link.pending && link.completed_request_id!=e->c)) {
        owner_reverb_cancel();return true;
    }
    if(link.pending)return false;
    if(!link.valid || !link.applied_sequence || link.wet_q15!=e->a || link.damp_q15!=e->b) {
        engine_link_reverb_cancel(e->c);owner_reverb_result(e,FM_RESULT_BACKEND_FAULT,0);
        web_control_reverb_release(e->c);memset(&s_reverb_owner,0,sizeof(s_reverb_owner));return true;
    }
    owner_reverb_result(e,FM_RESULT_OK,link.applied_sequence);
    s_reverb_applied=s_reverb_result;s_reverb_applied_wet=e->a;s_reverb_applied_damp=e->b;
    web_control_reverb_release(e->c);memset(&s_reverb_owner,0,sizeof(s_reverb_owner));return true;
}
static void handle_web_reverb(const web_v2_event_t *e,int64_t now)
{
    owner_local_sync();owner_reverb_poll(now);
    if(!web_control_reverb_matches(e))return;
    if(e->client>=WEB_CLIENTS || !web_source_current(e->client) || s_web[e->client].token!=e->token) {
        web_control_reverb_release(e->c);return;
    }
    uint32_t code=FM_RESULT_OK;
    if(!e->token || !e->sequence || !e->context_epoch || e->op ||
       e->a>REVERB_WET_MAX || e->b>REVERB_DAMP_MAX)code=FM_RESULT_BAD_VALUE;
    else if(e->context_epoch!=s_control_epoch || now>=e->expires ||
            e->sequence<=s_web[e->client].sequence)code=FM_RESULT_CANCELLED;
    engine_reverb_state_t link;engine_link_reverb_state(&link);
    if(code==FM_RESULT_OK) {
        if(!link.available || !link.valid)code=FM_RESULT_UNSUPPORTED;
        else if(s_reverb_owner.active || link.pending)code=FM_RESULT_BUSY;
        else if(!(s_web[e->client].source.epoch?engine_link_reverb_request_source(s_web[e->client].source,e->a,e->b,e->c):
                  engine_link_reverb_request(e->a,e->b,e->c)))code=FM_RESULT_BUSY;
        else {s_reverb_owner.event=*e;s_reverb_owner.source=s_web[e->client].source;s_reverb_owner.active=true;}
    }
    if(e->sequence>s_web[e->client].sequence)s_web[e->client].sequence=e->sequence;
    if(code!=FM_RESULT_OK) {owner_reverb_result(e,code,0);web_control_reverb_release(e->c);}
}
static void owner_reverb_snapshot(web_reverb_state_t *state)
{
    engine_link_reverb_state(&state->link);
    state->pending=s_reverb_owner.active;state->result=s_reverb_result;
    if(s_reverb_owner.active) {
        const web_v2_event_t *e=&s_reverb_owner.event;
        state->request=(web_effect_result_t){.token=e->token,.sequence=e->sequence,
            .context_epoch=e->context_epoch,.request_id=e->c};
    }
    if(state->link.valid && state->link.applied_sequence==s_reverb_applied.applied_sequence &&
       state->link.wet_q15==s_reverb_applied_wet && state->link.damp_q15==s_reverb_applied_damp)
        state->applied=s_reverb_applied;
}
#endif
