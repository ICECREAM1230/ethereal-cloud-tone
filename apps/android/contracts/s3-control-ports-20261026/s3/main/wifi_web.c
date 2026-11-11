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
#include "web_control_v2.h"
#include "fm_contract.h"
#include "control_api.h"
#include "dds_presets.h"


static const char *TAG = "wifi_web";
extern const char page_gzip_start[] asm("_binary_index_html_gz_start");
extern const char page_gzip_end[] asm("_binary_index_html_gz_end");

static void wifi_status_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == WIFI_EVENT_AP_STOP) {
        control_api_peer_invalidate((control_api_peer_t){CONTROL_API_WIFI,0});
        status_led_set_flag(STATUS_WIFI_READY, false);
        status_led_set_flag(STATUS_WIFI_CONNECTED, false);
        status_led_set_flag(STATUS_WIFI_FAULT, true);
    } else if (id == WIFI_EVENT_AP_STACONNECTED || id == WIFI_EVENT_AP_STADISCONNECTED) {
        // Read the actual station count; one disconnect must not hide other peers.
        wifi_sta_list_t stations;
        esp_err_t error = esp_wifi_ap_get_sta_list(&stations);
        if(error==ESP_OK && !stations.num)
            control_api_peer_invalidate((control_api_peer_t){CONTROL_API_WIFI,0});
        status_led_set_flag(STATUS_WIFI_CONNECTED, error == ESP_OK && stations.num > 0);
        status_led_set_flag(STATUS_WIFI_FAULT, error != ESP_OK);
    }
}

static esp_err_t wifi_reply(httpd_req_t *req, const char *status, const char *json)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}
static bool http_command_header(httpd_req_t *req)
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
#define reply wifi_reply
#include "web_asset.inc.h"
#undef reply
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
static const char *http_status_text(unsigned code)
{
    switch(code) {
    case 200:return "200 OK";case 201:return "201 Created";case 202:return "202 Accepted";
    case 400:return "400 Bad Request";case 403:return "403 Forbidden";case 404:return "404 Not Found";
    case 409:return "409 Conflict";case 410:return "410 Gone";
    case 500:return "500 Internal Server Error";case 503:return "503 Service Unavailable";
    default:return "500 Internal Server Error";
    }
}
static esp_err_t http_control_write(void *context,const char *bytes,size_t length)
{return httpd_resp_send_chunk(context,bytes,(int)length);}
static esp_err_t http_control(httpd_req_t *req,const char *route,bool post)
{
    char *path=NULL,*body=NULL;
    size_t query_len=httpd_req_get_url_query_len(req),route_len=strlen(route);
    path=malloc(route_len+(query_len?query_len+1:0)+1);
    if(!path)return wifi_reply(req,"503 Service Unavailable","{\"error\":\"out of memory\"}");
    memcpy(path,route,route_len+1);
    if(query_len) {
        path[route_len]='?';
        if(httpd_req_get_url_query_str(req,path+route_len+1,query_len+1)!=ESP_OK){free(path);return ESP_FAIL;}
    }
    control_api_request_t input={.path=path,.body=post?"":NULL,.body_length=req->content_len,
        .peer={CONTROL_API_WIFI,0},.control_authorized=http_command_header(req)};
    if(post && input.control_authorized) {
        size_t maximum=!strcmp(route,"/api/command") || !strcmp(route,"/api/v2/command")?95u:
            !strcmp(route,"/api/v2/effects/reverb")?54u:!strcmp(route,"/api/v1/midi/raw")?2048u:
            !strcmp(route,"/api/dds/presets")?DDS_TEXT_MAX:0u;
        if(req->content_len && req->content_len<=maximum) {
            body=malloc(req->content_len+1);
            if(!body){free(path);return wifi_reply(req,"503 Service Unavailable","{\"error\":\"out of memory\"}");}
            size_t received=0;
            while(received<req->content_len) {
                int count=httpd_req_recv(req,body+received,req->content_len-received);
                if(count<=0) {
                    free(body);free(path);
                    return !strcmp(route,"/api/dds/presets")?
                        wifi_reply(req,"400 Bad Request","{\"error\":\"incomplete DDS body\"}"):ESP_FAIL;
                }
                received+=(size_t)count;
            }
            body[received]=0;input.body=body;
            if(!strcmp(route,"/api/dds/presets"))input.owned_body=&body;
        }
    }
    char token[11],sequence[11],context[11];
    if(!strcmp(route,"/api/v1/midi/raw")) {
        if(httpd_req_get_hdr_value_str(req,"X-FPGA-Token",token,sizeof(token))==ESP_OK)input.raw_token=token;
        if(httpd_req_get_hdr_value_str(req,"X-FPGA-Sequence",sequence,sizeof(sequence))==ESP_OK)input.raw_sequence=sequence;
        if(httpd_req_get_hdr_value_str(req,"X-FPGA-Context",context,sizeof(context))==ESP_OK)input.raw_context=context;
    }
    control_api_response_t response={0};
    esp_err_t result=control_api_prepare(&input,&response);
    free(body);free(path);
    if(result!=ESP_OK) {
        control_api_release(&response);
        return wifi_reply(req,"503 Service Unavailable","{\"error\":\"control memory unavailable\"}");
    }
    httpd_resp_set_status(req,http_status_text(response.code));
    httpd_resp_set_type(req,response.content_type);
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    control_api_sink_t sink={req,http_control_write};
    result=control_api_emit(&response,&sink);
    if(result==ESP_OK)result=httpd_resp_send_chunk(req,NULL,0);
    control_api_release(&response);return result;
}
#define HTTP_CONTROL_HANDLER(name,path,post) static esp_err_t name(httpd_req_t *req){return http_control(req,path,post);}
HTTP_CONTROL_HANDLER(state,"/api/state",false)
HTTP_CONTROL_HANDLER(session,"/api/session",true)
HTTP_CONTROL_HANDLER(command,"/api/command",true)
HTTP_CONTROL_HANDLER(v2_capabilities,"/api/v2/capabilities",false)
HTTP_CONTROL_HANDLER(v2_state,"/api/v2/state",false)
HTTP_CONTROL_HANDLER(v2_session,"/api/v2/session",true)
HTTP_CONTROL_HANDLER(v2_command,"/api/v2/command",true)
HTTP_CONTROL_HANDLER(reverb_get,"/api/v2/effects/reverb",false)
HTTP_CONTROL_HANDLER(reverb_post,"/api/v2/effects/reverb",true)
HTTP_CONTROL_HANDLER(midi_raw_post,"/api/v1/midi/raw",true)
HTTP_CONTROL_HANDLER(v2_catalog,"/api/v2/fm/catalog",false)
HTTP_CONTROL_HANDLER(v2_fm_diagnostics,"/api/v2/fm/diagnostics",false)
HTTP_CONTROL_HANDLER(dds_list_http,"/api/dds/presets",false)
HTTP_CONTROL_HANDLER(dds_command_http,"/api/dds/presets",true)
HTTP_CONTROL_HANDLER(dds_status_http,"/api/dds/status",false)
HTTP_CONTROL_HANDLER(dds_result_http,"/api/dds/result",false)
#undef HTTP_CONTROL_HANDLER
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
    // The HTTP task starts only after this one-time readiness check.
    web_control_state_t *snapshot=malloc(sizeof(*snapshot));
    bool ready=false;
    if(snapshot){web_control_read(snapshot);ready=snapshot->ready;free(snapshot);}
    if (!ready) {
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
    // Bound DMA packet copies alongside the TCP send window. Three copies
    // leave headroom for ACK/RX processing with BLE feedback enabled.
    init.dynamic_tx_buf_num = 3;
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
    http.max_uri_handlers = 18;
    http.recv_wait_timeout = 2; http.send_wait_timeout = 2;
    WEB_TRY(httpd_start(&server, &http));
    const httpd_uri_t routes[] = {
        {.uri="/",.method=HTTP_GET,.handler=page},
        {.uri="/api/state",.method=HTTP_GET,.handler=state},
        {.uri="/api/session",.method=HTTP_POST,.handler=session},
        {.uri="/api/command",.method=HTTP_POST,.handler=command},
        {.uri="/api/v2/capabilities",.method=HTTP_GET,.handler=v2_capabilities},
        {.uri="/api/v2/state",.method=HTTP_GET,.handler=v2_state},
        {.uri="/api/v2/session",.method=HTTP_POST,.handler=v2_session},
        {.uri="/api/v2/command",.method=HTTP_POST,.handler=v2_command},
        {.uri="/api/v2/effects/reverb",.method=HTTP_GET,.handler=reverb_get},
        {.uri="/api/v2/effects/reverb",.method=HTTP_POST,.handler=reverb_post},
        {.uri="/api/v1/midi/raw",.method=HTTP_POST,.handler=midi_raw_post},
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
