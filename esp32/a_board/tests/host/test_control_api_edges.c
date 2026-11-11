// Regressions from the independent source-owner review.
#define main old_http_fixture_main
#include "test_http_api.c"
#undef main
static control_source_ref_t anonymous_source;
bool dds_presets_request_source(control_source_ref_t source,dds_operation_t operation,
    const char *path,const char *text,size_t length,unsigned argument,uint32_t *id,char *error)
{
    (void)operation;(void)path;(void)text;(void)length;(void)argument;
    anonymous_source=source;*id=1;error[0]=0;
    return control_source_reserve(source,0,*id,now,now+WEB_LEASE_US,CONTROL_SOURCE_OUTPUT);
}
static control_api_request_t edge_request(const char *path,const char *body,control_api_peer_t peer)
{
    return (control_api_request_t){.path=path,.body=body,.body_length=body?strlen(body):0,
        .peer=peer,.control_authorized=true};
}
int main(void)
{
    control_source_init();web_control_state_t owner={.ready=true,.context_epoch=9};web_control_store(&owner);
    control_api_response_t response={0};
    control_api_request_t request=edge_request("/api/v2/session","",(control_api_peer_t){CONTROL_API_BLE_CONTROL,20});
    for(unsigned i=0;i<WEB_CLIENTS;i++) {
        assert(control_api_prepare(&request,&response)==ESP_OK && response.code==201);control_api_release(&response);
    }
    control_source_ref_t old=published[0].source;unsigned before=publication_count;
    request.peer.connection_epoch=21;
    assert(control_api_prepare(&request,&response)==ESP_OK && response.code==201);
    control_source_ref_t current=published[before].source;control_api_release(&response);
    assert(!control_source_valid(old) && control_source_valid(current));
    control_api_disconnect((control_api_peer_t){CONTROL_API_BLE_CONTROL,20});assert(control_source_valid(current));
    request.peer.connection_epoch=19;before=publication_count;
    assert(control_api_prepare(&request,&response)==ESP_OK && response.code==410);
    control_api_release(&response);assert(publication_count==before && control_source_valid(current));

    request=edge_request("/api/dds/presets?op=load&path=Patches/Factory/Pads/Galaxy.toml","",
        (control_api_peer_t){CONTROL_API_WIFI,0});
    assert(control_api_prepare(&request,&response)==ESP_OK && response.code==202);control_api_release(&response);
    assert(control_source_valid(anonymous_source));
    control_api_disconnect((control_api_peer_t){CONTROL_API_WIFI,0});
    assert(!control_source_valid(anonymous_source));
    request=edge_request("/api/v2/session","",(control_api_peer_t){CONTROL_API_BLE_CONTROL,22});before=publication_count;
    assert(control_api_prepare(&request,&response)==ESP_OK && response.code==201);control_api_release(&response);
    current=published[before].source;control_api_disconnect((control_api_peer_t){CONTROL_API_WIFI,0});
    assert(control_source_valid(current));
    puts("control API review edges: four old BLE slots reclaimed on reconnect; older epoch rejected; anonymous WiFi DDS revoked; delayed WiFi cleanup preserves new BLE source PASS");
}
