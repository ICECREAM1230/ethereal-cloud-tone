// The real shared core must preserve AE350 CPU1/SUB1/channel observations.
#define HTTP_CUSTOM_FDC_STATE
#define HTTP_V2_TEST_MAIN previous_v2_main
#include "test_http_v2.c"
#undef HTTP_V2_TEST_MAIN
#include "control_api.h"
static engine_fdc_state_t http_fdc_link;
static bool advance_fdc_observation;
void engine_link_fdc_state(engine_fdc_state_t *out)
{
    if(advance_fdc_observation) {
        // The SPI owner publishes after prepare entered, before RAM is read.
        http_fdc_link.observed_us=++now;advance_fdc_observation=false;
    }
    *out=http_fdc_link;
}
typedef struct {char bytes[32768];size_t length;} api_capture_t;
static esp_err_t api_capture_write(void *context,const char *bytes,size_t length)
{
    api_capture_t *capture=context;assert(capture->length+length<sizeof capture->bytes);
    memcpy(capture->bytes+capture->length,bytes,length);capture->length+=length;capture->bytes[capture->length]=0;return ESP_OK;
}
static void api_capture(const control_api_response_t *response,api_capture_t *capture)
{
    *capture=(api_capture_t){0};control_api_sink_t sink={capture,api_capture_write};
    assert(control_api_emit(response,&sink)==ESP_OK);
}
static control_api_response_t ae_request(const char *path,const char *body)
{
    control_api_request_t request={.path=path,.body=body,.body_length=body?strlen(body):0,
        .peer={CONTROL_API_BLE_CONTROL,300},.control_authorized=true};
    control_api_response_t response={0};assert(control_api_prepare(&request,&response)==ESP_OK);return response;
}
static void http_ble_equivalent(const char *path,const char *body)
{
    char route[256];const char *query=strchr(path,'?');size_t route_length=query?(size_t)(query-path):strlen(path);
    assert(route_length<sizeof route);memcpy(route,path,route_length);route[route_length]=0;
    static httpd_req_t http;http=request(body?body:"","1");http.query=query?query+1:NULL;
    assert(http_control(&http,route,body!=NULL)==ESP_OK);
    control_api_response_t response=ae_request(path,body);static api_capture_t capture;
    api_capture(&response,&capture);
    assert(response.code==status_number(http.status) && !strcmp(response.content_type,http.type));
    assert(capture.length==http.response_len && !memcmp(capture.bytes,http.response,capture.length));
    control_api_release(&response);
}
static void target_features(web_control_state_t *owner)
{
    owner->cpu_mode.fault=false;owner->midi_channel[0]=1;
    owner->inputs=(web_input_state_t){.version=1,.available=true,.initialized=true,.enabled=true,.ready=true};
    web_control_store(owner);ks_timbre_store_init();
    ks_timbre_state_t ks={.phase=KST_APPLIED,.requested_generation=7,.requested_instrument=1,
        .setting={.name="Harp",.decay_ms=3000},.link={.timbre_available=true,.timbre_valid=true,
        .timbre_abi=65536,.timbre_capabilities=63,.timbre_id=1,.timbre_applied=7,.timbre_version=1,.timbre_factory=1}};
    assert(ks_timbre_factory(1,&ks.setting.preset));ks_timbre_state_publish(&ks);
    web_fdc_state_t fdc={.link={.available=true,.valid=true,.fresh=true,.word=FDC_STATUS_TAG|3u,.observed_us=now,
        .raw={.supported=true,.valid=true,.fresh=true,.sample_valid=true,.sample_fresh=true,
            .raw0=13000222,.raw1=11000000,.gain_q16=32768,.sequence=17,.revision=31}}};
    http_fdc_link=fdc.link;fdc_control_state_publish(&fdc);
    http_audio_state=(engine_audio_routes_state_t){.available=true,.valid=true,.ready=true,.pcm_a=1};
    unsigned publications=publication_count;
    for(size_t i=0;i<sizeof(s_routes)/sizeof(s_routes[0]);i++)
        http_ble_equivalent(s_routes[i].path,s_routes[i].post?"bad":NULL);
    const char *queries[]={"/api/v2/fm/catalog?profile=1&section=profile",
        "/api/v2/fm/catalog?profile=1&section=parameters&offset=0&limit=8",
        "/api/v2/fm/catalog?profile=1&section=presets&offset=0&limit=8",
        "/api/dds/result?id=1","/api/v2/ks/preset/document?slot=7"};
    for(unsigned i=0;i<sizeof queries/sizeof *queries;i++)http_ble_equivalent(queries[i],NULL);
    assert(publication_count==publications);

    advance_fdc_observation=true;
    control_api_response_t response=ae_request("/api/v2/fdc",NULL);assert(response.code==200);
    static api_capture_t before,after;api_capture(&response,&before);
    assert(strstr(before.bytes,"\"ch0\":13000222") && strstr(before.bytes,"\"gain_percent\":50.00"));
    assert(strstr(before.bytes,"\"available\":true,\"valid\":true,\"fresh\":true,\"status_word\":"));
    now+=FDC_FRESH_US+1;http_fdc_link.raw.raw0=99;http_fdc_link.valid=false;
    api_capture(&response,&after);assert(!strcmp(before.bytes,after.bytes));control_api_release(&response);
    now=100;http_fdc_link=fdc.link;
    response=ae_request("/api/v2/ks/timbre",NULL);api_capture(&response,&before);
    ks.link.timbre_valid=false;ks.phase=KST_UNKNOWN;ks_timbre_state_publish(&ks);
    api_capture(&response,&after);assert(!strcmp(before.bytes,after.bytes));control_api_release(&response);
    ks.link.timbre_valid=true;ks.phase=KST_APPLIED;ks_timbre_state_publish(&ks);

    publications=publication_count;response=ae_request("/api/v2/session","");assert(response.code==201);
    uint32_t token=published[publications].web.token;unsigned client=published[publications].web.client;
    control_api_release(&response);char body[512];
    snprintf(body,sizeof body,"%u 1 9 5 2 0 0",token);response=ae_request("/api/v2/command",body);assert(response.code==202);
    evt_t event=published[publication_count-1];assert(event.source.kind==CONTROL_SOURCE_BLE_CTRL && event.web_v2.a==2);
    control_source_complete(event.source,token,1,CONTROL_SOURCE_OUTPUT,true);control_api_release(&response);
    // Explicit owner fixture: route adoption changes this client, not global SUB0.
    owner->token[client]=token;owner->midi_channel[client]=2;owner->parameter_sequence[client]=1;web_control_store(owner);
    response=ae_request("/api/v2/state",NULL);api_capture(&response,&before);
    assert(strstr(before.bytes,"\"mode\":{\"requested\":0,\"applied\":0") && strstr(before.bytes,"\"midi_channel\":2"));
    control_api_release(&response);
    owner->sub_patch_supported=false;web_control_store(owner);
    snprintf(body,sizeof body,"%u 2 9 2 74 64 0",token);response=ae_request("/api/v2/command",body);
    assert(response.code==503);api_capture(&response,&before);assert(strstr(before.bytes,"sub_patch_unavailable"));control_api_release(&response);
    owner->sub_patch_supported=true;owner->mode_switching=true;web_control_store(owner);
    snprintf(body,sizeof body,"{\"source\":\"web\",\"bytes\":[146,60,100],\"token\":%u,\"sequence\":2,\"context_epoch\":9}",token);
    response=ae_request("/api/v1/midi/raw",body);assert(response.code==202);event=published[publication_count-1];
    web_raw_job_t raw;assert(web_raw_queue_take(event.web_v2.a,&raw) && raw.request.bytes[0]==146);
    assert(event.source.kind==CONTROL_SOURCE_BLE_CTRL);control_api_release(&response);
    snprintf(body,sizeof body,"%u 3 9 1 0 0 0 0 0 0",token);response=ae_request("/api/v2/fdc",body);assert(response.code==202);
    event=published[publication_count-1];fdc_request_t fdc_job;assert(event.kind==EVT_WEB_FDC && event.source.kind==CONTROL_SOURCE_BLE_CTRL);
    assert(fdc_control_claim(&event.web_v2,&fdc_job));fdc_control_release(fdc_job.event.c);control_api_release(&response);
    snprintf(body,sizeof body,"{\"token\":%u,\"sequence\":4,\"context\":9,\"instrument_id\":2}",token);
    response=ae_request("/api/v2/ks/timbre",body);assert(response.code==202);event=published[publication_count-1];
    ks_timbre_request_t ks_job;assert(event.kind==EVT_WEB_KS_TIMBRE && event.source.kind==CONTROL_SOURCE_BLE_CTRL);
    assert(ks_timbre_claim(&event.web_v2,&ks_job) && ks_job.instrument_id==2);ks_timbre_release(ks_job.event.c);control_api_release(&response);
    response=ae_request("/api/audio/routes","2 1");assert(response.code==202 && http_audio_state.pending);control_api_release(&response);
}
int main(void)
{
    web_control_state_t owner={.ready=true,.context_epoch=9,.ks_decay_ms=3000};patch_default(&owner.patch);
    owner.cpu_mode=(engine_cpu_mode_state_t){.supported=true,.valid=true,.ready=true,.channel_routes=true};
    owner.sub_patch_required=owner.sub_patch_supported=owner.sub_patch_valid=true;
    owner.midi_channel[0]=1;web_control_store(&owner);
    control_api_request_t request={.path="/api/v2/state",.peer={CONTROL_API_WIFI,0}};
    control_api_response_t response={0};assert(control_api_prepare(&request,&response)==ESP_OK && response.code==200);
    static api_capture_t first,second;api_capture(&response,&first);
    owner.cpu_mode.fault=true;owner.midi_channel[0]=2;web_control_store(&owner);api_capture(&response,&second);
    assert(!strcmp(first.bytes,second.bytes));
    assert(strstr(first.bytes,"\"route_only\":true") && strstr(first.bytes,"\"midi_channel\":1"));
    assert(strstr(first.bytes,"\"sub_patch\":{\"version\":1,\"required\":true,\"supported\":true,\"valid\":true}"));
    control_api_release(&response);assert(!publication_count);
    target_features(&owner);
    puts("AE350 shared API: coherent CPU route-only/SUB1/client channel response and repeatable emission PASS");
    puts("AE350 shared API: all30 routes HTTP/BLE equivalent; FDC/KS frozen GETs; channel/RAW/SUB1 guards; source-stamped FDC/KS and anonymous audio queue PASS");
}
