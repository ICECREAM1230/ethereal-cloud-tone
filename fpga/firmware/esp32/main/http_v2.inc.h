// Included by wifi_web.c: the same HTTP task exclusively owns all sessions->
#include "fm_diagnostics.h"
#include <stdarg.h>
#include "web_control_v2.h"
#include "fm_contract.h"
#include "fm_only_contract.h"

// Emit-local blocks keep both transports independent of slow sinks.
static esp_err_t json_send(api_req_t *req,const char *bytes,size_t length)
{return api_chunk(req,bytes,length);}
static esp_err_t json_flush(api_req_t *req)
{
    if(!req->json_used)return ESP_OK;
    esp_err_t result=json_send(req,req->json_block,req->json_used);
    req->json_used=0;
    return result;
}
static esp_err_t json_append(api_req_t *req,const char *bytes,size_t length)
{
    if(req->preparing)return api_chunk(req,bytes,length);
    while(length) {
        size_t n=CONTROL_JSON_BLOCK_BYTES-req->json_used;
        if(n>length)n=length;
        memcpy(req->json_block+req->json_used,bytes,n);req->json_used+=n;
        bytes+=n;length-=n;
        if(req->json_used==CONTROL_JSON_BLOCK_BYTES && json_flush(req)!=ESP_OK)return ESP_FAIL;
    }
    return ESP_OK;
}
static esp_err_t json_end(api_req_t *req)
{
    if(json_flush(req)!=ESP_OK)return ESP_FAIL;
    return json_send(req,NULL,0);
}
static esp_err_t core_v2_error(api_req_t *req, const char *status_code, const char *code)
{
    char body[128];
    snprintf(body, sizeof(body), "{\"api_major\":%" PRIu32 ",\"code\":\"%s\"}", FM_HTTP_API_MAJOR, code);
    return reply(req, status_code, body);
}
static esp_err_t json_begin(api_req_t *req)
{
    req->json_used=0;
    api_set_status(req, "200 OK");
    api_set_type(req, "application/json; charset=utf-8");
    api_set_header(req, "Cache-Control", "no-store");
    return api_set_header(req, "X-Content-Type-Options", "nosniff");
}
static esp_err_t json_format(api_req_t *req, const char *format, ...)
{
    char text[512]; va_list args; va_start(args, format);
    int n = vsnprintf(text, sizeof(text), format, args); va_end(args);
    if (n < 0 || (size_t)n >= sizeof(text)) return ESP_FAIL;
    return json_append(req, text, n);
}
static esp_err_t json_string(api_req_t *req, const char *value)
{
    if (json_append(req, "\"", 1) != ESP_OK) return ESP_FAIL;
    char text[96]; unsigned used = 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (used + 6 >= sizeof(text)) {
            if (json_append(req, text, used) != ESP_OK) return ESP_FAIL;
            used = 0;
        }
        if (*p == '"' || *p == '\\') { text[used++] = '\\'; text[used++] = *p; }
        else if (*p < 32) { snprintf(text + used, 7, "\\u%04x", *p); used += 6; }
        else text[used++] = *p;
    }
    if (used && json_append(req, text, used) != ESP_OK) return ESP_FAIL;
    return json_append(req, "\"", 1);
}
#define JSON_TRY(call) do { if ((call) != ESP_OK) return ESP_FAIL; } while (0)
static const char *fm_reason(uint32_t reason)
{
    switch(reason) {
    case FM_LINK_NO_CAPABILITY:return "capability_missing";
    case FM_LINK_ABI:return "abi_mismatch";
    case FM_LINK_PROFILE:return "profile_mismatch";
    case FM_LINK_READBACK:return "spi_readback_failed";
    case FM_LINK_TIMEOUT:return "confirmation_timeout";
    case FM_LINK_CONTEXT:return "context_changed_select_required";
    case FM_LINK_BACKEND:return "backend_fault";
    default:return "capability_not_verified";
    }
}
static bool fm_runtime(const web_control_state_t *s,uint32_t profile)
{return s->fm.status.available && s->fm.status.valid && s->fm.status.ready &&
    (s->fm.status.flags&FM_FLAG_READY) && !(s->fm.status.flags&FM_FLAG_FAULT) && s->fm.status.profile_id==profile;}
static esp_err_t json_identity(api_req_t *req,engine_fm_identity_t id)
{
    if(!id.sequence_id)return json_format(req,"null");
    return json_format(req,"{\"session_id\":%" PRIu32 ",\"engine_epoch\":%" PRIu32 ",\"sequence_id\":%" PRIu32 "}",id.session_id,id.engine_epoch,id.sequence_id);
}
static esp_err_t json_result(api_req_t *req,engine_fm_result_t result)
{
    if(!result.identity.sequence_id)return json_format(req,"null");
    return json_format(req,"{\"session_id\":%" PRIu32 ",\"engine_epoch\":%" PRIu32 ",\"sequence_id\":%" PRIu32 ",\"code\":%" PRIu32 "}",result.identity.session_id,result.identity.engine_epoch,result.identity.sequence_id,result.code);
}
static esp_err_t json_fm_state(api_req_t *req,const engine_fm_state_t *fm)
{
    const engine_fm_status_t *s=&fm->status;
    const fm_profile_t *p=s->available?fm_profile(s->profile_id):NULL;
    bool known=p!=NULL;
    JSON_TRY(json_format(req,"{\"available\":%s,\"valid\":%s,\"phase\":%" PRIu32 ",\"reason\":",s->available?"true":"false",s->valid?"true":"false",s->phase));
    if(known && !s->reason)JSON_TRY(json_format(req,"null"));else JSON_TRY(json_string(req,fm_reason(s->reason)));
    JSON_TRY(json_format(req,",\"backend_id\":%" PRIu32 ",\"profile_id\":%" PRIu32 ",\"session_id\":%" PRIu32 ",\"engine_epoch\":%" PRIu32 ",\"topology_generation\":%" PRIu32 ",\"profile_version\":",
        known?s->backend_id:0,known?s->profile_id:0,s->session_id,s->engine_epoch,s->topology_generation));
    if(known)JSON_TRY(json_format(req,"%" PRIu32,s->profile_version));else JSON_TRY(json_format(req,"null"));
    JSON_TRY(json_format(req,",\"profile_sha256\":"));
    if(known)JSON_TRY(json_string(req,p->profile_sha256));else JSON_TRY(json_format(req,"null"));
    JSON_TRY(json_format(req,",\"voice_count\":%" PRIu32 ",\"patch_operator_count\":%" PRIu32 ",\"operators_per_voice\":[",known?s->voice_count:0,known?p->patch_operator_count:0));
    if(known)for(unsigned i=0;i<s->voice_count && i<ENGINE_FM_DESCRIPTOR_VOICES;i++)JSON_TRY(json_format(req,"%s%" PRIu32,i?",":"",fm->operators_per_voice[i]));
    JSON_TRY(json_format(req,"],\"source_rate\":"));
    if(known && fm->source_denominator)JSON_TRY(json_format(req,"{\"numerator\":%" PRIu32 ",\"denominator\":%" PRIu32 "}",fm->source_numerator,fm->source_denominator));else JSON_TRY(json_format(req,"null"));
    JSON_TRY(json_format(req,",\"output_rate\":"));
    if(known && fm->output_denominator)JSON_TRY(json_format(req,"{\"numerator\":%" PRIu32 ",\"denominator\":%" PRIu32 "}",fm->output_numerator,fm->output_denominator));else JSON_TRY(json_format(req,"null"));
    JSON_TRY(json_format(req,",\"flags\":"));
    if(known)JSON_TRY(json_format(req,"%" PRIu32,s->flags));else JSON_TRY(json_format(req,"null"));
    JSON_TRY(json_format(req,",\"features\":"));
    if(known)JSON_TRY(json_format(req,"%" PRIu32,s->features));else JSON_TRY(json_format(req,"null"));
    JSON_TRY(json_format(req,",\"accepted\":"));JSON_TRY(json_identity(req,known?s->accepted:(engine_fm_identity_t){0}));
    JSON_TRY(json_format(req,",\"completed\":"));JSON_TRY(json_identity(req,known?s->completed:(engine_fm_identity_t){0}));
    JSON_TRY(json_format(req,",\"result\":"));JSON_TRY(json_result(req,known?s->result:(engine_fm_result_t){0}));
    JSON_TRY(json_format(req,",\"panic\":"));
    if(known) {
        JSON_TRY(json_format(req,"{\"accepted\":"));JSON_TRY(json_identity(req,s->panic_accepted));
        JSON_TRY(json_format(req,",\"completed\":"));JSON_TRY(json_identity(req,s->panic_completed));
        JSON_TRY(json_format(req,",\"result\":"));JSON_TRY(json_result(req,s->panic_result));JSON_TRY(json_format(req,"}"));
    } else JSON_TRY(json_format(req,"null"));
    JSON_TRY(json_format(req,",\"parameter_values\":["));
    if(known)for(unsigned i=0;i<fm->parameter_count && i<ENGINE_FM_PARAMETER_SLOTS;i++) {
        const engine_fm_parameter_t *v=&fm->parameters[i];
        JSON_TRY(json_format(req,"%s{\"parameter_id\":%" PRIu32 ",\"target_word\":%" PRIu32 ",\"value_u32\":%" PRIu32 ",\"sequence_id\":%" PRIu32 "}",i?",":"",v->parameter_id,v->target_word,v->value_u32,v->sequence_id));
    }
    JSON_TRY(json_format(req,"],\"voice_generations\":["));
    if(known)for(unsigned i=0;i<s->voice_count && i<ENGINE_FM_DESCRIPTOR_VOICES;i++)JSON_TRY(json_format(req,"%s%" PRIu32,i?",":"",fm->voice_generations[i]));
    return json_format(req,"]}");
}
static esp_err_t core_v2_capabilities(api_req_t *req)
{
    const web_control_state_t *s=http_snapshot(req);
    JSON_TRY(json_begin(req));
    JSON_TRY(json_format(req, "{\"api_major\":%" PRIu32 ",\"api_minor\":%" PRIu32 ",\"schema_sha256\":\"%s\",\"profiles_sha256\":\"%s\",\"contract_sha256\":\"%s\",\"context_epoch\":%" PRIu32 ",",
        FM_HTTP_API_MAJOR, FM_HTTP_API_MINOR, FM_SCHEMA_SHA256, FM_PROFILES_SHA256,
        FM_CONTRACT_SHA256, web_control_context_epoch(s)));
    JSON_TRY(json_format(req, "\"limits\":{\"clients\":%u,\"key_count\":%u,\"lease_ms\":%" PRIu32 ",\"max_body_bytes\":%" PRIu32 ",\"catalog_page_max\":8},\"engines\":[{\"id\":%" PRIu32 ",\"available\":%s},{\"id\":%" PRIu32 ",\"available\":%s},{\"id\":%" PRIu32 ",\"available\":%s}],\"catalog_profiles\":[",
        WEB_CLIENTS, WEB_KEYS, FM_PERFORMANCE_LEASE_MS, FM_HTTP_COMMAND_MAX_BYTES,
        FM_ENGINE_DDS, (!CONFIG_FPGA_FM_ONLY && s->ready)?"true":"false",
        FM_ENGINE_KS, (!CONFIG_FPGA_FM_ONLY && s->engine.available)?"true":"false", FM_ENGINE_FM,
        fm_runtime(s,s->fm.status.profile_id)?"true":"false"));
    for (size_t i = 0; i < sizeof(fm_profiles)/sizeof(fm_profiles[0]); i++) {
        const fm_profile_t *p = &fm_profiles[i];
        JSON_TRY(json_format(req, "%s{\"id\":%" PRIu32 ",\"backend_id\":%" PRIu32 ",\"key\":", i?",":"", p->id, p->backend_id));
        JSON_TRY(json_string(req, p->key));
        JSON_TRY(json_format(req, ",\"profile_version\":%" PRIu32 ",\"runtime_available\":%s}", p->profile_version,fm_runtime(s,p->id)?"true":"false"));
    }
    JSON_TRY(json_format(req, "],\"fm_reason\":"));
    if(fm_runtime(s,s->fm.status.profile_id))JSON_TRY(json_format(req,"null"));
    else JSON_TRY(json_string(req,fm_reason(s->fm.status.reason)));
    if(s->cpu_mode.supported)JSON_TRY(json_format(req,",\"cpu_mode\":{\"version\":1,\"route_only\":true,\"modes\":[0,1,2],\"channel_routes\":%s}",s->cpu_mode.channel_routes?"true":"false"));
    if(s->sub_patch_required)JSON_TRY(json_format(req,",\"sub_patch\":{\"version\":1,\"required\":true,\"supported\":%s}",s->sub_patch_supported?"true":"false"));
    JSON_TRY(json_format(req,"}"));
    return json_end(req);
}
static esp_err_t core_v2_session(api_req_t *req) { return core_create_session(req, FM_HTTP_API_MAJOR); }
static esp_err_t core_v2_command(api_req_t *req)
{
    if (!same_origin_command(req)) return core_v2_error(req, "403 Forbidden", "header_required");
    if (!req->content_len || req->content_len > FM_HTTP_COMMAND_MAX_BYTES)
        return core_v2_error(req, "400 Bad Request", "body_length");
    char body[FM_HTTP_COMMAND_MAX_BYTES + 1]; unsigned got = 0;
    while (got < req->content_len) {
        int n = api_recv(req, body + got, req->content_len - got);
        if (n <= 0) return ESP_FAIL;
        got += n;
    }
    body[got] = 0;
    evt_t event = {.kind=EVT_WEB_V2};
    if (memchr(body, 0, got) || !web_control_v2_parse(body, &event.web_v2))
        return core_v2_error(req, "400 Bad Request", "invalid_command");
    const web_control_state_t *s=http_snapshot(req);
    uint32_t context = web_control_context_epoch(s);
    bool cpu_select=s->cpu_mode.supported && event.web_v2.op==FM_HTTP_OPCODE_ENGINE_SELECT &&
        event.web_v2.a<=FM_ENGINE_FM && (!event.web_v2.b ||
        (event.web_v2.a==FM_ENGINE_FM && event.web_v2.b==FM_BACKEND_JT51_OPM)) && !event.web_v2.c;
    int64_t now = esp_timer_get_time();
    for (unsigned i = 0; i < WEB_CLIENTS; i++) {
        if (!session_matches(req,i,event.web_v2.token)) continue;
        if (s_sessions[i].api_version != FM_HTTP_API_MAJOR) return core_v2_error(req, "410 Gone", "protocol_session");
        if (now - s_sessions[i].seen >= 10000000LL) return core_v2_error(req, "410 Gone", "session_expired");
        if (event.web_v2.context_epoch != context || s_sessions[i].context_epoch != context)
            return core_v2_error(req, "409 Conflict", "stale_context");
        if (event.web_v2.sequence <= s_sessions[i].sequence)
            return core_v2_error(req, "409 Conflict", "stale_sequence");
        if(cpu_select && (!s->cpu_mode.valid || s->cpu_mode.fault || (!s->cpu_mode.channel_routes && s->cpu_mode.pending)))
            return core_v2_error(req,"503 Service Unavailable","cpu_mode_not_ready");
        if(s->sub_patch_required && event.web_v2.op==FM_HTTP_OPCODE_CC && sub_patch1_required_cc(event.web_v2.a) &&
           (!s->sub_patch_supported || !s->sub_patch_valid))return core_v2_error(req,"503 Service Unavailable","sub_patch_unavailable");
        bool handoff=s->local.supported && (s->local.local_active || s->local.blocked || s->local.remote_pending);
        if(handoff && event.web_v2.op!=FM_HTTP_OPCODE_STOP && event.web_v2.op!=FM_HTTP_OPCODE_ENGINE_SELECT &&
           event.web_v2.op!=FM_HTTP_OPCODE_KEYS_UPDATE)return core_v2_error(req,"409 Conflict","remote_handoff_required");
        if(handoff && event.web_v2.op==FM_HTTP_OPCODE_ENGINE_SELECT &&
           !(s->local.engine_mask&(1u<<event.web_v2.a)))return core_v2_error(req,"503 Service Unavailable","unsupported_engine");
        if(event.web_v2.op==FM_HTTP_OPCODE_ENGINE_SELECT && event.web_v2.a==ENGINE_MODE_FLUTE && !s->flute.available)
            return core_v2_error(req,"503 Service Unavailable","flute_unavailable");
        if((s->requested_engine==ENGINE_MODE_FLUTE || s->applied_engine==ENGINE_MODE_FLUTE) &&
           event.web_v2.op==FM_HTTP_OPCODE_KEYS_UPDATE &&
           (event.web_v2.a&((1u<<(60-WEB_FIRST_NOTE))-1u)))
            return core_v2_error(req,"400 Bad Request","unsupported_flute_note");
        if((s->requested_engine==ENGINE_MODE_FLUTE || s->applied_engine==ENGINE_MODE_FLUTE) &&
           event.web_v2.op!=FM_HTTP_OPCODE_ENGINE_SELECT && event.web_v2.op!=FM_HTTP_OPCODE_STOP &&
           event.web_v2.op!=FM_HTTP_OPCODE_KEYS_UPDATE)
            return core_v2_error(req,"400 Bad Request","unsupported_flute_parameter");
        if (!s->ready) return core_v2_error(req, "503 Service Unavailable", "owner_unavailable");
        if(CONFIG_FPGA_FM_ONLY && event.web_v2.op==FM_HTTP_OPCODE_ENGINE_SELECT && event.web_v2.a!=FM_ENGINE_FM)
            return core_v2_error(req,"503 Service Unavailable","unsupported_engine");
        if (event.web_v2.op == FM_HTTP_OPCODE_KEYS_UPDATE && event.web_v2.a &&
            !(s->cpu_mode.supported && s->cpu_mode.valid && !s->cpu_mode.fault && s->cpu_mode.channel_routes) &&
            (s->mode_switching || s->mode_recovering || s->fm.status.switching || s->fm.status.recovering || s->fm.status.needs_select ||
             (http_fm_active(s) && !engine_fm_playable(&s->fm.status)) ||
             s->engine.switching || s->engine.recovering || s->engine.requested_mode != s->engine.applied_mode ||
             (s->engine.requested_mode && !s->engine.valid)))
            return core_v2_error(req, "503 Service Unavailable", "engine_not_ready");
        if(event.web_v2.op==FM_HTTP_OPCODE_KEYS_UPDATE && event.web_v2.a) {
            const char *reason=http_performance(event.web_v2.token,context,now,HTTP_PERF_PROBE);
            if(reason)return core_v2_error(req,"409 Conflict",reason);
        }
        if(!handoff && !cpu_select && event.web_v2.op==FM_HTTP_OPCODE_ENGINE_SELECT && event.web_v2.a==FM_ENGINE_FM &&
           (!engine_fm_selectable(&s->fm.status) || event.web_v2.b!=s->fm.status.backend_id))
            return core_v2_error(req,"503 Service Unavailable","fm_unavailable");
        if(event.web_v2.op==FM_HTTP_OPCODE_FM_PARAMETER || event.web_v2.op==FM_HTTP_OPCODE_FM_PRESET) {
            if(!fm_runtime(s,s->fm.status.profile_id) || !s->fm.status.selected)
                return core_v2_error(req,"503 Service Unavailable","fm_unavailable");
            if(s->mode_switching || s->mode_recovering || !engine_fm_playable(&s->fm.status))
                return core_v2_error(req,"503 Service Unavailable","engine_not_ready");
            if(event.web_v2.op==FM_HTTP_OPCODE_FM_PARAMETER) {
                const fm_parameter_t *p=fm_parameter(s->fm.status.profile_id,event.web_v2.a);
                if(!p || !fm_validate_parameter(s->fm.status.profile_id,event.web_v2.a,p->scope_id,event.web_v2.b,event.web_v2.c))
                    return core_v2_error(req,"400 Bad Request","invalid_command");
            } else if(!fm_preset(s->fm.status.profile_id,event.web_v2.a))
                return core_v2_error(req,"400 Bad Request","invalid_command");
        }
        if (!handoff && !cpu_select && event.web_v2.op == FM_HTTP_OPCODE_ENGINE_SELECT && event.web_v2.a == FM_ENGINE_KS && !s->engine.available)
            return core_v2_error(req, "503 Service Unavailable", "ks_unavailable");
        control_source_activity_t activity=event.web_v2.op==FM_HTTP_OPCODE_KEYS_UPDATE?
            (event.web_v2.a?CONTROL_SOURCE_KEYS:CONTROL_SOURCE_IDLE):CONTROL_SOURCE_OUTPUT;
        if(!source_reserve(req,s_sessions[i].source,event.web_v2.token,event.web_v2.sequence,activity))
            return core_v2_error(req,"409 Conflict","control_source_busy");
        event.source=s_sessions[i].source;
        s_sessions[i].sequence = event.web_v2.sequence; s_sessions[i].seen = now;
        event.web_v2.client = i; event.web_v2.expires = now + WEB_LEASE_US;
        event_bus_publish(&event);
        if(event.web_v2.op==FM_HTTP_OPCODE_KEYS_UPDATE)
            (void)http_performance(event.web_v2.token,context,now,event.web_v2.a?HTTP_PERF_CLAIM:HTTP_PERF_RENEW);
        else if(event.web_v2.op==FM_HTTP_OPCODE_STOP)
            (void)http_performance(event.web_v2.token,context,now,HTTP_PERF_RELEASE);
        char response[160];
        snprintf(response, sizeof(response), "{\"api_major\":%" PRIu32 ",\"accepted\":%" PRIu32 ",\"stage\":\"received\",\"context_epoch\":%" PRIu32 ",\"fpga\":\"unverified\"}", FM_HTTP_API_MAJOR, event.web_v2.sequence, context);
        return reply(req, "202 Accepted", response);
    }
    return core_v2_error(req, "410 Gone", "unknown_session");
}
static esp_err_t core_v2_state(api_req_t *req)
{
    const web_control_state_t *s=http_snapshot(req);
    const char *legacy=req->data->legacy;int length=req->data->legacy_length;
    if(length<0 || length>=3072)
        return core_v2_error(req, "500 Internal Server Error", "state_too_large");
    JSON_TRY(json_begin(req));
    JSON_TRY(json_format(req, "{\"api_major\":%" PRIu32 ",\"api_minor\":%" PRIu32 ",\"context_epoch\":%" PRIu32 ",\"mode\":{\"requested\":%u,\"applied\":%u,\"switching\":%s,\"recovering\":%s},\"clients\":[",
        FM_HTTP_API_MAJOR, FM_HTTP_API_MINOR, web_control_context_epoch(s),
        s->requested_engine, s->applied_engine,
        (s->mode_switching||s->engine.switching)?"true":"false", (s->mode_recovering||s->engine.recovering)?"true":"false"));
    for (unsigned i = 0; i < WEB_CLIENTS; i++) {
        uint32_t received = req->data->received[i];
        JSON_TRY(json_format(req, "%s{\"token\":%" PRIu32 ",\"received\":%" PRIu32 ",\"processed\":%" PRIu32 ",\"parameter_processed\":%" PRIu32 ",\"stop_processed\":%" PRIu32 ",\"keys\":%" PRIu32 ",\"result_sequence\":%" PRIu32 ",\"result_code\":%" PRIu32 ",\"result_context_epoch\":%" PRIu32 ",\"midi_channel\":%u}",
            i?",":"", s->token[i], received, s->sequence[i], s->parameter_sequence[i], s->stop_sequence[i],
            s->keys[i], s->v2_result_sequence[i], s->v2_result_code[i], s->v2_result_context[i],(unsigned)s->midi_channel[i]));
    }
    JSON_TRY(json_format(req,"],\"fm\":"));
    JSON_TRY(json_fm_state(req,&s->fm));
    JSON_TRY(json_format(req,",\"local_control\":{\"supported\":%s,\"valid\":%s,\"active\":%s,\"busy\":%s,\"ready\":%s,\"revision\":%" PRIu32 ",\"completed\":%" PRIu32 ",\"fault\":%" PRIu32 ",\"engine_mask\":%" PRIu32 "}",
        s->local.supported?"true":"false",s->local.valid?"true":"false",s->local.local_active?"true":"false",
        s->local.busy?"true":"false",s->local.ready?"true":"false",s->local.revision,s->local.completed,s->local.fault,s->local.engine_mask));
    JSON_TRY(json_format(req,",\"flute\":{\"available\":%s,\"valid\":%s,\"selected\":%s,\"ready\":%s,\"busy\":%s,\"active\":%s,\"preset\":%u,\"accepted_word\":%u,\"fault\":%u,\"reject_count\":%u,\"deadline_count\":%u,\"frame_count\":%u}",
        s->flute.available?"true":"false",s->flute.valid?"true":"false",s->flute.applied_mode?"true":"false",
        s->flute.ready?"true":"false",s->flute.busy?"true":"false",s->flute.active?"true":"false",
        s->flute.preset,s->flute.accepted_word,s->flute.fault,s->flute.reject_count,s->flute.deadline_count,s->flute.frame_count));
    JSON_TRY(json_format(req,",\"legacy\":"));
    JSON_TRY(json_append(req, legacy, length));
    if(s->cpu_mode.supported) {
        const engine_cpu_mode_state_t *m=&s->cpu_mode;
        JSON_TRY(json_format(req,",\"cpu_mode\":{\"version\":1,\"route_only\":true,\"valid\":%s,\"sequence\":%" PRIu32 ",\"desired\":%" PRIu32 ",\"applied\":%" PRIu32 ",\"ready\":%s,\"busy\":%s,\"fault\":%s,\"pending\":%s,\"channel_routes\":%s}",
            m->valid?"true":"false",m->sequence,m->desired,m->applied,m->ready?"true":"false",
            m->busy?"true":"false",m->fault?"true":"false",m->pending?"true":"false",m->channel_routes?"true":"false"));
    }
    if(s->sub_patch_required)JSON_TRY(json_format(req,",\"sub_patch\":{\"version\":1,\"required\":true,\"supported\":%s,\"valid\":%s}",
        s->sub_patch_supported?"true":"false",s->sub_patch_valid?"true":"false"));
    JSON_TRY(json_format(req, "}"));
    return json_end(req);
}
static bool query_u32(const char *text, uint32_t *value)
{
    if (!*text) return false;
    uint32_t n = 0;
    for (; *text; text++) {
        if (*text < '0' || *text > '9') return false;
        unsigned digit = (unsigned)(*text - '0');
        if (n > (UINT32_MAX-digit)/10) return false;
        n = n*10+digit;
    }
    *value = n; return true;
}
static esp_err_t core_v2_catalog(api_req_t *req)
{
    char query[160]; size_t n = api_query_len(req);
    if (!n || n >= sizeof(query) || api_query_copy(req, query, sizeof(query)) != ESP_OK)
        return core_v2_error(req, "400 Bad Request", "invalid_query");
    uint32_t id = 0, offset = 0, limit = 8; unsigned seen = 0;
    const char *section = "profile";
    for (char *part = query; part && *part;) {
        char *next = strchr(part, '&'); if (next) *next++ = 0;
        char *value = strchr(part, '='); if (!value) return core_v2_error(req, "400 Bad Request", "invalid_query");
        *value++ = 0; unsigned bit;
        if (!strcmp(part, "profile")) { bit=1; if (!query_u32(value, &id)) return core_v2_error(req,"400 Bad Request","invalid_query"); }
        else if (!strcmp(part, "offset")) { bit=2; if (!query_u32(value, &offset)) return core_v2_error(req,"400 Bad Request","invalid_query"); }
        else if (!strcmp(part, "limit")) { bit=4; if (!query_u32(value, &limit)) return core_v2_error(req,"400 Bad Request","invalid_query"); }
        else if (!strcmp(part, "section")) { bit=8; section=value; }
        else return core_v2_error(req, "400 Bad Request", "invalid_query");
        if (seen & bit || (next && !*next)) return core_v2_error(req,"400 Bad Request","invalid_query");
        seen |= bit; part = next;
    }
    if (!(seen&1) || !limit || limit>8) return core_v2_error(req,"400 Bad Request","invalid_query");
    const fm_profile_t *p = fm_profile(id);
    if (!p) return core_v2_error(req,"404 Not Found","unknown_profile");
    bool parameters = !strcmp(section,"parameters"), presets = !strcmp(section,"presets");
    if (!parameters && !presets && strcmp(section,"profile")) return core_v2_error(req,"400 Bad Request","invalid_query");
    JSON_TRY(json_begin(req));
    JSON_TRY(json_format(req, "{\"api_major\":%" PRIu32 ",\"profiles_sha256\":\"%s\",\"profile_id\":%" PRIu32 ",\"backend_id\":%" PRIu32 ",\"profile_version\":%" PRIu32 ",\"runtime_available\":%s,",
        FM_HTTP_API_MAJOR, FM_PROFILES_SHA256, p->id, p->backend_id, p->profile_version,
        fm_runtime(http_snapshot(req),p->id)?"true":"false"));
    if (!parameters && !presets) {
        JSON_TRY(json_format(req,"\"key\":")); JSON_TRY(json_string(req,p->key));
        JSON_TRY(json_format(req,",\"voice_count\":%" PRIu32 ",\"patch_operator_count\":%" PRIu32 ",\"parameter_count\":%" PRIu32 ",\"preset_count\":%" PRIu32 ",\"operators_per_voice\":[",p->voice_count,p->patch_operator_count,p->parameter_count,p->preset_count));
        for (uint32_t i=0;i<p->voice_count;i++) JSON_TRY(json_format(req,"%s%" PRIu32,i?",":"",p->operators_per_voice[i]));
        JSON_TRY(json_format(req,"],\"operator_labels\":["));
        for (uint32_t i=0;i<p->patch_operator_count;i++) { if(i) JSON_TRY(json_format(req,",")); JSON_TRY(json_string(req,p->operator_labels[i])); }
        JSON_TRY(json_format(req,"]}"));
    } else {
        uint32_t total = parameters?p->parameter_count:p->preset_count;
        uint32_t start = offset < total ? offset : total;
        uint32_t end = total-start > limit ? start+limit : total;
        JSON_TRY(json_format(req,"\"section\":\"%s\",\"offset\":%" PRIu32 ",\"total\":%" PRIu32 ",\"next_offset\":",section,offset,total));
        if (end<total) JSON_TRY(json_format(req,"%" PRIu32,end)); else JSON_TRY(json_format(req,"null"));
        JSON_TRY(json_format(req,",\"items\":["));
        for (uint32_t i=start;i<end;i++) {
            if (i>start) JSON_TRY(json_format(req,","));
            if (parameters) {
                const fm_parameter_t *d=&p->parameters[i];
                JSON_TRY(json_format(req,"{\"id\":%" PRIu32 ",\"name\":",d->id)); JSON_TRY(json_string(req,d->name));
                JSON_TRY(json_format(req,",\"minimum\":%" PRIu32 ",\"maximum\":%" PRIu32 ",\"scope_id\":%" PRIu32 ",\"unit_id\":%" PRIu32 ",\"apply_time_id\":%" PRIu32 ",\"value_type_id\":%" PRIu32 "}",d->minimum,d->maximum,d->scope_id,d->unit_id,d->apply_time_id,d->value_type_id));
            } else {
                const fm_preset_t *d=&p->presets[i];
                JSON_TRY(json_format(req,"{\"id\":%" PRIu32 ",\"name\":",d->id)); JSON_TRY(json_string(req,d->name));
                JSON_TRY(json_format(req,",\"key_on_logical_mask\":%" PRIu32 ",\"values_count\":%" PRIu32 "}",d->key_on_logical_mask,d->values_count));
            }
        }
        JSON_TRY(json_format(req,"]}"));
    }
    return json_end(req);
}
#include "http_fm_diagnostics.inc.h"
#undef JSON_TRY
