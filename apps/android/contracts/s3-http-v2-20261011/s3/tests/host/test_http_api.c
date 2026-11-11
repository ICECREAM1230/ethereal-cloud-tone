#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi_web.c"

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
    assert(!strcmp(key, "X-FPGA-Control"));
    if (!r->control_header || strlen(r->control_header) >= size) return ESP_FAIL;
    strcpy(out, r->control_header);
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
    assert(target && "Unexpected response header (including CORS)");
    strcpy(target, value);
    return ESP_OK;
}
esp_err_t httpd_resp_send(httpd_req_t *r, const char *body, int length)
{
    size_t n = length == HTTPD_RESP_USE_STRLEN ? strlen(body) : (size_t)length;
    assert(n < sizeof(r->response));
    memcpy(r->response, body, n); r->response[n] = 0; r->response_len = n;
    return ESP_OK;
}
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
    assert(handler(&r) == ESP_OK && !strcmp(r.status, status_code));
    assert(!strcmp(r.cache, "no-store") && !strcmp(r.nosniff, "nosniff"));
    assert(!strcmp(r.type, "application/json; charset=utf-8"));
    if (status_code[0] != '2') assert(publication_count == before);
}
int main(int argc, char **argv)
{
    assert(argc == 3); // Actual HTML path, real state JSON output for JS validation.
    web_control_state_t snapshot = {.ready = true, .revision = 1, .ks_decay_ms = 3000};
    patch_default(&snapshot.patch);
    snapshot.engine.available = snapshot.engine.valid = true;
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
    expect(command, "1002 1 3 1 100", "1", "202 Accepted");
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
    puts("HTTP handlers: real HTML/state/parser, fragmented body, header, four sessions, collisions, sequence, expiry/reuse, async received-only PASS");
    return 0;
}
