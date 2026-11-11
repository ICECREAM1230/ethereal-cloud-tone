// Share the SDK boundary and production-handler inclusion with the v1 test.
#define main http_v1_test_entry
#include "test_http_api.c"
#undef main

esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *bytes, int length)
{
    size_t n = length == HTTPD_RESP_USE_STRLEN ? strlen(bytes) : (size_t)length;
    assert(r->response_len + n < sizeof(r->response));
    if (n) memcpy(r->response + r->response_len, bytes, n);
    r->response_len += n; r->response[r->response_len] = 0;
    return ESP_OK;
}
size_t httpd_req_get_url_query_len(httpd_req_t *r)
{ return r->query ? strlen(r->query) : 0; }
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *out, size_t size)
{
    if (!r->query || strlen(r->query) >= size) return ESP_FAIL;
    strcpy(out, r->query); return ESP_OK;
}
static void write_response(const char *path, const httpd_req_t *r)
{
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(r->response, 1, r->response_len, f) == r->response_len);
    assert(!fclose(f));
}
int main(int argc, char **argv)
{
    assert(argc == 9); // Six unavailable fixtures and two observed-FM serialization fixtures.
    web_control_state_t snapshot = {.ready=true, .revision=1, .context_epoch=9, .ks_decay_ms=3000};
    patch_default(&snapshot.patch); snapshot.engine.available=snapshot.engine.valid=true;
    web_control_store(&snapshot);
    httpd_req_t caps = request("", NULL);
    assert(v2_capabilities(&caps) == ESP_OK);
    assert(strstr(caps.response, "\"available\":false"));
    assert(strstr(caps.response, FM_SCHEMA_SHA256)); write_response(argv[1], &caps);
    expect(v2_session, "", NULL, "403 Forbidden");
    expect(v2_session, "x", "1", "400 Bad Request");
    httpd_req_t s = request("", "1");
    assert(v2_session(&s) == ESP_OK && !strcmp(s.status, "201 Created"));
    assert(strstr(s.response, "\"context_epoch\":9"));
    expect(v2_command, "1001 1 9 3 1 100 0", "1", "202 Accepted");
    assert(published[publication_count-1].kind == EVT_WEB_V2);
    assert(published[publication_count-1].web_v2.context_epoch == 9);
    expect(v2_command, "1001 1 9 3 0 100 0", "1", "409 Conflict");
    expect(v2_command, "1001 2 8 3 0 100 0", "1", "409 Conflict");
    expect(v2_command, "1001 2 9 5 2 1 0", "1", "503 Service Unavailable");
    expect(v2_command, "1001 2 9 7 18 4294901760 31", "1", "503 Service Unavailable");
    expect(v2_command, "1001 2 9 8 0 0 0", "1", "503 Service Unavailable");
    assert(s_sessions[0].sequence == 1); // Rejected FM request did not consume seq.
    expect(command, "1001 2 3 0 100", "1", "410 Gone"); // No cross-version use.
    httpd_req_t v1 = request("", "1");
    assert(session(&v1) == ESP_OK && !strcmp(v1.status, "201 Created"));
    expect(v2_command, "1002 1 9 3 1 100 0", "1", "410 Gone");
    httpd_req_t cat = request("", NULL);
    cat.query = "profile=1&section=parameters&offset=0&limit=8";
    assert(v2_catalog(&cat) == ESP_OK && !strcmp(cat.status, "200 OK"));
    assert(strstr(cat.response, "attack_rate")); write_response(argv[3], &cat);
    cat = request("", NULL); cat.query="profile=1&section=parameters&offset=8&limit=8";
    assert(v2_catalog(&cat) == ESP_OK && strstr(cat.response, "am_enable"));
    write_response(argv[4], &cat);
    cat = request("", NULL); cat.query="profile=1&section=profile";
    assert(v2_catalog(&cat) == ESP_OK && strstr(cat.response, "operators_per_voice"));
    write_response(argv[5], &cat);
    cat = request("", NULL); cat.query="profile=1&section=presets&offset=0&limit=8";
    assert(v2_catalog(&cat) == ESP_OK && strstr(cat.response, "four_op_bell"));
    write_response(argv[6], &cat);
    cat = request("", NULL); cat.query="profile=1&section=parameters&offset=0&limit=9";
    assert(v2_catalog(&cat) == ESP_OK && !strcmp(cat.status, "400 Bad Request"));
    cat = request("", NULL); cat.query="profile=1&profile=2";
    assert(v2_catalog(&cat) == ESP_OK && !strcmp(cat.status, "400 Bad Request"));
    httpd_req_t state_r = request("", NULL);
    assert(v2_state(&state_r) == ESP_OK && strstr(state_r.response, "\"legacy\":"));
    write_response(argv[2], &state_r);
    snapshot.context_epoch=10; web_control_store(&snapshot);
    expect(v2_command, "1001 2 9 3 1 100 0", "1", "409 Conflict");
    s=request("", "1"); assert(v2_session(&s) == ESP_OK);
    assert(s_sessions[0].token != 1001 && s_sessions[0].context_epoch == 10);
    snapshot.engine.switching=true;web_control_store(&snapshot);
    char input[96];snprintf(input,sizeof(input),"%" PRIu32 " 1 10 3 1 100 0",s_sessions[0].token);
    expect(v2_command,input,"1","503 Service Unavailable");
    assert(!s_sessions[0].sequence);
    httpd_req_t escaped=request("",NULL);
    assert(json_string(&escaped,"M1 \"quote\"\n\001") == ESP_OK);
    assert(!strcmp(escaped.response,"\"M1 \\\"quote\\\"\\u000a\\u0001\""));
    snapshot.engine.switching=false;
    snapshot.fm.status=(engine_fm_status_t){.available=true,.valid=true,.ready=true,.selected=true,
        .backend_id=1,.profile_id=1,.profile_version=65536,.voice_count=8,.session_id=42,.engine_epoch=4,
        .topology_generation=1,.phase=2,.flags=3,.features=1023,.requested_backend=1,.applied_backend=1,
        .accepted={42,4,7},.completed={42,4,6},.result={{42,4,6},0}};
    snapshot.requested_engine=snapshot.applied_engine=2;
    snapshot.fm.source_numerator=3579545;snapshot.fm.source_denominator=64;
    snapshot.fm.output_numerator=96000;snapshot.fm.output_denominator=1;
    snapshot.fm.parameter_count=1;snapshot.fm.parameters[0]=(engine_fm_parameter_t){18,0xffff0000,31,6};
    for(unsigned j=0;j<8;j++)snapshot.fm.operators_per_voice[j]=4;
    web_control_store(&snapshot);
    httpd_req_t live=request("",NULL);assert(v2_capabilities(&live)==ESP_OK);
    assert(strstr(live.response,"\"id\":2,\"available\":true"));
    write_response(argv[7],&live);
    live=request("",NULL);assert(v2_state(&live)==ESP_OK);
    assert(strstr(live.response,"\"session_id\":42,\"engine_epoch\":4,\"sequence_id\":7"));
    assert(strstr(live.response,"\"parameter_id\":18,\"target_word\":4294901760,\"value_u32\":31,\"sequence_id\":6"));
    write_response(argv[8],&live);
    snprintf(input,sizeof(input),"%" PRIu32 " 1 10 7 18 4294901760 31",s_sessions[0].token);
    expect(v2_command,input,"1","202 Accepted");
    snprintf(input,sizeof(input),"%" PRIu32 " 2 10 7 18 4294901760 32",s_sessions[0].token);
    expect(v2_command,input,"1","400 Bad Request");
    assert(s_sessions[0].sequence==1);
    expect(command,"1002 1 3 1 100","1","503 Service Unavailable");
    puts("HTTP v2: actual handlers, isolated sessions, epoch fencing, no unavailable FM writes, generated catalog pagination and state PASS");
    return 0;
}
