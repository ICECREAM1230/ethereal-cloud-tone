// Real HTTP handlers with the prepared CPU route and configuration-only FM.
#include "../../contract/fm/fm_config_v1.h"
#define HTTP_V2_TEST_MAIN legacy_http_v2_entry
#include "test_http_v2.c"
#undef HTTP_V2_TEST_MAIN

int main(int argc,char **argv)
{
    assert(argc==4);
    web_control_state_t s={.ready=true,.revision=1,.context_epoch=9,.ks_decay_ms=3000};
    patch_default(&s.patch);s.engine.available=s.engine.valid=true;
    s.cpu_mode=(engine_cpu_mode_state_t){.supported=true,.valid=true,.ready=true,
        .channel_routes=true,.desired=0,.applied=0,.sequence=3};
    s.requested_engine=s.applied_engine=0;
    s.midi_channel[0]=2; // Current Web client selects FM while global route stays0.
    s.fm.status=(engine_fm_status_t){.available=true,.valid=true,.ready=true,.selected=true,
        .backend_id=1,.profile_id=1,.profile_version=65536,.voice_count=ENGINE_CPU_FM_VOICES,.session_id=42,.engine_epoch=1,
        .topology_generation=1,.phase=FM_PHASE_READY,.flags=FM_FLAG_READY|FM_FLAG_SELECTED,
        .features=FM_FEATURE_SET_PARAMETER|FM_FEATURE_APPLY_PRESET|FM_FEATURE_NEXT_NOTE_PARAMETERS|FM_FEATURE_NATIVE_STEREO_PAN,
        .requested_backend=1,.applied_backend=1};
    s.fm.source_numerator=s.fm.output_numerator=91015625;
    s.fm.source_denominator=s.fm.output_denominator=948;
    for(unsigned i=0;i<ENGINE_CPU_FM_VOICES;i++)s.fm.operators_per_voice[i]=ENGINE_CPU_FM_OPERATORS;
    const fm_preset_t *p=fm_preset(1,FMC_DEFAULT_PRESET_ID);assert(p&&p->values_count==46);
    for(unsigned i=0;i<p->values_count;i++){
        uint32_t target;assert(fm_pack_target(FM_TARGET_NONE,p->values[i].operator_index,&target));
        s.fm.parameters[i]=(engine_fm_parameter_t){p->values[i].parameter_id,target,p->values[i].value_u32,0};
    }
    s.fm.parameter_count=p->values_count;web_control_store(&s);
    httpd_req_t req=request("",NULL);assert(v2_capabilities(&req)==ESP_OK);
    assert(strstr(req.response,"\"cpu_mode\":{\"version\":1"));write_response(argv[1],&req);
    req=request("",NULL);assert(v2_state(&req)==ESP_OK);
    assert(strstr(req.response,"\"features\":816"));assert(strstr(req.response,"\"voice_count\":32"));write_response(argv[2],&req);
    req=request("",NULL);req.query="profile=1&section=presets&offset=0&limit=8";
    assert(v2_catalog(&req)==ESP_OK);
    assert(strstr(req.response,"electric_piano_tine")&&strstr(req.response,"metal_bell"));write_response(argv[3],&req);
    req=request("","1");assert(v2_session(&req)==ESP_OK);
    assert(s_sessions[0].token==1001);
    unsigned before=publication_count;
    expect(v2_command,"1001 1 9 8 3 0 0","1","202 Accepted");
    expect(v2_command,"1001 2 9 8 4 0 0","1","202 Accepted");
    expect(v2_command,"1001 3 9 7 18 4294901760 30","1","202 Accepted");
    expect(v2_command,"1001 4 9 5 2 1 0","1","202 Accepted");
    expect(v2_command,"1001 5 9 5 2 2 0","1","503 Service Unavailable");
    expect(v2_command,"1001 5 9 8 99 0 0","1","400 Bad Request");
    expect(v2_command,"1001 5 9 7 18 4294901760 32","1","400 Bad Request");
    assert(s_sessions[0].sequence==4&&publication_count==before+4);
    for(unsigned i=before;i<publication_count;i++)assert(published[i].kind==EVT_WEB_V2);
    s.fm.status.valid=false;web_control_store(&s);
    expect(v2_command,"1001 5 9 8 3 0 0","1","503 Service Unavailable");
    assert(publication_count==before+4&&s_sessions[0].sequence==4);
    puts("PASS ae350_fm_http: actual v2 handlers, five presets, piano/bell/operator and JT51 route accepted, unsupported backend/range/cache rejected without consuming sequence");
}
