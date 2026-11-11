// Real Wi-Fi event callback and API core; only the SDK station query is fake.
#define main old_http_fixture_main
#include "test_http_api.c"
#undef main
#include <unistd.h>
static unsigned stations;
static esp_err_t station_error;
static control_source_ref_t anonymous_source;
esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *out)
{out->num=stations;return station_error;}
bool dds_presets_request_source(control_source_ref_t source,dds_operation_t operation,
    const char *path,const char *text,size_t length,unsigned argument,uint32_t *id,char *error)
{
    (void)operation;(void)path;(void)text;(void)length;(void)argument;
    anonymous_source=source;*id=1;error[0]=0;
    return control_source_reserve(source,0,*id,now,now+WEB_LEASE_US,CONTROL_SOURCE_OUTPUT);
}
static void anonymous_job(void)
{
    control_api_request_t request={.path="/api/dds/presets?op=load&path=Patches/Factory/Pads/Galaxy.toml",
        .body="",.peer={CONTROL_API_WIFI,0},.control_authorized=true};
    control_api_response_t response={0};
    assert(control_api_prepare(&request,&response)==ESP_OK && response.code==202);
    control_api_release(&response);assert(control_source_valid(anonymous_source));
    for(unsigned i=0;i<WEB_CLIENTS;i++)assert(!s_sessions[i].token);
}
static void sdk_event(int event,unsigned count,esp_err_t error)
{
    stations=count;station_error=error;
    // The SDK callback must not acquire the session mutex: an HTTP task may
    // hold it when the independent event task reports a physical link loss.
    core_lock();alarm(3);wifi_status_event(NULL,WIFI_EVENT,event,NULL);alarm(0);core_unlock();
}
int main(void)
{
    control_source_init();anonymous_job();
    sdk_event(WIFI_EVENT_AP_STADISCONNECTED,1,ESP_OK);assert(control_source_valid(anonymous_source));
    sdk_event(WIFI_EVENT_AP_STADISCONNECTED,0,ESP_FAIL);assert(control_source_valid(anonymous_source));
    sdk_event(WIFI_EVENT_AP_STADISCONNECTED,0,ESP_OK);assert(!control_source_valid(anonymous_source));
    anonymous_job();sdk_event(WIFI_EVENT_AP_STACONNECTED,1,ESP_OK);assert(control_source_valid(anonymous_source));
    sdk_event(WIFI_EVENT_AP_STOP,1,ESP_OK);assert(!control_source_valid(anonymous_source));
    assert(!publication_count);
    puts("control API WiFi events: real AP_STOP/last-STA callback fences anonymous DDS without core mutex; remaining STA and query errors preserve source PASS");
}
