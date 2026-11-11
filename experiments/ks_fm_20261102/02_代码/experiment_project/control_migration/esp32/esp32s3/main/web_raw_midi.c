#include "web_raw_midi.h"
#include <string.h>

typedef struct {
    const char *body;
    size_t length, position;
} json_cursor_t;

static void whitespace(json_cursor_t *cursor)
{
    while (cursor->position < cursor->length) {
        char c = cursor->body[cursor->position];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        cursor->position++;
    }
}

static bool take(json_cursor_t *cursor, char expected)
{
    whitespace(cursor);
    if (cursor->position == cursor->length ||
        cursor->body[cursor->position] != expected) return false;
    cursor->position++;
    return true;
}

static int hex_digit(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// All accepted keys and the source value are ASCII. Decode JSON escapes before
// matching, but reject non-ASCII/control code points and embedded NULs.
static bool ascii_string(json_cursor_t *cursor, char *out, size_t capacity)
{
    if (!take(cursor, '"')) return false;
    size_t used = 0;
    while (cursor->position < cursor->length) {
        unsigned c = (unsigned char)cursor->body[cursor->position++];
        if (c == '"') {
            out[used] = 0;
            return true;
        }
        if (c == '\\') {
            if (cursor->position == cursor->length) return false;
            c = (unsigned char)cursor->body[cursor->position++];
            switch (c) {
            case '"': case '\\': case '/': break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u':
                c = 0;
                for (unsigned i = 0; i < 4; i++) {
                    if (cursor->position == cursor->length) return false;
                    int digit = hex_digit((unsigned char)cursor->body[cursor->position++]);
                    if (digit < 0) return false;
                    c = c * 16u + (unsigned)digit;
                }
                break;
            default: return false;
            }
        }
        if (c < 0x20 || c > 0x7f || used + 1 >= capacity) return false;
        out[used++] = (char)c;
    }
    return false;
}

static bool unsigned_integer(json_cursor_t *cursor, uint32_t limit, uint32_t *out)
{
    whitespace(cursor);
    size_t start = cursor->position;
    uint32_t value = 0;
    while (cursor->position < cursor->length) {
        unsigned char c = (unsigned char)cursor->body[cursor->position];
        if (c < '0' || c > '9') break;
        if (cursor->position > start && cursor->body[start] == '0') return false;
        unsigned digit = c - '0';
        if (value > (limit - digit) / 10u) return false;
        value = value * 10u + digit;
        cursor->position++;
    }
    if (cursor->position == start) return false;
    *out = value;
    return true;
}

static bool byte_array(json_cursor_t *cursor, web_raw_midi_request_t *request)
{
    if (!take(cursor, '[')) return false;
    do {
        uint32_t byte;
        if (request->length == sizeof(request->bytes) ||
            !unsigned_integer(cursor, UINT8_MAX, &byte)) return false;
        request->bytes[request->length++] = (uint8_t)byte;
        if (take(cursor, ']')) return true;
    } while (take(cursor, ','));
    return false;
}

bool web_raw_midi_parse(const char *body, size_t length,
                        web_raw_midi_request_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!body) return false;
    web_raw_midi_request_t request = {0};
    json_cursor_t cursor = {.body = body, .length = length};
    unsigned seen = 0;
    if (!take(&cursor, '{')) return false;
    do {
        char key[14]; // Longest accepted key: context_epoch.
        if (!ascii_string(&cursor, key, sizeof(key)) || !take(&cursor, ':')) return false;
        unsigned field;
        if (!strcmp(key, "source")) field = 1u;
        else if (!strcmp(key, "bytes")) field = 2u;
        else if (!strcmp(key, "token")) field = 4u;
        else if (!strcmp(key, "sequence")) field = 8u;
        else if (!strcmp(key, "context_epoch")) field = 16u;
        else return false;
        if (seen & field) return false;
        seen |= field;
        if (field == 1u) {
            char source[4];
            if (!ascii_string(&cursor, source, sizeof(source)) || strcmp(source, "web")) return false;
        } else if (field == 2u) {
            if (!byte_array(&cursor, &request)) return false;
        } else {
            uint32_t value;
            if (!unsigned_integer(&cursor, UINT32_MAX, &value) || !value) return false;
            if (field == 4u) request.token = value;
            else if (field == 8u) request.sequence = value;
            else request.context_epoch = value;
        }
        if (take(&cursor, '}')) break;
        if (!take(&cursor, ',')) return false;
    } while (true);
    whitespace(&cursor);
    if (cursor.position != cursor.length || (seen != 3u && seen != 31u)) return false;
    request.has_binding = seen == 31u;
    *out = request;
    return true;
}
