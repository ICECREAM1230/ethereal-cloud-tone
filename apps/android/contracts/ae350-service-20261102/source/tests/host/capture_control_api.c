// Explicit SDK-boundary observations, serialized by the production API core.
// Owner adoption below is a host fixture, not a physical FPGA or audio receipt.
#define HTTP_V2_TEST_MAIN old_v2_entry
#include "test_http_v2.c"
#undef HTTP_V2_TEST_MAIN
static const control_api_peer_t fixture_peer={CONTROL_API_BLE_CONTROL,1001};
static esp_err_t fixture_write(void *context,const char *bytes,size_t length)
{return fwrite(bytes,1,length,context)==length?ESP_OK:ESP_FAIL;}
static void save_response(const char *directory,const char *name,control_api_response_t *response)
{
    char path[4096];assert(snprintf(path,sizeof path,"%s/%s.json",directory,name)>0);
    FILE *out=fopen(path,"wb");assert(out);control_api_sink_t sink={out,fixture_write};
    assert(control_api_emit(response,&sink)==ESP_OK && !fclose(out));control_api_release(response);
}
static control_api_response_t fixture_request(const char *path,const char *body)
{
    control_api_request_t request={.path=path,.body=body,.body_length=body?strlen(body):0,
        .peer=fixture_peer,.control_authorized=true};control_api_response_t response={0};
    assert(control_api_prepare(&request,&response)==ESP_OK);return response;
}
static void capture(const char *directory,const char *name,const char *path)
{control_api_response_t response=fixture_request(path,NULL);assert(response.code==200);save_response(directory,name,&response);}
static void adopted(web_control_state_t *owner,unsigned client,uint32_t token,uint32_t sequence)
{
    owner->token[client]=token;owner->sequence[client]=owner->parameter_sequence[client]=sequence;
    owner->v2_result_sequence[client]=sequence;owner->v2_result_code[client]=FM_RESULT_OK;
    owner->v2_result_context[client]=owner->context_epoch;owner->revision++;
    const evt_t *event=&published[publication_count-1];
    control_source_complete(event->source,token,sequence,CONTROL_SOURCE_OUTPUT,true);web_control_store(owner);
}
int main(int argc,char **argv)
{
    assert(argc==2);const char *directory=argv[1];
    web_control_state_t owner={.ready=true,.revision=1,.context_epoch=9,.ks_decay_ms=3000,
        .sub_patch_required=true,.sub_patch_supported=true,.sub_patch_valid=true};patch_default(&owner.patch);
    owner.engine.available=owner.engine.valid=true;
    owner.cpu_mode=(engine_cpu_mode_state_t){.supported=true,.valid=true,.ready=true,.channel_routes=true};
    owner.inputs=(web_input_state_t){.version=1,.available=true,.initialized=true,.enabled=true,.ready=true,
        .capabilities_known=true,.independent_usb=true,.usb_configured=true};
    for(unsigned i=0;i<WEB_CLIENTS;i++)owner.midi_channel[i]=1;
    web_control_store(&owner);
    capture(directory,"capabilities-channel-routes","/api/v2/capabilities");
    capture(directory,"state-global-sub-client-ks","/api/v2/state");
    capture(directory,"legacy-state-global-sub","/api/state");
    control_api_response_t response=fixture_request("/api/v2/session","");assert(response.code==201);
    unsigned client=published[0].web.client;uint32_t token=published[0].web.token;
    save_response(directory,"session",&response);owner.token[client]=token;web_control_store(&owner);
    char body[128];snprintf(body,sizeof body,"%u 1 9 2 74 64 0",token);
    response=fixture_request("/api/v2/command",body);assert(response.code==202);save_response(directory,"sub-parameter-received",&response);
    adopted(&owner,client,token,1);capture(directory,"sub-parameter-owner-adopted","/api/v2/state");
    snprintf(body,sizeof body,"%u 2 9 5 0 0 0",token);
    response=fixture_request("/api/v2/command",body);assert(response.code==202);save_response(directory,"channel-0-received",&response);
    owner.midi_channel[client]=0;adopted(&owner,client,token,2);capture(directory,"state-channel-0-global-sub","/api/v2/state");
    snprintf(body,sizeof body,"%u 3 9 5 2 0 0",token);
    response=fixture_request("/api/v2/command",body);assert(response.code==202);save_response(directory,"channel-2-received",&response);
    owner.midi_channel[client]=2;adopted(&owner,client,token,3);capture(directory,"state-channel-2-global-sub","/api/v2/state");
    snprintf(body,sizeof body,"%u 4 9 5 1 0 0",token);
    response=fixture_request("/api/v2/command",body);assert(response.code==202);save_response(directory,"channel-1-received",&response);
    owner.midi_channel[client]=1;adopted(&owner,client,token,4);capture(directory,"state-channel-1-global-sub","/api/v2/state");
    assert(owner.requested_engine==0 && owner.applied_engine==0 && owner.cpu_mode.desired==0 && owner.cpu_mode.applied==0);
    puts("AE350 fixture capture: real core serializer, explicit host owner adoption, global SUB0 retained across client channel0/2/1 PASS");
}
