// The production raw handler is included directly; only HTTP SDK boundaries,
// session snapshots, and event delivery are simulated here.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "web_control_v2.h"
#include "raw_midi.h"
#include "web_raw_queue.h"

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
typedef struct {
    const char *body, *control, *token, *sequence, *context;
    size_t content_len, received, chunk;
    bool receive_fails;
    char status[64], response[1024];
} httpd_req_t;
static struct {
    uint32_t token, sequence, context_epoch;
    int64_t seen;
    unsigned api_version;
} s_sessions[WEB_CLIENTS];
static web_control_state_t snapshot;
static raw_midi_state_t raw_state;
static int64_t now;
static evt_t published[16];
static unsigned publications;

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req,const char *name,char *out,size_t capacity)
{
    const char *value=NULL;
    if (!strcmp(name,"X-FPGA-Control")) value=req->control;
    else if (!strcmp(name,"X-FPGA-Token")) value=req->token;
    else if (!strcmp(name,"X-FPGA-Sequence")) value=req->sequence;
    else if (!strcmp(name,"X-FPGA-Context")) value=req->context;
    else assert(!"unexpected header");
    if (!value || strlen(value)>=capacity) return ESP_FAIL;
    strcpy(out,value);
    return ESP_OK;
}
int httpd_req_recv(httpd_req_t *req,char *out,size_t length)
{
    if(req->receive_fails)return ESP_FAIL;
    if(length>req->chunk)length=req->chunk;
    assert(length && length<=req->content_len-req->received);
    memcpy(out,req->body+req->received,length);
    req->received+=length;
    return (int)length;
}
bool same_origin_command(httpd_req_t *req)
{
    char value[4];
    return httpd_req_get_hdr_value_str(req,"X-FPGA-Control",value,sizeof(value))==ESP_OK && !strcmp(value,"1");
}
esp_err_t reply(httpd_req_t *req,const char *status,const char *body)
{
    assert(strlen(body)<sizeof(req->response));
    strcpy(req->status,status);strcpy(req->response,body);return ESP_OK;
}
esp_err_t v2_error(httpd_req_t *req,const char *status,const char *code)
{
    char body[128];snprintf(body,sizeof(body),"{\"code\":\"%s\"}",code);
    return reply(req,status,body);
}
const web_control_state_t *http_snapshot(void) { return &snapshot; }
uint32_t web_control_context_epoch(const web_control_state_t *state)
{ return state->context_epoch?state->context_epoch:1; }
int64_t esp_timer_get_time(void) { return now; }
void raw_midi_state(raw_midi_state_t *out) { *out=raw_state; }
void event_bus_publish(const evt_t *event)
{
    assert(publications<sizeof(published)/sizeof(published[0]));
    published[publications++]=*event;
}
#include "http_performance.inc.h"
#include "http_raw_midi.inc.h"

static httpd_req_t request(const char *body)
{
    return (httpd_req_t){.body=body,.content_len=strlen(body),.control="1",
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
    web_raw_queue_reset();
}
static void check(httpd_req_t *req,const char *status,const char *code)
{
    unsigned before=publications;
    uint32_t sequence=s_sessions[2].sequence;
    int64_t seen=s_sessions[2].seen;
    assert(midi_raw_post(req)==ESP_OK);
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
        httpd_req_t req=request(body);req.sequence=seq;
        check(&req,"202 Accepted",NULL);
        assert(!strcmp(req.response,"{\"accepted\":true,\"queued_bytes\":3}"));
        assert(s_sessions[2].sequence==i && publications==i);
        const evt_t *event=&published[i-1];
        assert(event->kind==EVT_WEB_RAW && event->web_v2.token==77);
        assert(event->web_v2.sequence==i && event->web_v2.context_epoch==7);
        assert(event->web_v2.client==2 && event->web_v2.expires==now+WEB_LEASE_US);
    }
    httpd_req_t req=request(body);req.sequence="5";now++;
    check(&req,"503 Service Unavailable","raw_queue_full");
    web_raw_job_t job;
    assert(web_raw_queue_take(published[0].web_v2.a,&job));
    assert(job.client==2 && job.request.token==77 && job.request.context_epoch==7);
    assert(job.request.sequence==1 && job.request.has_binding && job.request.length==3);
    const uint8_t expected[]={144,60,100};
    assert(!memcmp(job.request.bytes,expected,3) && job.expires==101+WEB_LEASE_US-1);
    req=request(body);req.sequence="5";
    check(&req,"202 Accepted",NULL);
    assert(s_sessions[2].seen==now && s_sessions[2].sequence==5);

    reset();
    req=request("{\"source\":\"web\",\"bytes\":[0,255],\"token\":77,\"sequence\":9,\"context_epoch\":7}");
    req.token=req.sequence=req.context=NULL;
    check(&req,"202 Accepted",NULL);
    assert(web_raw_queue_take(published[0].web_v2.a,&job));
    assert(job.request.sequence==9 && job.request.length==2 && job.request.bytes[1]==255);

    reset();
    s_sessions[2].token=s_sessions[2].context_epoch=UINT32_MAX;
    snapshot.context_epoch=UINT32_MAX;
    req=request(body);req.token=req.sequence=req.context="4294967295";
    check(&req,"202 Accepted",NULL);
    assert(web_raw_queue_take(published[0].web_v2.a,&job));
    assert(job.request.token==UINT32_MAX && job.request.sequence==UINT32_MAX);
    assert(job.request.context_epoch==UINT32_MAX);
}

static void rejection(void)
{
    reset();httpd_req_t req=request(body);req.control=NULL;
    check(&req,"403 Forbidden","header_required");
    req=request(body);req.control="01";check(&req,"403 Forbidden",NULL);
    req=request("{}");check(&req,"400 Bad Request","invalid_raw_midi");
    req=request(body);req.content_len=2049;check(&req,"400 Bad Request","body_length");
    req=request("");check(&req,"400 Bad Request","body_length");
    const char embedded[]="{\"source\":\"web\",\"bytes\":[0]}\0x";
    req=request(embedded);req.content_len=sizeof(embedded)-1;
    check(&req,"400 Bad Request","invalid_raw_midi");
    req=request(body);req.receive_fails=true;
    assert(midi_raw_post(&req)==ESP_FAIL && !publications);
    const char *bad[]={NULL,"","0","01","-1","+1","1.0","1e0","true","1 ","4294967296","99999999999999999999"};
    for(unsigned field=0;field<3;field++)for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        req=request(body);
        if(!field)req.token=bad[i];else if(field==1)req.sequence=bad[i];else req.context=bad[i];
        check(&req,"400 Bad Request","invalid_binding");
    }
    req=request("{\"source\":\"web\",\"bytes\":[0],\"token\":77}");
    check(&req,"400 Bad Request","invalid_raw_midi");
    req=request(body);req.token="78";check(&req,"410 Gone","unknown_session");
    s_sessions[2].api_version=1;req=request(body);check(&req,"410 Gone","protocol_session");
    s_sessions[2].api_version=FM_HTTP_API_MAJOR;s_sessions[2].seen=now-10000000LL;
    req=request(body);check(&req,"410 Gone","session_expired");
    s_sessions[2].seen=now;
    snapshot.context_epoch=8;req=request(body);check(&req,"409 Conflict","stale_context");
    snapshot.context_epoch=7;s_sessions[2].context_epoch=8;
    req=request(body);check(&req,"409 Conflict","stale_context");
    s_sessions[2].context_epoch=7;s_sessions[2].sequence=2;
    req=request(body);check(&req,"409 Conflict","stale_sequence");
    s_sessions[2].sequence=0;
    bool *flags[]={&snapshot.ready,&raw_state.available,&raw_state.enabled,&raw_state.ready};
    for(unsigned i=0;i<4;i++) {
        *flags[i]=false;req=request(body);check(&req,"409 Conflict","raw_not_ready");*flags[i]=true;
    }
    snapshot.mode_switching=true;req=request(body);check(&req,"409 Conflict","raw_not_ready");
    snapshot.mode_switching=false;snapshot.mode_recovering=true;
    req=request(body);check(&req,"409 Conflict","raw_not_ready");snapshot.mode_recovering=false;
    raw_state.source[MIDI_RAW_SOURCE_WEB].session=78;
    req=request(body);check(&req,"409 Conflict","raw_source_busy");
    raw_state.source[MIDI_RAW_SOURCE_WEB].session=UINT64_C(1)<<32;
    req=request(body);check(&req,"409 Conflict","raw_source_busy");
    raw_state.source[MIDI_RAW_SOURCE_WEB].session=77;
    req=request(body);check(&req,"202 Accepted",NULL);
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
    httpd_req_t req=request("");req.control=NULL;
    assert(midi_debug_get(&req)==ESP_OK);
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
    snapshot.mode_switching=true;req=request("");
    assert(midi_debug_get(&req)==ESP_OK && strstr(req.response,"\"ready\":false"));
    snapshot.mode_switching=false;raw_state.ready=false;req=request("");
    assert(midi_debug_get(&req)==ESP_OK && strstr(req.response,"\"ready\":false"));
    raw_state.source[MIDI_RAW_SOURCE_WEB].session=UINT64_MAX;req=request("");
    assert(midi_debug_get(&req)==ESP_OK);
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
