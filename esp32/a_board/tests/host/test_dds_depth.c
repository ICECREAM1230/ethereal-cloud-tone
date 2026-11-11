/* DDS pre-parser boundary regression. Tests public APIs and the real parser.
 * The link wrapper counts actual vendor entry, not a mirrored scanner. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dds_document.h"
#include "dds_factory.inc.h"
#include "vendor/tomlc17/tomlc17.h"

static unsigned vendor_calls, checks, failures;
toml_result_t __real_toml_parse(const char *text, int length);
toml_result_t __wrap_toml_parse(const char *text, int length)
{ vendor_calls++; return __real_toml_parse(text, length); }

static void verify(const char *name, const char *text, size_t length,
                   bool expected, bool reject_before_vendor)
{
    static const char *paths[] = {
        NULL, "Patches/User/Pads/Depth.toml", "Patches/Factory/Pads/Depth.toml",
        "system.toml", "banks.toml"
    };
    for (unsigned api = 0; api < sizeof(paths) / sizeof(paths[0]); api++) {
        char error[DDS_ERROR_MAX] = "";
        patch_t output, before;
        memset(&output, 0x5a, sizeof output); before = output;
        unsigned warnings = 0xa5a5u;
        vendor_calls = 0;
        bool actual = api ? dds_document_validate(paths[api], text, length, error) :
            dds_patch_parse(text, length, &output, &warnings, error);
        bool ok = actual == expected &&
            (!reject_before_vendor || vendor_calls == 0) &&
            (!expected || vendor_calls == 1) &&
            (actual || error[0]) &&
            (api || actual || (!memcmp(&before, &output, sizeof output) && warnings == 0xa5a5u));
        checks++;
        if (!ok) {
            failures++;
            printf("FAIL case=%s api=%s bytes=%zu expected=%u actual=%u vendor_calls=%u early_reject=%u error=%s\n",
                name, paths[api] ? paths[api] : "patch_parse", length,
                expected, actual, vendor_calls, reject_before_vendor, error);
        }
    }
}

static size_t nested(char *out, const char *prefix, unsigned depth, unsigned kind)
{
    size_t n = strlen(prefix); memcpy(out, prefix, n);
    memcpy(out + n, "future=", 7); n += 7;
    for (unsigned i = 0; i < depth; i++) {
        bool table = kind == 0 || (kind == 2 && !(i & 1u));
        if (table) { memcpy(out + n, "{v=", 3); n += 3; }
        else out[n++] = '[';
    }
    out[n++] = '0';
    for (unsigned i = depth; i; i--) {
        bool table = kind == 0 || (kind == 2 && !((i - 1) & 1u));
        out[n++] = table ? '}' : ']';
    }
    out[n++] = '\n'; out[n] = 0; return n;
}

int main(void)
{
    char text[DDS_TEXT_MAX + 2], name[128], prefix[512];
    const unsigned depths[] = {0, 1, 3, 6, 7, 8, 60};
    for (unsigned kind = 0; kind < 3; kind++)
        for (unsigned i = 0; i < sizeof(depths) / sizeof(depths[0]); i++) {
            unsigned depth = depths[i];
            size_t n = nested(text, "format=1\n", depth, kind);
            snprintf(name, sizeof name, "plain-kind%u-depth%u", kind, depth);
            verify(name, text, n, depth <= 6, depth > 6);
        }

    /* TOML permits 3/4/5 closing quotes. Extra closing quotes belong to the
     * string value. The delimiters after that value are real containers. */
    for (unsigned literal = 0; literal < 2; literal++)
        for (unsigned closing = 3; closing <= 5; closing++) {
            char quote = literal ? '\'' : '"';
            size_t n = (size_t)snprintf(prefix, sizeof prefix, "format=1\nfuture_string=");
            for (unsigned j = 0; j < 3; j++) prefix[n++] = quote;
            memcpy(prefix + n, "body", 4); n += 4;
            for (unsigned j = 0; j < closing; j++) prefix[n++] = quote;
            prefix[n++] = '\n'; prefix[n] = 0;
            const unsigned mixed_depths[] = {6, 7, 8, 60};
            for (unsigned j = 0; j < sizeof(mixed_depths) / sizeof(mixed_depths[0]); j++) {
                unsigned depth = mixed_depths[j]; n = nested(text, prefix, depth, 2);
                snprintf(name, sizeof name, "%s-close%u-depth%u", literal ? "literal" : "basic", closing, depth);
                verify(name, text, n, depth <= 6, depth > 6);
                if (!literal && closing == 4 && depth == 60 && n != 224) {
                    puts("FAIL known regression payload must be exactly 224 bytes"); failures++;
                }
            }
        }

    /* The pinned TOML 1.1 scanner also supports x/e escapes below. */
    static const char *const ignored[] = {
        "format=1\n# [ { } ] [[ {{{ # ignored comment\n",
        "format=1\nfuture_string=\"[ { ] } # not a comment\"\n",
        "format=1\nfuture_string='[ { ] } # literal \\\\ text'\n",
        "format=1\nfuture_string=\"escaped \\\" quote and \\\\ slash [ { #\"\n",
        "format=1\nfuture_string=\"\"\"multiline [ { #\nclose \\\" quote \\\\ slash } ]\"\"\"\n",
        "format=1\nfuture_string='''multiline [ { #\nliteral \\ ' text } ]'''\n",
        "format=1\nfuture_string=\"\"\n",
        "format=1\nfuture_string=''\n",
        "format=1\nfuture_string=\"\"\"\"\"\"\n",
        "format=1\nfuture_string=''''''\n",
        "format=1\r\nfuture_string=\"CRLF [ { # } ]\"\r\n",
        "format=1\nfuture_string=\"\"\"first \\\n   continued [ { # } ]\nlast\"\"\"\n",
        "format=1\r\nfuture_string=\"\"\"first \\ \t\r\n\r\n   continued [ { # } ]\r\nlast\"\"\"\r\n",
        "format=1\nfuture_string=\"\\u4F60\\u597D \\U0001F600 \\u005B \\u007B \\U0000005D\"\n",
        "format=1\nfuture_string=\"\"\"Unicode \\u4F60\\u597D \\U0001F600 [ { } ]\"\"\"\n",
        "format=1\nfuture_string=\"\\x5B \\x7B \\e [ { } ]\"\n"
    };
    for (unsigned i = 0; i < sizeof(ignored) / sizeof(ignored[0]); i++) {
        size_t n = nested(text, ignored[i], 6, 2);
        snprintf(name, sizeof name, "strings-comments-%u-depth6", i);
        verify(name, text, n, true, false);
        n = nested(text, ignored[i], 7, 2);
        snprintf(name, sizeof name, "strings-comments-%u-depth7", i);
        verify(name, text, n, false, true);
    }
    static const char *const malformed[] = {
        "format=1\nfuture={v=0\n", "format=1\nfuture=[0\n",
        "format=1\nfuture={v=0]\n", "format=1\nfuture=[0}\n",
        "format=1\nfuture=0}\n", "format=1\nfuture=0]\n",
        "format=1\nfuture=\"unclosed", "format=1\nfuture='unclosed",
        "format=1\nfuture=\"\"\"unclosed", "format=1\nfuture='''unclosed",
        "format=1\nfuture=\"dangling\\", "format=1\nfuture=\"line\nbreak\"\n"
    };
    for (unsigned i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++) {
        snprintf(name, sizeof name, "unclosed-mismatched-%u", i);
        verify(name, malformed[i], strlen(malformed[i]), false, true);
    }
    verify("empty", "", 0, false, true);
    memcpy(text, "format=1\nfuture=0", 17); text[9] = 0;
    verify("embedded-NUL", text, 17, false, true);
    /* Comment padding is valid even at the precise byte limit. */
    memcpy(text, "format=1\n#", 10); memset(text + 10, 'x', DDS_TEXT_MAX - 10);
    text[DDS_TEXT_MAX] = 0;
    verify("exact-length-limit", text, DDS_TEXT_MAX, true, false);
    text[DDS_TEXT_MAX] = 'x'; text[DDS_TEXT_MAX + 1] = 0;
    verify("length-limit-plus-one", text, DDS_TEXT_MAX + 1, false, true);
    verify("normal-sparse-user", "format=1\n[amp_env]\nattack=0.01\n", 31, true, false);
    verify("factory-supersaw", dds_factory_supersaw, strlen(dds_factory_supersaw), true, false);
    verify("factory-galaxy", dds_factory_galaxy, strlen(dds_factory_galaxy), true, false);
    verify("missing-final-LF", "format=1", 8, true, false);
    printf("DDS_DEPTH checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
