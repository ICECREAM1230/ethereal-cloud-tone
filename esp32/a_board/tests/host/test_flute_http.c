#define HTTP_V2_TEST_MAIN previous_http_v2_main
#include "test_http_v2.c"
#undef HTTP_V2_TEST_MAIN
int main(void)
{
    static web_control_state_t snapshot;
    now=100;publication_count=random_call=0;memset(s_sessions,0,sizeof(s_sessions));
    snapshot.ready=true;snapshot.context_epoch=9;
    patch_default(&snapshot.patch);web_control_store(&snapshot);
    httpd_req_t session_req=request("","1");assert(v2_session(&session_req)==ESP_OK);
    assert(s_sessions[0].token==1001);
    httpd_req_t caps=request("",NULL);assert(v2_capabilities(&caps)==ESP_OK);
    assert(strstr(caps.response,"{\"id\":4,\"available\":false,\"note_min\":60,\"note_max\":84}"));
    expect(v2_command,"1001 1 9 5 4 0 0","1","503 Service Unavailable");
    assert(s_sessions[0].sequence==0 && publication_count==1);
    snapshot.flute=(engine_flute_state_t){.available=true,.valid=true,.ready=true,.preset=1};
    web_control_store(&snapshot);caps=request("",NULL);assert(v2_capabilities(&caps)==ESP_OK);
    assert(strstr(caps.response,"{\"id\":4,\"available\":true"));
    expect(v2_command,"1001 1 9 5 4 0 0","1","202 Accepted");
    assert(published[1].web_v2.a==4 && published[1].web_v2.b==0);
    snapshot.requested_engine=snapshot.applied_engine=4;snapshot.flute.applied_mode=true;
    web_control_store(&snapshot);
    httpd_req_t state_req=request("",NULL);assert(v2_state(&state_req)==ESP_OK);
    assert(strstr(state_req.response,"\"requested\":4,\"applied\":4"));
    assert(strstr(state_req.response,"\"flute\":{\"available\":true"));
    expect(v2_command,"1001 2 9 2 74 100 0","1","400 Bad Request");
    expect(v2_command,"1001 2 9 6 2000 0 0","1","400 Bad Request");
    expect(v2_command,"1001 2 9 3 1 100 0","1","400 Bad Request");
    assert(s_sessions[0].sequence==1 && publication_count==2);
    httpd_req_t raw=request("{\"source\":\"web\",\"bytes\":[144,60,100,128,60,0,144,84,100,128,84,0],\"token\":1001,\"sequence\":2,\"context_epoch\":9}","1");
    assert(midi_raw_post(&raw)==ESP_OK && !strcmp(raw.status,"202 Accepted"));
    assert(published[2].kind==EVT_WEB_RAW);
    expect(v2_command,"1001 3 9 3 4096 100 0","1","202 Accepted");
    expect(v2_command,"1001 4 9 5 3 0 0","1","400 Bad Request");
    puts("FLU1 HTTP: absent/present capabilities, mode4 RPC/state, RAW60/84, explicit unsupported parameters/notes PASS");
    return 0;
}
