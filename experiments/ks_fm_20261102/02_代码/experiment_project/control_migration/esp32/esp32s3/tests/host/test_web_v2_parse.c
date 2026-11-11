#include <assert.h>
#include <stdio.h>
#include "web_control_v2.h"
#include "fm_contract.h"

int main(void)
{
    web_v2_event_t e;
    assert(web_control_v2_parse("1001 1 9 2 7 127 0", &e));
    assert(e.token == 1001 && e.sequence == 1 && e.context_epoch == 9);
    assert(e.op == FM_HTTP_OPCODE_CC && e.a == 7 && e.b == 127 && !e.c);
    assert(web_control_v2_parse("1001 2 9 3 33554431 127 0", &e));
    assert(web_control_v2_parse("1001 3 9 4 0 0 0", &e));
    assert(web_control_v2_parse("1001 4 9 5 2 1 0", &e));
    assert(web_control_v2_parse("1001 5 9 6 8000 0 0", &e));
    assert(web_control_v2_parse("1001 6 9 7 18 4294901760 31", &e));
    assert(e.b == 4294901760u && e.c == 31); // Never truncate the target to uint8.
    assert(web_control_v2_parse("1001 7 9 8 0 0 0", &e));
    const char *bad[] = {"0 1 9 4 0 0 0", "1 0 9 4 0 0 0", "1 1 0 4 0 0 0",
        "1 1 9 2 64 127 0", "1 1 9 2 7 128 0", "1 1 9 3 33554432 1 0",
        "1 1 9 3 1 0 0", "1 1 9 4 1 0 0", "1 1 9 5 1 1 0",
        "1 1 9 5 2 0 0", "1 1 9 5 3 1 0", "1 1 9 5 3 0 0", "1 1 9 6 199 0 0",
        "1 1 9 6 8001 0 0", "1 1 9 7 0 4294967295 0", "1 1 9 8 0 1 0",
        "1 1 9 4 0 0", "1 1 9 4 0 0 0 0", "1  1 9 4 0 0 0",
        "1 1 9 4 0 0 0\n", "1 1 9 4 0 0 0x", "1 1 9 4 0 0 -1",
        "4294967296 1 9 4 0 0 0"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        assert(!web_control_v2_parse(bad[i], &e));
    _Static_assert(sizeof(web_v2_event_t) <= 40, "v2 event must remain bounded");
    puts("Web v2: seven uint32 fields, epoch, frozen opcodes, full-width target, legacy bounds and strict grammar PASS");
    return 0;
}
