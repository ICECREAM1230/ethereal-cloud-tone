// Copyright (c) 2026 ICECREAM1230
// event_bus.c — tiny publish/subscribe fan-out for inter-task events
//
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
//
// See event_bus.h for the design rationale.

#include "event_bus.h"
#include <string.h>

static QueueHandle_t s_subs[EVENT_BUS_MAX_SUBSCRIBERS];
static uint32_t s_dropped[EVENT_BUS_MAX_SUBSCRIBERS];
#define RELIABLE_CAPACITY 64
static struct {
    bool enabled, panic;
    unsigned count;
    evt_t events[RELIABLE_CAPACITY];
} s_reliable[EVENT_BUS_MAX_SUBSCRIBERS];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

// CCs that change protocol/voice state are ordered, never merged/evicted.
static bool expression(const evt_t *e)
{
    if (e->kind != EVT_MIDI) return false;
    unsigned type = e->midi.status & 0xF0;
    if (type == 0xE0 || type == 0xD0 || type == 0xA0) return true;
    if (type != 0xB0) return false;
    unsigned cc = e->midi.data[0];
    return cc != 0 && cc != 32 && cc != 6 && cc != 38 && cc != 64 && cc != 66 && cc != 67 &&
           cc != 96 && cc != 97 && cc != 98 && cc != 99 &&
           cc != 100 && cc != 101 && cc < 120;
}
static bool same_expression(const evt_t *a, const evt_t *b)
{
    return expression(a) && expression(b) && a->midi_source==b->midi_source && a->midi.status == b->midi.status &&
        ((a->midi.status & 0xF0) != 0xB0 || a->midi.data[0] == b->midi.data[0]) &&
        ((a->midi.status & 0xF0) != 0xA0 || a->midi.data[0] == b->midi.data[0]);
}
static void reliable_publish(int i, const evt_t *evt)
{
    portENTER_CRITICAL(&s_lock);
    unsigned n = s_reliable[i].count;
    if (evt->kind == EVT_PANIC) {
        s_reliable[i].panic = true;
        s_reliable[i].count = 0;
    } else if (n && same_expression(&s_reliable[i].events[n-1], evt)) {
        s_reliable[i].events[n-1] = *evt;
    } else {
        if (n == RELIABLE_CAPACITY) {
            unsigned victim = 0;
            while (victim < n && !expression(&s_reliable[i].events[victim])) victim++;
            if (victim < n) {
                memmove(&s_reliable[i].events[victim], &s_reliable[i].events[victim+1],
                        (n-victim-1) * sizeof(evt_t));
                n--;
            } else if (expression(evt)) {
                s_dropped[i]++;
                portEXIT_CRITICAL(&s_lock);
                return;
            } else {
                // No key transition can be safely evicted. Silence all old
                // voices before accepting any transition of the new stream.
                s_reliable[i].panic = true;
                n = 0;
            }
            s_dropped[i]++;
        }
        s_reliable[i].events[n++] = *evt;
        s_reliable[i].count = n;
    }
    portEXIT_CRITICAL(&s_lock);
    // Wake is only a hint; the ring/latch is authoritative, so a full wake
    // queue cannot discard a note or panic.
    xQueueSend(s_subs[i], evt, 0);
}
int event_bus_subscribe_reliable(QueueHandle_t queue)
{
    int i = event_bus_subscribe(queue);
    if (i >= 0) s_reliable[i].enabled = true;
    return i;
}
BaseType_t event_bus_receive(int i, evt_t *evt, TickType_t wait)
{
    if (i < 0 || i >= EVENT_BUS_MAX_SUBSCRIBERS) return pdFALSE;
    if (!s_reliable[i].enabled) return xQueueReceive(s_subs[i], evt, wait);
    // Check before waiting as notifications may have been consumed while
    // more than one event remained in the authoritative ring.
    for (int attempt = 0; attempt < 2; attempt++) {
        portENTER_CRITICAL(&s_lock);
        if (s_reliable[i].panic) {
            s_reliable[i].panic = false;
            *evt = (evt_t){.kind = EVT_PANIC};
            portEXIT_CRITICAL(&s_lock);
            return pdTRUE;
        }
        unsigned n = s_reliable[i].count;
        if (n) {
            *evt = s_reliable[i].events[0];
            memmove(s_reliable[i].events, s_reliable[i].events+1, (n-1)*sizeof(evt_t));
            s_reliable[i].count--;
            portEXIT_CRITICAL(&s_lock);
            return pdTRUE;
        }
        portEXIT_CRITICAL(&s_lock);
        if (attempt == 0 && xQueueReceive(s_subs[i], evt, wait) != pdTRUE) break;
    }
    return pdFALSE;
}

void event_bus_init(void)
{
    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        s_subs[i] = NULL;
        s_dropped[i] = 0;
        memset(&s_reliable[i], 0, sizeof(s_reliable[i]));
    }
}

int event_bus_subscribe(QueueHandle_t queue)
{
    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        if (s_subs[i] == NULL) {
            s_subs[i] = queue;
            return i;
        }
    }
    return -1;
}

void event_bus_publish(const evt_t *evt)
{
    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        QueueHandle_t q = s_subs[i];
        if (q == NULL) {
            continue;
        }
        if (s_reliable[i].enabled) {
            reliable_publish(i, evt);
        } else if (xQueueSend(q, evt, 0) != pdTRUE) {
            s_dropped[i]++;
        }
    }
}

uint32_t event_bus_dropped(int sub_id)
{
    if (sub_id < 0 || sub_id >= EVENT_BUS_MAX_SUBSCRIBERS) {
        return 0;
    }
    return s_dropped[sub_id];
}

void event_bus_reset_dropped(int sub_id)
{
    if (sub_id >= 0 && sub_id < EVENT_BUS_MAX_SUBSCRIBERS) {
        s_dropped[sub_id] = 0;
    }
}
