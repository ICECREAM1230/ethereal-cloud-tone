#include "fdc_control.h"
#define FJSON(call) do {if((call)!=ESP_OK)return ESP_FAIL;} while(0)
static const char *fdc_stage(uint32_t code)
{return code==FDC_SENT?"transport_sent":code==FDC_CANCELLED?"canceled":code==FDC_UNKNOWN?"unknown":"failed";}
static esp_err_t fdc_identity_json(api_req_t *req,const fdc_result_t *r,bool result)
{
    if(!r->request_id)return json_format(req,"null");
    FJSON(json_format(req,"{\"token\":%" PRIu32 ",\"sequence\":%" PRIu32 ",\"context_epoch\":%" PRIu32 ",\"request_id\":%" PRIu32 ",\"op\":%" PRIu32,
        r->token,r->sequence,r->context_epoch,r->request_id,r->op));
    if(result)FJSON(json_format(req,",\"code\":%" PRIu32,r->code));
    return json_format(req,",\"stage\":\"%s\"}",result?fdc_stage(r->code):"queued");
}
static esp_err_t core_fdc_get(api_req_t *req)
{
    web_fdc_state_t state;fdc_control_state_read(&state);engine_link_fdc_state(&state.link);
    const engine_fdc_state_t *s=&state.link;uint32_t word=s->word;
    bool fresh=s->fresh && esp_timer_get_time()>=s->observed_us && esp_timer_get_time()-s->observed_us<=FDC_FRESH_US;
    FJSON(json_begin(req));
    FJSON(json_format(req,"{\"api_major\":2,\"contract_version\":1,\"available\":%s,\"valid\":%s,\"fresh\":%s,\"status_word\":%" PRIu32 ",\"revision\":%" PRIu32,
        s->available?"true":"false",s->valid?"true":"false",fresh?"true":"false",word,(word>>8)&255u));
    bool fm_theremin=(word&UINT32_C(0xffff0000))==UINT32_C(0xfdc20000);
    bool sub_theremin=(word&UINT32_C(0xffff0000))==UINT32_C(0xfdc10000);
    FJSON(json_format(req,",\"audio_engine\":\"%s\",\"timbre\":\"%s\",\"continuous\":%s",
        fm_theremin?"fm":sub_theremin?"subtractive":"unknown",
        fm_theremin?"electric_piano_tine_sustained":sub_theremin?"current_subtractive_patch":"unknown",
        (fm_theremin||sub_theremin)?"true":"false"));
#define FFLAG(name,bit) FJSON(json_format(req,",\"" name "\":%s",(word&(1u<<(bit)))?"true":"false"))
    FFLAG("eligible",1);FFLAG("requested_enable",2);FFLAG("driver_enabled",3);FFLAG("busy",4);
    FFLAG("driver_fault",5);FFLAG("local_error",6);FFLAG("user_config_active",7);
#undef FFLAG
    FJSON(json_format(req,",\"pending\":%s,\"request\":",(state.pending || s->pending || s->disable_pending)?"true":"false"));
    FJSON(fdc_identity_json(req,&state.request,false));FJSON(json_format(req,",\"result\":"));
    FJSON(fdc_identity_json(req,&state.result,true));
    const engine_fdc_raw_state_t *t=&s->raw;
    FJSON(json_format(req,",\"confirmation\":\"transport_only\",\"raw\":{\"version\":1,\"supported\":%s,\"valid\":%s,\"fresh\":%s,\"sample_valid\":%s,\"sample_fresh\":%s",
        t->supported?"true":"false",t->valid?"true":"false",t->fresh?"true":"false",t->sample_valid?"true":"false",t->sample_fresh?"true":"false"));
    if(t->valid && t->sample_valid)
        FJSON(json_format(req,",\"ch0\":%" PRIu32 ",\"ch1\":%" PRIu32,t->raw0,t->raw1));
    else FJSON(json_format(req,",\"ch0\":null,\"ch1\":null"));
    if(t->valid) {
        uint32_t percent=t->gain_q16*10000u/65536u;
        FJSON(json_format(req,",\"gain_q16\":%" PRIu32 ",\"gain_percent\":%" PRIu32 ".%02" PRIu32,t->gain_q16,percent/100u,percent%100u));
    } else FJSON(json_format(req,",\"gain_q16\":null,\"gain_percent\":null"));
    FJSON(json_format(req,",\"sequence\":%" PRIu32 ",\"revision\":%" PRIu32 ",\"quality_ch0\":%" PRIu32 ",\"quality_ch1\":%" PRIu32 ",\"muted\":%s,\"pitch_stale\":%s,\"volume_stale\":%s}}",
        t->sequence,t->revision,t->quality0,t->quality1,t->muted?"true":"false",t->pitch_stale?"true":"false",t->volume_stale?"true":"false"));
    return json_end(req);
}
static bool fdc_parse(const char *body,size_t length,fdc_request_t *request)
{
    uint32_t values[10];size_t at=0;
    for(unsigned i=0;i<10;i++) {
        if(at==length || body[at]<'0' || body[at]>'9')return false;
        uint32_t value=0;
        do {unsigned digit=(unsigned)(body[at++]-'0');if(value>(UINT32_MAX-digit)/10u)return false;value=value*10u+digit;}
        while(at<length && body[at]>='0' && body[at]<='9');
        values[i]=value;if(i<9 && (at==length || body[at++]!=' '))return false;
    }
    if(at!=length || !values[0] || !values[1] || !values[2])return false;
    memset(request,0,sizeof *request);request->event.token=values[0];request->event.sequence=values[1];
    request->event.context_epoch=values[2];request->op=values[3];
    memcpy(request->words,values+4,sizeof request->words);return fdc_request_valid(request);
}
static esp_err_t core_fdc_post(api_req_t *req)
{
    if(!same_origin_command(req))return core_v2_error(req,"403 Forbidden","header_required");
    if(!req->content_len || req->content_len>FDC_HTTP_BODY_MAX)return core_v2_error(req,"400 Bad Request","invalid_fdc");
    char body[FDC_HTTP_BODY_MAX];size_t received=0;
    while(received<req->content_len){int n=api_recv(req,body+received,req->content_len-received);if(n<=0)return ESP_FAIL;received+=(size_t)n;}
    fdc_request_t request;if(!fdc_parse(body,received,&request))return core_v2_error(req,"400 Bad Request","invalid_fdc");
    const web_control_state_t *s=http_snapshot(req);uint32_t context=web_control_context_epoch(s);int64_t now=esp_timer_get_time();
    for(unsigned i=0;i<WEB_CLIENTS;i++) {
        if(!session_matches(req,i,request.event.token))continue;
        if(s_sessions[i].api_version!=FM_HTTP_API_MAJOR)return core_v2_error(req,"410 Gone","protocol_session");
        if(now-s_sessions[i].seen>=10000000LL)return core_v2_error(req,"410 Gone","session_expired");
        if(request.event.context_epoch!=context || s_sessions[i].context_epoch!=context)return core_v2_error(req,"409 Conflict","stale_context");
        if(request.event.sequence<=s_sessions[i].sequence)return core_v2_error(req,"409 Conflict","stale_sequence");
        web_fdc_state_t f;fdc_control_state_read(&f);engine_link_fdc_state(&f.link);
        if(!f.link.available || !f.link.valid || !f.link.fresh || now<f.link.observed_us || now-f.link.observed_us>FDC_FRESH_US)
            return core_v2_error(req,"503 Service Unavailable","fdc_unavailable");
        if(request.op!=FDC_DISABLE) {
            if((request.op==FDC_ENABLE || request.op==FDC_CALIBRATE) && !(f.link.word&2u))
                return core_v2_error(req,"409 Conflict","fdc_sub_not_ready");
            if(f.pending || f.link.pending || f.link.disable_pending || (f.link.word&0x10u) ||
               ((request.op==FDC_ENABLE || request.op==FDC_CALIBRATE) && (f.link.word&0x20u)))return core_v2_error(req,"409 Conflict","fdc_busy_or_fault");
        }
        if(!source_reserve(req,s_sessions[i].source,request.event.token,request.event.sequence,CONTROL_SOURCE_OUTPUT))
            return core_v2_error(req,"409 Conflict","control_source_busy");
        request.event.client=i;request.event.expires=now+WEB_LEASE_US;
        if(!fdc_control_reserve(&request,now)) {
            control_source_complete(s_sessions[i].source,request.event.token,request.event.sequence,CONTROL_SOURCE_OUTPUT,false);
            return core_v2_error(req,"409 Conflict","fdc_busy");
        }
        s_sessions[i].sequence=request.event.sequence;s_sessions[i].seen=now;
        const evt_t event={.kind=EVT_WEB_FDC,.source=s_sessions[i].source,.web_v2=request.event};event_bus_publish(&event);
        char response[220];snprintf(response,sizeof response,"{\"api_major\":2,\"accepted\":%" PRIu32 ",\"context_epoch\":%" PRIu32 ",\"request_id\":%" PRIu32 ",\"stage\":\"queued\",\"confirmation\":\"transport_only\"}",request.event.sequence,context,request.event.c);
        return reply(req,"202 Accepted",response);
    }
    return core_v2_error(req,"410 Gone","unknown_session");
}
#undef FJSON
