/* HTTP task: validation/immutable queue only; no SPI or patch mutation. */
#include "web_additive.h"
static esp_err_t json_additive_patch(httpd_req_t *req,const additive_patch_t *p)
{
    JSON_TRY(json_format(req,"{\"bypass_lowpass\":%s,\"centroid_q8\":%u,\"slope\":%u,\"comb_period\":%u,\"comb_depth\":%u,\"master_gain\":%u,\"master_pan\":%u,\"bend_range\":%u,\"envelopes\":[",
        p->bypass_lowpass?"true":"false",p->centroid_q8,p->slope,p->comb_period,p->comb_depth,p->master_gain,p->master_pan,p->bend_range));
    for(unsigned i=0;i<ADDITIVE_ENVELOPE_GROUPS;i++) {
        const additive_envelope_t *e=&p->env[i];
        JSON_TRY(json_format(req,"%s{\"attack_inc\":%u,\"decay_inc\":%u,\"sustain\":%u,\"release_inc\":%u}",i?",":"",e->attack_inc,e->decay_inc,e->sustain,e->release_inc));
    }
    JSON_TRY(json_format(req,"],\"partials\":["));
    for(unsigned i=0;i<ADDITIVE_PARTIALS;i++) {
        const additive_partial_t *p0=&p->partials[i];
        JSON_TRY(json_format(req,"%s{\"ratio_q16\":%" PRIu32 ",\"weight\":%u,\"group\":%u}",i?",":"",p0->ratio_q16,p0->weight,p0->group));
    }
    return json_format(req,"]}");
}
static esp_err_t json_additive_state(httpd_req_t *req,const web_control_state_t *s)
{
    const additive_link_state_t *a=&s->additive.link;
    bool busy=a->busy || s->additive.pending;
    JSON_TRY(json_format(req,"{\"contract_version\":1,\"available\":%s,\"valid\":%s,\"ready\":%s,\"busy\":%s,\"instance_id\":%" PRIu32 ",\"revision\":%" PRIu32 ",\"patch\":",
        a->available?"true":"false",a->valid?"true":"false",
        (a->ready && !busy && !s->mode_recovering)?"true":"false",busy?"true":"false",a->instance_id,a->revision));
    if(a->valid && a->patch_known)JSON_TRY(json_additive_patch(req,&a->confirmed_patch));else JSON_TRY(json_format(req,"null"));
    JSON_TRY(json_format(req,",\"applied\":"));
    if(a->valid && a->patch_known && s->additive.token && s->additive.commit_id)
        JSON_TRY(json_format(req,"{\"token\":%" PRIu32 ",\"sequence\":%" PRIu32 ",\"context_epoch\":%" PRIu32 ",\"commit_id\":%" PRIu32 "}",
            s->additive.token,s->additive.sequence,s->additive.context_epoch,s->additive.commit_id));
    else JSON_TRY(json_format(req,"null"));
    return json_format(req,"}");
}
static esp_err_t additive_patch_post(httpd_req_t *req)
{
    if(!same_origin_command(req))return v2_error(req,"403 Forbidden","header_required");
    if(!req->content_len || req->content_len>WEB_ADDITIVE_MAX_BODY_BYTES)
        return v2_error(req,"400 Bad Request","body_length");
    // Serialized HTTP task; keep the maximum body off the 6 KiB task stack.
    static char body[WEB_ADDITIVE_MAX_BODY_BYTES];
    static web_additive_request_t request;
    size_t got=0;
    while(got<req->content_len) {
        int n=httpd_req_recv(req,body+got,req->content_len-got);
        if(n<=0)return ESP_FAIL;
        got+=(size_t)n;
    }
    if(!web_additive_parse(body,got,&request))return v2_error(req,"400 Bad Request","invalid_patch");
    const web_control_state_t *s=http_snapshot();
    uint32_t context=web_control_context_epoch(s);
    int64_t now=esp_timer_get_time();
    for(unsigned i=0;i<WEB_CLIENTS;i++) {
        if(s_sessions[i].token!=request.token)continue;
        if(s_sessions[i].api_version!=FM_HTTP_API_MAJOR)return v2_error(req,"410 Gone","protocol_session");
        if(now-s_sessions[i].seen>=10000000LL)return v2_error(req,"410 Gone","session_expired");
        if(request.context_epoch!=context || s_sessions[i].context_epoch!=context)return v2_error(req,"409 Conflict","stale_context");
        if(request.sequence<=s_sessions[i].sequence)return v2_error(req,"409 Conflict","stale_sequence");
        if(CONFIG_FPGA_FM_ONLY || !s->ready || !s->additive.link.available || !s->additive.link.valid ||
            !s->additive.link.patch_known ||
            !s->additive.link.ready || !s->additive.link.selected || s->additive.pending || s->additive.link.busy ||
            s->mode_switching || s->mode_recovering || s->applied_engine!=WEB_ENGINE_ADDITIVE)
            return v2_error(req,"409 Conflict","additive_not_ready");
        uint32_t id;
        if(!web_additive_queue_push(&request,i,now+WEB_LEASE_US,&id))return v2_error(req,"503 Service Unavailable","additive_queue_full");
        evt_t event={.kind=EVT_WEB_ADDITIVE,.web_v2={.token=request.token,.sequence=request.sequence,
            .context_epoch=context,.client=i,.expires=now+WEB_LEASE_US,.a=id}};
        s_sessions[i].sequence=request.sequence;s_sessions[i].seen=now;
        event_bus_publish(&event);
        char response[160];
        snprintf(response,sizeof(response),"{\"api_major\":2,\"accepted\":%" PRIu32 ",\"context_epoch\":%" PRIu32 ",\"stage\":\"received\",\"fpga\":\"unverified\"}",request.sequence,context);
        return reply(req,"202 Accepted",response);
    }
    return v2_error(req,"410 Gone","unknown_session");
}
