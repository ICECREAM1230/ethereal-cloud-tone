#define HTTP_V2_TEST_MAIN existing_v2_main
#include "test_http_v2.c"
#include "fm_diagnostics.h"
static engine_fm_diagnostics_t diagnostic;
static unsigned requested;
uint32_t engine_link_fm_diagnostics_request(void) { requested++;return 7; }
void engine_link_fm_diagnostics_state(engine_fm_diagnostics_t *out) { *out=diagnostic; }
int main(int argc,char **argv)
{
    (void)argv;
    assert(argc==2);
    // GET may queue a diagnostic refresh only. Existing sessions/leases/events
    // must survive untouched; no SPI dependency is linked into this HTTP test.
    s_sessions[0].token=99;s_sessions[0].sequence=38;s_sessions[0].seen=123;
    s_http_performance.token=99;s_http_performance.context=6;s_http_performance.expires=777;
    unsigned published_before=publication_count;
    diagnostic=(engine_fm_diagnostics_t){.supported=true,.pending=true,.valid=true,
        .request_id=8,.sample_request_id=7,.requested_at_us=200,.started_at_us=50,
        .completed_at_us=80,.raw_cached_at_us=79,.raw_cache_valid=true};
    diagnostic.blocks[0]=(engine_fm_diagnostic_block_t){.address=0x7f0,.count=11,.read_ok=true,
        .started_at_us=50,.completed_at_us=55,.words={0x464d5631,0x10000,0x4000,99,123,17,16,999,12345,88,5}};
    diagnostic.blocks[1]=(engine_fm_diagnostic_block_t){.address=0x670,.count=3,.read_ok=true,
        .started_at_us=57,.completed_at_us=60,.words={0x80000004,0x80000003,FM_RESULT_CANCELLED}};
    diagnostic.blocks[2]=(engine_fm_diagnostic_block_t){.address=0x700,.count=7,.read_ok=true,
        .started_at_us=62,.completed_at_us=65,.words={MIDI_RAW_ABI_MAGIC,0,2,91,7,88,3}};
    diagnostic.blocks[3]=(engine_fm_diagnostic_block_t){.address=0x7f0,.count=2,.read_ok=true,
        .started_at_us=70,.completed_at_us=75,.words={0x464d5631,0x10000}};
    diagnostic.raw_cache=(raw_midi_state_t){.available=true,.initialized=true,.enabled=true,
        .ready=true,.inflight=true,.accepted=91,.rejected=7,.events=88,.usb_status=3};
    diagnostic.raw_cache.source[1]=(raw_midi_source_state_t){.session=UINT64_C(0x123456789abcdef0),
        .queued_bytes=6,.sent_bytes=33,.dropped_bytes=2,.flushes=8,.fault=5,.flushing=true};
    httpd_req_t req=request("",NULL);assert(v2_fm_diagnostics(&req)==ESP_OK);
#if CONFIG_FPGA_FM_ONLY
    assert(!strcmp(req.status,"200 OK") && requested==1);
    assert(strstr(req.response,"\"non_atomic\":true"));
    assert(strstr(req.response,"\"expression_applied_data\":16384"));
    assert(strstr(req.response,"\"historical_peak\":12345"));
    assert(strstr(req.response,"\"queued_bytes\":6"));
    assert(strstr(req.response,"\"session\":1311768467463790320"));
    assert(strstr(req.response,"\"lease_expires_at_us\":null"));
    assert(strstr(req.response,"\"age_us\":20"));
    write_response(argv[1],&req);
    diagnostic.valid=false;diagnostic.raw_cache_valid=false;
    memset(diagnostic.blocks,0,sizeof(diagnostic.blocks));
    httpd_req_t missing=request("",NULL);assert(v2_fm_diagnostics(&missing)==ESP_OK);
    assert(strstr(missing.response,"\"expression_applied_data\":null"));
    assert(strstr(missing.response,"\"raw_cache\":null"));
    fail_chunk_call=chunk_calls+1;httpd_req_t failed=request("",NULL);
    assert(v2_fm_diagnostics(&failed)==ESP_FAIL);
#else
    assert(!strcmp(req.status,"404 Not Found") && !requested);
#endif
    assert(publication_count==published_before && s_sessions[0].token==99 &&
        s_sessions[0].sequence==38 && s_sessions[0].seen==123 && s_http_performance.expires==777);
    puts("diagnostics HTTP: real cache serialization, no sessions/leases/events/SPI, streaming failure bounded PASS");
}
