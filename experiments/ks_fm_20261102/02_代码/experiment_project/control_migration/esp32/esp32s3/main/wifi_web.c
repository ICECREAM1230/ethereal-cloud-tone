// Copyright (c) 2026 ICECREAM1230
#include "sdkconfig.h"
#include "wifi_web.h"
#include "status_led.h"
#if CONFIG_ECT_WIFI_WEB
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "web_control.h"
#include "sub_patch1.h"
#include "web_control_v2.h"
#include "fm_contract.h"
#include "http_performance.inc.h"

static esp_err_t v2_error(httpd_req_t *req, const char *status_code, const char *code);

static const char *TAG = "wifi_web";
// These objects belong exclusively to the HTTP server task. No timer/WiFi
// callback calls HTTP APIs or accesses sessions (HTTP APIs are not thread safe).
static struct {
    uint32_t token, sequence, context_epoch;
    int64_t seen;
    unsigned api_version;
} s_sessions[WEB_CLIENTS];
static web_control_state_t s_http_state;
static const web_control_state_t *http_snapshot(void)
{web_control_read(&s_http_state);return &s_http_state;}
static bool http_fm_active(const web_control_state_t *s)
{return engine_fm_control_active(&s->fm.status);}
extern const char page_gzip_start[] asm("_binary_index_html_gz_start");
extern const char page_gzip_end[] asm("_binary_index_html_gz_end");

static void wifi_status_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == WIFI_EVENT_AP_STOP) {
        status_led_set_flag(STATUS_WIFI_READY, false);
        status_led_set_flag(STATUS_WIFI_CONNECTED, false);
        status_led_set_flag(STATUS_WIFI_FAULT, true);
    } else if (id == WIFI_EVENT_AP_STACONNECTED || id == WIFI_EVENT_AP_STADISCONNECTED) {
        // Read the actual station count; one disconnect must not hide other peers.
        wifi_sta_list_t stations;
        esp_err_t error = esp_wifi_ap_get_sta_list(&stations);
        status_led_set_flag(STATUS_WIFI_CONNECTED, error == ESP_OK && stations.num > 0);
        status_led_set_flag(STATUS_WIFI_FAULT, error != ESP_OK);
    }
}

static esp_err_t reply(httpd_req_t *req, const char *status, const char *json)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}
static bool same_origin_command(httpd_req_t *req)
{
    char header[4];
    // Custom header requires cross-origin preflight; no CORS is enabled.
    return httpd_req_get_hdr_value_str(req, "X-FPGA-Control", header, sizeof(header)) == ESP_OK &&
           strcmp(header, "1") == 0;
}
static bool page_accepts_gzip(httpd_req_t *req)
{
    char encodings[192];
    if(httpd_req_get_hdr_value_str(req,"Accept-Encoding",encodings,sizeof(encodings))!=ESP_OK)return false;
    int gzip=-1,wildcard=-1;
    for(char *item=encodings;*item;) {
        char *end=item+strcspn(item,",");
        char *coding=item;
        while(coding<end && (*coding==' ' || *coding=='\t'))coding++;
        char *parameters=memchr(coding,';',end-coding);
        char *coding_end=parameters?parameters:end;
        while(coding_end>coding && (coding_end[-1]==' ' || coding_end[-1]=='\t'))coding_end--;
        bool is_gzip=coding_end-coding==4 && !strncasecmp(coding,"gzip",4);
        bool is_wildcard=coding_end-coding==1 && *coding=='*';
        if(is_gzip || is_wildcard) {
            int enabled=1;
            for(char *p=parameters;p && p<end;) {
                p++;while(p<end && (*p==' ' || *p=='\t'))p++;
                char *next=memchr(p,';',end-p),*limit=next?next:end;
                char *equal=memchr(p,'=',limit-p);
                char *name_end=equal;
                if(name_end)while(name_end>p && (name_end[-1]==' ' || name_end[-1]=='\t'))name_end--;
                if(equal && name_end-p==1 && (*p=='q' || *p=='Q')) {
                    char *q=equal+1,*q_end=limit;
                    while(q<q_end && (*q==' ' || *q=='\t'))q++;
                    while(q_end>q && (q_end[-1]==' ' || q_end[-1]=='\t'))q_end--;
                    bool valid=q<q_end && (*q=='0' || *q=='1');
                    bool positive=valid && *q=='1';
                    if(valid && q+1<q_end) {
                        valid=q[1]=='.' && q_end-q<=5;
                        for(char *d=q+2;d<q_end;d++) {
                            valid &= *d>='0' && *d<='9' && (*q!='1' || *d=='0');
                            positive |= *d>='1' && *d<='9';
                        }
                    }
                    enabled=valid && positive;
                }
                p=next;
            }
            if(is_gzip)gzip=enabled;else wildcard=enabled;
        }
        item=*end?end+1:end;
    }
    return gzip>=0?gzip!=0:wildcard>0;
}
#include "web_asset.inc.h"
static esp_err_t page(httpd_req_t *req)
{
    bool compressed=page_accepts_gzip(req);
    const char *bytes=page_gzip_start;
    size_t length=(size_t)(page_gzip_end-page_gzip_start);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'");
    httpd_resp_set_hdr(req,"Vary","Accept-Encoding");
    if(compressed)httpd_resp_set_hdr(req,"Content-Encoding","gzip");
    int64_t started=esp_timer_get_time();
    esp_err_t result=compressed?httpd_resp_send(req,bytes,length):page_identity(req);
    if(result!=ESP_OK)ESP_LOGW(TAG,"page send failed encoding=%s bytes=%u error=%d elapsed_ms=%" PRId64,
        compressed?"gzip":"identity",(unsigned)length,result,(esp_timer_get_time()-started)/1000);
    return result; // The server closes a failed socket; never report a partial page as complete.
}
static esp_err_t create_session(httpd_req_t *req, unsigned api_version)
{
    if (!same_origin_command(req)) return api_version == 1 ?
        reply(req, "403 Forbidden", "{\"error\":\"header required\"}") : v2_error(req,"403 Forbidden","header_required");
    if (req->content_len) return api_version == 1 ?
        reply(req, "400 Bad Request", "{\"error\":\"empty body required\"}") : v2_error(req,"400 Bad Request","empty_body_required");
    int64_t now = esp_timer_get_time();
    const web_control_state_t *snapshot=http_snapshot();
    uint32_t context = snapshot->context_epoch ? snapshot->context_epoch : 1;
    if (api_version == FM_HTTP_API_MAJOR && !snapshot->ready)
        return v2_error(req, "503 Service Unavailable", "owner_unavailable");
    for (unsigned i = 0; i < WEB_CLIENTS; i++) {
        bool stale_v2 = s_sessions[i].api_version == FM_HTTP_API_MAJOR && s_sessions[i].context_epoch != context;
        if (s_sessions[i].token && now - s_sessions[i].seen < 10000000LL && !stale_v2) continue;
        uint32_t token;
        bool collision;
        do {
            token = esp_random(); collision = !token;
            for (unsigned j = 0; j < WEB_CLIENTS; j++) if (s_sessions[j].token == token) collision = true;
        } while (collision);
        s_sessions[i].token = token; s_sessions[i].sequence = 0; s_sessions[i].seen = now;
        s_sessions[i].api_version = api_version; s_sessions[i].context_epoch = context;
        evt_t event = {.kind=EVT_WEB, .web={.client=i, .token=token, .op=WEB_START}};
        event_bus_publish(&event);
        char response[128];
        if (api_version == FM_HTTP_API_MAJOR)
            snprintf(response, sizeof(response), "{\"api_major\":%" PRIu32 ",\"api_minor\":%" PRIu32 ",\"token\":%" PRIu32 ",\"client\":%u,\"lease_ms\":%" PRIu32 ",\"context_epoch\":%" PRIu32 "}", FM_HTTP_API_MAJOR, FM_HTTP_API_MINOR, token, i, FM_PERFORMANCE_LEASE_MS, context);
        else snprintf(response, sizeof(response), "{\"token\":%" PRIu32 ",\"client\":%u,\"lease_ms\":1200}", token, i);
        return reply(req, "201 Created", response);
    }
    return api_version == 1 ? reply(req, "503 Service Unavailable", "{\"error\":\"four active pages; retry later\"}") :
        v2_error(req,"503 Service Unavailable","session_full");
}
static esp_err_t session(httpd_req_t *req) { return create_session(req, 1); }
static esp_err_t command(httpd_req_t *req)
{
    if (!same_origin_command(req)) return reply(req, "403 Forbidden", "{\"error\":\"header required\"}");
    if (!req->content_len || req->content_len >= 96)
        return reply(req, "400 Bad Request", "{\"error\":\"body length\"}");
    char body[96]; unsigned got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n <= 0) return ESP_FAIL; // close slow/broken socket; leases still expire
        got += n;
    }
    body[got] = 0;
    evt_t event = {.kind=EVT_WEB};
    if (memchr(body, 0, got) || !web_control_parse(body, &event.web))
        return reply(req, "400 Bad Request", "{\"error\":\"invalid command\"}");
    const web_control_state_t *snapshot=http_snapshot();
    if((snapshot->requested_engine==ENGINE_MODE_FLUTE || snapshot->applied_engine==ENGINE_MODE_FLUTE) &&
       event.web.op!=WEB_STOP && !(event.web.op==WEB_KEYS_UPDATE && !event.web.a))
        return reply(req,"400 Bad Request","{\"error\":\"flute requires API v2 / raw MIDI\"}");
    if(http_fm_active(snapshot) && event.web.op!=WEB_STOP && !(event.web.op==WEB_KEYS_UPDATE && !event.web.a))
        return reply(req,"503 Service Unavailable","{\"error\":\"FM requires API v2\"}");
    int64_t now = esp_timer_get_time();
    for (unsigned i = 0; i < WEB_CLIENTS; i++) {
        if (s_sessions[i].token != event.web.token) continue;
        if (s_sessions[i].api_version != 1)
            return reply(req, "410 Gone", "{\"error\":\"protocol session\"}");
        if (now - s_sessions[i].seen >= 10000000LL)
            return reply(req, "410 Gone", "{\"error\":\"session expired\"}");
        if (event.web.sequence <= s_sessions[i].sequence)
            return reply(req, "409 Conflict", "{\"error\":\"stale sequence\"}");
        if(snapshot->sub_patch_required && event.web.op==WEB_CC &&
           sub_patch1_required_cc(event.web.a) &&
           (!snapshot->sub_patch_supported || !snapshot->sub_patch_valid))
            return reply(req,"503 Service Unavailable","{\"error\":\"sub_patch_unavailable\"}");
        if(event.web.op==WEB_KEYS_UPDATE && event.web.a) {
            const char *reason=http_performance(event.web.token,web_control_context_epoch(snapshot),now,HTTP_PERF_PROBE);
            if(reason)return v2_error(req,"409 Conflict",reason);
        }
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
        (s->ready && !http_fm_active(s) && s->requested_engine!=ENGINE_MODE_FLUTE &&
         s->applied_engine!=ENGINE_MODE_FLUTE) ? "true":"false",s->revision,s->panic_revision,s->rendering_pending?"true":"false",s->test_tone_requested?"true":"false",
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
static esp_err_t state(httpd_req_t *req)
{
    const web_control_state_t *snapshot=http_snapshot();
    char body[3072];
    int n = format_state(body, sizeof(body), snapshot);
    if (n < 0 || (size_t)n >= sizeof(body)) return reply(req,"500 Internal Server Error","{\"error\":\"state too large\"}");
    return reply(req,"200 OK",body);
}
#include "http_v2.inc.h"
#include "http_reverb.inc.h"
#include "http_audio_routes.inc.h"
#include "http_fdc.inc.h"
#include "http_raw_midi.inc.h"
#include "http_dds.inc.h"
#include "http_ks_timbre.inc.h"
static esp_err_t ap_password(char *out, size_t size)
{
    const char *configured = CONFIG_ECT_WIFI_PASSWORD;
    if (*configured) {
        size_t n = strlen(configured);
        if (n < 8 || n > 63 || size <= n) return ESP_ERR_INVALID_ARG;
        for (size_t i = 0; i < n; i++) if ((unsigned char)configured[i] < 32 || (unsigned char)configured[i] > 126) return ESP_ERR_INVALID_ARG;
        strcpy(out, configured); return ESP_OK;
    }
    nvs_handle_t handle;
    esp_err_t error = nvs_open("web_ap", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    size_t length = size;
    error = nvs_get_str(handle, "password", out, &length);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        snprintf(out, size, "%08" PRIx32 "%08" PRIx32 "%08" PRIx32, esp_random(), esp_random(), esp_random());
        error = nvs_set_str(handle, "password", out);
        if (error == ESP_OK) error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}
void wifi_web_start(void)
{
    static web_control_state_t initial;
    web_control_read(&initial);
    const web_control_state_t *snapshot=&initial;
    if (!snapshot->ready) {
        ESP_LOGE(TAG,"voice owner unavailable; web disabled");
        status_led_set_flag(STATUS_WIFI_FAULT, true); return;
    }
    esp_netif_t *ap = NULL;
    httpd_handle_t server = NULL;
    bool wifi_initialized = false, wifi_started = false, event_loop_created = false;
    esp_event_handler_instance_t wifi_status_handler = NULL;
    char password[64];
    esp_err_t error = ap_password(password, sizeof(password)); // slider_init has initialized NVS
    size_t ssid_len = strlen(CONFIG_ECT_WIFI_SSID);
    if (error != ESP_OK || ssid_len < 1 || ssid_len > 32) {
        ESP_LOGE(TAG, "invalid AP config/NVS; web disabled (%s)", esp_err_to_name(error));
        status_led_set_flag(STATUS_WIFI_FAULT, true); return;
    }
    // Avoid turning an optional UI startup failure into a musical-task reboot.
#define WEB_TRY(call) do { error = (call); if (error != ESP_OK) { ESP_LOGE(TAG, "%s: %s", #call, esp_err_to_name(error)); goto failed; } } while (0)
    WEB_TRY(esp_netif_init());
    error = esp_event_loop_create_default();
    event_loop_created = error == ESP_OK;
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) { ESP_LOGE(TAG,"event loop: %s",esp_err_to_name(error));goto failed; }
    ap = esp_netif_create_default_wifi_ap();
    if (!ap) { ESP_LOGE(TAG,"AP netif allocation failed");goto failed; }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    // Match the configured RX Block Ack window (6); leave ~6.4KB more RAM
    // available for TCP/WiFi packet allocations while BLE is enabled.
    init.static_rx_buf_num = 6;
    // Bound packet copies during the full-page transfer; the default 32 TX
    // buffers can consume the small remaining heap before ACKs are processed.
    init.dynamic_tx_buf_num = 6;
    WEB_TRY(esp_wifi_init(&init));
    wifi_initialized = true;
    WEB_TRY(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_status_event, NULL, &wifi_status_handler));
    WEB_TRY(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t config = {.ap={.channel=CONFIG_ECT_WIFI_CHANNEL,.max_connection=WEB_CLIENTS,.authmode=WIFI_AUTH_WPA2_PSK}};
    memcpy(config.ap.ssid, CONFIG_ECT_WIFI_SSID, ssid_len); config.ap.ssid_len = ssid_len;
    strcpy((char *)config.ap.password, password);
    WEB_TRY(esp_wifi_set_mode(WIFI_MODE_AP));
    WEB_TRY(esp_wifi_set_config(WIFI_IF_AP, &config));
    WEB_TRY(esp_wifi_start());
    wifi_started = true;
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.task_priority = 3; http.stack_size = 6144;
    http.max_open_sockets = 5; http.lru_purge_enable = true;
    http.max_uri_handlers = 32;
    http.recv_wait_timeout = 2; http.send_wait_timeout = 2;
    WEB_TRY(httpd_start(&server, &http));
    const httpd_uri_t routes[] = {
        {.uri="/",.method=HTTP_GET,.handler=page},
        {.uri="/api/state",.method=HTTP_GET,.handler=state},
        {.uri="/api/audio/routes",.method=HTTP_GET,.handler=audio_routes_get},
        {.uri="/api/audio/routes",.method=HTTP_POST,.handler=audio_routes_post},
        {.uri="/api/session",.method=HTTP_POST,.handler=session},
        {.uri="/api/command",.method=HTTP_POST,.handler=command},
        {.uri="/api/v2/capabilities",.method=HTTP_GET,.handler=v2_capabilities},
        {.uri="/api/v2/state",.method=HTTP_GET,.handler=v2_state},
        {.uri="/api/v2/session",.method=HTTP_POST,.handler=v2_session},
        {.uri="/api/v2/command",.method=HTTP_POST,.handler=v2_command},
        {.uri="/api/v2/ks/timbre",.method=HTTP_GET,.handler=ks_timbre_get},
        {.uri="/api/v2/ks/timbre",.method=HTTP_POST,.handler=ks_timbre_post},
        {.uri="/api/v2/ks/presets",.method=HTTP_GET,.handler=ks_presets_get},
        {.uri="/api/v2/ks/presets",.method=HTTP_POST,.handler=ks_presets_post},
        {.uri="/api/v2/ks/preset/load",.method=HTTP_POST,.handler=ks_load_post},
        {.uri="/api/v2/ks/preset/document",.method=HTTP_GET,.handler=ks_document_get},
        {.uri="/api/v2/ks/preset/document",.method=HTTP_POST,.handler=ks_document_post},
        {.uri="/api/v2/ks/startup",.method=HTTP_POST,.handler=ks_startup_post},
        {.uri="/api/v2/fdc",.method=HTTP_GET,.handler=fdc_get},
        {.uri="/api/v2/fdc",.method=HTTP_POST,.handler=fdc_post},
        {.uri="/api/v2/effects/reverb",.method=HTTP_GET,.handler=reverb_get},
        {.uri="/api/v2/effects/reverb",.method=HTTP_POST,.handler=reverb_post},
        {.uri="/api/v1/midi/raw",.method=HTTP_POST,.handler=midi_raw_post},
        {.uri="/api/debug/midi",.method=HTTP_POST,.handler=midi_raw_post},
        {.uri="/api/debug/midi",.method=HTTP_GET,.handler=midi_debug_get},
        {.uri="/api/v2/fm/catalog",.method=HTTP_GET,.handler=v2_catalog},
        {.uri="/api/v2/fm/diagnostics",.method=HTTP_GET,.handler=v2_fm_diagnostics},
        {.uri="/api/dds/presets",.method=HTTP_GET,.handler=dds_list_http},
        {.uri="/api/dds/presets",.method=HTTP_POST,.handler=dds_command_http},
        {.uri="/api/dds/status",.method=HTTP_GET,.handler=dds_status_http},
        {.uri="/api/dds/result",.method=HTTP_GET,.handler=dds_result_http},
    };
    for (unsigned i = 0; i < sizeof(routes)/sizeof(routes[0]); i++) {
        error = httpd_register_uri_handler(server,&routes[i]);
        if (error != ESP_OK) { ESP_LOGE(TAG,"route registration failed");goto failed; }
    }
    status_led_set_flag(STATUS_WIFI_FAULT, false);
    status_led_set_flag(STATUS_WIFI_READY, true);
    ESP_LOGI(TAG,"SSID: %s; open http://192.168.4.1; BLE: FPGA_Bluetooth",CONFIG_ECT_WIFI_SSID);
    // Generated passwords are disclosed only to the local USB serial console.
    if (!*CONFIG_ECT_WIFI_PASSWORD) ESP_LOGI(TAG,"Stored local AP password: %s",password);
    return;
failed:
    status_led_set_flag(STATUS_WIFI_READY, false);
    status_led_set_flag(STATUS_WIFI_CONNECTED, false);
    status_led_set_flag(STATUS_WIFI_FAULT, true);
    if (wifi_status_handler)
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_status_handler);
    if (server) httpd_stop(server);
    if (wifi_started) esp_wifi_stop();
    if (wifi_initialized) esp_wifi_deinit();
    if (ap) esp_netif_destroy_default_wifi(ap);
    if (event_loop_created) esp_event_loop_delete_default();
    ESP_LOGE(TAG,"web disabled; Wi-Fi resources released");
#undef WEB_TRY
}
#else
void wifi_web_start(void) {}
#endif
