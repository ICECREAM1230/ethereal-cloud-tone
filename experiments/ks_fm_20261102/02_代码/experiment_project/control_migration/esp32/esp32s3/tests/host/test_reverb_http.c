/* Real reverb handlers using only the existing HTTP SDK boundary. */
#define main previous_http_api_main
#include "test_http_api.c"
#undef main
size_t httpd_req_get_url_query_len(httpd_req_t *r){return r->query?strlen(r->query):0;}
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r,char *out,size_t size)
{if(!r->query || strlen(r->query)>=size)return ESP_FAIL;strcpy(out,r->query);return ESP_OK;}
static web_control_state_t snapshot;
static httpd_req_t captured;
static uint32_t last_job;
static void fixture_reverb(void)
{
    web_control_reverb_release(last_job);
    now=100;publication_count=random_call=0;
    memset(s_sessions,0,sizeof(s_sessions));memset(&s_http_performance,0,sizeof(s_http_performance));
    memset(&snapshot,0,sizeof(snapshot));snapshot.ready=true;snapshot.context_epoch=9;
    snapshot.reverb.link=(engine_reverb_state_t){.available=true,.valid=true,
        .wet_q15=8192,.damp_q15=123,.applied_sequence=7,.generation=5};
    web_control_store(&snapshot);
    s_sessions[0].token=101;s_sessions[0].context_epoch=9;s_sessions[0].api_version=2;s_sessions[0].seen=now;
}
static void call_reverb(const char *input,const char *header,const char *status_code,const char *code)
{
    uint32_t before=s_sessions[0].sequence;unsigned publications=publication_count;
    captured=request(input,header);
    assert(reverb_post(&captured)==ESP_OK);
    if(strcmp(captured.status,status_code))fprintf(stderr,"reverb expected %s got %s: %s\n",status_code,captured.status,captured.response);
    assert(!strcmp(captured.status,status_code));
    assert(!strcmp(captured.cache,"no-store") && !strcmp(captured.nosniff,"nosniff"));
    if(code)assert(strstr(captured.response,code));
    if(status_code[0]!='2')assert(s_sessions[0].sequence==before && publication_count==publications);
    else last_job=published[publication_count-1].web_v2.c;
}
static void strict_ingress(void)
{
    fixture_reverb();
    call_reverb("101 1 9 0 0",NULL,"403 Forbidden","header_required");
    const char *invalid[]={"","101 1 9 32769 0","101 1 9 0 28673","101 1 9 -1 0",
        "101 1 9 0 0\n","101  1 9 0 0","101 1 9 0 0 ","101 1 9 0 0 0",
        "4294967296 1 9 0 0","101 0 9 0 0","101 1 0 0 0","0 1 9 0 0"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)call_reverb(invalid[i],"1","400 Bad Request","invalid_reverb");
    captured=request("101 1 9 0 0","1");const unsigned char nul[]="101 1 9 0 0\0x";
    captured.input=nul;captured.content_len=sizeof(nul)-1;
    assert(reverb_post(&captured)==ESP_OK && !strcmp(captured.status,"400 Bad Request"));
    captured=request("101 1 9 0 0","1");captured.receive_fails=true;
    assert(reverb_post(&captured)==ESP_FAIL && !publication_count && !s_sessions[0].sequence);
    call_reverb("999 1 9 0 0","1","410 Gone","unknown_session");
    call_reverb("101 1 8 0 0","1","409 Conflict","stale_context");
    s_sessions[0].api_version=1;
    call_reverb("101 1 9 0 0","1","410 Gone","protocol_session");
    s_sessions[0].api_version=2;s_sessions[0].sequence=1;
    call_reverb("101 1 9 0 0","1","409 Conflict","stale_sequence");
    now+=10000000LL;
    call_reverb("101 2 9 0 0","1","410 Gone","session_expired");
}
static void availability_and_bounded_queue(void)
{
    fixture_reverb();snapshot.reverb.link.available=false;web_control_store(&snapshot);
    call_reverb("101 1 9 0 0","1","503 Service Unavailable","reverb_unavailable");
    snapshot.reverb.link.available=true;snapshot.reverb.link.valid=false;web_control_store(&snapshot);
    call_reverb("101 1 9 0 0","1","503 Service Unavailable","reverb_unavailable");
    snapshot.reverb.link.valid=true;snapshot.local.local_active=true;web_control_store(&snapshot);
    call_reverb("101 1 9 32768 28672","1","202 Accepted","\"stage\":\"received\"");
    assert(strstr(captured.response,"\"fpga\":\"unverified\"") && !strstr(captured.response,"applied"));
    assert(publication_count==1 && published[0].kind==EVT_WEB_REVERB && last_job);
    assert(published[0].web_v2.a==32768 && published[0].web_v2.b==28672);
    assert(published[0].web_v2.token==101 && published[0].web_v2.context_epoch==9);
    for(unsigned i=0;i<100;i++)call_reverb("101 2 9 0 0","1","409 Conflict","reverb_busy");
    assert(publication_count==1 && web_control_reverb_matches(&published[0].web_v2));
    web_control_reverb_release(last_job);
    call_reverb("101 2 9 0 0","1","202 Accepted",NULL);
    assert(publication_count==2 && published[1].web_v2.c!=published[0].web_v2.c);
}
static void coherent_get(void)
{
    fixture_reverb();snapshot.reverb.applied=(web_effect_result_t){.token=101,.sequence=3,
        .context_epoch=9,.request_id=44,.applied_sequence=7};
    snapshot.reverb.result=snapshot.reverb.applied;web_control_store(&snapshot);
    captured=request("",NULL);assert(reverb_get(&captured)==ESP_OK);
    assert(!strcmp(captured.status,"200 OK") && strstr(captured.response,"\"wet_q15\":8192"));
    assert(strstr(captured.response,"\"damp_q15\":123") && strstr(captured.response,"\"request_id\":44"));
    assert(strstr(captured.response,"\"confirmation\":\"synth_parameters\""));
    assert(!publication_count && !s_sessions[0].sequence);
    snapshot.reverb.link.valid=false;web_control_store(&snapshot);
    captured=request("",NULL);assert(reverb_get(&captured)==ESP_OK);
    assert(strstr(captured.response,"\"wet_q15\":null,\"damp_q15\":null"));
    assert(strstr(captured.response,"\"applied\":null"));
}
int main(void)
{
    strict_ingress();availability_and_bounded_queue();coherent_get();
    web_control_reverb_release(last_job);
    puts("HTTP reverb: strict v2 ingress, unknown-capability rejection, 202-only bounded reservation and RAM readback PASS");
}
