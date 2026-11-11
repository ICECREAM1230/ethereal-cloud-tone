#include "midi_feedback_packet.h"

void midi_feedback_packet_begin(midi_feedback_packet_t *p, size_t capacity,
                                uint64_t epoch, uint16_t timestamp_ms)
{
    p->capacity = capacity < sizeof p->bytes ? capacity : sizeof p->bytes;
    p->length = 0;
    p->epoch = epoch;
    if (p->capacity)
        p->bytes[p->length++] = (uint8_t)(0x80 | ((timestamp_ms >> 7) & 0x3f));
}

bool midi_feedback_packet_add(midi_feedback_packet_t *p,
                              const midi_message_t *m, uint64_t epoch,
                              uint16_t timestamp_ms)
{
    size_t needed = 2 + m->len;
    if (!epoch || epoch != p->epoch || p->length + needed > p->capacity)
        return false;
    p->bytes[p->length++] = (uint8_t)(0x80 | (timestamp_ms & 0x7f));
    p->bytes[p->length++] = m->status;
    for (unsigned i = 0; i < m->len; i++)
        p->bytes[p->length++] = m->data[i];
    return true;
}

bool midi_feedback_packet_has_messages(const midi_feedback_packet_t *p)
{
    return p->length > 1;
}

const uint8_t *midi_feedback_packet_bytes(const midi_feedback_packet_t *p)
{
    return p->bytes;
}

size_t midi_feedback_packet_length(const midi_feedback_packet_t *p)
{
    return p->length;
}
