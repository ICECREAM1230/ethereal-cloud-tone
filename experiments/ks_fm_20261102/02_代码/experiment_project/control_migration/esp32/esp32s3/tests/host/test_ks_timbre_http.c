#define main previous_http_api_main
#include "test_http_api.c"
#undef main
size_t httpd_req_get_url_query_len(httpd_req_t *r){return r->query?strlen(r->query):0;}
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r,char *out,size_t size)
{if(!r->query||strlen(r->query)>=size)return ESP_FAIL;strcpy(out,r->query);return ESP_OK;}
static void fixture(void)
{
    now=100;publication_count=0;memset(s_sessions,0,sizeof s_sessions);
    web_control_state_t snapshot={.ready=true,.context_epoch=9};web_control_store(&snapshot);
    s_sessions[0].token=101;s_sessions[0].context_epoch=9;s_sessions[0].api_version=2;s_sessions[0].seen=now;
    ks_timbre_state_t state={.phase=KST_APPLIED,.setting={.decay_ms=3000,.name="Harp"},.link={.timbre_available=true,.timbre_valid=true,.timbre_abi=65536,.timbre_capabilities=63,.timbre_id=1,.timbre_applied=2,.timbre_template=3,.timbre_version=1,.timbre_factory=1}};
    ks_timbre_factory(1,&state.setting.preset);state.requested_generation=2;state.requested_instrument=1;ks_timbre_state_publish(&state);
}
static void fixture_write(const char *name,const httpd_req_t *r)
{
    const char *dir=getenv("KS_TIMBRE_HTTP_FIXTURES");if(!dir)return;
    char path[1024];assert(snprintf(path,sizeof path,"%s/%s.json",dir,name)>0);
    FILE *out=fopen(path,"wb");assert(out);assert(fwrite(r->response,1,strlen(r->response),out)==strlen(r->response));assert(fclose(out)==0);
}
int main(void)
{
    ks_timbre_store_init();fixture();
    httpd_req_t r=request("{\"token\":101,\"sequence\":1,\"context\":9,\"instrument_id\":2}",NULL);
    assert(ks_timbre_post(&r)==ESP_OK&&!strcmp(r.status,"403 Forbidden"));
    r=request("{\"token\":101,\"sequence\":1,\"context\":8,\"instrument_id\":2}","1");assert(ks_timbre_post(&r)==ESP_OK&&!strcmp(r.status,"409 Conflict"));
    r=request("{\"token\":101,\"sequence\":1,\"context\":9,\"instrument_id\":2}","1");
    assert(ks_timbre_post(&r)==ESP_OK&&!strcmp(r.status,"202 Accepted"));
    assert(strstr(r.response,"\"accepted\":1")&&strstr(r.response,"\"stage\":\"received\"")&&!strstr(r.response,"applied"));
    fixture_write("post-receipt",&r);
    assert(publication_count==1&&published[0].kind==EVT_WEB_KS_TIMBRE);ks_timbre_request_t job;
    assert(ks_timbre_claim(&published[0].web_v2,&job)&&job.instrument_id==2);
    r=request("{\"token\":101,\"sequence\":2,\"context\":9,\"instrument_id\":1}","1");assert(ks_timbre_post(&r)==ESP_OK&&!strcmp(r.status,"409 Conflict"));
    ks_timbre_release(job.event.c);
    r=request("",NULL);assert(ks_timbre_get(&r)==ESP_OK&&strstr(r.response,"\"model_crc\":0")&&strstr(r.response,"\"phase\":\"applied\""));
    fixture_write("applied",&r);
    web_control_state_t common;web_control_read(&common);common.local=(engine_local_state_t){.supported=true,.local_active=true};web_control_store(&common);
    r=request("",NULL);assert(ks_timbre_get(&r)==ESP_OK&&strstr(r.response,"\"writable\":false"));fixture_write("local-frozen",&r);
    r=request("{\"token\":101,\"sequence\":2,\"context\":9,\"instrument_id\":2}","1");assert(ks_timbre_post(&r)==ESP_OK&&!strcmp(r.status,"409 Conflict")&&strstr(r.response,"ks_local_control"));
    common.local=(engine_local_state_t){0};web_control_store(&common);
    r=request("",NULL);assert(ks_timbre_get(&r)==ESP_OK);

    /* An editable object copied from the actual GET must pass the actual POST. */
    char full_preset[4096];char *preset=strstr(r.response,"\"preset\":");assert(preset);preset+=9;
    size_t preset_len=strlen(preset)-1; /* final outer-object closing brace */
    assert(snprintf(full_preset,sizeof full_preset,"{\"token\":101,\"sequence\":2,\"context\":9,\"preset\":%.*s}",(int)preset_len,preset)>0);
    r=request(full_preset,"1");assert(ks_timbre_post(&r)==ESP_OK&&!strcmp(r.status,"202 Accepted"));
    assert(ks_timbre_claim(&published[publication_count-1].web_v2,&job)&&job.has_preset&&job.setting.preset.instrument_id==1);ks_timbre_release(job.event.c);

    r=request("",NULL);assert(ks_presets_get(&r)==ESP_OK);fixture_write("directory",&r);
    r=request("",NULL);assert(ks_document_get(&r)==ESP_OK&&strstr(r.response,"[[modes]]"));
    ks_timbre_state_t state;ks_timbre_state_read(&state);state.phase=KST_IDLE;state.link.timbre_applied=3;ks_timbre_state_publish(&state);
    r=request("",NULL);assert(ks_timbre_get(&r)==ESP_OK&&strstr(r.response,"\"preset\":null"));fixture_write("unknown-preset",&r);
    state.phase=KST_UNKNOWN;state.result=(ks_timbre_result_t){.token=101,.sequence=2,.context_epoch=9,.request_id=77,.code=FM_RESULT_BACKEND_FAULT};ks_timbre_state_publish(&state);
    r=request("",NULL);assert(ks_timbre_get(&r)==ESP_OK);fixture_write("unknown-fault",&r);
    state.phase=KST_APPLIED;state.requested_generation=state.link.timbre_applied;state.dirty=true;ks_timbre_state_publish(&state);
    r=request("",NULL);assert(ks_timbre_get(&r)==ESP_OK);fixture_write("late-applied-old-fault",&r);
    state.link.timbre_valid=false;ks_timbre_state_publish(&state);
    r=request("",NULL);assert(ks_timbre_get(&r)==ESP_OK&&strstr(r.response,"\"available\":false"));fixture_write("unavailable",&r);
    r=request("{\"token\":101,\"sequence\":3,\"context\":9,\"instrument_id\":2}","1");assert(ks_timbre_post(&r)==ESP_OK&&!strcmp(r.status,"503 Service Unavailable"));
    puts("KS actual HTTP handlers: shared identity, native availability, immutable reservation, no fake 202 success and CRC zero PASS");
}
