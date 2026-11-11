// Production shared core + real HTTP adapter; only SDK/storage boundaries fake.
#define main previous_http_api_main
#include "test_http_api.c"
#undef main
typedef struct {
    const char *body,*control,*token,*sequence,*context;
    size_t content_len,received,chunk;bool receive_fails;char status[64],response[2048];
} raw_req_t;
static web_control_state_t snapshot;
#define raw_state http_raw_state
#define publications publication_count
static esp_err_t raw_dispatch_kind(raw_req_t *req,bool debug)
{
    web_control_store(&snapshot);httpd_req_t http=request(req->body,req->control);
    http.content_len=req->content_len;http.chunk=req->chunk;http.receive_fails=req->receive_fails;
    http.token_header=req->token;http.sequence_header=req->sequence;http.context_header=req->context;
    esp_err_t result=debug?midi_debug_get(&http):midi_raw_post(&http);
    assert(strlen(http.status)<sizeof(req->status) && http.response_len<sizeof(req->response));
    strcpy(req->status,http.status);strcpy(req->response,http.response);req->received=http.received;return result;
}
static esp_err_t raw_dispatch(raw_req_t *req){return raw_dispatch_kind(req,false);}
static esp_err_t raw_debug_dispatch(raw_req_t *req){return raw_dispatch_kind(req,true);}
static raw_req_t raw_request(const char *body)
{
    return (raw_req_t){.body=body,.content_len=strlen(body),.control="1",
        .token="77",.sequence="1",.context="7",.chunk=3};
}
static void reset(void)
{
    memset(s_sessions,0,sizeof(s_sessions));
    memset(&snapshot,0,sizeof(snapshot));
    memset(&raw_state,0,sizeof(raw_state));
    memset(&s_http_performance,0,sizeof(s_http_performance));
    snapshot.ready=true;snapshot.context_epoch=7;
    raw_state.available=raw_state.enabled=raw_state.ready=true;
    now=100;publications=0;
    s_sessions[2].token=77;s_sessions[2].context_epoch=7;
    s_sessions[2].api_version=FM_HTTP_API_MAJOR;s_sessions[2].seen=now;
    web_raw_queue_reset();control_source_init();
    s_sessions[2].peer=(control_api_peer_t){CONTROL_API_WIFI,0};
    assert(control_source_claim(CONTROL_SOURCE_WIFI,now,&s_sessions[2].source));
}
static void check(raw_req_t *req,const char *status,const char *code)
{
    unsigned before=publications;
    uint32_t sequence=s_sessions[2].sequence;
    int64_t seen=s_sessions[2].seen;
    assert(raw_dispatch(req)==ESP_OK);
    if(strcmp(req->status,status))fprintf(stderr,"expected %s got %s: %s\n",status,req->status,req->response);
    assert(!strcmp(req->status,status));
    if(code)assert(strstr(req->response,code));
    if(status[0]!='2') {
        assert(publications==before && s_sessions[2].sequence==sequence);
        assert(s_sessions[2].seen==seen);
    }
}
static const char *body="{\"source\":\"web\",\"bytes\":[144,60,100]}";

static void acceptance_and_full(void)
{
    reset();
    for(unsigned i=1;i<=4;i++) {
        char seq[12];snprintf(seq,sizeof(seq),"%u",i);
        raw_req_t req=raw_request(body);req.sequence=seq;
        check(&req,"202 Accepted",NULL);
        assert(!strcmp(req.response,"{\"accepted\":true,\"queued_bytes\":3}"));
        assert(s_sessions[2].sequence==i && publications==i);
        const evt_t *event=&published[i-1];
        assert(event->kind==EVT_WEB_RAW && event->web_v2.token==77);
        assert(event->web_v2.sequence==i && event->web_v2.context_epoch==7);
        assert(event->web_v2.client==2 && event->web_v2.expires==now+WEB_LEASE_US);
    }
    raw_req_t req=raw_request(body);req.sequence="5";now++;
    check(&req,"503 Service Unavailable","raw_queue_full");
    web_raw_job_t job;
    assert(web_raw_queue_take(published[0].web_v2.a,&job));
    assert(job.client==2 && job.request.token==77 && job.request.context_epoch==7);
    assert(job.request.sequence==1 && job.request.has_binding && job.request.length==3);
    const uint8_t expected[]={144,60,100};
    assert(!memcmp(job.request.bytes,expected,3) && job.expires==101+WEB_LEASE_US-1);
    req=raw_request(body);req.sequence="5";
    check(&req,"202 Accepted",NULL);
    assert(s_sessions[2].seen==now && s_sessions[2].sequence==5);

    reset();
    req=raw_request("{\"source\":\"web\",\"bytes\":[0,255],\"token\":77,\"sequence\":9,\"context_epoch\":7}");
    req.token=req.sequence=req.context=NULL;
    check(&req,"202 Accepted",NULL);
    assert(web_raw_queue_take(published[0].web_v2.a,&job));
    assert(job.request.sequence==9 && job.request.length==2 && job.request.bytes[1]==255);

    reset();
    s_sessions[2].token=s_sessions[2].context_epoch=UINT32_MAX;
    snapshot.context_epoch=UINT32_MAX;
    req=raw_request(body);req.token=req.sequence=req.context="4294967295";
    check(&req,"202 Accepted",NULL);
    assert(web_raw_queue_take(published[0].web_v2.a,&job));
    assert(job.request.token==UINT32_MAX && job.request.sequence==UINT32_MAX);
    assert(job.request.context_epoch==UINT32_MAX);
}

static void rejection(void)
{
    reset();raw_req_t req=raw_request(body);req.control=NULL;
    check(&req,"403 Forbidden","header_required");
    req=raw_request(body);req.control="01";check(&req,"403 Forbidden",NULL);
    req=raw_request("{}");check(&req,"400 Bad Request","invalid_raw_midi");
    req=raw_request(body);req.content_len=2049;check(&req,"400 Bad Request","body_length");
    req=raw_request("");check(&req,"400 Bad Request","body_length");
    const char embedded[]="{\"source\":\"web\",\"bytes\":[0]}\0x";
    req=raw_request(embedded);req.content_len=sizeof(embedded)-1;
    check(&req,"400 Bad Request","invalid_raw_midi");
    req=raw_request(body);req.receive_fails=true;
    assert(raw_dispatch(&req)==ESP_FAIL && !publications);
    const char *bad[]={NULL,"","0","01","-1","+1","1.0","1e0","true","1 ","4294967296","99999999999999999999"};
    for(unsigned field=0;field<3;field++)for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        req=raw_request(body);
        if(!field)req.token=bad[i];else if(field==1)req.sequence=bad[i];else req.context=bad[i];
        check(&req,"400 Bad Request","invalid_binding");
    }
    req=raw_request("{\"source\":\"web\",\"bytes\":[0],\"token\":77}");
    check(&req,"400 Bad Request","invalid_raw_midi");
    req=raw_request(body);req.token="78";check(&req,"410 Gone","unknown_session");
    s_sessions[2].api_version=1;req=raw_request(body);check(&req,"410 Gone","protocol_session");
    s_sessions[2].api_version=FM_HTTP_API_MAJOR;s_sessions[2].seen=now-10000000LL;
    req=raw_request(body);check(&req,"410 Gone","session_expired");
    s_sessions[2].seen=now;
    snapshot.context_epoch=8;req=raw_request(body);check(&req,"409 Conflict","stale_context");
    snapshot.context_epoch=7;s_sessions[2].context_epoch=8;
    req=raw_request(body);check(&req,"409 Conflict","stale_context");
    s_sessions[2].context_epoch=7;s_sessions[2].sequence=2;
    req=raw_request(body);check(&req,"409 Conflict","stale_sequence");
    s_sessions[2].sequence=0;
    bool *flags[]={&snapshot.ready,&raw_state.available,&raw_state.enabled,&raw_state.ready};
    for(unsigned i=0;i<4;i++) {
        *flags[i]=false;req=raw_request(body);check(&req,"409 Conflict","raw_not_ready");*flags[i]=true;
    }
    snapshot.mode_switching=true;req=raw_request(body);check(&req,"409 Conflict","raw_not_ready");
    snapshot.mode_switching=false;snapshot.mode_recovering=true;
    req=raw_request(body);check(&req,"409 Conflict","raw_not_ready");snapshot.mode_recovering=false;
    raw_state.source[MIDI_RAW_SOURCE_WEB].session=78;
    req=raw_request(body);check(&req,"409 Conflict","raw_source_busy");
    raw_state.source[MIDI_RAW_SOURCE_WEB].session=UINT64_C(1)<<32;
    req=raw_request(body);check(&req,"409 Conflict","raw_source_busy");
    raw_state.source[MIDI_RAW_SOURCE_WEB].session=77;
    req=raw_request(body);check(&req,"202 Accepted",NULL);
}

static void debug_snapshot(void)
{
    reset();
    raw_state.initialized=true;
    raw_state.source[MIDI_RAW_SOURCE_WEB]=(raw_midi_source_state_t){
        .session=77,.queued_bytes=6,.sent_bytes=123,.dropped_bytes=9,
        .flushes=4,.fault=RAW_MIDI_EXPIRED};
    raw_state.reset_count=2;
    unsigned before=publications;
    raw_req_t req=raw_request("");req.control=NULL;
    assert(raw_debug_dispatch(&req)==ESP_OK);
    assert(!strcmp(req.status,"200 OK"));
    assert(strstr(req.response,"\"version\":1"));
    assert(strstr(req.response,"\"ready\":true"));
    assert(strstr(req.response,"\"context_epoch\":7"));
    assert(strstr(req.response,"\"session\":77"));
    assert(strstr(req.response,"\"sent_bytes\":123"));
    assert(strstr(req.response,"\"dropped_bytes\":9"));
    assert(strstr(req.response,"\"pcm_capture\":false"));
    assert(publications==before && s_sessions[2].sequence==0);
    assert(raw_state.source[MIDI_RAW_SOURCE_WEB].queued_bytes==6);
    snapshot.mode_switching=true;req=raw_request("");
    assert(raw_debug_dispatch(&req)==ESP_OK && strstr(req.response,"\"ready\":false"));
    snapshot.mode_switching=false;raw_state.ready=false;req=raw_request("");
    assert(raw_debug_dispatch(&req)==ESP_OK && strstr(req.response,"\"ready\":false"));
    raw_state.source[MIDI_RAW_SOURCE_WEB].session=UINT64_MAX;req=raw_request("");
    assert(raw_debug_dispatch(&req)==ESP_OK);
    assert(strstr(req.response,"18446744073709551615"));
    assert(publications==before);
}

int main(void)
{
    acceptance_and_full();
    rejection();
    debug_snapshot();
    puts("raw HTTP: authorization, strict binding, context/sequence/lease fences, queue-only 202 and backpressure PASS");
    return 0;
}
