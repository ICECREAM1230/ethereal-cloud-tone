#include "ks_timbre_control.h"
#include "ks_timbre_contract.h"
#define KJSON(call) do {if((call)!=ESP_OK)return ESP_FAIL;} while(0)
static const char *kst_phase_name(ks_timbre_phase_t phase)
{
    static const char *const names[]={"unavailable","idle","queued","uploading","switching","applied","canceled","failed","unknown"};
    return phase<=KST_UNKNOWN?names[phase]:"unknown";
}
static esp_err_t kst_preset_json(api_req_t *req,const ks_timbre_preset_t *p)
{
    KJSON(json_format(req,"{\"instrument_id\":%" PRIu32 ",\"preset_schema\":%" PRIu32 ",\"model_version\":%" PRIu32 ",\"factory_revision\":%" PRIu32,p->instrument_id,p->preset_schema,p->model_version,p->factory_revision));
#define KPHYS(f) KJSON(json_format(req,",\"" #f "\":%.17g",p->f));
    KPHYS(pluck_position) KPHYS(noise_mix) KPHYS(noise_lowpass) KPHYS(velocity_brightness)
    KPHYS(loss_weight) KPHYS(dispersion) KPHYS(release_ms) KPHYS(dry)
#undef KPHYS
    KJSON(json_format(req,",\"modes\":["));
    for(unsigned i=0;i<4;i++)KJSON(json_format(req,"%s{\"frequency_hz\":%.17g,\"t60_seconds\":%.17g,\"weight\":%.17g}",i?",":"",p->modes[i].frequency_hz,p->modes[i].t60_seconds,p->modes[i].weight));
    return json_format(req,"]}");
}
static esp_err_t core_ks_timbre_get(api_req_t *req)
{
    ks_timbre_state_t *saved=req->data->work;ks_timbre_state_read(saved);
    const ks_timbre_state_t s=*saved;
    bool available=s.link.timbre_available&&s.link.timbre_valid;
    const engine_local_state_t *local=&http_snapshot(req)->local;
    bool frozen=local->supported && (local->local_active || local->blocked || local->remote_pending);
    bool known=s.phase!=KST_IDLE || s.link.timbre_applied==s.requested_generation;
    bool writable=available&&!frozen&&known&&!s.initializing&&!s.link.busy&&!(s.link.timbre_state&0x100)&&
        s.phase!=KST_QUEUED&&s.phase!=KST_UPLOADING&&s.phase!=KST_SWITCHING&&s.phase!=KST_UNKNOWN;
    KJSON(json_begin(req));
    KJSON(json_format(req,"{\"available\":%s,\"writable\":%s,\"extension_abi\":65536,\"capabilities\":%" PRIu32 ",\"requested\":{\"instrument_id\":%" PRIu32 ",\"model_generation\":%" PRIu32 "},\"active\":",available?"true":"false",writable?"true":"false",s.link.timbre_capabilities,s.requested_instrument,s.requested_generation));
    if(available)KJSON(json_format(req,"{\"instrument_id\":%" PRIu32 ",\"model_generation\":%" PRIu32 ",\"model_version\":%" PRIu32 ",\"factory_revision\":%" PRIu32 ",\"model_crc\":%" PRIu32 ",\"image_crc\":%" PRIu32 ",\"template_generation\":%" PRIu32 "}",s.link.timbre_id,s.link.timbre_applied,s.link.timbre_version,s.link.timbre_factory,s.link.timbre_crc,s.link.timbre_image_crc,s.link.timbre_template));
    else KJSON(json_format(req,"null"));
    KJSON(json_format(req,",\"phase\":\"%s\",\"message\":\"%s\",\"dirty\":%s,\"result\":",available?kst_phase_name(s.phase):"unavailable",s.phase==KST_UNKNOWN?"Readback uncertain; explicit retry requires a stable native status.":"KST1 physical model; board audio acceptance pending.",s.dirty?"true":"false"));
    if(s.result.request_id)KJSON(json_format(req,"{\"token\":%" PRIu32 ",\"sequence\":%" PRIu32 ",\"context_epoch\":%" PRIu32 ",\"request_id\":%" PRIu32 ",\"code\":%" PRIu32 "}",s.result.token,s.result.sequence,s.result.context_epoch,s.result.request_id,s.result.code));
    else KJSON(json_format(req,"null"));
    KJSON(json_format(req,",\"preset\":"));if(available && s.link.timbre_applied!=s.requested_generation)KJSON(json_format(req,"null"));else KJSON(kst_preset_json(req,&s.setting.preset));KJSON(json_format(req,"}"));return json_end(req);
}
static esp_err_t core_ks_presets_get(api_req_t *req)
{
    api_ks_directory_t *saved=req->data->work;ks_timbre_slot_t *slots=saved->slots;
    ks_timbre_startup_t startup;ks_timbre_directory(slots,&startup);
    KJSON(json_begin(req));KJSON(json_format(req,"{\"factory\":[{\"id\":0,\"name\":\"Legacy\"},{\"id\":1,\"name\":\"Harp\"},{\"id\":2,\"name\":\"Guzheng\"}],\"user\":["));
    unsigned count=0;for(unsigned i=0;i<8;i++)if(slots[i].occupied) {
        char escaped[KS_TIMBRE_NAME*6+1];dds_escape(slots[i].setting.name,escaped,sizeof escaped);
        KJSON(json_format(req,"%s{\"slot\":%u,\"name\":\"%s\",\"instrument_id\":%" PRIu32 "}",count++?",":"",i,escaped,slots[i].setting.preset.instrument_id));
    }
    KJSON(json_format(req,"],\"startup\":{\"enabled\":%s,\"slot\":",startup.enabled?"true":"false"));
    if(startup.has_slot)KJSON(json_format(req,"%" PRIu32,startup.slot));else KJSON(json_format(req,"null"));
    KJSON(json_format(req,",\"instrument_id\":"));if(startup.has_slot)KJSON(json_format(req,"null"));else KJSON(json_format(req,"%" PRIu32,startup.instrument_id));
    KJSON(json_format(req,"}}"));return json_end(req);
}
static esp_err_t core_ks_document_get(api_req_t *req)
{
    api_ks_document_t *saved=req->data->work;ks_timbre_setting_t *setting=&saved->setting;size_t n=api_query_len(req);
    if(n) {
        char query[24];uint32_t slot;
        if(n>=sizeof query||api_query_copy(req,query,sizeof query)!=ESP_OK||strncmp(query,"slot=",5)||!query_u32(query+5,&slot)||slot>=8)return core_v2_error(req,"400 Bad Request","invalid_ks_slot");
        if(!ks_timbre_slot_read(slot,setting))return core_v2_error(req,"404 Not Found","ks_slot_empty");
    } else {
        ks_timbre_state_t *saved_state=&saved->state;ks_timbre_state_read(saved_state);
        const ks_timbre_state_t state=*saved_state;
        if(state.phase!=KST_APPLIED||!state.link.timbre_valid||state.link.timbre_applied!=state.requested_generation)return core_v2_error(req,"409 Conflict","ks_not_applied");
        *setting=state.setting;
    }
    char *text=malloc(KS_TIMBRE_DOCUMENT_MAX+1);if(!text)return core_v2_error(req,"503 Service Unavailable","memory");
    if(!ks_timbre_document_emit(setting,text,KS_TIMBRE_DOCUMENT_MAX+1)){free(text);return core_v2_error(req,"500 Internal Server Error","ks_document");}
    api_set_status(req,"200 OK");api_set_type(req,"text/plain; charset=utf-8");api_set_header(req,"Cache-Control","no-store");api_set_header(req,"X-Content-Type-Options","nosniff");
    return api_adopt_text(req,text,strlen(text),"text/plain; charset=utf-8");
}
static esp_err_t core_ks_post(api_req_t *req,ks_timbre_op_t op)
{
    if(!same_origin_command(req))return core_v2_error(req,"403 Forbidden","header_required");
    if(!req->content_len||req->content_len>KS_TIMBRE_HTTP_MAX)return core_v2_error(req,"400 Bad Request","invalid_ks_body");
    const char *body=req->input->body;
    api_ks_post_t *saved=req->data->work;ks_timbre_request_t request;
    bool valid=ks_timbre_request_parse(body,req->content_len,op,&request);
    if(!valid)return core_v2_error(req,"400 Bad Request","invalid_ks_body");
    const web_control_state_t *s=http_snapshot(req);uint32_t context=web_control_context_epoch(s);int64_t now=esp_timer_get_time();
    for(unsigned i=0;i<WEB_CLIENTS;i++) {
        if(!session_matches(req,i,request.event.token))continue;
        if(s_sessions[i].api_version!=FM_HTTP_API_MAJOR)return core_v2_error(req,"410 Gone","protocol_session");
        if(now-s_sessions[i].seen>=10000000LL)return core_v2_error(req,"410 Gone","session_expired");
        if(request.event.context_epoch!=context||s_sessions[i].context_epoch!=context)return core_v2_error(req,"409 Conflict","stale_context");
        if(request.event.sequence<=s_sessions[i].sequence)return core_v2_error(req,"409 Conflict","stale_sequence");
        ks_timbre_state_t *saved_state=&saved->state;ks_timbre_state_read(saved_state);
        const ks_timbre_state_t state=*saved_state;
        if(!s->ready||!state.link.timbre_available||!state.link.timbre_valid)return core_v2_error(req,"503 Service Unavailable","ks_unavailable");
        if(s->local.supported && (s->local.local_active || s->local.blocked || s->local.remote_pending))return core_v2_error(req,"409 Conflict","ks_local_control");
        if(state.initializing || (state.phase==KST_IDLE && state.link.timbre_applied!=state.requested_generation))return core_v2_error(req,"409 Conflict","ks_initializing");
        if(state.link.busy||(state.link.timbre_state&0x100)||state.phase==KST_QUEUED||state.phase==KST_UPLOADING||state.phase==KST_SWITCHING||state.phase==KST_UNKNOWN)return core_v2_error(req,"409 Conflict","ks_busy");
        if(!source_reserve(req,s_sessions[i].source,request.event.token,request.event.sequence,CONTROL_SOURCE_OUTPUT))
            return core_v2_error(req,"409 Conflict","control_source_busy");
        request.event.client=i;request.event.expires=now+WEB_LEASE_US;
        if(!ks_timbre_reserve(&request,now)) {
            control_source_complete(s_sessions[i].source,request.event.token,request.event.sequence,CONTROL_SOURCE_OUTPUT,false);
            return core_v2_error(req,"409 Conflict","ks_busy");
        }
        s_sessions[i].sequence=request.event.sequence;s_sessions[i].seen=now;
        const evt_t event={.kind=EVT_WEB_KS_TIMBRE,.source=s_sessions[i].source,.web_v2=request.event};event_bus_publish(&event);
        char response[160];snprintf(response,sizeof response,"{\"api_major\":2,\"accepted\":%" PRIu32 ",\"context_epoch\":%" PRIu32 ",\"stage\":\"received\",\"fpga\":\"unverified\"}",request.event.sequence,context);
        return reply(req,"202 Accepted",response);
    }
    return core_v2_error(req,"410 Gone","unknown_session");
}
static esp_err_t core_ks_timbre_post(api_req_t *r){return core_ks_post(r,KST_APPLY);}
static esp_err_t core_ks_presets_post(api_req_t *r){return core_ks_post(r,KST_SAVE);}
static esp_err_t core_ks_load_post(api_req_t *r){return core_ks_post(r,KST_LOAD);}
static esp_err_t core_ks_document_post(api_req_t *r){return core_ks_post(r,KST_IMPORT);}
static esp_err_t core_ks_startup_post(api_req_t *r){return core_ks_post(r,KST_STARTUP);}
#undef KJSON
