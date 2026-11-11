// Copyright (c) 2026 ICECREAM1230
// event_bus.h — tiny publish/subscribe fan-out for inter-task events
//
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
//
// Best-effort consumers subscribe a FreeRTOS evt_t queue. Musical consumers
// use a reliable bounded ring with expression merging and a panic latch.
// Publishers never block on a slow consumer; subscriber policy decides
// whether excess traffic is dropped, merged or conservatively silenced.
//
// Consumers then run a single event loop on their own queue — all
// their state stays inside that loop, no locks needed.
//
// The subscriber table is static configuration done at boot (before
// tasks start publishing), so the table itself needs no locking.
// Drop counters are 32-bit and only observed asynchronously, which is
// benign.

#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "midi_parser.h"
#include "control_source.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_BUS_MAX_SUBSCRIBERS 4

typedef enum {
    EVT_MIDI = 1,
    EVT_WEB,     // bounded semantic commands for voice owner
    EVT_PANIC,   // conservative all-sound-off after source loss/overflow
    EVT_WEB_V2,  // full-width client fields with a voice-owner context fence
    EVT_DDS_PRESET, // job ID only; payload/IO owned by the preset service
    EVT_WEB_RAW, // job ID in web_v2.a; voice owner revalidates before enqueue
    EVT_RESERVED_7, // retired additive job; preserve later event identifiers
    EVT_WEB_KS_TIMBRE, // immutable KS settings reservation; identity in web_v2.c
    EVT_WEB_REVERB, // immutable parameter pair; job identity in web_v2.c
    EVT_WEB_FDC, // immutable bounded FDC control request; no MIDI/voice owner
} evt_kind_t;

typedef enum { WEB_START = 1, WEB_CC, WEB_KEYS_UPDATE, WEB_STOP, WEB_ENGINE, WEB_KS_DECAY } web_op_t;
typedef enum { MIDI_SOURCE_DEFAULT, MIDI_SOURCE_BLE, MIDI_SOURCE_DIN, MIDI_SOURCE_PANEL, MIDI_SOURCE_STRESS } midi_source_id_t;
typedef struct {
    int64_t expires;
    uint32_t token, sequence, a;
    uint8_t client, op, b;
} web_event_t;

typedef struct {
    int64_t expires;
    uint32_t token, sequence, context_epoch, a, b, c;
    uint8_t client, op;
} web_v2_event_t;

// Tagged union: extend with new kinds and members as the system grows.
typedef struct {
    evt_kind_t kind;
    uint8_t midi_source;
    control_source_ref_t source; // transport fence; independent of MIDI source
    union {
        midi_message_t midi;
        web_event_t web;
        web_v2_event_t web_v2;
        uint32_t dds_id;
    };
} evt_t;

// Call once at boot, before any subscribe/publish.
void event_bus_init(void);

// Register a consumer queue. Returns the subscriber id (>= 0) or -1
// if no subscriber slot is free.
int event_bus_subscribe(QueueHandle_t queue);

// Fan out without blocking. Best-effort queue-full events are counted as
// dropped; reliable subscribers preserve transitions or force panic.
void event_bus_publish(const evt_t *evt);

// Voice consumers: merge consecutive expression updates, evict expression
// before losing a note, and latch panic if note-only capacity is exhausted.
// The supplied queue is used only to wake event_bus_receive().
int event_bus_subscribe_reliable(QueueHandle_t wake_queue);
BaseType_t event_bus_receive(int sub_id, evt_t *evt, TickType_t wait);

// Number of events dropped for a subscriber since the last reset.
uint32_t event_bus_dropped(int sub_id);

void event_bus_reset_dropped(int sub_id);

#ifdef __cplusplus
}
#endif
