/* SPDX-License-Identifier: MIT
 * Portable C port of the frozen Lets-go midi_parser.sv / midi_hub.sv.
 * See ../docs/parser-port.md for source attribution and integration rules.
 */
#ifndef CONTROL_MIDI_PARSER_H
#define CONTROL_MIDI_PARSER_H

#include <stdbool.h>
#include <stdint.h>
#include "control_event.h"

#define MIDI_PARSER_SOURCE_COUNT 3u
#ifndef MIDI_PARSER_QUEUE_DEPTH
#define MIDI_PARSER_QUEUE_DEPTH 8u
#endif
#if MIDI_PARSER_QUEUE_DEPTH < 1 || MIDI_PARSER_QUEUE_DEPTH > 255
#error "MIDI_PARSER_QUEUE_DEPTH must be in 1..255"
#endif

typedef enum {
    MIDI_PARSER_OK = 0,
    MIDI_PARSER_WOULD_BLOCK = 1,
    MIDI_PARSER_BAD_SOURCE = 2,
    MIDI_PARSER_BAD_ARGUMENT = 3
} midi_parser_result_t;

/* Public storage for static allocation only; fields are private to the parser.
 * Source IDs are USB=0, Web=1, BLE=2. Sensor source 3 is never MIDI.
 */
typedef struct {
    control_midi_event_t events[MIDI_PARSER_QUEUE_DEPTH];
    control_sysex_chunk_t sysex[MIDI_PARSER_QUEUE_DEPTH];
    uint64_t short_start, sysex_start;
    uint32_t next_seq;
    uint8_t running_status, active_status, first_data, remaining;
    uint8_t sysex_buffer[3], sysex_count;
    uint8_t event_head, event_count, sysex_head, sysex_queued;
    bool have_first, in_sysex, sysex_first;
} midi_parser_source_t;

typedef struct {
    midi_parser_source_t sources[MIDI_PARSER_SOURCE_COUNT];
    uint8_t event_rr, sysex_rr, event_source, sysex_source;
    bool event_held, sysex_held;
} midi_parser_t;

/* Single owner API: serialize push, flush, peek and pop in the control loop.
 * No allocation, locks, callbacks, tick conversion or transport dependency.
 */
void midi_parser_init(midi_parser_t *parser);
midi_parser_result_t midi_parser_push(midi_parser_t *parser, uint8_t source,
                                    uint8_t byte, uint64_t tick);
/* Bits 0..2 cancel protocol state and all queued/held output for that source.
 * Bit 3 is accepted but ignored (native sensor); bits 4..7 are invalid.
 * Sequence counters survive flush. No synthetic SysEx abort is emitted.
 */
midi_parser_result_t midi_parser_flush(midi_parser_t *parser, uint8_t mask);
/* Independent round-robin lanes. A successful peek locks the selected head
 * until pop or matching flush. out must be non-null for peek; pop may use a
 * null out to acknowledge an already submitted item. Pop also works sans peek.
 */
bool midi_parser_peek_event(midi_parser_t *parser, control_midi_event_t *out);
bool midi_parser_pop_event(midi_parser_t *parser, control_midi_event_t *out);
bool midi_parser_peek_sysex(midi_parser_t *parser, control_sysex_chunk_t *out);
bool midi_parser_pop_sysex(midi_parser_t *parser, control_sysex_chunk_t *out);

#endif
