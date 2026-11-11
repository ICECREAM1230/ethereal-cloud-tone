#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "midi_parser.h"
typedef struct { uint8_t bytes[517]; size_t length,capacity; uint64_t epoch; } midi_feedback_packet_t;
void midi_feedback_packet_begin(midi_feedback_packet_t *packet,size_t capacity,uint64_t epoch,uint16_t timestamp_ms);
bool midi_feedback_packet_add(midi_feedback_packet_t *packet,const midi_message_t *message,uint64_t epoch,uint16_t timestamp_ms);
bool midi_feedback_packet_has_messages(const midi_feedback_packet_t *packet);
const uint8_t *midi_feedback_packet_bytes(const midi_feedback_packet_t *packet);
size_t midi_feedback_packet_length(const midi_feedback_packet_t *packet);
