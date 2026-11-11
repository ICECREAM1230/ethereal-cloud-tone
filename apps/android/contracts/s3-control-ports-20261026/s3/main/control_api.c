#include "sdkconfig.h"
#include "control_api.h"
#include "control_source.h"
#include "web_control.h"
#include "web_control_v2.h"
#include "fm_contract.h"
#include "fm_only_contract.h"
#include "fm_diagnostics.h"
#include "dds_presets.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "freertos/semphr.h"
static StaticSemaphore_t s_core_storage;
static SemaphoreHandle_t s_core_mutex;
static portMUX_TYPE s_peer_lock=portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_init_lock=portMUX_INITIALIZER_UNLOCKED;
static void core_lock(void)
{
    portENTER_CRITICAL(&s_init_lock);
    if(!s_core_mutex)s_core_mutex=xSemaphoreCreateMutexStatic(&s_core_storage);
    portEXIT_CRITICAL(&s_init_lock);
    (void)xSemaphoreTake(s_core_mutex,portMAX_DELAY);
}
static void core_unlock(void){(void)xSemaphoreGive(s_core_mutex);}
#define PEER_LOCK() portENTER_CRITICAL(&s_peer_lock)
#define PEER_UNLOCK() portEXIT_CRITICAL(&s_peer_lock)
#else
#include <pthread.h>
static pthread_mutex_t s_core_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_peer_lock=PTHREAD_MUTEX_INITIALIZER;
static void core_lock(void){(void)pthread_mutex_lock(&s_core_mutex);}
static void core_unlock(void){(void)pthread_mutex_unlock(&s_core_mutex);}
#define PEER_LOCK() ((void)pthread_mutex_lock(&s_peer_lock))
#define PEER_UNLOCK() ((void)pthread_mutex_unlock(&s_peer_lock))
#endif

#define CONTROL_JSON_BLOCK_BYTES 2048u
typedef struct api_req api_req_t;
typedef esp_err_t (*api_handler_t)(api_req_t *);
typedef struct {
    char *path;
    api_handler_t handler;
    bool mutation,fixed,view_acquired;
    int64_t prepared_at;
    uint32_t received[WEB_CLIENTS];
    web_control_state_t *snapshot;
    char *legacy;
    int legacy_length;
    dds_job_status_t *dds_status;
    engine_fm_diagnostics_t *diagnostics;
    dds_result_view_t view;
    char small[256];
    char *text;
    size_t length,capacity;
} api_response_data_t;
struct api_req {
    const control_api_request_t *input;
    control_api_response_t *output;
    api_response_data_t *data;
    const control_api_sink_t *sink;
    size_t content_len,received,json_used;
    char *json_block;
    bool preparing,locked,snapshot_ready;
    esp_err_t error;
};

static struct {
    uint32_t token,sequence,context_epoch;
    int64_t seen;
    unsigned api_version;
    control_api_peer_t peer;
    control_source_ref_t source;
} s_sessions[WEB_CLIENTS];
static uint64_t s_ble_closed_epoch,s_ble_bound_epoch;
static control_source_ref_t s_ble_bound_source,s_wifi_bound_source;
static bool peer_same(control_api_peer_t a,control_api_peer_t b)
{return a.transport==b.transport && a.connection_epoch==b.connection_epoch;}
static bool peer_live_locked(control_api_peer_t peer)
{
    return peer.transport==CONTROL_API_WIFI ? peer.connection_epoch==0 :
        peer.transport==CONTROL_API_BLE_CONTROL && peer.connection_epoch && peer.connection_epoch>s_ble_closed_epoch &&
        peer.connection_epoch>=s_ble_bound_epoch;
}
static bool peer_live(control_api_peer_t peer)
{PEER_LOCK();bool live=peer_live_locked(peer);PEER_UNLOCK();return live;}
static bool source_claim(api_req_t *req,control_source_ref_t *source)
{
    bool accepted=false;control_api_peer_t peer=req->input->peer;
    PEER_LOCK();
    if(peer.transport==CONTROL_API_BLE_CONTROL && s_ble_bound_epoch &&
       peer.connection_epoch>s_ble_bound_epoch && peer_live_locked(peer)) {
        // A newer physical connection cannot inherit the older mode fence,
        // even when its worker cleanup has not run yet.
        s_ble_closed_epoch=s_ble_bound_epoch;
        control_source_revoke(s_ble_bound_source);
        s_ble_bound_epoch=0;s_ble_bound_source=(control_source_ref_t){0};
    }
    if(peer_live_locked(peer)) {
        accepted=control_source_claim(peer.transport==CONTROL_API_WIFI?CONTROL_SOURCE_WIFI:CONTROL_SOURCE_BLE_CTRL,
                                      req->data->prepared_at,source);
        if(accepted && peer.transport==CONTROL_API_BLE_CONTROL) {
            s_ble_bound_epoch=peer.connection_epoch;s_ble_bound_source=*source;
        } else if(accepted)s_wifi_bound_source=*source;
    }
    PEER_UNLOCK();return accepted;
}
static bool source_reserve(api_req_t *req,control_source_ref_t source,uint32_t token,uint32_t sequence,
                           control_source_activity_t activity)
{
    PEER_LOCK();
    bool accepted=peer_live_locked(req->input->peer) && control_source_reserve(source,token,sequence,
        req->data->prepared_at,req->data->prepared_at+WEB_LEASE_US,activity);
    PEER_UNLOCK();return accepted;
}
static bool session_matches(api_req_t *req,unsigned i,uint32_t token)
{return s_sessions[i].token==token && peer_same(s_sessions[i].peer,req->input->peer) &&
        control_source_valid(s_sessions[i].source) && peer_live(req->input->peer);}
static void request_unlock(api_req_t *req)
{if(req->locked){req->locked=false;core_unlock();}}
static const web_control_state_t *http_snapshot(api_req_t *req)
{
    if(!req->preparing || req->snapshot_ready)return req->data->snapshot;
    if(!req->locked){core_lock();req->locked=true;}
    web_control_read(req->data->snapshot);req->snapshot_ready=true;
    for(unsigned i=0;i<WEB_CLIENTS;i++)
        req->data->received[i]=s_sessions[i].token==req->data->snapshot->token[i] &&
            s_sessions[i].api_version==FM_HTTP_API_MAJOR?s_sessions[i].sequence:0;
    if(!req->data->mutation)request_unlock(req);
    return req->data->snapshot;
}
static bool http_fm_active(const web_control_state_t *snapshot)
{return engine_fm_control_active(&snapshot->fm.status);}
static bool same_origin_command(api_req_t *req){return req->input->control_authorized;}
static unsigned status_number(const char *status)
{return (unsigned)(status[0]-'0')*100u+(unsigned)(status[1]-'0')*10u+(unsigned)(status[2]-'0');}
static esp_err_t api_set_status(api_req_t *req,const char *status)
{if(req->preparing)req->output->code=status_number(status);return ESP_OK;}
static esp_err_t api_set_type(api_req_t *req,const char *type)
{if(req->preparing)req->output->content_type=type;return ESP_OK;}
static esp_err_t api_set_header(api_req_t *req,const char *key,const char *value)
{(void)req;(void)key;(void)value;return ESP_OK;}
static esp_err_t capture_append(api_req_t *req,const char *bytes,size_t length)
{
    api_response_data_t *data=req->data;
    if(length>SIZE_MAX-data->length-1)return ESP_FAIL;
    size_t needed=data->length+length+1;
    if(needed>data->capacity) {
        char *text=malloc(needed);
        if(!text)return req->error=ESP_ERR_NO_MEM;
        memcpy(text,data->text,data->length);
        if(data->text!=data->small)free(data->text);
        data->text=text;data->capacity=needed;
    }
    if(length)memcpy(data->text+data->length,bytes,length);
    data->length+=length;data->text[data->length]=0;return ESP_OK;
}
static esp_err_t api_chunk(api_req_t *req,const char *bytes,size_t length)
{
    if(!length)return ESP_OK;
    if(req->preparing)return req->data->mutation?capture_append(req,bytes,length):ESP_OK;
    return req->sink->write(req->sink->context,bytes,length);
}
static esp_err_t api_send(api_req_t *req,const char *bytes,int length)
{return api_chunk(req,bytes,length<0?strlen(bytes):(size_t)length);}
static esp_err_t reply(api_req_t *req,const char *status,const char *json)
{
    request_unlock(req);
    (void)api_set_status(req,status);(void)api_set_type(req,"application/json; charset=utf-8");
    if(req->preparing) {
        req->data->fixed=true;req->data->length=0;
        return capture_append(req,json,strlen(json));
    }
    return api_send(req,json,-1);
}
static int api_recv(api_req_t *req,char *out,size_t length)
{
    if(!req->preparing || req->received>req->content_len || length>req->content_len-req->received)return -1;
    memcpy(out,req->input->body+req->received,length);req->received+=length;return (int)length;
}
static size_t api_query_len(api_req_t *req)
{const char *query=strchr(req->data->path,'?');return query?strlen(query+1):0;}
static esp_err_t api_query_copy(api_req_t *req,char *out,size_t capacity)
{
    const char *query=strchr(req->data->path,'?');
    if(!query || strlen(++query)>=capacity)return ESP_FAIL;
    strcpy(out,query);return ESP_OK;
}
static esp_err_t api_header(api_req_t *req,const char *key,char *out,size_t capacity)
{
    const char *value=!strcmp(key,"X-FPGA-Token")?req->input->raw_token:
        !strcmp(key,"X-FPGA-Sequence")?req->input->raw_sequence:req->input->raw_context;
    if(!value || strlen(value)>=capacity)return ESP_FAIL;
    strcpy(out,value);return ESP_OK;
}
static void api_dds_status(api_req_t *req,dds_job_status_t *out)
{
    if(req->preparing)dds_presets_status(req->data->dds_status);
    *out=*req->data->dds_status;
}
static bool api_dds_acquire(api_req_t *req,uint32_t id,dds_result_view_t *view)
{
    if(req->preparing)req->data->view_acquired=dds_presets_result_acquire(id,&req->data->view);
    *view=req->data->view;return req->data->view_acquired;
}
#if CONFIG_FPGA_FM_ONLY
static void api_diagnostics(api_req_t *req,engine_fm_diagnostics_t *out)
{
    if(req->preparing) {
        (void)engine_link_fm_diagnostics_request();
        engine_link_fm_diagnostics_state(req->data->diagnostics);
    }
    *out=*req->data->diagnostics;
}
#endif
static esp_err_t core_v2_error(api_req_t *,const char *,const char *);
static esp_err_t json_begin(api_req_t *);
static esp_err_t json_append(api_req_t *,const char *,size_t);
static esp_err_t json_end(api_req_t *);
#include "http_performance.inc.h"

static esp_err_t core_create_session(api_req_t *req, unsigned api_version)
{
    if (!same_origin_command(req)) return api_version == 1 ?
        reply(req, "403 Forbidden", "{\"error\":\"header required\"}") : core_v2_error(req,"403 Forbidden","header_required");
    if (req->content_len) return api_version == 1 ?
        reply(req, "400 Bad Request", "{\"error\":\"empty body required\"}") : core_v2_error(req,"400 Bad Request","empty_body_required");
    int64_t now = esp_timer_get_time();
    const web_control_state_t *snapshot=http_snapshot(req);
    uint32_t context = snapshot->context_epoch ? snapshot->context_epoch : 1;
    if (api_version == FM_HTTP_API_MAJOR && !snapshot->ready)
        return core_v2_error(req, "503 Service Unavailable", "owner_unavailable");
    for (unsigned i = 0; i < WEB_CLIENTS; i++) {
        bool stale_v2 = s_sessions[i].api_version == FM_HTTP_API_MAJOR && s_sessions[i].context_epoch != context;
        bool stale_source=!control_source_valid(s_sessions[i].source) || s_sessions[i].peer.transport!=req->input->peer.transport ||
            !peer_same(s_sessions[i].peer,req->input->peer);
        if (s_sessions[i].token && now - s_sessions[i].seen < 10000000LL && !stale_v2 && !stale_source) continue;
        uint32_t token;
        bool collision;
        do {
            token = esp_random(); collision = !token;
            for (unsigned j = 0; j < WEB_CLIENTS; j++) if (s_sessions[j].token == token) collision = true;
        } while (collision);
        control_source_ref_t source;
        if(!source_claim(req,&source))return core_v2_error(req,"409 Conflict","control_source_busy");
        s_sessions[i].peer=req->input->peer;s_sessions[i].source=source;
        s_sessions[i].token = token; s_sessions[i].sequence = 0; s_sessions[i].seen = now;
        s_sessions[i].api_version = api_version; s_sessions[i].context_epoch = context;
        evt_t event = {.kind=EVT_WEB, .source=source, .web={.client=i, .token=token, .op=WEB_START}};
        event_bus_publish(&event);
        char response[128];
        if (api_version == FM_HTTP_API_MAJOR)
            snprintf(response, sizeof(response), "{\"api_major\":%" PRIu32 ",\"api_minor\":%" PRIu32 ",\"token\":%" PRIu32 ",\"client\":%u,\"lease_ms\":%" PRIu32 ",\"context_epoch\":%" PRIu32 "}", FM_HTTP_API_MAJOR, FM_HTTP_API_MINOR, token, i, FM_PERFORMANCE_LEASE_MS, context);
        else snprintf(response, sizeof(response), "{\"token\":%" PRIu32 ",\"client\":%u,\"lease_ms\":1200}", token, i);
        return reply(req, "201 Created", response);
    }
    return api_version == 1 ? reply(req, "503 Service Unavailable", "{\"error\":\"four active pages; retry later\"}") :
        core_v2_error(req,"503 Service Unavailable","session_full");
}
static esp_err_t core_session(api_req_t *req) { return core_create_session(req, 1); }
static esp_err_t core_command(api_req_t *req)
{
    if (!same_origin_command(req)) return reply(req, "403 Forbidden", "{\"error\":\"header required\"}");
    if (!req->content_len || req->content_len >= 96)
        return reply(req, "400 Bad Request", "{\"error\":\"body length\"}");
    char body[96]; unsigned got = 0;
    while (got < req->content_len) {
        int n = api_recv(req, body + got, req->content_len - got);
        if (n <= 0) return ESP_FAIL; // close slow/broken socket; leases still expire
        got += n;
    }
    body[got] = 0;
    evt_t event = {.kind=EVT_WEB};
    if (memchr(body, 0, got) || !web_control_parse(body, &event.web))
        return reply(req, "400 Bad Request", "{\"error\":\"invalid command\"}");
    const web_control_state_t *snapshot=http_snapshot(req);
    if(http_fm_active(snapshot) && event.web.op!=WEB_STOP && !(event.web.op==WEB_KEYS_UPDATE && !event.web.a))
        return reply(req,"503 Service Unavailable","{\"error\":\"FM requires API v2\"}");
    int64_t now = esp_timer_get_time();
    for (unsigned i = 0; i < WEB_CLIENTS; i++) {
        if (!session_matches(req,i,event.web.token)) continue;
        if (s_sessions[i].api_version != 1)
            return reply(req, "410 Gone", "{\"error\":\"protocol session\"}");
        if (now - s_sessions[i].seen >= 10000000LL)
            return reply(req, "410 Gone", "{\"error\":\"session expired\"}");
        if (event.web.sequence <= s_sessions[i].sequence)
            return reply(req, "409 Conflict", "{\"error\":\"stale sequence\"}");
        if(event.web.op==WEB_KEYS_UPDATE && event.web.a) {
            const char *reason=http_performance(event.web.token,web_control_context_epoch(snapshot),now,HTTP_PERF_PROBE);
            if(reason)return core_v2_error(req,"409 Conflict",reason);
        }
        control_source_activity_t activity=event.web.op==WEB_KEYS_UPDATE?
            (event.web.a?CONTROL_SOURCE_KEYS:CONTROL_SOURCE_IDLE):CONTROL_SOURCE_OUTPUT;
        if(!source_reserve(req,s_sessions[i].source,event.web.token,event.web.sequence,activity))
            return core_v2_error(req,"409 Conflict","control_source_busy");
        event.source=s_sessions[i].source;
        s_sessions[i].sequence = event.web.sequence; s_sessions[i].seen = now;
        event.web.client = i; event.web.expires = now + WEB_LEASE_US;
        event_bus_publish(&event);
        if(event.web.op==WEB_KEYS_UPDATE)
            (void)http_performance(event.web.token,web_control_context_epoch(snapshot),now,event.web.a?HTTP_PERF_CLAIM:HTTP_PERF_RENEW);
        else if(event.web.op==WEB_STOP)
            (void)http_performance(event.web.token,web_control_context_epoch(snapshot),now,HTTP_PERF_RELEASE);
        char response[128];
        snprintf(response, sizeof(response), "{\"accepted\":%" PRIu32 ",\"stage\":\"received\",\"fpga\":\"unverified\"}", event.web.sequence);
        return reply(req, "202 Accepted", response);
    }
    return reply(req, "410 Gone", "{\"error\":\"unknown session\"}");
}
#include "http_inputs.inc.h"
static int format_state(char *body, size_t capacity, const web_control_state_t *snapshot)
{
    const web_control_state_t *s = snapshot;
    const patch_t *p = &s->patch;
    // Ten uint32 digits plus a comma per voice; the final byte holds NUL.
    char started[ENGINE_KS_VOICES * 11u];
    size_t used=0;
    for(unsigned voice=0;voice<ENGINE_KS_VOICES;voice++) {
        int count=snprintf(started+used,sizeof(started)-used,"%s%" PRIu32,
                           voice ? "," : "",s->engine.started_sequence[voice]);
        if(count<0 || (size_t)count>=sizeof(started)-used)return -1;
        used+=(size_t)count;
    }
    int n = snprintf(body, capacity,
        "{\"ready\":%s,\"revision\":%" PRIu32 ",\"panic\":%" PRIu32 ",\"rendering\":%s,\"test_tone\":%s,\"fpga\":\"unverified\","
        "\"tokens\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],\"processed\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
        "\"parameter_processed\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],\"stop_processed\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
        "\"keys\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
        "\"engine\":{\"available\":%s,\"valid\":%s,\"requested\":%u,\"applied\":%u,\"switching\":%s,\"busy\":%s,"
        "\"recovering\":%s,\"fault\":%" PRIu32 ",\"recoveries\":%" PRIu32 ",\"timeouts\":%" PRIu32 ","
        "\"active\":%u,\"init\":%u,\"release\":%u,\"accepted\":%" PRIu32 ",\"mode_sequence\":%" PRIu32 ",\"rejects\":%" PRIu32 ","
        "\"frames\":%" PRIu32 ",\"deadlines\":%" PRIu32 ",\"started\":[%s],"
        "\"diagnostics\":{\"mode_generation\":%" PRIu32 ",\"mode_sent_generation\":%" PRIu32 ",\"inflight_sequence\":%" PRIu32 ",\"inflight_op\":%" PRIu32 ","
        "\"wait_reason\":%" PRIu32 ",\"wait_ms\":%" PRIu32 ",\"fault_sequence\":%" PRIu32 ",\"fault_op\":%" PRIu32 ","
        "\"gate_off_pending\":%" PRIu32 ",\"shadow_gate_pending\":%" PRIu32 ",\"control_status\":%" PRIu32 ",\"control_status_valid\":%s,"
        "\"owner_pending\":%s,\"bank_pending\":%s,\"flushing\":%s}},\"ks_decay_ms\":%u,"
        "\"osc\":[[%d,%d,%d,%" PRId32 "],[%d,%d,%d,%" PRId32 "]],"
        "\"voice\":%d,\"mix\":%d,\"detune\":%d,\"spread\":%d,\"volume\":%u,\"pan\":%d,"
        "\"filter\":[%u,%u,%u,%u,%d],\"cutoff_offset\":%" PRId32 ",\"resonance_offset\":%" PRId32 ","
        "\"amp\":[%u,%u,%u,%u],\"mod\":[%u,%u,%u,%u],\"mod_depth\":%d,"
        "\"lfo\":[[%u,%u,%d,%u],[%u,%u,%d,%u]],\"velocity\":[%u,%u]}",
        (s->ready && !http_fm_active(s)) ? "true":"false",s->revision,s->panic_revision,s->rendering_pending?"true":"false",s->test_tone_requested?"true":"false",
        s->token[0],s->token[1],s->token[2],s->token[3],s->sequence[0],s->sequence[1],s->sequence[2],s->sequence[3],
        s->parameter_sequence[0],s->parameter_sequence[1],s->parameter_sequence[2],s->parameter_sequence[3],
        s->stop_sequence[0],s->stop_sequence[1],s->stop_sequence[2],s->stop_sequence[3],
        s->keys[0],s->keys[1],s->keys[2],s->keys[3],
        s->engine.available?"true":"false",s->engine.valid?"true":"false",s->engine.requested_mode,s->engine.applied_mode,
        s->engine.switching?"true":"false",s->engine.busy?"true":"false",
        s->engine.recovering?"true":"false",s->engine.fault,s->engine.recovery_count,s->engine.timeout_count,
        s->engine.active_mask,s->engine.init_mask,s->engine.release_mask,
        s->engine.accepted_sequence,s->engine.mode_sequence,s->engine.reject_count,s->engine.frame_count,s->engine.deadline_count,
        started,
        s->engine.mode_generation,s->engine.mode_sent_generation,s->engine.inflight_sequence,s->engine.inflight_op,
        s->engine.wait_reason,s->engine.wait_ms,s->engine.fault_sequence,s->engine.fault_op,
        s->engine.gate_off_pending,s->engine.shadow_gate_pending,s->engine.control_status,s->engine.control_status_valid?"true":"false",
        s->engine.owner_pending?"true":"false",s->engine.bank_pending?"true":"false",s->engine.flushing?"true":"false",s->ks_decay_ms,
        p->osc[0].wave,p->osc[0].coarse,p->osc[0].fine,p->osc[0].duty,
        p->osc[1].wave,p->osc[1].coarse,p->osc[1].fine,p->osc[1].duty,
        p->voice_struct,p->osc_mix,p->unison_detune,p->unison_stereo,p->volume,p->pan,
        p->filter.cutoff,p->filter.resonance,p->filter.type,p->filter.dual,p->filter.key_track,
        s->cutoff_offset,s->resonance_offset,
        p->env[0].attack,p->env[0].decay,p->env[0].sustain,p->env[0].release,
        p->env[1].attack,p->env[1].decay,p->env[1].sustain,p->env[1].release,p->env1_depth,
        p->lfo[0].shape,p->lfo[0].rate,p->lfo[0].depth,p->lfo[0].dest,
        p->lfo[1].shape,p->lfo[1].rate,p->lfo[1].depth,p->lfo[1].dest,p->vel_amp_amt,p->vel_mod_amt);
    if(n<1 || (size_t)n>=capacity || !s->inputs.version)return n;
    // Replace only the final object delimiter. Preserve snprintf's required
    // length on truncation so v1 and v2 return a bounded state_too_large error.
    int extra=format_inputs(body+n-1,capacity-(size_t)n+1,&s->inputs);
    return extra<0?extra:n-1+extra;
}
static esp_err_t core_state(api_req_t *req)
{
    int n=req->data->legacy_length;
    if (n < 0 || n >= 3072) return reply(req,"500 Internal Server Error","{\"error\":\"state too large\"}");
    if(json_begin(req)!=ESP_OK || json_append(req,req->data->legacy,(size_t)n)!=ESP_OK)return ESP_FAIL;
    return json_end(req);
}

#include "http_v2.inc.h"
#include "http_reverb.inc.h"
#include "http_raw_midi.inc.h"
#include "http_dds.inc.h"

typedef struct {
    const char *path;
    bool post,snapshot,mutation,dds_status,diagnostics;
    api_handler_t handler;
} api_route_t;
static const api_route_t s_routes[]={
    {"/api/state",false,true,false,false,false,core_state},
    {"/api/session",true,true,true,false,false,core_session},
    {"/api/command",true,true,true,false,false,core_command},
    {"/api/v2/capabilities",false,true,false,false,false,core_v2_capabilities},
    {"/api/v2/state",false,true,false,false,false,core_v2_state},
    {"/api/v2/session",true,true,true,false,false,core_v2_session},
    {"/api/v2/command",true,true,true,false,false,core_v2_command},
    {"/api/v2/effects/reverb",false,true,false,false,false,core_reverb_get},
    {"/api/v2/effects/reverb",true,true,true,false,false,core_reverb_post},
    {"/api/v1/midi/raw",true,true,true,false,false,core_midi_raw_post},
    {"/api/v2/fm/catalog",false,true,false,false,false,core_v2_catalog},
    {"/api/v2/fm/diagnostics",false,false,false,false,true,core_v2_fm_diagnostics},
    {"/api/dds/presets",false,false,true,false,false,core_dds_list_http},
    {"/api/dds/presets",true,false,true,false,false,core_dds_command_http},
    {"/api/dds/status",false,false,false,true,false,core_dds_status_http},
    {"/api/dds/result",false,false,false,true,false,core_dds_result_http},
};

static void consume_input(const control_api_request_t *request)
{
    if(request && request->owned_body) {
        free(*request->owned_body);*request->owned_body=NULL;
    }
}
esp_err_t control_api_prepare(const control_api_request_t *request,control_api_response_t *response)
{
    esp_err_t result=ESP_ERR_INVALID_ARG;
    if(!request || !response || response->private_data || !request->path ||
       (request->owned_body && *request->owned_body!=request->body)) {
        consume_input(request);return result;
    }
    *response=(control_api_response_t){0};
    api_response_data_t *data=calloc(1,sizeof(*data));
    if(!data){consume_input(request);return ESP_ERR_NO_MEM;}
    response->private_data=data;data->text=data->small;data->capacity=sizeof(data->small);
    data->prepared_at=esp_timer_get_time();
    size_t path_length=strlen(request->path);
    data->path=malloc(path_length+1);
    if(!data->path){result=ESP_ERR_NO_MEM;goto failed;}
    memcpy(data->path,request->path,path_length+1);
    api_req_t req={.input=request,.output=response,.data=data,.preparing=true,.content_len=request->body_length};
    if(!peer_live(request->peer)) {
        result=core_v2_error(&req,"410 Gone","unknown_session");goto prepared;
    }
    const api_route_t *route=NULL;
    const char *query=strchr(data->path,'?');
    size_t route_length=query?(size_t)(query-data->path):path_length;
    for(size_t i=0;i<sizeof(s_routes)/sizeof(s_routes[0]);i++)
        if(strlen(s_routes[i].path)==route_length && !memcmp(data->path,s_routes[i].path,route_length) &&
           s_routes[i].post==(request->body!=NULL)){route=&s_routes[i];break;}
    if(!route){result=core_v2_error(&req,"404 Not Found","unknown_path");goto prepared;}
    data->handler=route->handler;data->mutation=route->mutation;data->fixed=route->mutation;
    if(route->snapshot && !(data->snapshot=malloc(sizeof(*data->snapshot)))){result=ESP_ERR_NO_MEM;goto failed;}
    if(route->handler==core_state || route->handler==core_v2_state) {
        data->legacy=malloc(3072);
        if(!data->legacy){result=ESP_ERR_NO_MEM;goto failed;}
        data->legacy_length=format_state(data->legacy,3072,http_snapshot(&req));
    }
    if(route->dds_status && !(data->dds_status=malloc(sizeof(*data->dds_status)))){result=ESP_ERR_NO_MEM;goto failed;}
#if CONFIG_FPGA_FM_ONLY
    if(route->diagnostics && !(data->diagnostics=malloc(sizeof(*data->diagnostics)))){result=ESP_ERR_NO_MEM;goto failed;}
#endif
    response->code=200;response->content_type="application/json; charset=utf-8";
    result=route->handler(&req);
    request_unlock(&req);
    if(route->handler==core_state){free(data->snapshot);data->snapshot=NULL;}
    if(req.error!=ESP_OK)result=req.error;
prepared:
    consume_input(request);
    if(result==ESP_OK)return ESP_OK;
failed:
    consume_input(request);control_api_release(response);return result;
}

esp_err_t control_api_emit(const control_api_response_t *response,const control_api_sink_t *sink)
{
    if(!response || !response->private_data || !sink || !sink->write)return ESP_ERR_INVALID_ARG;
    api_response_data_t *data=response->private_data;
    if(data->fixed)return data->length?sink->write(sink->context,data->text,data->length):ESP_OK;
    char *block=malloc(CONTROL_JSON_BLOCK_BYTES);
    if(!block)return ESP_ERR_NO_MEM;
    control_api_request_t input={.path=data->path};
    api_req_t req={.input=&input,.data=data,.sink=sink,.json_block=block,.snapshot_ready=true};
    esp_err_t result=data->handler(&req);
    free(block);return result;
}

void control_api_release(control_api_response_t *response)
{
    if(!response)return;
    api_response_data_t *data=response->private_data;
    if(data) {
        if(data->view_acquired)dds_presets_result_release(&data->view);
        if(data->text!=data->small)free(data->text);
        free(data->path);free(data->snapshot);free(data->legacy);free(data->dds_status);free(data->diagnostics);free(data);
    }
    *response=(control_api_response_t){0};
}

void control_api_peer_invalidate(control_api_peer_t peer)
{
    if(peer.transport==CONTROL_API_WIFI && !peer.connection_epoch) {
        PEER_LOCK();
        // Includes anonymous DDS jobs, which never consume a session slot.
        control_source_revoke(s_wifi_bound_source);s_wifi_bound_source=(control_source_ref_t){0};
        PEER_UNLOCK();return;
    }
    if(peer.transport!=CONTROL_API_BLE_CONTROL || !peer.connection_epoch)return;
    PEER_LOCK();
    if(peer.connection_epoch>s_ble_closed_epoch)s_ble_closed_epoch=peer.connection_epoch;
    if(s_ble_bound_epoch && s_ble_bound_epoch<=s_ble_closed_epoch) {
        control_source_revoke(s_ble_bound_source);
        s_ble_bound_epoch=0;s_ble_bound_source=(control_source_ref_t){0};
    }
    PEER_UNLOCK();
}
void control_api_disconnect(control_api_peer_t peer)
{
    control_api_peer_invalidate(peer);
    core_lock();
    for(unsigned i=0;i<WEB_CLIENTS;i++)if(s_sessions[i].token && peer_same(peer,s_sessions[i].peer)) {
        // BLE invalidation already revoked its exact old fence. Revoking
        // again here could target a source subsequently shared by a new peer.
        if(peer.transport==CONTROL_API_WIFI && control_source_valid(s_sessions[i].source))continue;
        if(s_http_performance.token==s_sessions[i].token)memset(&s_http_performance,0,sizeof(s_http_performance));
        memset(&s_sessions[i],0,sizeof(s_sessions[i]));
    }
    core_unlock();
}
