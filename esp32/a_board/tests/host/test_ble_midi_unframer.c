#include "ble_midi_unframer.h"
#include "midi_parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BYTES(...) ((const uint8_t[]){__VA_ARGS__})
#define EXPECT(s, pkt, want) expect((s), (pkt), sizeof(pkt), (want), sizeof(want))

static void expect(ble_midi_unframer_t *s, const uint8_t *pkt, size_t n,
                   const uint8_t *want, size_t want_n)
{
    uint8_t out[128];
    size_t out_n = 999;
    memset(out, 0xa5, sizeof(out));
    assert(ble_midi_unframer_feed_packet(s, pkt, n, out, sizeof(out), &out_n));
    assert(out_n == want_n);
    assert(memcmp(out, want, want_n) == 0);
}

static void reject(ble_midi_unframer_t *s, const uint8_t *pkt, size_t n,
                   size_t capacity)
{
    uint8_t out[128], untouched[128];
    size_t out_n = 999;
    memset(out, 0xa5, sizeof(out));
    memcpy(untouched, out, sizeof(out));
    assert(!ble_midi_unframer_feed_packet(s, pkt, n, out, capacity, &out_n));
    assert(out_n == 0);
    assert(memcmp(out, untouched, sizeof(out)) == 0);
    /* A failed packet must not leave a SysEx continuation context alive. */
    assert(!ble_midi_unframer_feed_packet(s, BYTES(0x80, 1, 2), 3,
                                          out, sizeof(out), &out_n));
    assert(out_n == 0);
}

static void test_common_restores_only_the_next_omitted_status(void)
{
    ble_midi_unframer_t s;
    ble_midi_unframer_init(&s);
    EXPECT(&s, BYTES(0x80, 0x80, 0x91, 60, 100, 0x81, 0xf1, 7,
                      0x82, 60, 0, 61, 1),
               BYTES(0x91, 60, 100, 0xf1, 7, 0x91, 60, 0, 61, 1));
    EXPECT(&s, BYTES(0x80, 0x80, 0x91, 60, 100, 0x81, 0xf0, 0x7d, 1,
                      0x82, 0xf7, 0x83, 60, 0, 61, 1),
               BYTES(0x91, 60, 100, 0xf0, 0x7d, 1, 0xf7, 0x91, 60, 0, 61, 1));
    /* A new explicit status already repairs the classic running status. */
    EXPECT(&s, BYTES(0x80, 0x80, 0x91, 60, 100, 0x81, 0xf6,
                      0x82, 0x82, 60, 0, 61, 1),
               BYTES(0x91, 60, 100, 0xf6, 0x82, 60, 0, 61, 1));
    /* Expected output is 7 bytes; capacity 6 only fails when restoration counts. */
    reject(&s, BYTES(0x80, 0x80, 0x91, 60, 100, 0x81, 0xf6, 0x82, 60, 0),
           10, 6);
}

static void test_timestamp_and_running_status(void)
{
    ble_midi_unframer_t s;
    ble_midi_unframer_init(&s);
    EXPECT(&s, BYTES(0x80, 0x80, 0x90, 60, 0), BYTES(0x90, 60, 0));
    /* Catch treating timestamp/status pairs as MIDI, or expanding running status. */
    EXPECT(&s, BYTES(0x81, 0xfe, 0x90, 60, 70, 61, 71, 0x81, 62, 72,
                      0x82, 0xc0, 3, 4, 0x83, 0x80, 60, 0),
               BYTES(0x90, 60, 70, 61, 71, 62, 72, 0xc0, 3, 4, 0x80, 60, 0));
    /* Timestamp values overlap all MIDI status values, including F7. */
    EXPECT(&s, BYTES(0x80, 0xf7, 0x90, 1, 2, 0xff, 3, 4),
               BYTES(0x90, 1, 2, 3, 4));
    reject(&s, BYTES(0x80, 0x80, 3, 4), 4, 128); /* RS is packet-local. */
}

/* RP-052 section 7 retains BLE running status across System Common. Restore
 * the next omitted channel status for a classic MIDI byte-stream consumer.
 */
static void test_system_messages(void)
{
    ble_midi_unframer_t s;
    ble_midi_unframer_init(&s);
    EXPECT(&s, BYTES(0x80, 0x80, 0x90, 60, 1, 0x81, 0xf8, 0x82, 61, 2,
                      0x83, 0xf1, 0x23, 0x84, 62, 3, 0x85, 0xf2, 1, 2,
                      0x86, 0xf3, 4, 0x87, 0xf6, 0x88, 0xff),
               BYTES(0x90, 60, 1, 0xf8, 61, 2, 0xf1, 0x23, 0x90, 62, 3,
                      0xf2, 1, 2, 0xf3, 4, 0xf6, 0xff));
    reject(&s, BYTES(0x80, 0x80, 0x90, 1, 2, 0x81, 0xf8, 3, 4), 9, 128);
}

static void test_sysex_fragments_and_timestamp_ambiguity(void)
{
    ble_midi_unframer_t a, b;
    ble_midi_unframer_init(&a);
    ble_midi_unframer_init(&b);
    EXPECT(&a, BYTES(0x80, 0x80, 0xf0, 0x7d, 1), BYTES(0xf0, 0x7d, 1));
    EXPECT(&b, BYTES(0x80, 0x80, 0x91, 2, 3), BYTES(0x91, 2, 3));
    EXPECT(&a, BYTES(0x80, 2, 3, 0xf7, 0xf8, 4), BYTES(2, 3, 0xf8, 4));
    EXPECT(&a, BYTES(0x80, 0xf7, 0xf7, 0x81, 0x90, 5, 0),
               BYTES(0xf7, 0x90, 5, 0));
    EXPECT(&a, BYTES(0x80, 0x80, 0xf0, 1, 0x81, 0xfa), BYTES(0xf0, 1, 0xfa));
    EXPECT(&a, BYTES(0x80, 0x82, 0xf8), BYTES(0xf8));
    EXPECT(&a, BYTES(0x80, 0x83, 0xf7), BYTES(0xf7));
    EXPECT(&a, BYTES(0x80, 0x80, 0xf0, 1), BYTES(0xf0, 1));
    ble_midi_unframer_reset(&a);
    reject(&a, BYTES(0x80, 2, 3), 3, 128);
    reject(&b, BYTES(0x80, 2, 3), 3, 128);
}

static void test_invalid_packets_are_atomic_and_reset(void)
{
    ble_midi_unframer_t s;
    ble_midi_unframer_init(&s);
    reject(&s, BYTES(0), 0, 128);
    reject(&s, BYTES(0x80), 1, 128);
    reject(&s, BYTES(0, 0x80, 0xf8), 3, 128);
    reject(&s, BYTES(0xc0, 0x80, 0xf8), 3, 128);
    reject(&s, BYTES(0x80, 0x80), 2, 128);
    reject(&s, BYTES(0x80, 0x80, 0x90, 1), 4, 128);
    /* Voice-internal real-time is invalid in BLE (RP-052 section 7). */
    reject(&s, BYTES(0x80, 0x80, 0x90, 1, 0x81, 0xf8, 2), 7, 128);
    reject(&s, BYTES(0x80, 0x80, 0x90, 1, 0xf8, 2), 6, 128);
    reject(&s, BYTES(0x80, 0x80, 0x90, 1, 2, 0x81), 6, 128);
    reject(&s, BYTES(0x80, 0x80, 0xf2, 1), 4, 128);
    reject(&s, BYTES(0x80, 0x80, 0x90, 1, 2), 5, 2);
    EXPECT(&s, BYTES(0x80, 0x80, 0xf0, 1), BYTES(0xf0, 1));
    reject(&s, BYTES(0x80, 2, 0x81, 0x90, 1, 2), 6, 128);
    EXPECT(&s, BYTES(0x80, 0x80, 0xf0, 1), BYTES(0xf0, 1));
    reject(&s, BYTES(0x80, 2, 3, 0x81, 0xf7), 5, 2);
    EXPECT(&s, BYTES(0x80, 0x80, 0x90, 60, 0), BYTES(0x90, 60, 0));
}

static midi_message_t messages[8];
static size_t message_count;
static void capture_message(const midi_message_t *message, void *user)
{
    (void)user;
    assert(message_count < sizeof(messages) / sizeof(messages[0]));
    messages[message_count++] = *message;
}

static void test_classic_parser_interoperability(void)
{
    ble_midi_unframer_t s;
    ble_midi_unframer_init(&s);
    const uint8_t packet[] = {
        0x80, 0x80, 0x91, 60, 100,
        0x81, 0xf1, 7, 0x82, 60, 0,
        0x83, 0xf2, 1, 2, 0x84, 62, 90,
        0x85, 0xf6, 0x86, 62, 0,
        0x87, 0xf0, 0x7d, 1, 0x88, 0xf8, 2, 0x89, 0xf7, 0x8a, 64, 80,
        0x8b, 0xf8, 0x8c, 64, 0
    };
    uint8_t out[128];
    size_t n;
    assert(ble_midi_unframer_feed_packet(&s, packet, sizeof(packet), out,
                                          sizeof(out), &n));
    midi_parser_t parser;
    message_count = 0;
    midi_parser_init(&parser, capture_message, NULL);
    for (size_t i = 0; i < n; ++i) midi_parser_feed(&parser, out[i]);
    const uint8_t statuses[] = {0x91, 0x81, 0x91, 0x81, 0x91, 0x81};
    const uint8_t notes[] = {60, 60, 62, 62, 64, 64};
    const uint8_t velocities[] = {100, 0, 90, 0, 80, 0};
    assert(message_count == 6);
    for (size_t i = 0; i < 6; ++i) {
        assert(messages[i].status == statuses[i] && messages[i].len == 2);
        assert(messages[i].data[0] == notes[i] && messages[i].data[1] == velocities[i]);
    }
}

static void test_capacity_including_restoration_and_maximum_att(void)
{
    ble_midi_unframer_t s;
    ble_midi_unframer_init(&s);
    const uint8_t common[] = {0x80, 0x80, 0x91, 60, 100, 0x81, 0xf6, 0x82, 60, 0};
    const uint8_t expected[] = {0x91, 60, 100, 0xf6, 0x91, 60, 0};
    uint8_t small[7];
    size_t n;
    assert(ble_midi_unframer_feed_packet(&s, common, sizeof(common), small,
                                          sizeof(small), &n));
    assert(n == 7 && !memcmp(small, expected, sizeof(expected)));
    reject(&s, common, sizeof(common), 6);

    /* The exact overall upper bound is ATT length minus its header: a
     * 517-byte SysEx continuation contains 516 unchanged data bytes. */
    uint8_t packet[517], out[516];
    memset(packet, 1, sizeof(packet));
    packet[0] = 0x80;
    EXPECT(&s, BYTES(0x80, 0x80, 0xf0), BYTES(0xf0));
    assert(ble_midi_unframer_feed_packet(&s, packet, sizeof(packet), out,
                                          sizeof(out), &n));
    assert(n == 516);
    for (size_t i = 0; i < n; ++i) assert(out[i] == 1);
    memset(out, 0xa5, sizeof(out));
    assert(!ble_midi_unframer_feed_packet(&s, packet, sizeof(packet), out, 515, &n));
    assert(n == 0 && !s.in_sysex);
    for (size_t i = 0; i < sizeof(out); ++i) assert(out[i] == 0xa5);
    reject(&s, packet, sizeof(packet), 128); /* Context cleared after shortage. */

    /* 128 Common/omitted-status pairs in one maximum-size ATT packet. Each
     * restoration replaces its mandatory timestamp, so no expansion beyond
     * the ATT bound occurs even for one-data-byte channel messages. */
    packet[0] = packet[1] = 0x80; packet[2] = 0xc0; packet[3] = 1;
    for (size_t i = 4; i < 516; i += 4) {
        packet[i] = 0x80; packet[i+1] = 0xf6;
        packet[i+2] = 0x80; packet[i+3] = 1;
    }
    packet[516] = 2;
    assert(ble_midi_unframer_feed_packet(&s, packet, sizeof(packet), out,
                                          sizeof(out), &n));
    assert(n == 387 && out[0] == 0xc0 && out[1] == 1 && out[386] == 2);
    for (size_t i = 2; i < 386; i += 3)
        assert(out[i] == 0xf6 && out[i+1] == 0xc0 && out[i+2] == 1);
}

static void test_sysex_does_not_extend_packet_running_status(void)
{
    ble_midi_unframer_t s;
    ble_midi_unframer_init(&s);
    EXPECT(&s, BYTES(0x80, 0x80, 0x91, 60, 100, 0x81, 0xf0, 0x7d),
               BYTES(0x91, 60, 100, 0xf0, 0x7d));
    reject(&s, BYTES(0x80, 1, 0x81, 0xf7, 0x82, 60, 0), 7, 128);
}

int main(void)
{
    test_common_restores_only_the_next_omitted_status();
    test_classic_parser_interoperability();
    test_capacity_including_restoration_and_maximum_att();
    test_sysex_does_not_extend_packet_running_status();
    test_timestamp_and_running_status();
    test_system_messages();
    test_sysex_fragments_and_timestamp_ambiguity();
    test_invalid_packets_are_atomic_and_reset();
    puts("ble_midi_unframer: all tests passed");
    return 0;
}
