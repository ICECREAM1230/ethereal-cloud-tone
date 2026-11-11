// Included by wifi_web.c after http_v2.inc.h.
#include "raw_midi.h"
#include "web_raw_queue.h"
#include <inttypes.h>

// RAM observation only. sent_bytes advances after the SPI owner observes a
// successful FPGA raw-byte acknowledgement; it is not a rendered PCM receipt.
static esp_err_t core_midi_debug_get(api_req_t *req)
{
    const web_control_state_t *state=http_snapshot(req);
    raw_midi_state_t raw;raw_midi_state(&raw);
    const raw_midi_source_state_t *web=&raw.source[MIDI_RAW_SOURCE_WEB];
    bool channels=state->cpu_mode.supported && state->cpu_mode.valid && !state->cpu_mode.fault && state->cpu_mode.channel_routes;
    bool ready=state->ready && (channels || (!state->mode_switching && !state->mode_recovering)) &&
        raw.available && raw.enabled && raw.ready;
    char body[896];
    int n=snprintf(body,sizeof(body),
        "{\"version\":1,\"max_bytes\":256,\"lease_ms\":%u,\"context_epoch\":%" PRIu32
        ",\"ready\":%s,\"available\":%s,\"enabled\":%s,\"inflight\":%s,\"fault\":%" PRIu32
        ",\"reset_count\":%" PRIu32 ",\"ack_scope\":\"fpga_raw_byte\",\"pcm_capture\":false,"
        "\"web\":{\"session\":%" PRIu64 ",\"queued_bytes\":%" PRIu32 ",\"sent_bytes\":%" PRIu32
        ",\"dropped_bytes\":%" PRIu32 ",\"flushes\":%" PRIu32 ",\"fault\":%" PRIu32 "}}",
        (unsigned)(WEB_LEASE_US/1000),web_control_context_epoch(state),
        ready?"true":"false",raw.available?"true":"false",raw.enabled?"true":"false",
        raw.inflight?"true":"false",raw.fault,raw.reset_count,web->session,
        web->queued_bytes,web->sent_bytes,web->dropped_bytes,web->flushes,web->fault);
    if(n<0 || (size_t)n>=sizeof(body))
        return core_v2_error(req,"500 Internal Server Error","debug_state_too_large");
    return reply(req,"200 OK",body);
}

static bool raw_http_binding_header(api_req_t *req,const char *name,uint32_t *out)
{
    char value[11]; // UINT32_MAX has ten decimal digits.
    if(api_header(req,name,value,sizeof(value))!=ESP_OK ||
       value[0]<'1' || value[0]>'9') return false;
    uint32_t number=0;
    for(const char *p=value;*p;p++) {
        if(*p<'0' || *p>'9')return false;
        unsigned digit=(unsigned)(*p-'0');
        if(number>(UINT32_MAX-digit)/10u)return false;
        number=number*10u+digit;
    }
    *out=number;
    return true;
}

static esp_err_t core_midi_raw_post(api_req_t *req)
{
    if(!same_origin_command(req))return core_v2_error(req,"403 Forbidden","header_required");
    // Bounded JSON permits all 256 bytes and escaped keys without heap IO.
    if(!req->content_len || req->content_len>2048)
        return core_v2_error(req,"400 Bad Request","body_length");
    char body[2048];
    size_t got=0;
    while(got<req->content_len) {
        int n=api_recv(req,body+got,req->content_len-got);
        if(n<=0)return ESP_FAIL;
        got+=(size_t)n;
    }
    web_raw_job_t job={0};
    if(!web_raw_midi_parse(body,got,&job.request))
        return core_v2_error(req,"400 Bad Request","invalid_raw_midi");
    if(!job.request.has_binding) {
        if(!raw_http_binding_header(req,"X-FPGA-Token",&job.request.token) ||
           !raw_http_binding_header(req,"X-FPGA-Sequence",&job.request.sequence) ||
           !raw_http_binding_header(req,"X-FPGA-Context",&job.request.context_epoch))
            return core_v2_error(req,"400 Bad Request","invalid_binding");
        job.request.has_binding=true;
    }
    const web_control_state_t *snapshot=http_snapshot(req);
    uint32_t context=web_control_context_epoch(snapshot);
    int64_t now=esp_timer_get_time();
    for(unsigned i=0;i<WEB_CLIENTS;i++) {
        if(!session_matches(req,i,job.request.token))continue;
        if(s_sessions[i].api_version!=FM_HTTP_API_MAJOR)
            return core_v2_error(req,"410 Gone","protocol_session");
        if(now-s_sessions[i].seen>=10000000LL)
            return core_v2_error(req,"410 Gone","session_expired");
        if(job.request.context_epoch!=context || s_sessions[i].context_epoch!=context)
            return core_v2_error(req,"409 Conflict","stale_context");
        if(job.request.sequence<=s_sessions[i].sequence)
            return core_v2_error(req,"409 Conflict","stale_sequence");
        raw_midi_state_t raw;
        raw_midi_state(&raw);
        bool channels=snapshot->cpu_mode.supported && snapshot->cpu_mode.valid && !snapshot->cpu_mode.fault && snapshot->cpu_mode.channel_routes;
        if(!snapshot->ready || (!channels && (snapshot->mode_switching || snapshot->mode_recovering)) ||
           !raw.available || !raw.enabled || !raw.ready)
            return core_v2_error(req,"409 Conflict","raw_not_ready");
        const char *reason=http_performance(job.request.token,context,now,HTTP_PERF_PROBE);
        if(reason)return core_v2_error(req,"409 Conflict",reason);
        job.client=(uint8_t)i;
        job.expires=now+WEB_LEASE_US;
        uint32_t id;
        if(!web_raw_queue_push(&job,&id))
            return core_v2_error(req,"503 Service Unavailable","raw_queue_full");
        if(!source_reserve(req,s_sessions[i].source,job.request.token,job.request.sequence,CONTROL_SOURCE_RAW)) {
            web_raw_job_t discarded;(void)web_raw_queue_take(id,&discarded);
            return core_v2_error(req,"409 Conflict","control_source_busy");
        }
        (void)http_performance(job.request.token,context,now,HTTP_PERF_CLAIM);
        s_sessions[i].sequence=job.request.sequence;
        s_sessions[i].seen=now;
        evt_t event={.kind=EVT_WEB_RAW,.source=s_sessions[i].source,.web_v2={.token=job.request.token,
            .sequence=job.request.sequence,.context_epoch=context,.a=id,
            .client=(uint8_t)i,.expires=job.expires}};
        event_bus_publish(&event);
        char response[80];
        snprintf(response,sizeof(response),"{\"accepted\":true,\"queued_bytes\":%u}",
            (unsigned)job.request.length);
        return reply(req,"202 Accepted",response);
    }
    return core_v2_error(req,"410 Gone","unknown_session");
}
