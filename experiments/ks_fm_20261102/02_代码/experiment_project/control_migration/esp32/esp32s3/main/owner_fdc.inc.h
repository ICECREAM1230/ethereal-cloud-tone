/* Patch/control requests only; AE remains the sole sensor/MIDI voice owner. */
#if defined(ESP_PLATFORM) || defined(S3_FDC_HOST_TEST)
static struct {bool active;fdc_request_t request;} s_fdc_owner;
static fdc_result_t s_fdc_result;
static void owner_fdc_result(const fdc_request_t *request,uint32_t code)
{
    const web_v2_event_t *e=&request->event;
    s_fdc_result=(fdc_result_t){.token=e->token,.sequence=e->sequence,.context_epoch=e->context_epoch,
        .request_id=e->c,.op=request->op,.code=code};
    /* Transport completion never advances parameter_sequence/native adoption. */
}
static void owner_fdc_cancel(void)
{
    if(s_fdc_owner.active) {
        engine_link_fdc_cancel(s_fdc_owner.request.event.c,true);
        owner_fdc_result(&s_fdc_owner.request,FDC_CANCELLED);s_fdc_owner.active=false;
    }
    fdc_request_t waiting;
    if(fdc_control_cancel_reserved(&waiting))owner_fdc_result(&waiting,FDC_CANCELLED);
}
static void owner_fdc_stop(void)
{owner_fdc_cancel();engine_link_fdc_stop();}
static bool owner_fdc_poll(int64_t now)
{
    engine_fdc_state_t link;engine_link_fdc_state(&link);bool changed=false;
    if(s_fdc_owner.active) {
        const web_v2_event_t *e=&s_fdc_owner.request.event;
        if(e->client>=WEB_CLIENTS || s_web[e->client].token!=e->token ||
           e->context_epoch!=s_control_epoch || now>=e->expires) {
            owner_fdc_cancel();changed=true;
        } else if(link.completed_request_id==e->c) {
            owner_fdc_result(&s_fdc_owner.request,link.result);s_fdc_owner.active=false;changed=true;
        } else if(!link.valid || !link.available || !link.fresh || !link.pending || link.request_id!=e->c) {
            engine_link_fdc_cancel(e->c,true);owner_fdc_result(&s_fdc_owner.request,FDC_UNKNOWN);
            s_fdc_owner.active=false;changed=true;
        }
    }
    web_fdc_state_t state={.link=link,.pending=s_fdc_owner.active,.result=s_fdc_result};
    if(s_fdc_owner.active) {
        const fdc_request_t *r=&s_fdc_owner.request;const web_v2_event_t *e=&r->event;
        state.request=(fdc_result_t){.token=e->token,.sequence=e->sequence,.context_epoch=e->context_epoch,.request_id=e->c,.op=r->op};
    }
    fdc_control_state_publish(&state);return changed;
}
static void handle_web_fdc(const web_v2_event_t *e,int64_t now)
{
    fdc_request_t request;if(!fdc_control_claim(e,&request))return;
    fdc_control_release(e->c);owner_local_sync();
    uint32_t code=FDC_SENT;
    if(e->client>=WEB_CLIENTS || s_web[e->client].token!=e->token ||
       e->context_epoch!=s_control_epoch || now>=e->expires || e->sequence<=s_web[e->client].sequence)
        code=FDC_CANCELLED;
    else if(s_fdc_owner.active && request.op!=FDC_DISABLE)code=FDC_FAILED;
    else {
        if(s_fdc_owner.active) {
            engine_link_fdc_cancel(s_fdc_owner.request.event.c,true);
            owner_fdc_result(&s_fdc_owner.request,FDC_CANCELLED);s_fdc_owner.active=false;
        }
        if(!engine_link_fdc_request(&request))code=FDC_FAILED;
        else {s_fdc_owner.request=request;s_fdc_owner.active=true;}
    }
    if(e->client<WEB_CLIENTS && s_web[e->client].token==e->token && e->sequence>s_web[e->client].sequence)
        s_web[e->client].sequence=e->sequence;
    if(code!=FDC_SENT)owner_fdc_result(&request,code);
    owner_fdc_poll(now);
}
#else
static void owner_fdc_cancel(void){}
static void owner_fdc_stop(void){}
static bool owner_fdc_poll(int64_t now){(void)now;return false;}
static void handle_web_fdc(const web_v2_event_t *e,int64_t now){(void)e;(void)now;}
#endif
