#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "control_api.c"
#include "wifi_web.c"
static engine_audio_routes_state_t http_audio_state;
void engine_link_audio_routes_state(engine_audio_routes_state_t *out) { *out=http_audio_state; }
bool engine_link_audio_routes_request(uint32_t hp,uint32_t a) {
    if(hp>3 || a>3 || !http_audio_state.available || !http_audio_state.valid || !http_audio_state.ready || http_audio_state.pending)return false;
    http_audio_state.pending=true;return true;
}
static control_source_ref_t http_audio_source;
static uint32_t http_audio_ticket;
bool engine_link_audio_routes_request_source(control_source_ref_t source,uint32_t hp,uint32_t a)
{
    if(!http_audio_state.pending && http_audio_ticket) {
        control_source_complete(http_audio_source,0,http_audio_ticket,CONTROL_SOURCE_OUTPUT,true);http_audio_ticket=0;
    }
    int64_t time=esp_timer_get_time();uint32_t ticket;
    if(!control_source_reserve_job(source,time,time+WEB_LEASE_US,&ticket))return false;
    bool accepted=engine_link_audio_routes_request(hp,a);
    if(!accepted)control_source_complete(source,0,ticket,CONTROL_SOURCE_OUTPUT,false);
    else {http_audio_source=source;http_audio_ticket=ticket;}
    return accepted;
}
#ifndef HTTP_CUSTOM_FDC_STATE
static engine_fdc_state_t http_fdc_link;
void engine_link_fdc_state(engine_fdc_state_t *out){*out=http_fdc_link;}
#endif
#if CONFIG_FPGA_FM_ONLY && !defined(HTTP_CUSTOM_DIAGNOSTICS)
uint32_t engine_link_fm_diagnostics_request(void){assert(!"unexpected diagnostics boundary");return 0;}
void engine_link_fm_diagnostics_state(engine_fm_diagnostics_t *out)
{assert(!"unexpected diagnostics boundary");*out=(engine_fm_diagnostics_t){0};}
#endif
static raw_midi_state_t http_raw_state={.available=true,.initialized=true,.enabled=true,.ready=true};
void raw_midi_state(raw_midi_state_t *out) { *out=http_raw_state; }

static int64_t now = 100;
static evt_t published[128];
static unsigned publication_count, random_call;

int64_t esp_timer_get_time(void) { return now; }
uint32_t esp_random(void)
{
    unsigned n = random_call++;
    if (!n) return 0;
    if (n <= 2) return 1001; // Also exercise collision rejection.
    return 999 + n;
}
void event_bus_publish(const evt_t *event)
{
    assert(publication_count < sizeof(published) / sizeof(published[0]));
    published[publication_count++] = *event;
}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *key, char *out, size_t size)
{
    const char *value=!strcmp(key,"Accept-Encoding")?r->accept_encoding:
        !strcmp(key,"X-FPGA-Control")?r->control_header:!strcmp(key,"X-FPGA-Token")?r->token_header:
        !strcmp(key,"X-FPGA-Sequence")?r->sequence_header:!strcmp(key,"X-FPGA-Context")?r->context_header:NULL;
    if (!value || strlen(value) >= size) return ESP_FAIL;
    strcpy(out, value);
    return ESP_OK;
}
int httpd_req_recv(httpd_req_t *r, char *out, size_t length)
{
    if (r->receive_fails) return -1;
    if (length > r->chunk) length = r->chunk;
    assert(r->received + length <= r->content_len);
    memcpy(out, r->input + r->received, length);
    r->received += length;
    return (int)length;
}
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *value)
{ assert(strlen(value) < sizeof(r->status)); strcpy(r->status, value); return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *value)
{ assert(strlen(value) < sizeof(r->type)); strcpy(r->type, value); return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *key, const char *value)
{
    char *target = !strcmp(key, "Cache-Control") ? r->cache
        : !strcmp(key, "Content-Security-Policy") ? r->csp
        : !strcmp(key, "X-Content-Type-Options") ? r->nosniff : NULL;
    if(!strcmp(key,"Content-Encoding"))target=r->encoding;
    if(!strcmp(key,"Vary"))target=r->vary;
    assert(target && "Unexpected response header (including CORS)");
    strcpy(target, value);
    return ESP_OK;
}
esp_err_t httpd_resp_send(httpd_req_t *r, const char *body, int length)
{
    size_t n = length == HTTPD_RESP_USE_STRLEN ? strlen(body) : (size_t)length;
    if(r->send_limit && n>r->send_limit)return ESP_FAIL;
    assert(n < sizeof(r->response));
    memcpy(r->response, body, n); r->response[n] = 0; r->response_len = n;
    return ESP_OK;
}

#ifndef HTTP_SHARED_CHUNK_IMPL
esp_err_t httpd_resp_send_chunk(httpd_req_t *r,const char *body,int length)
{
    if(!body || !length)return ESP_OK;
    size_t n=(size_t)length;
    if(r->send_limit && r->response_len+n>r->send_limit)return ESP_FAIL;
    assert(r->response_len+n<sizeof(r->response));
    memcpy(r->response+r->response_len,body,n);r->response_len+=n;
    r->response[r->response_len]=0;return ESP_OK;
}
#endif
#ifndef HTTP_SHARED_QUERY_IMPL
size_t httpd_req_get_url_query_len(httpd_req_t *r){return r->query?strlen(r->query):0;}
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r,char *out,size_t size)
{if(!r->query || strlen(r->query)>=size)return ESP_FAIL;strcpy(out,r->query);return ESP_OK;}
#endif
static httpd_req_t request(const char *body, const char *header)
{
    return (httpd_req_t){.input = (const unsigned char *)body,
        .content_len = strlen(body), .control_header = header, .chunk = 3};
}
static void expect(esp_err_t (*handler)(httpd_req_t *), const char *body,
                   const char *header, const char *status_code)
{
    unsigned before = publication_count;
    httpd_req_t r = request(body, header);
    assert(handler(&r) == ESP_OK);
    if(strcmp(r.status,status_code))fprintf(stderr,"HTTP %s expected %s got %s: %s\n",body,status_code,r.status,r.response);
    assert(!strcmp(r.status, status_code));
    assert(!strcmp(r.cache, "no-store") && !strcmp(r.nosniff, "nosniff"));
    assert(!strcmp(r.type, "application/json; charset=utf-8"));
    if (status_code[0] != '2') assert(publication_count == before);
}
int main(int argc, char **argv)
{
    assert(argc == 4); // HTML, state JSON and compressed handler body outputs.
    web_control_state_t snapshot = {.ready = true, .revision = 1, .ks_decay_ms = 3000};
    patch_default(&snapshot.patch);
    snapshot.engine.available = snapshot.engine.valid = true;
    for(unsigned v=0;v<ENGINE_KS_VOICES;v++)snapshot.engine.started_sequence[v]=1000+v;
    snapshot.engine.mode_generation=7;snapshot.engine.wait_reason=KS_WAIT_BANK;
    snapshot.engine.control_status=0x00010002;snapshot.engine.control_status_valid=true;
    snapshot.inputs=(web_input_state_t){.version=1,.available=true,.initialized=true,
        .enabled=true,.ready=false,.capabilities_known=true,.independent_usb=true,
        .usb_configured=true,.stop_requested=9,.stop_completed=8,.stop_pending=true};
    web_control_store(&snapshot);

    expect(session, "", NULL, "403 Forbidden");
    expect(session, "", "01", "403 Forbidden");
    expect(session, "x", "1", "400 Bad Request");
    for (unsigned i = 0; i < WEB_CLIENTS; i++) {
        httpd_req_t r = request("", "1");
        assert(session(&r) == ESP_OK && !strcmp(r.status, "201 Created"));
        assert(strstr(r.response, "\"lease_ms\":1200"));
        assert(published[i].kind == EVT_WEB && published[i].web.op == WEB_START);
        assert(published[i].web.client == i && published[i].web.token == 1001 + i);
    }
    expect(session, "", "1", "503 Service Unavailable");
    expect(command, "1001 1 3 1 100", NULL, "403 Forbidden");
    expect(command, "", "1", "400 Bad Request");
    char long_body[97]; memset(long_body, '0', 96); long_body[96] = 0;
    expect(command, long_body, "1", "400 Bad Request");
    const char *bad[] = {"1001 1 2 64 100", "1001 1 2 119 127", "1001 1 2 120 0",
        "1001 1 3 33554432 100", "1001 1 3 1 0", "1001 1 4 1 0",
        "1001 1 5 2 0", "1001 1 6 199 0", "1001 1 6 8001 0",
        "1001 1 3 1 100\n", "1001  1 3 1 100", "1001 1 3 1 -1",
        "4294967296 1 3 1 100"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        expect(command, bad[i], "1", "400 Bad Request");
    httpd_req_t nul = request("1001 1 3 1 100", "1");
    const unsigned char embedded[] = "1001 1 3 1 100\0x";
    nul.input = embedded; nul.content_len = sizeof(embedded) - 1;
    unsigned before = publication_count;
    assert(command(&nul) == ESP_OK && !strcmp(nul.status, "400 Bad Request"));
    assert(publication_count == before);

    httpd_req_t good = request("1001 1 3 33554431 127", "1");
    assert(command(&good) == ESP_OK && !strcmp(good.status, "202 Accepted"));
    assert(!strcmp(good.response, "{\"accepted\":1,\"stage\":\"received\",\"fpga\":\"unverified\"}"));
    assert(published[publication_count - 1].web.expires == now + WEB_LEASE_US);
    assert(published[publication_count - 1].web.a == WEB_KEYS_MASK);
    expect(command, "1001 1 3 0 100", "1", "409 Conflict");
    expect(command, "9000 1 3 0 100", "1", "410 Gone");
    expect(command, "1002 1 3 1 100", "1", "409 Conflict");
    expect(command, "1002 1 2 7 80", "1", "202 Accepted");
    assert(published[publication_count - 1].web.client == 1);
    expect(command, "1001 2 2 7 127", "1", "202 Accepted");
    expect(command, "1001 3 4 0 0", "1", "202 Accepted");
    expect(command, "1001 4 5 1 0", "1", "202 Accepted");
    expect(command, "1001 5 6 8000 0", "1", "202 Accepted");
    before = publication_count;
    httpd_req_t broken = request("1001 6 3 0 100", "1"); broken.receive_fails = true;
    assert(command(&broken) == ESP_FAIL && publication_count == before);
    assert(s_sessions[0].sequence == 5);

    // The real snapshot must validate against the delivered client's parser.
    int64_t seen = s_sessions[0].seen;
    now += 9999999;
    httpd_req_t state_req = request("", NULL);
    assert(state(&state_req) == ESP_OK && !strcmp(state_req.status, "200 OK"));
    assert(strstr(state_req.response, "\"fpga\":\"unverified\""));
    assert(strstr(state_req.response,"\"started\":[1000,1001,1002,1003,1004,1005,1006,1007,1008,1009,1010,1011,1012,1013,1014,1015]"));
    assert(strstr(state_req.response,"\"inputs\":{\"version\":1,\"available\":true"));
    assert(strstr(state_req.response,"\"ready\":false,\"fault\":0"));
    assert(strstr(state_req.response,"\"independent_usb\":true,\"usb_configured\":true"));
    assert(strstr(state_req.response,"\"stop_requested\":9,\"stop_completed\":8"));
    assert(strstr(state_req.response,"\"stop_fault\":0,\"stop_pending\":true"));
    // Keep the existing 3 KiB HTTP stack buffer and bound every append,
    // including the largest counter values and deliberately short buffers.
    char bounded[3072];
    web_control_state_t large=snapshot;
    large.inputs.fault=large.inputs.stop_requested=large.inputs.stop_completed=UINT32_MAX;
    large.inputs.stop_fault=UINT32_MAX;
    for(unsigned v=0;v<ENGINE_KS_VOICES;v++)large.engine.started_sequence[v]=UINT32_MAX;
    int length=format_state(bounded,sizeof(bounded),&large);
    assert(length>0 && (size_t)length<sizeof(bounded));
    for(size_t capacity=1;capacity<64;capacity++) {
        memset(bounded,0x55,sizeof(bounded));
        assert(format_state(bounded,capacity,&large)>=(int)capacity);
        assert(bounded[capacity-1]==0 && bounded[capacity]==0x55);
    }
    assert(strstr(state_req.response,"\"mode_generation\":7") &&
           strstr(state_req.response,"\"wait_reason\":4") &&
           strstr(state_req.response,"\"control_status\":65538"));
    assert(s_sessions[0].seen == seen && publication_count == before);
    FILE *json = fopen(argv[2], "wb"); assert(json);
    assert(fwrite(state_req.response, 1, state_req.response_len, json) == state_req.response_len);
    assert(!fclose(json));
    web_control_state_t after; web_control_read(&after);
    assert(!memcmp(&snapshot.patch, &after.patch, sizeof(snapshot.patch)));
    assert(!after.sequence[0] && !after.keys[0]); // HTTP receipt did not adopt an event.
    now++;
    expect(command, "1001 6 3 0 100", "1", "410 Gone");
    httpd_req_t reused = request("", "1");
    assert(session(&reused) == ESP_OK && !strcmp(reused.status, "201 Created"));
    assert(s_sessions[0].token != 1001 && !s_sessions[0].sequence);
    expect(command, "1001 6 3 0 100", "1", "410 Gone");

    FILE *html = fopen(argv[1], "rb"); assert(html);
    assert(!fseek(html, 0, SEEK_END)); long n = ftell(html); assert(n > 0);
    rewind(html); char *bytes = malloc((size_t)n); assert(bytes);
    assert(fread(bytes, 1, (size_t)n, html) == (size_t)n); assert(!fclose(html));
    httpd_req_t page_req = request("", NULL);
    assert(page(&page_req) == ESP_OK && page_req.response_len == (size_t)n);
    assert(!memcmp(page_req.response, bytes, (size_t)n)); free(bytes);
    assert(strstr(page_req.csp, "connect-src 'self'"));
    // A constrained link cannot complete the former 168 KB response in its
    // send window. The real handler must negotiate a smaller complete body.
    static httpd_req_t compressed;
    compressed=request("",NULL);compressed.accept_encoding="gzip, deflate, br";
    compressed.send_limit=65536;
    assert(page(&compressed)==ESP_OK && compressed.response_len<65536 &&
           !strcmp(compressed.encoding,"gzip") && !strcmp(compressed.vary,"Accept-Encoding"));
    FILE *gz=fopen(argv[3],"wb");assert(gz);
    assert(fwrite(compressed.response,1,compressed.response_len,gz)==compressed.response_len);
    assert(!fclose(gz));
    const char *encodings[]={"gzip;q=0","br, gzip;q=0.000","xgzip","gzip;q=0.5","GZip ; q=1.000","*;q=0.5","*;q=1, gzip;q=0","gzip;q=1.5"};
    const bool wants_gzip[]={false,false,false,true,true,true,false,false};
    for(unsigned i=0;i<8;i++) {
        compressed=request("",NULL);compressed.accept_encoding=encodings[i];
        assert(page(&compressed)==ESP_OK);
        assert((!strcmp(compressed.encoding,"gzip"))==wants_gzip[i]);
        if(!wants_gzip[i])assert(compressed.response_len==(size_t)n);
    }
    compressed=request("",NULL);compressed.accept_encoding="gzip";compressed.send_limit=1;
    assert(page(&compressed)==ESP_FAIL && !compressed.response_len);
    puts("HTTP handlers: real HTML/state/parser, all 16 KS started including15, fragmented body, header, four sessions, collisions, sequence, expiry/reuse, async received-only PASS");
    return 0;
}
