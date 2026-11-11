#define HTTP_V2_TEST_MAIN existing_http_v2_main
#include "test_http_v2.c"
int main(void)
{
    web_control_state_t snapshot={.ready=true,.context_epoch=9,.revision=1,.ks_decay_ms=3000};
    patch_default(&snapshot.patch);
    snapshot.engine.available=snapshot.engine.valid=true;
    web_control_store(&snapshot);
    httpd_req_t caps=request("",NULL);assert(v2_capabilities(&caps)==ESP_OK);
    assert(strstr(caps.response,"\"id\":0,\"available\":false"));
    assert(strstr(caps.response,"\"id\":1,\"available\":false"));
    assert(strstr(caps.response,"\"id\":2,\"available\":false"));
    snapshot.fm.status=(engine_fm_status_t){.available=true,.valid=true,.ready=true,
        .backend_id=1,.profile_id=1,.flags=FM_FLAG_READY|FM_FLAG_MUTED};
    web_control_store(&snapshot);
    caps=request("",NULL);assert(v2_capabilities(&caps)==ESP_OK);
    assert(strstr(caps.response,"\"id\":2,\"available\":true"));
    httpd_req_t s=request("","1");assert(v2_session(&s)==ESP_OK);
    uint32_t token=s_sessions[0].token;assert(token);
    unsigned before=publication_count;char body[100];
    for(unsigned engine=0;engine<2;engine++) {
        snprintf(body,sizeof(body),"%" PRIu32 " 1 9 5 %u 0 0",token,engine);
        httpd_req_t rejected=request(body,"1");assert(v2_command(&rejected)==ESP_OK);
        assert(!strcmp(rejected.status,"503 Service Unavailable"));
        assert(strstr(rejected.response,"unsupported_engine"));
        assert(!s_sessions[0].sequence && publication_count==before);
    }
    snprintf(body,sizeof(body),"%" PRIu32 " 1 9 5 2 1 0",token);
    expect(v2_command,body,"1","202 Accepted");
    assert(s_sessions[0].sequence==1 && publication_count==before+1);
    snapshot.fm.status.ready=false;web_control_store(&snapshot);
    caps=request("",NULL);assert(v2_capabilities(&caps)==ESP_OK);
    assert(strstr(caps.response,"\"id\":2,\"available\":false"));
    puts("FM_ONLY HTTP: absent DDS/KS hidden and 503 before enqueue/sequence mutation; FM follows actual descriptor readiness PASS");
}
