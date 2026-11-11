/* HTTP owns validation and reservation only; every observation is a RAM copy. */
#define REVERB_HTTP_BODY_MAX 54u
#define REVERB_JSON(call) do {if((call)!=ESP_OK)return ESP_FAIL;} while(0)
static esp_err_t reverb_json_identity(httpd_req_t *req,const web_effect_result_t *r,bool result,bool applied)
{
    if(!r->token || !r->sequence || !r->request_id)return json_format(req,"null");
    REVERB_JSON(json_format(req,"{\"token\":%" PRIu32 ",\"sequence\":%" PRIu32 ",\"context_epoch\":%" PRIu32 ",\"request_id\":%" PRIu32,
        r->token,r->sequence,r->context_epoch,r->request_id));
    if(result)REVERB_JSON(json_format(req,",\"code\":%" PRIu32,r->code));
    if(applied)REVERB_JSON(json_format(req,",\"applied_sequence\":%" PRIu32,r->applied_sequence));
    return json_format(req,"}");
}
static esp_err_t reverb_get(httpd_req_t *req)
{
    const web_control_state_t *s=http_snapshot();
    const web_reverb_state_t *effect=&s->reverb;
    const engine_reverb_state_t *link=&effect->link;
    REVERB_JSON(json_begin(req));
    REVERB_JSON(json_format(req,"{\"api_major\":2,\"contract_version\":1,\"available\":%s,\"valid\":%s,\"pending\":%s,\"fault\":%" PRIu32 ",\"wet_q15\":",
        link->available?"true":"false",link->valid?"true":"false",
        (effect->pending || link->pending)?"true":"false",link->fault));
    if(link->available && link->valid)
        REVERB_JSON(json_format(req,"%" PRIu32 ",\"damp_q15\":%" PRIu32,link->wet_q15,link->damp_q15));
    else REVERB_JSON(json_format(req,"null,\"damp_q15\":null"));
    REVERB_JSON(json_format(req,",\"applied_sequence\":%" PRIu32 ",\"generation\":%" PRIu32 ",\"request\":",
        link->applied_sequence,link->generation));
    if(effect->pending)REVERB_JSON(reverb_json_identity(req,&effect->request,false,false));
    else REVERB_JSON(json_format(req,"null"));
    REVERB_JSON(json_format(req,",\"result\":"));
    REVERB_JSON(reverb_json_identity(req,&effect->result,true,false));
    REVERB_JSON(json_format(req,",\"applied\":"));
    if(link->available && link->valid && effect->applied.applied_sequence &&
       effect->applied.applied_sequence==link->applied_sequence)
        REVERB_JSON(reverb_json_identity(req,&effect->applied,false,true));
    else REVERB_JSON(json_format(req,"null"));
    REVERB_JSON(json_format(req,",\"confirmation\":\"synth_parameters\"}"));
    return json_end(req);
}
static bool reverb_parse(const char *body,size_t length,web_v2_event_t *event)
{
    uint32_t values[5];size_t position=0;
    for(unsigned i=0;i<5;i++) {
        if(position==length || body[position]<'0' || body[position]>'9')return false;
        uint32_t value=0;
        do {
            unsigned digit=(unsigned)(body[position++]-'0');
            if(value>(UINT32_MAX-digit)/10u)return false;
            value=value*10u+digit;
        } while(position<length && body[position]>='0' && body[position]<='9');
        values[i]=value;
        if(i<4 && (position==length || body[position++]!=' '))return false;
    }
    if(position!=length || !values[0] || !values[1] || !values[2] ||
       values[3]>REVERB_WET_MAX || values[4]>REVERB_DAMP_MAX)return false;
    *event=(web_v2_event_t){.token=values[0],.sequence=values[1],.context_epoch=values[2],
        .a=values[3],.b=values[4]};return true;
}
static esp_err_t reverb_post(httpd_req_t *req)
{
    if(!same_origin_command(req))return v2_error(req,"403 Forbidden","header_required");
    if(!req->content_len || req->content_len>REVERB_HTTP_BODY_MAX)
        return v2_error(req,"400 Bad Request","invalid_reverb");
    char body[REVERB_HTTP_BODY_MAX];size_t received=0;
    while(received<req->content_len) {
        int n=httpd_req_recv(req,body+received,req->content_len-received);
        if(n<=0)return ESP_FAIL;
        received+=(size_t)n;
    }
    evt_t event={.kind=EVT_WEB_REVERB};
    if(!reverb_parse(body,received,&event.web_v2))return v2_error(req,"400 Bad Request","invalid_reverb");
    const web_control_state_t *s=http_snapshot();
    uint32_t context=web_control_context_epoch(s);int64_t now=esp_timer_get_time();
    for(unsigned i=0;i<WEB_CLIENTS;i++) {
        if(s_sessions[i].token!=event.web_v2.token)continue;
        if(s_sessions[i].api_version!=FM_HTTP_API_MAJOR)return v2_error(req,"410 Gone","protocol_session");
        if(now-s_sessions[i].seen>=10000000LL)return v2_error(req,"410 Gone","session_expired");
        if(event.web_v2.context_epoch!=context || s_sessions[i].context_epoch!=context)
            return v2_error(req,"409 Conflict","stale_context");
        if(event.web_v2.sequence<=s_sessions[i].sequence)return v2_error(req,"409 Conflict","stale_sequence");
        if(!s->ready || !s->reverb.link.available || !s->reverb.link.valid)
            return v2_error(req,"503 Service Unavailable","reverb_unavailable");
        if(s->reverb.pending || s->reverb.link.pending)return v2_error(req,"409 Conflict","reverb_busy");
        event.web_v2.client=i;event.web_v2.expires=now+WEB_LEASE_US;
        if(!web_control_reverb_reserve(&event.web_v2,now))return v2_error(req,"409 Conflict","reverb_busy");
        s_sessions[i].sequence=event.web_v2.sequence;s_sessions[i].seen=now;
        event_bus_publish(&event);
        char response[160];
        snprintf(response,sizeof(response),"{\"api_major\":2,\"accepted\":%" PRIu32 ",\"context_epoch\":%" PRIu32 ",\"stage\":\"received\",\"fpga\":\"unverified\"}",
            event.web_v2.sequence,context);
        return reply(req,"202 Accepted",response);
    }
    return v2_error(req,"410 Gone","unknown_session");
}
#undef REVERB_JSON
#undef REVERB_HTTP_BODY_MAX
