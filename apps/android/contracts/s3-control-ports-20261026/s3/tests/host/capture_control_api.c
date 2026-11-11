// Serialize the production core from explicit SDK-boundary RAM fixtures.
// These values are not physical FPGA observations.
#define HTTP_V2_TEST_MAIN old_v2_entry
#include "test_http_v2.c"
#undef HTTP_V2_TEST_MAIN

static esp_err_t file_write(void *context,const char *bytes,size_t length)
{return fwrite(bytes,1,length,context)==length?ESP_OK:ESP_FAIL;}
static void capture(const char *directory,const char *filename,const char *path)
{
    control_api_request_t request={.path=path,.peer={CONTROL_API_WIFI,0}};
    control_api_response_t response={0};
    assert(control_api_prepare(&request,&response)==ESP_OK && response.code==200);
    char destination[4096];assert(snprintf(destination,sizeof(destination),"%s/%s",directory,filename)>0);
    FILE *file=fopen(destination,"wb");assert(file);
    control_api_sink_t sink={file,file_write};assert(control_api_emit(&response,&sink)==ESP_OK);
    assert(!fclose(file));control_api_release(&response);
}
int main(int argc,char **argv)
{
    assert(argc==2);const char *directory=argv[1];
    web_control_state_t owner={.ready=true,.context_epoch=9,.revision=1,.ks_decay_ms=3000};
    patch_default(&owner.patch);owner.engine.available=owner.engine.valid=true;
    owner.inputs=(web_input_state_t){.version=1,.available=true,.initialized=true,.enabled=true,.ready=true,
        .capabilities_known=true,.independent_usb=true,.usb_configured=true};
    web_control_store(&owner);
    capture(directory,"capabilities.json","/api/v2/capabilities");
    capture(directory,"v2-state.json","/api/v2/state");
    capture(directory,"v1-state.json","/api/state");
    capture(directory,"fm-profile.json","/api/v2/fm/catalog?profile=1&section=profile");
    capture(directory,"fm-parameters-0.json","/api/v2/fm/catalog?profile=1&section=parameters&offset=0&limit=8");
    capture(directory,"fm-parameters-8.json","/api/v2/fm/catalog?profile=1&section=parameters&offset=8&limit=8");
    capture(directory,"fm-presets.json","/api/v2/fm/catalog?profile=1&section=presets&offset=0&limit=8");
    owner.requested_engine=owner.applied_engine=FM_ENGINE_KS;
    owner.engine.requested_mode=owner.engine.applied_mode=1;
    web_control_store(&owner);
    capture(directory,"v2-state-ks.json","/api/v2/state");
    owner.engine.requested_mode=owner.engine.applied_mode=0;
    owner.fm.status=(engine_fm_status_t){.available=true,.valid=true,.ready=true,.selected=true,
        .backend_id=1,.profile_id=1,.profile_version=65536,.voice_count=8,.session_id=42,.engine_epoch=4,
        .topology_generation=1,.phase=2,.flags=FM_FLAG_READY|FM_FLAG_SELECTED,.features=1023,
        .requested_backend=1,.applied_backend=1,.accepted={42,4,7},.completed={42,4,6},.result={{42,4,6},0}};
    owner.requested_engine=owner.applied_engine=FM_ENGINE_FM;
    owner.fm.source_numerator=3579545;owner.fm.source_denominator=64;
    owner.fm.output_numerator=96000;owner.fm.output_denominator=1;
    owner.fm.parameter_count=1;owner.fm.parameters[0]=(engine_fm_parameter_t){18,0xffff0000,31,6};
    for(unsigned i=0;i<8;i++)owner.fm.operators_per_voice[i]=4;
    web_control_store(&owner);
    capture(directory,"capabilities-fm-selected.json","/api/v2/capabilities");
    capture(directory,"v2-state-fm-selected.json","/api/v2/state");
    owner.fm.status.ready=false;owner.fm.status.valid=false;
    owner.fm.status.flags=FM_FLAG_FAULT|FM_FLAG_MUTED;owner.fm.status.reason=FM_LINK_BACKEND;
    owner.fm.status.recovering=true;owner.mode_recovering=true;
    web_control_store(&owner);
    capture(directory,"capabilities-fm-fault.json","/api/v2/capabilities");
    capture(directory,"v2-state-fm-fault.json","/api/v2/state");
    assert(!publication_count);
    puts("control API fixture capture: real core serialization, capabilities/v1/v2/catalog/selected/fault, no musical events PASS");
}
