#define main previous_http_main
#include "test_http_api.c"
#undef main
int main(void)
{
    http_audio_state=(engine_audio_routes_state_t){.available=true,.valid=true,.ready=true,.pcm_a=1};
    httpd_req_t req=request("",NULL);assert(audio_routes_get(&req)==ESP_OK);
    assert(strstr(req.response,"\"pt8211\":0")&&strstr(req.response,"\"pcm_a\":1")&&strstr(req.response,"\"pcm_b\":3"));
    req=request("0 1",NULL);assert(audio_routes_post(&req)==ESP_OK&&!strcmp(req.status,"403 Forbidden"));
    const char *bad[]={"","0","0 1 ","0 4","-1 0","0\t1","0\n1","a 1","00 1"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        req=request(bad[i],"1");assert(audio_routes_post(&req)==ESP_OK&&!strcmp(req.status,"400 Bad Request"));
        assert(!http_audio_state.pending);
    }
    for(unsigned hp=0;hp<4;hp++)for(unsigned a=0;a<4;a++) {
        char body[4]={(char)('0'+hp),' ',(char)('0'+a),0};
        http_audio_state.pending=false;req=request(body,"1");req.chunk=1;
        assert(audio_routes_post(&req)==ESP_OK&&!strcmp(req.status,"202 Accepted"));
        assert(strstr(req.response,"pending_audio_frame")&&http_audio_state.pending);
        req=request("0 0","1");assert(audio_routes_post(&req)==ESP_OK&&!strcmp(req.status,"409 Conflict"));
    }
    http_audio_state.pending=false;http_audio_state.valid=false;
    req=request("0 1","1");assert(audio_routes_post(&req)==ESP_OK&&!strcmp(req.status,"409 Conflict"));
    puts("PASS AOR1 HTTP: strict body/header; all16 selections; pending is not applied; busy/offline reject");
    return 0;
}
