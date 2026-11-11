#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_raw_midi.h"

static void rejected_span(const char *body, size_t length)
{
    web_raw_midi_request_t out;
    memset(&out, 0xa5, sizeof(out));
    assert(!web_raw_midi_parse(body, length, &out));
    const unsigned char *bytes = (const unsigned char *)&out;
    for (size_t i = 0; i < sizeof(out); i++) assert(bytes[i] == 0);
}

static void rejected(const char *body)
{
    rejected_span(body, strlen(body));
}

static void valid_transport(void)
{
    const char *body = "{\"source\":\"web\",\"bytes\":[144,60,100]}";
    const uint8_t expected[] = {144, 60, 100};
    web_raw_midi_request_t out;
    assert(web_raw_midi_parse(body, strlen(body), &out));
    assert(out.length == 3 && !memcmp(out.bytes, expected, 3));
    assert(!out.has_binding && !out.token && !out.sequence && !out.context_epoch);
    for (size_t i = out.length; i < sizeof(out.bytes); i++) assert(!out.bytes[i]);

    // Transport must preserve realtime, SysEx, and arbitrary partial messages.
    body = " \r\n\t{\"bytes\" : [0,255,248,240,1,247,144], \"source\" : \"web\"}\n";
    const uint8_t raw[] = {0, 255, 248, 240, 1, 247, 144};
    assert(web_raw_midi_parse(body, strlen(body), &out));
    assert(out.length == sizeof(raw) && !memcmp(out.bytes, raw, sizeof(raw)));

    body = "{\"context_epoch\":4294967295,\"source\":\"web\",\"sequence\":2,\"bytes\":[1],\"token\":1}";
    assert(web_raw_midi_parse(body, strlen(body), &out));
    assert(out.has_binding && out.token == 1 && out.sequence == 2);
    assert(out.context_epoch == UINT32_MAX && out.length == 1 && out.bytes[0] == 1);

    body = "{\"sour\\u0063e\":\"\\u0077eb\",\"bytes\":[0]}";
    assert(web_raw_midi_parse(body, strlen(body), &out));
    assert(out.length == 1 && !out.bytes[0]);
}

static void invalid_transport(void)
{
    const char *bad[] = {
        "", "null", "[]", "{}", "{\"source\":\"web\"}", "{\"bytes\":[1]}",
        "{\"source\":\"ble\",\"bytes\":[1]}",
        "{\"source\":1,\"bytes\":[1]}",
        "{\"source\":\"web\",\"bytes\":[]}",
        "{\"source\":\"web\",\"bytes\":1}",
        "{\"source\":\"web\",\"bytes\":[-1]}",
        "{\"source\":\"web\",\"bytes\":[256]}",
        "{\"source\":\"web\",\"bytes\":[4294967296]}",
        "{\"source\":\"web\",\"bytes\":[999999999999999999999999999999]}",
        "{\"source\":\"web\",\"bytes\":[1.0]}",
        "{\"source\":\"web\",\"bytes\":[1e0]}",
        "{\"source\":\"web\",\"bytes\":[01]}",
        "{\"source\":\"web\",\"bytes\":[+1]}",
        "{\"source\":\"web\",\"bytes\":[true]}",
        "{\"source\":\"web\",\"bytes\":[null]}",
        "{\"source\":\"web\",\"bytes\":[\"1\"]}",
        "{\"source\":\"web\",\"bytes\":[[1]]}",
        "{\"source\":\"web\",\"bytes\":[1,]}",
        "{\"source\":\"web\",\"bytes\":[,1]}",
        "{\"source\":\"web\",\"bytes\":[1 2]}",
        "{\"source\":\"web\",\"bytes\":[1],}",
        "{\"source\":\"web\",\"bytes\":[1]}false",
        "{\"source\":\"web\",\"bytes\":[1]}{}",
        "{\"source\":\"web\",\"bytes\":[1],\"unknown\":0}",
        "{\"source\":\"web\",\"source\":\"web\",\"bytes\":[1]}",
        "{\"source\":\"web\",\"bytes\":[1],\"bytes\":[2]}",
        "{\"source\":\"web\",\"bytes\":[1],\"sour\\u0063e\":\"web\"}",
        "{\"source\":\"web\\u0000\",\"bytes\":[1]}",
        "{\"source\":\"we\nb\",\"bytes\":[1]}",
        "{\"source\":\"we\\qb\",\"bytes\":[1]}",
        "{\"source\":\"\\u00xz\",\"bytes\":[1]}",
        "{\"source\":\"\\uD800\",\"bytes\":[1]}",
        "{\"source\":\"\\uDC00\",\"bytes\":[1]}",
        "{\"source\":\"\\uD83D\\uDE00\",\"bytes\":[1]}",
        "{\"source\":\"web\",\"bytes\":[1]}\v",
        "{'source':'web','bytes':[1]}",
        "{\"source\":\"web\"/* comment */,\"bytes\":[1]}",
        "{\"source\":\"web\",\"bytes\":[1],\"token\":1}",
        "{\"source\":\"web\",\"bytes\":[1],\"token\":1,\"sequence\":2}",
        "{\"source\":\"web\",\"bytes\":[1],\"context_epoch\":3}",
        "{\"source\":\"web\",\"bytes\":[1],\"token\":1,\"sequence\":2,\"context_epoch\":0}",
        "{\"source\":\"web\",\"bytes\":[1],\"token\":0,\"sequence\":2,\"context_epoch\":3}",
        "{\"source\":\"web\",\"bytes\":[1],\"token\":1,\"sequence\":0,\"context_epoch\":3}",
        "{\"source\":\"web\",\"bytes\":[1],\"token\":4294967296,\"sequence\":2,\"context_epoch\":3}",
        "{\"source\":\"web\",\"bytes\":[1],\"token\":1,\"sequence\":2.0,\"context_epoch\":3}",
        "{\"source\":\"web\",\"bytes\":[1],\"token\":1,\"sequence\":2,\"context_epoch\":3,\"token\":1}",
        "{\"source\":\"web\",\"bytes\":[1],\"token\":1,\"sequence\":2,\"context_epoch\":3,\"sequence\":2}",
        "{\"source\":\"web\",\"bytes\":[1],\"token\":1,\"sequence\":2,\"context_epoch\":3,\"context_epoch\":3}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) rejected(bad[i]);
    rejected_span(NULL, 1);
    const char embedded[] = "{\"source\":\"web\",\"bytes\":[1]}\0";
    rejected_span(embedded, sizeof(embedded) - 1);
    assert(!web_raw_midi_parse("{}", 2, NULL));
}

static void bounded_input(void)
{
    const char *valid = "{\"source\":\"web\",\"bytes\":[144,60,100],\"token\":1,\"sequence\":2,\"context_epoch\":3}";
    const size_t length = strlen(valid);
    for (size_t n = 0; n < length; n++) {
        char *span = malloc(n ? n : 1);
        assert(span);
        memcpy(span, valid, n);
        rejected_span(span, n);
        free(span);
    }
    char *span = malloc(length);
    assert(span);
    memcpy(span, valid, length);
    web_raw_midi_request_t out;
    assert(web_raw_midi_parse(span, length, &out));
    free(span);

    char body[1400];
    size_t used = (size_t)sprintf(body, "{\"source\":\"web\",\"bytes\":[");
    for (unsigned i = 0; i < 256; i++)
        used += (size_t)sprintf(body + used, "%s%u", i ? "," : "", i);
    strcpy(body + used, "]}");
    assert(web_raw_midi_parse(body, used + 2, &out));
    assert(out.length == 256);
    for (size_t i = 0; i < 256; i++) assert(out.bytes[i] == i);
    strcpy(body + used, ",0]}");
    rejected(body);
}

int main(void)
{
    valid_transport();
    invalid_transport();
    bounded_input();
    puts("raw MIDI JSON: byte fidelity, strict grammar, binding, bounds and failure clearing PASS");
    return 0;
}
