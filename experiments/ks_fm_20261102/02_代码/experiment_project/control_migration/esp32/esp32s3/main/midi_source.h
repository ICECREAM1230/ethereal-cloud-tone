// Source generations prevent queued/in-flight MIDI from reviving lost notes.
#pragma once
#include <stdatomic.h>
#include "event_bus.h"

typedef struct {
    atomic_uint epoch;
    atomic_bool live;
    uint8_t id;
} midi_source_t;

static inline uint32_t midi_source_snapshot(const midi_source_t *s)
{
    return atomic_load(&s->live) ? atomic_load(&s->epoch) : 0;
}
static inline bool midi_source_current(const midi_source_t *s, uint32_t epoch)
{
    return epoch && atomic_load(&s->live) && atomic_load(&s->epoch)==epoch;
}
static inline void midi_source_panic(void)
{
    const evt_t panic={.kind=EVT_PANIC}; event_bus_publish(&panic);
}
static inline void midi_source_open(midi_source_t *s)
{
    atomic_fetch_add(&s->epoch,1); atomic_store(&s->live,true);
}
static inline void midi_source_close(midi_source_t *s)
{
    atomic_store(&s->live,false); atomic_fetch_add(&s->epoch,1);
    midi_source_panic();
}
static inline void midi_source_overflow(midi_source_t *s)
{
    // Invalidate all pre-gap packets/partial running status before accepting
    // a fresh stream. Ingress overflow may have lost a NoteOff, so silence.
    atomic_fetch_add(&s->epoch,1); midi_source_panic();
}
static inline void midi_source_publish(midi_source_t *s, uint32_t epoch,
                                       const midi_message_t *message)
{
    if (!midi_source_current(s,epoch)) return;
    const evt_t event={.kind=EVT_MIDI,.midi_source=s->id,.midi=*message}; event_bus_publish(&event);
    // Source loss can race the nonblocking fan-out. If this message was
    // published after its disconnect/overflow panic, follow it with panic
    // again. This closes the race without blocking the BLE host on a mutex.
    if (!midi_source_current(s,epoch)) midi_source_panic();
}
