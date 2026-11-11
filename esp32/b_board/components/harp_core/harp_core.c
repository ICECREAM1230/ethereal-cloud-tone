#include "harp_core.h"
#include <string.h>

static int clamp(int64_t v, int low, int high)
{
    return v < low ? low : (v > high ? high : (int)v);
}

bool harp_set_mapping(harp_model_t *m,const uint8_t notes[12],const uint16_t gains[12])
{
    if(!m || !notes || !gains) return false;
    bool used[128]={0};
    for(unsigned i=0;i<HARP_LASERS;i++) {
        if(notes[i]>127 || used[notes[i]] || gains[i]<25 || gains[i]>200) return false;
        used[notes[i]]=true;
    }
    memcpy(m->note_map,notes,sizeof(m->note_map));
    memcpy(m->velocity_gain,gains,sizeof(m->velocity_gain));
    return true;
}
int harp_effective_note(const harp_model_t *m,unsigned beam)
{
    if(!m || beam>=HARP_LASERS) return -1;
    int note=(int)m->note_map[beam]+m->transpose;
    return note>=0 && note<128 ? note : -1;
}

bool harp_config_valid(const harp_config_t *c)
{
    return c && !(c->channel > 15 || c->first_note < 24 || c->first_note > 92 ||
        !c->velocity_min || c->velocity_min>127 || !c->velocity_max || c->velocity_max > 127 ||
        c->duration_min_ms >= c->duration_max_ms || !c->gate_ms ||
        (c->encoder_edges != 2 && c->encoder_edges != 4));
}

bool harp_init(harp_model_t *m, const harp_config_t *c)
{
    if (!m || !harp_config_valid(c)) return false;
    memset(m, 0, sizeof(*m));
    m->config = *c;
    m->volume = 100;
    m->cutoff = 127;
    for(unsigned i=0;i<HARP_LASERS;i++) {
        m->note_map[i]=(uint8_t)(c->first_note+i);
        m->velocity_gain[i]=100;
    }
    return true;
}

void harp_reset_inputs(harp_model_t *m)
{
    memset(m->laser, 0, sizeof(m->laser));
    memset(m->button, 0, sizeof(m->button));
    memset(m->encoder_accum, 0, sizeof(m->encoder_accum));
    m->sampled = false;
    m->encoder_seen = 0;
    m->active_beams = 0;
    m->playing_beams = 0;
}

void harp_panic(harp_model_t *m)
{
    m->active_beams = 0;
    m->playing_beams = 0;
    ++m->panic_serial;
    for (unsigned i = 0; i < HARP_LASERS; ++i) m->laser[i].armed = false;
}

void harp_turn(harp_model_t *m, unsigned encoder, int steps)
{
    switch (encoder) {
    case 0: m->transpose = clamp((int64_t)m->transpose + steps, -24, 24); break;
    case 1: m->volume = (uint8_t)clamp((int64_t)m->volume + steps, 0, 127); break;
    case 2: m->cutoff = (uint8_t)clamp((int64_t)m->cutoff + steps, 0, 127); break;
    case 3: m->resonance = (uint8_t)clamp((int64_t)m->resonance + steps, 0, 127); break;
    default: break;
    }
}

static bool filter(harp_filter_t *f, bool raw, uint32_t now, uint16_t delay)
{
    if (raw != f->candidate) { f->candidate = raw; f->changed = now; }
    if ((uint32_t)(now - f->changed) < delay) return false;
    bool changed = f->stable != raw;
    f->stable = raw;
    return changed;
}

static void trigger_beam(harp_model_t *m,unsigned i,uint32_t duration,uint32_t now,bool synthetic)
{
    const harp_config_t *c=&m->config;
    uint32_t clipped=(uint32_t)clamp(duration,c->duration_min_ms,c->duration_max_ms);
    uint32_t span=c->duration_max_ms-c->duration_min_ms;
    m->last_duration_ms[i]=duration;
    uint32_t offset=clipped-c->duration_min_ms;
    /* Positive endpoint weights keep reverse curves symmetric at half steps. */
    uint32_t mapped=((span-offset)*c->velocity_min+offset*c->velocity_max+span/2)/span;
    m->velocity[i]=(uint8_t)clamp((mapped*m->velocity_gain[i]+50)/100,1,127);
    m->last_synthetic[i]=synthetic;
    ++m->hit_count[i];
    m->playing_beams |= (uint16_t)(1u<<i);
    m->note_deadline[i]=now+c->gate_ms;
    if (++m->trigger_serial==0) ++m->trigger_serial;
    m->note_serial[i]=m->trigger_serial;
}

bool harp_debug_strike(harp_model_t *m,unsigned beam,uint32_t duration,uint32_t now)
{
    if(!m || beam>=HARP_LASERS || duration<1 || duration>60000) return false;
    trigger_beam(m,beam,duration,now,true);
    return true;
}

void harp_sample(harp_model_t *m, uint16_t levels, const uint8_t ab[4],
                 uint8_t pressed, uint8_t enabled, uint32_t now)
{
    static const int8_t direction[16] = {0,1,-1,0, -1,0,0,1, 1,0,0,-1, 0,-1,1,0};
    for (unsigned i = 0; i < HARP_LASERS; ++i) {
        uint16_t bit=(uint16_t)(1u<<i);
        if ((m->playing_beams & bit) && (int32_t)(now-m->note_deadline[i])>=0)
            m->playing_beams &= (uint16_t)~bit;
        bool high = (levels & (1u << i)) != 0;
        bool broken = high == m->config.laser_active_high;
        harp_filter_t *f = &m->laser[i];
        if (!m->sampled) { f->candidate = broken; f->changed = now; }
        filter(f, broken, now, m->config.laser_debounce_ms);
        if ((uint32_t)(now - f->changed) < m->config.laser_debounce_ms) continue;
        if (!f->stable) {
            /* Measure the qualified raw edges, not the delayed scan callbacks. */
            if (m->active_beams & bit) trigger_beam(m,i,f->changed-m->shade_started[i],now,false);
            f->armed = true;
            m->active_beams &= (uint16_t)~bit;
        } else if (f->armed) {
            if (!(m->active_beams & bit)) m->shade_started[i]=f->changed;
            m->active_beams |= bit;
        }
    }
    m->sampled = true;
    m->encoder_seen &= enabled;
    for (unsigned i = 0; i < HARP_ENCODERS; ++i) {
        if (!(enabled & (1u << i))) continue;
        uint8_t state = ab[i] & 3u;
        harp_filter_t *b = &m->button[i];
        bool pushed = (pressed & (1u << i)) != 0;
        if (!(m->encoder_seen & (1u << i))) {
            m->encoder_ab[i] = state;
            m->encoder_accum[i] = 0;
            b->candidate = pushed; b->stable = false; b->changed = now;
            m->encoder_seen |= (uint8_t)(1u << i);
        } else {
            uint8_t old = m->encoder_ab[i];
            m->encoder_ab[i] = state;
            if ((old ^ state) == 3u) {
                ++m->illegal_edges;
                m->encoder_accum[i] = 0;
            } else {
                m->encoder_accum[i] += direction[(old << 2) | state];
                int edges = m->config.encoder_edges;
                if (m->encoder_accum[i] >= edges || m->encoder_accum[i] <= -edges) {
                    int step = m->encoder_accum[i] > 0 ? 1 : -1;
                    if (m->config.reverse_mask & (1u << i)) step = -step;
                    harp_turn(m, i, step);
                    m->encoder_accum[i] = 0;
                }
            }
        }
        if (filter(b, pushed, now, m->config.button_debounce_ms) && b->stable) {
            if (i == 0) m->fine_cents = clamp(m->fine_cents - 1, -100, 100);
            else if (i == 1) m->fine_cents = clamp(m->fine_cents + 1, -100, 100);
            else if (i == 2) { m->cutoff = 127; m->resonance = 0; }
            else harp_panic(m);
        }
    }
}

void harp_target(const harp_model_t *m, harp_target_t *t)
{
    memset(t, 0, sizeof(*t));
    t->channel = m->config.channel;
    t->volume = m->volume; t->cutoff = m->cutoff; t->resonance = m->resonance;
    int cents = clamp(m->fine_cents, -100, 100);
    t->bend = (uint16_t)(8192 + (cents * 8192 + (cents < 0 ? -100 : 100)) / 200);
    t->panic_serial = m->panic_serial;
    for (unsigned i = 0; i < HARP_LASERS; ++i)
        if (m->playing_beams & (1u << i)) {
            int note=harp_effective_note(m,i);
            if(note<0) continue;
            t->notes[note] = true;
            t->velocity[note] = m->velocity[i];
            t->note_serial[note] = m->note_serial[i];
        }
}

static bool valid_midi(const harp_midi_t *m)
{
    if (!m || m->data[0] < 0x80 || m->data[0] >= 0xf0) return false;
    uint8_t type = m->data[0] >> 4;
    unsigned length = type == 0xc || type == 0xd ? 2 : 3;
    if (m->length != length) return false;
    for (unsigned i = 1; i < length; ++i) if (m->data[i] > 127) return false;
    return true;
}

bool harp_usb_encode(const harp_midi_t *m, uint8_t p[4])
{
    if (!p || !valid_midi(m)) return false;
    p[0] = m->data[0] >> 4;
    p[1] = m->data[0]; p[2] = m->data[1];
    p[3] = m->length == 3 ? m->data[2] : 0;
    return true;
}

size_t harp_ble_encode(const harp_midi_t *m, uint32_t now, uint8_t p[5])
{
    if (!p || !valid_midi(m)) return 0;
    uint32_t ts = now & 0x1fff;
    p[0] = 0x80 | (uint8_t)(ts >> 7);
    p[1] = 0x80 | (uint8_t)(ts & 0x7f);
    memcpy(p + 2, m->data, m->length);
    return m->length + 2u;
}

unsigned harp_route_mask(harp_route_t route, bool usb, bool ble)
{
    unsigned available = (usb ? 1u : 0u) | (ble ? 2u : 0u);
    switch (route) {
    case HARP_ROUTE_AUTO: return usb ? 1u : (ble ? 2u : 0u);
    case HARP_ROUTE_USB: return available & 1u;
    case HARP_ROUTE_BLE: return available & 2u;
    case HARP_ROUTE_BOTH: return available;
    default: return 0;
    }
}

void harp_sink_init(harp_sink_t *s)
{
    memset(s, 0, sizeof(*s));
    for (unsigned i = 0; i < 4; ++i) s->parameter[i] = -1;
}

enum { EFFECT_RESET, EFFECT_NOTE, EFFECT_PARAMETER };

static bool plan_message(harp_sink_t *s, const harp_target_t *t, bool enabled)
{
    if (!s->initialized) {
        if (!enabled) return false;
        s->initialized = true; s->phase = 0; s->panic_seen = t->panic_serial;
    } else if (s->panic_seen != t->panic_serial) {
        s->panic_seen = t->panic_serial; s->phase = 0;
    }
    s->message.length = 3;
    if (s->phase < 2) {
        s->message = (harp_midi_t){3, {(uint8_t)(0xb0 | t->channel),
                                      s->phase == 0 ? 120 : 123, 0}};
        s->effect = EFFECT_RESET; s->index = s->phase;
        return true;
    }
    /* Release old pitches before applying controllers or starting replacements. */
    for (unsigned n = 0; n < 128; ++n) {
        if (s->notes[n] && (!enabled || !t->notes[n] || s->note_serial[n]!=t->note_serial[n])) {
            s->message = (harp_midi_t){3, {(uint8_t)(0x80 | t->channel), (uint8_t)n, 0}};
            s->effect = EFFECT_NOTE; s->index = (uint8_t)n; s->value = false;
            return true;
        }
    }
    if (!enabled) return false;
    const int values[4] = {t->volume, t->cutoff, t->resonance, t->bend};
    const uint8_t cc[3] = {7, 74, 71};
    for (unsigned p = 0; p < 4; ++p) {
        if (s->parameter[p] == values[p]) continue;
        if (p < 3) s->message = (harp_midi_t){3, {(uint8_t)(0xb0 | t->channel), cc[p], (uint8_t)values[p]}};
        else s->message = (harp_midi_t){3, {(uint8_t)(0xe0 | t->channel),
                                          (uint8_t)(t->bend & 127), (uint8_t)(t->bend >> 7)}};
        s->effect = EFFECT_PARAMETER; s->index = (uint8_t)p; s->value = values[p];
        return true;
    }
    for (unsigned n = 0; n < 128; ++n) {
        if (t->notes[n] && !s->notes[n]) {
            s->message = (harp_midi_t){3, {(uint8_t)(0x90 | t->channel), (uint8_t)n, t->velocity[n]}};
            s->effect = EFFECT_NOTE; s->index = (uint8_t)n; s->value = true;
            s->pending_serial = t->note_serial[n];
            return true;
        }
    }
    return false;
}

static void acknowledge(harp_sink_t *s)
{
    if (s->effect == EFFECT_RESET) {
        if (s->index == 0) {
            memset(s->notes, 0, sizeof(s->notes));
            for (unsigned i = 0; i < 4; ++i) s->parameter[i] = -1;
        }
        s->phase = s->index + 1;
    } else if (s->effect == EFFECT_NOTE) {
        s->notes[s->index] = s->value != 0;
        if(s->value) s->note_serial[s->index] = s->pending_serial;
    }
    else s->parameter[s->index] = s->value;
    s->pending = false;
    ++s->sent;
}

unsigned harp_sink_pump(harp_sink_t *s, const harp_target_t *t, bool enabled, bool ready,
                        uint32_t epoch, unsigned budget, harp_send_fn send, void *ctx)
{
    if (!ready || !s->online || s->epoch != epoch) {
        uint32_t sent = s->sent, retries = s->retries;
        harp_sink_init(s); s->sent = sent; s->retries = retries;
        s->online = ready; s->epoch = epoch;
        if (!ready) return 0;
    }
    unsigned accepted = 0;
    while (accepted < budget) {
        if (!s->pending) {
            if (!plan_message(s, t, enabled)) break;
            s->pending = true;
        }
        /* An asynchronous sender must acknowledge this exact message before it
         * can be replaced, even if desired inputs changed in the meantime. */
        if (!send(ctx, &s->message, epoch)) { ++s->retries; break; }
        acknowledge(s);
        ++accepted;
    }
    return accepted;
}

bool harp_sink_quiet(const harp_sink_t *s)
{
    if (s->pending) return false;
    for (unsigned n = 0; n < 128; ++n) if (s->notes[n]) return false;
    return true;
}
