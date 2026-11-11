#define HTTP_CUSTOM_FDC_STATE
#define HTTP_SHARED_QUERY_IMPL
#define main previous_http_main
#include "../test_http_api.c"
#undef main
size_t httpd_req_get_url_query_len(httpd_req_t *r){return r->query?strlen(r->query):0;}
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r,char *out,size_t size)
{if(!r->query || strlen(r->query)>=size)return ESP_FAIL;strcpy(out,r->query);return ESP_OK;}
static web_control_state_t snapshot;
static web_fdc_state_t fs;
void engine_link_fdc_state(engine_fdc_state_t *out){*out=fs.link;}
static uint32_t last_id;
static void fixture(void)
{
    fdc_control_release(last_id);now=100;publication_count=0;
    control_source_init();memset(s_sessions,0,sizeof s_sessions);memset(&snapshot,0,sizeof snapshot);
    snapshot.ready=true;snapshot.context_epoch=9;
    snapshot.cpu_mode=(engine_cpu_mode_state_t){.supported=true,.valid=true,.ready=true};web_control_store(&snapshot);
    fs=(web_fdc_state_t){.link={.available=true,.valid=true,.fresh=true,.word=FDC_STATUS_TAG|3u,.observed_us=now}};
    fdc_control_state_publish(&fs);
    s_sessions[0].token=101;s_sessions[0].context_epoch=9;s_sessions[0].api_version=2;s_sessions[0].seen=now;
    s_sessions[0].peer=(control_api_peer_t){CONTROL_API_WIFI,0};
    assert(control_source_claim(CONTROL_SOURCE_WIFI,now,&s_sessions[0].source));
}
static httpd_req_t call(const char *input,const char *header,const char *status_code,const char *code)
{
    unsigned before=publication_count;uint32_t sequence=s_sessions[0].sequence;
    httpd_req_t r=request(input,header);assert(fdc_post(&r)==ESP_OK);
    if(strcmp(r.status,status_code))fprintf(stderr,"input %s expected %s got %s: %s\n",input,status_code,r.status,r.response);
    assert(!strcmp(r.status,status_code));if(code)assert(strstr(r.response,code));
    if(status_code[0]!='2')assert(publication_count==before && sequence==s_sessions[0].sequence);
    else last_id=published[publication_count-1].web_v2.c;
    return r;
}
static void save_fixture(const char *name,httpd_req_t *r)
{
    const char *directory=getenv("FDC_FIXTURE_DIR");if(!directory)return;
    char path[1024];assert(snprintf(path,sizeof path,"%s/%s.json",directory,name)>0);
    FILE *f=fopen(path,"w");assert(f);fputs(r->response,f);fputc('\n',f);fclose(f);
}
int main(void)
{
    fixture();call("101 1 9 1 0 0 0 0 0 0",NULL,"403 Forbidden","header_required");
    const char *bad[]={"","101 1 9 1 0 0 0 0 0","101 1 9 1 0 0 0 0 0 0\n","101  1 9 1 0 0 0 0 0 0",
        "101 1 9 4 0 0 0 0 0 0","101 1 9 1 1 0 0 0 0 0","0 1 9 0 0 0 0 0 0 0",
        "101 1 9 3 1000 2000 1000 2000 2147483648 0","101 1 9 3 1000 2000 1000 2000 2139095040 0",
        "101 1 9 3 1000 1001 1000 2000 0 0","101 1 9 3 0 2000 1000 2000 0 0",
        "101 1 9 3 1000 67108864 1000 2000 0 0","4294967296 1 9 0 0 0 0 0 0 0"};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++)call(bad[i],"1","400 Bad Request","invalid_fdc");
    call("999 1 9 1 0 0 0 0 0 0","1","410 Gone","unknown_session");
    call("101 1 8 1 0 0 0 0 0 0","1","409 Conflict","stale_context");
    fs.link.available=false;fdc_control_state_publish(&fs);
    call("101 1 9 1 0 0 0 0 0 0","1","503 Service Unavailable","fdc_unavailable");
    fixture();snapshot.cpu_mode.applied=snapshot.cpu_mode.desired=2;snapshot.cpu_mode.valid=false;snapshot.ready=false;web_control_store(&snapshot);
    fs.link.word&=~2u;fdc_control_state_publish(&fs);
    call("101 1 9 1 0 0 0 0 0 0","1","409 Conflict","fdc_sub_not_ready");
    fs.link.word|=0x30;fdc_control_state_publish(&fs);
    httpd_req_t receipt=call("101 1 9 0 0 0 0 0 0 0","1","202 Accepted","\"stage\":\"queued\"");
    assert(!strstr(receipt.response,"native") && !strstr(receipt.response,"applied"));save_fixture("queued_receipt",&receipt);
    call("101 2 9 0 0 0 0 0 0 0","1","409 Conflict","fdc_busy");
    fdc_request_t claimed;assert(fdc_control_claim(&published[0].web_v2,&claimed));assert(claimed.op==FDC_DISABLE);
    fixture();snapshot.cpu_mode.supported=false;snapshot.ready=false;web_control_store(&snapshot);
    call("101 1 9 1 0 0 0 0 0 0","1","202 Accepted",NULL);
    fixture();fs.link.word|=0x20;fdc_control_state_publish(&fs);
    call("101 1 9 2 0 0 0 0 0 0","1","202 Accepted",NULL);
    fixture();receipt=call("101 1 9 3 1000 2000 3000 2000 0 0","1","202 Accepted",NULL);
    assert(fdc_control_claim(&published[0].web_v2,&claimed) && claimed.words[2]==3000 && claimed.words[3]==2000);
    fdc_control_release(last_id);now+=FDC_FRESH_US+1;
    call("101 2 9 1 0 0 0 0 0 0","1","503 Service Unavailable","fdc_unavailable");
    fixture();fs.result=(fdc_result_t){.token=101,.sequence=1,.context_epoch=9,.request_id=9,.op=1,.code=FDC_SENT};
    fdc_control_state_publish(&fs);httpd_req_t get=request("",NULL);assert(fdc_get(&get)==ESP_OK);
    assert(strstr(get.response,"\"driver_enabled\":false") && strstr(get.response,"\"stage\":\"transport_sent\""));
    assert(!strstr(get.response,"adopted") && !strstr(get.response,"native_ack"));save_fixture("sent_observed_off",&get);
    fs.link.word|=0x8c;fdc_control_state_publish(&fs);get=request("",NULL);assert(fdc_get(&get)==ESP_OK);save_fixture("observed_enabled",&get);
    fs.link.valid=fs.link.available=fs.link.fresh=false;fdc_control_state_publish(&fs);get=request("",NULL);assert(fdc_get(&get)==ESP_OK);save_fixture("unavailable",&get);
    puts("PASS actual FDC HTTP: strict bounded10-word parser, IEEE sigma/span, sessions/context, mode/cap/fresh guards, disable priority, queued-only receipt and observed-only GET");
}
