// Actual v2/snapshot/raw handlers with their shared HTTP sessions and FIFO.
#define HTTP_V2_TEST_MAIN old_http_v2_entry
#include "test_http_v2.c"
#undef HTTP_V2_TEST_MAIN
#include "web_raw_queue.h"

static void raw_request(uint32_t token,uint32_t sequence,const char *expected)
{
    char body[180];
    snprintf(body,sizeof(body),"{\"source\":\"web\",\"bytes\":[224,0,64],\"token\":%u,\"sequence\":%u,\"context_epoch\":9}",token,sequence);
    httpd_req_t req=request(body,"1");
    assert(midi_raw_post(&req)==ESP_OK);
    if(strcmp(req.status,expected))fprintf(stderr,"raw expected %s got %s: %s\n",expected,req.status,req.response);
    assert(!strcmp(req.status,expected));
    if(expected[0]=='2')assert(!strcmp(req.response,"{\"accepted\":true,\"queued_bytes\":3}"));
}
int main(void)
{
    now=100;publication_count=0;web_raw_queue_reset();
    control_source_init();memset(s_sessions,0,sizeof(s_sessions));memset(&s_http_performance,0,sizeof(s_http_performance));
    web_control_state_t snapshot={.ready=true,.context_epoch=9};
    patch_default(&snapshot.patch);snapshot.engine.available=snapshot.engine.valid=true;
    web_control_store(&snapshot);
    http_raw_state=(raw_midi_state_t){.available=true,.initialized=true,.enabled=true,.ready=true};
    for(unsigned i=0;i<2;i++) {
        s_sessions[i].token=101+101*i;s_sessions[i].context_epoch=9;
        s_sessions[i].api_version=FM_HTTP_API_MAJOR;s_sessions[i].seen=now;
    s_sessions[i].peer=(control_api_peer_t){CONTROL_API_WIFI,0};
    assert(control_source_claim(CONTROL_SOURCE_WIFI,now,&s_sessions[i].source));
    }
    expect(v2_command,"101 1 9 3 1 100 0","1","202 Accepted");
    raw_request(101,2,"202 Accepted");
    assert(publication_count==2&&published[0].kind==EVT_WEB_V2&&published[1].kind==EVT_WEB_RAW);
    assert(published[0].web_v2.sequence==1&&published[1].web_v2.sequence==2);
    web_raw_job_t job;assert(web_raw_queue_take(published[1].web_v2.a,&job));
    assert(job.request.token==101&&job.request.sequence==2&&job.request.length==3);
    assert(job.request.bytes[0]==0xe0&&job.request.bytes[1]==0&&job.request.bytes[2]==64);
    // No owner task ran yet: the HTTP reservation must still reject a second
    // token rather than accept two independent Running Status/SysEx streams.
    raw_request(202,1,"409 Conflict");
    expect(v2_command,"202 1 9 3 2 100 0","1","409 Conflict");
    expect(v2_command,"202 1 9 2 7 64 0","1","202 Accepted");
    expect(v2_command,"202 2 9 3 0 100 0","1","202 Accepted");
    raw_request(202,3,"409 Conflict");
    expect(v2_command,"101 3 9 3 0 100 0","1","202 Accepted");
    raw_request(101,4,"202 Accepted"); // release bitmap followed by bend center
    assert(published[publication_count-2].kind==EVT_WEB_V2&&published[publication_count-1].kind==EVT_WEB_RAW);
    expect(v2_command,"202 3 9 4 0 0 0","1","202 Accepted");
    raw_request(202,4,"409 Conflict"); // queued Panic has not changed owner context yet
    expect(v2_command,"101 4 9 3 1 100 0","1","409 Conflict"); // shared sequence
    // A stale owner observation also blocks a takeover until the driver has
    // actually invalidated it; an HTTP-side timeout is not an FPGA flush ACK.
    now+=WEB_LEASE_US+1;http_raw_state.source[1].session=101;
    raw_request(202,4,"409 Conflict");
    http_raw_state.source[1].session=0;
    raw_request(202,4,"202 Accepted");
    http_raw_state.ready=false;raw_request(202,5,"409 Conflict");
    assert(s_sessions[1].sequence==4);
    puts("HTTP mixed ingress: shared sequence/FIFO, single Web token before owner claim, unrelated empty/STOP isolation, lease and actual driver readiness PASS");
    return 0;
}
