/* SPDX-License-Identifier: MIT
 * Behavior port of Lets-go's frozen src/midi_decode/parser/midi_parser.sv
 * and per-source queues/arbitration in midi_hub.sv (2026-10-28 snapshot).
 * The RTL credits audioxtreamer for its three-byte packet boundary.
 * Source/provenance and intentional scheduling differences: parser-port.md.
 */
#include "midi_parser.h"

static bool short_type(uint8_t status)
{
    return (status & 0xf0u) == 0xc0u || (status & 0xf0u) == 0xd0u ||
           status == 0xf1u || status == 0xf3u;
}

static uint8_t advance(uint8_t index)
{
    return index + 1u == MIDI_PARSER_QUEUE_DEPTH ? 0u : (uint8_t)(index + 1u);
}

static uint8_t tail(uint8_t head, uint8_t count)
{
    unsigned index = (unsigned)head + count;
    if (index >= MIDI_PARSER_QUEUE_DEPTH) index -= MIDI_PARSER_QUEUE_DEPTH;
    return (uint8_t)index;
}

static void clear_source(midi_parser_source_t *s)
{
    s->running_status = 0; s->active_status = 0; s->first_data = 0;
    s->remaining = 0; s->have_first = false; s->in_sysex = false;
    s->sysex_first = false; s->sysex_count = 0;
    s->sysex_buffer[0] = 0; s->sysex_buffer[1] = 0; s->sysex_buffer[2] = 0;
    s->event_head = 0; s->event_count = 0; s->sysex_head = 0; s->sysex_queued = 0;
    s->short_start = 0; s->sysex_start = 0;
}

void midi_parser_init(midi_parser_t *p)
{
    unsigned i;
    if (!p) return;
    /* Queue payloads are invalid until written; avoid clearing 1.5 KiB of
     * unused storage or introducing a freestanding memset dependency. */
    for (i = 0; i < MIDI_PARSER_SOURCE_COUNT; ++i) {
        clear_source(&p->sources[i]);
        p->sources[i].next_seq = 0;
    }
    p->event_rr = 0; p->sysex_rr = 0;
    p->event_source = 0; p->sysex_source = 0;
    p->event_held = false; p->sysex_held = false;
}

static void enqueue_event(midi_parser_source_t *s, uint8_t source,
                          uint8_t status, uint8_t d1, uint8_t d2,
                          uint8_t nbytes, uint64_t start, uint64_t end)
{
    control_midi_event_t *e = &s->events[tail(s->event_head, s->event_count)];
    e->source = source; e->status = status; e->data1 = d1; e->data2 = d2;
    e->nbytes = nbytes; e->seq = s->next_seq++;
    e->start_tick = start; e->end_tick = end;
    ++s->event_count;
}

static void enqueue_sysex(midi_parser_source_t *s, uint8_t source,
                          uint8_t count, bool last, bool aborted, uint64_t end)
{
    control_sysex_chunk_t *c = &s->sysex[tail(s->sysex_head, s->sysex_queued)];
    c->source = source;
    c->data[0] = s->sysex_buffer[0]; c->data[1] = s->sysex_buffer[1];
    c->data[2] = s->sysex_buffer[2]; c->count = count;
    c->first = s->sysex_first; c->last = last; c->aborted = aborted;
    c->seq = s->next_seq++; c->start_tick = s->sysex_start; c->end_tick = end;
    ++s->sysex_queued;
}

static void clear_sysex_buffer(midi_parser_source_t *s)
{
    s->sysex_buffer[0] = 0; s->sysex_buffer[1] = 0; s->sysex_buffer[2] = 0;
    s->sysex_count = 0;
}

midi_parser_result_t midi_parser_push(midi_parser_t *p, uint8_t source,
                                    uint8_t byte, uint64_t tick)
{
    midi_parser_source_t *s;
    bool needs_event = false, needs_sysex = false;
    if (!p) return MIDI_PARSER_BAD_ARGUMENT;
    if (source >= MIDI_PARSER_SOURCE_COUNT) return MIDI_PARSER_BAD_SOURCE;
    s = &p->sources[source];

    /* Reserve every output this byte can produce BEFORE any state change.
     * F6 can emit both a SysEx abort and a common event on one input byte. */
    if (byte >= 0xf8u) {
        needs_event = byte != 0xf9u && byte != 0xfdu;
    } else if (byte & 0x80u) {
        needs_sysex = s->in_sysex;
        needs_event = byte == 0xf6u;
    } else if (s->in_sysex) {
        needs_sysex = s->sysex_count == 2u;
    } else {
        needs_event = s->remaining == 1u ||
            (s->remaining == 0u && s->running_status != 0u && short_type(s->running_status));
    }
    if ((needs_event && s->event_count == MIDI_PARSER_QUEUE_DEPTH) ||
        (needs_sysex && s->sysex_queued == MIDI_PARSER_QUEUE_DEPTH))
        return MIDI_PARSER_WOULD_BLOCK;

    if (byte >= 0xf8u) {
        if (needs_event) enqueue_event(s, source, byte, 0, 0, 1, tick, tick);
    } else if (byte & 0x80u) {
        bool was_sysex = s->in_sysex;
        if (was_sysex && byte != 0xf7u)
            enqueue_sysex(s, source, s->sysex_count, true, true, tick);
        /* F7 must append before clearing the old SysEx buffer. */
        if (was_sysex && byte == 0xf7u) {
            s->sysex_buffer[s->sysex_count] = byte;
            enqueue_sysex(s, source, (uint8_t)(s->sysex_count + 1u), true, false, tick);
        }
        s->remaining = 0; s->have_first = false; s->in_sysex = false;
        clear_sysex_buffer(s);
        if (byte < 0xf0u) {
            s->running_status = byte; s->active_status = byte;
            s->remaining = short_type(byte) ? 1u : 2u;
            s->short_start = tick;
        } else {
            s->running_status = 0;
            switch (byte) {
            case 0xf0:
                s->in_sysex = true; s->sysex_first = true;
                s->sysex_buffer[0] = byte; s->sysex_count = 1;
                s->sysex_start = tick;
                break;
            case 0xf1: case 0xf2: case 0xf3:
                s->active_status = byte; s->remaining = short_type(byte) ? 1u : 2u;
                s->short_start = tick;
                break;
            case 0xf6:
                enqueue_event(s, source, byte, 0, 0, 1, tick, tick);
                break;
            default:
                break; /* Reserved common/F7 cancel pending and running state. */
            }
        }
    } else if (s->in_sysex) {
        s->sysex_buffer[s->sysex_count] = byte;
        if (s->sysex_count == 2u) {
            enqueue_sysex(s, source, 3, false, false, tick);
            clear_sysex_buffer(s); s->sysex_first = false;
        } else {
            ++s->sysex_count;
        }
    } else if (s->remaining != 0u) {
        if (s->remaining == 1u) {
            enqueue_event(s, source, s->active_status,
                          s->have_first ? s->first_data : byte,
                          s->have_first ? byte : 0u, s->have_first ? 3u : 2u,
                          s->short_start, tick);
            s->remaining = 0; s->have_first = false;
        } else {
            s->first_data = byte; s->have_first = true; s->remaining = 1;
        }
    } else if (s->running_status != 0u) {
        if (short_type(s->running_status)) {
            enqueue_event(s, source, s->running_status, byte, 0, 2, tick, tick);
        } else {
            s->active_status = s->running_status; s->first_data = byte;
            s->have_first = true; s->remaining = 1; s->short_start = tick;
        }
    }
    return MIDI_PARSER_OK;
}

midi_parser_result_t midi_parser_flush(midi_parser_t *p, uint8_t mask)
{
    unsigned source;
    if (!p || (mask & 0xf0u)) return MIDI_PARSER_BAD_ARGUMENT;
    for (source = 0; source < MIDI_PARSER_SOURCE_COUNT; ++source) {
        if (mask & (1u << source)) {
            clear_source(&p->sources[source]);
            if (p->event_held && p->event_source == source) p->event_held = false;
            if (p->sysex_held && p->sysex_source == source) p->sysex_held = false;
        }
    }
    return MIDI_PARSER_OK;
}

static bool select_event(midi_parser_t *p)
{
    unsigned i;
    if (!p) return false;
    if (p->event_held) return true;
    for (i = 0; i < MIDI_PARSER_SOURCE_COUNT; ++i) {
        unsigned source = (unsigned)p->event_rr + i;
        if (source >= MIDI_PARSER_SOURCE_COUNT) source -= MIDI_PARSER_SOURCE_COUNT;
        if (p->sources[source].event_count) {
            p->event_source = (uint8_t)source; p->event_held = true;
            return true;
        }
    }
    return false;
}

static bool select_sysex(midi_parser_t *p)
{
    unsigned i;
    if (!p) return false;
    if (p->sysex_held) return true;
    for (i = 0; i < MIDI_PARSER_SOURCE_COUNT; ++i) {
        unsigned source = (unsigned)p->sysex_rr + i;
        if (source >= MIDI_PARSER_SOURCE_COUNT) source -= MIDI_PARSER_SOURCE_COUNT;
        if (p->sources[source].sysex_queued) {
            p->sysex_source = (uint8_t)source; p->sysex_held = true;
            return true;
        }
    }
    return false;
}

static void copy_event(control_midi_event_t *out, const control_midi_event_t *in)
{
    out->source = in->source; out->status = in->status;
    out->data1 = in->data1; out->data2 = in->data2; out->nbytes = in->nbytes;
    out->seq = in->seq; out->start_tick = in->start_tick; out->end_tick = in->end_tick;
}

static void copy_sysex(control_sysex_chunk_t *out, const control_sysex_chunk_t *in)
{
    out->source = in->source;
    out->data[0] = in->data[0]; out->data[1] = in->data[1]; out->data[2] = in->data[2];
    out->count = in->count; out->first = in->first; out->last = in->last;
    out->aborted = in->aborted; out->seq = in->seq;
    out->start_tick = in->start_tick; out->end_tick = in->end_tick;
}

bool midi_parser_peek_event(midi_parser_t *p, control_midi_event_t *out)
{
    midi_parser_source_t *s;
    if (!out || !select_event(p)) return false;
    s = &p->sources[p->event_source]; copy_event(out, &s->events[s->event_head]);
    return true;
}

bool midi_parser_pop_event(midi_parser_t *p, control_midi_event_t *out)
{
    midi_parser_source_t *s;
    if (!select_event(p)) return false;
    s = &p->sources[p->event_source];
    if (out) copy_event(out, &s->events[s->event_head]);
    s->event_head = advance(s->event_head); --s->event_count;
    p->event_rr = (uint8_t)((p->event_source + 1u) % MIDI_PARSER_SOURCE_COUNT);
    p->event_held = false;
    return true;
}

bool midi_parser_peek_sysex(midi_parser_t *p, control_sysex_chunk_t *out)
{
    midi_parser_source_t *s;
    if (!out || !select_sysex(p)) return false;
    s = &p->sources[p->sysex_source]; copy_sysex(out, &s->sysex[s->sysex_head]);
    return true;
}

bool midi_parser_pop_sysex(midi_parser_t *p, control_sysex_chunk_t *out)
{
    midi_parser_source_t *s;
    if (!select_sysex(p)) return false;
    s = &p->sources[p->sysex_source];
    if (out) copy_sysex(out, &s->sysex[s->sysex_head]);
    s->sysex_head = advance(s->sysex_head); --s->sysex_queued;
    p->sysex_rr = (uint8_t)((p->sysex_source + 1u) % MIDI_PARSER_SOURCE_COUNT);
    p->sysex_held = false;
    return true;
}
