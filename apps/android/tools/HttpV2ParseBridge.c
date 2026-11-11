/* Length-framed stdin adapter around the complete frozen production parser.
 * The byte-length/NUL checks match the production HTTP request envelope.
 * This adapter neither simulates a device nor supplies a second text parser. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_control.c"

int main(void)
{
    unsigned char prefix[4];
    for (;;) {
        size_t count = fread(prefix, 1, sizeof(prefix), stdin);
        if (!count && feof(stdin)) return 0;
        if (count != sizeof(prefix)) return 2;
        uint32_t size = ((uint32_t)prefix[0] << 24) |
            ((uint32_t)prefix[1] << 16) | ((uint32_t)prefix[2] << 8) | prefix[3];
        if (size > 1024 * 1024) return 2;
        char *body = malloc((size_t)size + 1);
        if (!body) return 2;
        if (fread(body, 1, size, stdin) != size) { free(body); return 2; }
        body[size] = '\0';
        web_v2_event_t event = {0};
        bool ok = size && size <= FM_HTTP_COMMAND_MAX_BYTES &&
            !memchr(body, 0, size) && web_control_v2_parse(body, &event);
        free(body);
        printf("%u %" PRIu32 " %" PRIu32 " %" PRIu32 " %u %" PRIu32
            " %" PRIu32 " %" PRIu32 "\n", (unsigned)ok, event.token,
            event.sequence, event.context_epoch, (unsigned)event.op,
            event.a, event.b, event.c);
        if (ferror(stdout)) return 2;
    }
}
