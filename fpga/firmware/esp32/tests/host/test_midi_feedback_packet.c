#include "midi_feedback_packet.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void batching_respects_mtu_and_epoch(void)
{
    midi_feedback_packet_t packet;
    midi_feedback_packet_begin(&packet, 12, 0x1234, 0x123);
    const midi_message_t a = {.status=0xb0,.len=2,.data={7,100}};
    const midi_message_t b = {.status=0xb0,.len=2,.data={10,64}};
    const midi_message_t c = {.status=0xb0,.len=2,.data={74,80}};
    assert(midi_feedback_packet_add(&packet, &a, 0x1234, 0x123));
    assert(midi_feedback_packet_add(&packet, &b, 0x1234, 0x124));
    assert(!midi_feedback_packet_add(&packet, &c, 0x1234, 0x125));
    assert(!midi_feedback_packet_add(&packet, &c, 0x9999, 0x125));
    assert(midi_feedback_packet_length(&packet) == 9);
    const uint8_t *bytes = midi_feedback_packet_bytes(&packet);
    assert(bytes[0] == 0x82 && bytes[1] == 0xa3 &&
           bytes[2] == 0xb0 && bytes[3] == 7 &&
           bytes[4] == 100 && bytes[6] == 0xb0 && bytes[7] == 10 && bytes[8] == 64);
}

static void empty_and_tiny_mtu_are_safe(void)
{
    midi_feedback_packet_t packet;
    midi_feedback_packet_begin(&packet, 3, 7, 0);
    const midi_message_t m = {.status=0xb0,.len=2,.data={7,1}};
    assert(!midi_feedback_packet_add(&packet, &m, 7, 0));
    assert(!midi_feedback_packet_has_messages(&packet));
    midi_feedback_packet_begin(&packet, 517, 8, 8191);
    assert(midi_feedback_packet_add(&packet, &m, 8, 8191));
    assert(midi_feedback_packet_has_messages(&packet));
}

int main(void)
{
    batching_respects_mtu_and_epoch();
    empty_and_tiny_mtu_are_safe();
    puts("MIDI feedback packets: MTU batching and epoch fence PASS");
    return 0;
}
