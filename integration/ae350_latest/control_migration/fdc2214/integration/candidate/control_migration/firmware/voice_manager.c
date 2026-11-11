/* SPDX-License-Identifier: MIT */
#include "voice_manager.h"

/* Preserve each frozen RTL owner's capabilities. In particular the DDS
 * performance block has no CC7/10/11 path, and KS has no pitch-bend path. */
static const vm_engine_caps_t caps[] = {
    {32, 1, 0, 0, 1, 1, 0},
    {16, 0, 1, 19, 1, 0, 1},
    {8, 1, 1, 0, 0, 0, 0}
};
enum action { A_NONE, A_START, A_RELEASE, A_KILL, A_GAIN, A_PITCH, A_EXPRESSION, A_RESET };

static void zero_bytes(void *target, size_t count)
{
    volatile unsigned char *p = (volatile unsigned char *)target;
    while (count-- != 0u) *p++ = 0;
}

static void copy_command(control_voice_command_t *to, const control_voice_command_t *from)
{
    to->engine = from->engine; to->voice = from->voice; to->kind = from->kind;
    to->source = from->source; to->channel = from->channel; to->note = from->note;
    to->velocity = from->velocity; to->volume = from->volume; to->pan = from->pan;
    to->generation = from->generation; to->pitch = from->pitch; to->aux = from->aux;
}

static void copy_channel(vm_channel_t *to, const vm_channel_t *from)
{
    to->bend = from->bend; to->volume = from->volume;
    to->expression = from->expression; to->pan = from->pan; to->sustain = from->sustain;
    to->wheel = from->wheel; to->touch_x = from->touch_x; to->touch_y = from->touch_y;
}

const vm_engine_caps_t *vm_engine_caps(uint8_t engine)
{
    return engine < 3u ? &caps[engine] : NULL;
}

static int valid_manager(const voice_manager_t *m)
{
    return m != NULL && m->engine < 3u &&
           m->voice_count == caps[m->engine].voices &&
           m->queue_limit > 0u && m->queue_limit <= VM_QUEUE_CAPACITY &&
           m->queue_count <= m->queue_limit;
}

static void channel_default(vm_channel_t *c)
{
    c->bend = 8192; c->volume = 127; c->expression = 127;
    c->pan = 64; c->sustain = 0;
    c->wheel = 0; c->touch_x = 0; c->touch_y = 0;
}

vm_result_t vm_init(voice_manager_t *m, uint8_t engine, uint8_t queue_limit)
{
    size_t i;
    if (m == NULL || engine >= 3u || queue_limit > VM_QUEUE_CAPACITY)
        return VM_INVALID;
    /* The isolated AE350 toolchain has no libc headers/runtime. Volatile
     * stores also prevent a compiler loop-pattern pass inventing memset. */
    zero_bytes(m, sizeof(*m));
    m->engine = engine; m->voice_count = caps[engine].voices;
    m->queue_limit = queue_limit != 0u ? queue_limit : VM_QUEUE_CAPACITY;
    m->adapter_ready = (uint8_t)!caps[engine].requires_template;
    for (i = 0; i < VM_CHANNELS; ++i) channel_default(&m->channels[i]);
    return VM_OK;
}

vm_result_t vm_enable_extended(voice_manager_t *m)
{
    unsigned i;
    if (!valid_manager(m) || m->queue_count) return VM_NOT_READY;
    for (i = 0; i < m->voice_count; ++i)
        if (m->voices[i].state != VM_IDLE || m->voices[i].backend_live) return VM_NOT_READY;
    m->extended = 1;
    return VM_OK;
}

vm_result_t vm_set_adapter_ready(voice_manager_t *m, uint8_t ready)
{
    unsigned i;
    if (!valid_manager(m) || ready > 1u) return VM_INVALID;
    if (!ready) {
        if (m->queue_count != 0u) return VM_NOT_READY;
        for (i = 0; i < m->voice_count; ++i)
            if (m->voices[i].state != VM_IDLE || m->voices[i].backend_live)
                return VM_NOT_READY;
    }
    m->adapter_ready = ready;
    return VM_OK;
}

static unsigned owner_index(const vm_voice_t *v)
{
    return (unsigned)v->source * 16u + v->channel;
}

static uint32_t pitch_for(const voice_manager_t *m, uint8_t note, uint16_t bend)
{
    int32_t delta;
    if (!caps[m->engine].has_bend) return (uint32_t)note << 16;
    delta = ((int32_t)bend - 8192) * 2; /* Existing default two-semitone range. */
    if (m->engine == VM_SUBTRACTIVE) {
        /* RTL truncates signed product/96 in its pitch14 (1024/octave)
         * domain before applying the bend. 768 Q16 units = one pitch14. */
        delta = (delta / 96) * 768;
    } else delta *= 8; /* FM exactly (bend - 8192) * 16, no quantization. */
    return (uint32_t)note * 65536u + (uint32_t)delta;
}

static int legal_pitch(const voice_manager_t *m, uint32_t pitch)
{
    return m->engine != VM_FM ||
           (pitch >= UINT32_C(0x000d0000) && pitch <= UINT32_C(0x006cfc00));
}

static void command_for(const voice_manager_t *m, unsigned slot, uint8_t kind,
                        control_voice_command_t *c)
{
    const vm_voice_t *v = &m->voices[slot];
#if ENABLE_AE350_FDC
    if (v->source == 3u) {
        c->engine=m->engine; c->voice=(uint8_t)slot; c->kind=kind;
        c->source=3; c->channel=0; c->note=69; c->velocity=127;
        c->volume=127; c->pan=64; c->generation=v->generation;
        c->pitch=v->pitch; c->aux=0;
        return; /* source3 must never address channels[48]. */
    }
#endif
    const vm_channel_t *ch = &m->channels[owner_index(v)];
    c->engine = m->engine; c->voice = (uint8_t)slot; c->kind = kind;
    c->source = v->source; c->channel = v->channel; c->note = v->note;
    c->velocity = v->velocity; c->generation = v->generation; c->pitch = v->pitch;
    c->volume = (uint8_t)(((uint16_t)ch->volume * ch->expression) / 127u);
    c->pan = ch->pan;
    c->aux = m->extended && m->engine == VM_SUBTRACTIVE && kind == VM_ON ?
        (uint16_t)(0x8000u | ch->bend) : 0;
    if (kind == VM_SUB_BEND) c->pitch = ch->bend;
    if (kind == VM_SUB_EXPRESSION)
        c->pitch = (uint32_t)ch->wheel | ((uint32_t)ch->touch_x << 8) | ((uint32_t)ch->touch_y << 15);
}

static int queued_on(const voice_manager_t *m, unsigned voice)
{
    unsigned i;
    for (i = 0; i < m->queue_count; ++i)
        if (m->commands[i].voice == voice && m->commands[i].kind == VM_ON &&
            m->commands[i].generation == m->voices[voice].generation) return 1;
    return 0;
}

static int remove_command(const voice_manager_t *m,
                          const control_voice_command_t *c, uint8_t action)
{
    if (action == A_START || action == A_RELEASE || action == A_KILL) return 1;
    return c->generation == m->voices[c->voice].generation &&
           ((action == A_GAIN && c->kind == VM_GAIN) ||
            (action == A_PITCH && (c->kind == VM_PITCH || c->kind == VM_SUB_BEND)) ||
            (action == A_EXPRESSION && c->kind == VM_SUB_EXPRESSION) ||
            (action == A_RESET && (c->kind == VM_PITCH || c->kind == VM_SUB_BEND || c->kind == VM_SUB_EXPRESSION)));
}

/* Read-only reservation precedes every state mutation. There are at most
 * 32 voices and 64 queue entries; event service time has no data-dependent
 * retry loop or allocation. Gain/pitch are coalesced into an unsent On. */
static int actions_fit(const voice_manager_t *m, const uint8_t actions[VM_MAX_VOICES])
{
    unsigned count = m->queue_count, i;
    for (i = 0; i < m->queue_count; ++i)
        if (remove_command(m, &m->commands[i], actions[m->commands[i].voice])) --count;
    for (i = 0; i < m->voice_count; ++i) {
        const vm_voice_t *v = &m->voices[i];
        switch (actions[i]) {
        case A_START:
            count += v->backend_on ? 2u : 1u;
            if (m->extended && m->engine == VM_SUBTRACTIVE) ++count;
            break;
        case A_RELEASE: count += v->backend_on ? 1u : 0u; break;
        case A_KILL: count += v->backend_live ? 1u : 0u; break;
        case A_GAIN: case A_PITCH:
            if (!queued_on(m, i) && v->backend_live &&
                v->backend.generation == v->generation) ++count;
            break;
        case A_EXPRESSION:
            if (queued_on(m, i) || (v->backend_live && v->backend.generation == v->generation)) ++count;
            break;
        case A_RESET:
            if (queued_on(m, i)) ++count;
            else if (v->backend_live && v->backend.generation == v->generation) count += 2u;
            break;
        default: break;
        }
    }
    return count <= m->queue_limit;
}

static void discard_actions(voice_manager_t *m, const uint8_t actions[VM_MAX_VOICES])
{
    unsigned in, out = 0;
    for (in = 0; in < m->queue_count; ++in) {
        const control_voice_command_t *c = &m->commands[in];
        if (!remove_command(m, c, actions[c->voice])) {
            if (out != in) copy_command(&m->commands[out], c);
            ++out;
        }
    }
    m->queue_count = (uint8_t)out;
}

static void append(voice_manager_t *m, const control_voice_command_t *c)
{
    /* actions_fit has reserved the complete transaction before mutation. */
    copy_command(&m->commands[m->queue_count++], c);
}

static void restore_backend_owner(vm_voice_t *v)
{
    const control_voice_command_t *b = &v->backend;
    v->generation = b->generation; v->source = b->source; v->channel = b->channel;
    v->note = b->note; v->velocity = b->velocity; v->pitch = b->pitch;
    v->age = v->backend_age;
}

static void release_voice(voice_manager_t *m, unsigned voice, int kill)
{
    vm_voice_t *v = &m->voices[voice];
    v->key_down = 0;
    if (!v->backend_live) { v->state = VM_IDLE; return; }
    /* An unaccepted replacement may already have a newer identity. Its
     * cancellation restores the real tail's generation for idle feedback. */
    restore_backend_owner(v);
    v->state = VM_RELEASE;
    if (kill || v->backend_on) {
        control_voice_command_t c;
        copy_command(&c, &v->backend);
        c.kind = kill ? VM_KILL : VM_OFF;
        c.aux = 0;
        if (kill) c.volume = 0;
        append(m, &c);
    }
}

static void apply_updates(voice_manager_t *m,
                          const uint8_t actions[VM_MAX_VOICES])
{
    unsigned i, q;
    discard_actions(m, actions);
    for (i = 0; i < m->voice_count; ++i) {
        vm_voice_t *v = &m->voices[i];
        if (actions[i] == A_RELEASE || actions[i] == A_KILL)
            release_voice(m, i, actions[i] == A_KILL);
        else if (actions[i] == A_GAIN || actions[i] == A_PITCH ||
                 actions[i] == A_EXPRESSION || actions[i] == A_RESET) {
            uint8_t kind = actions[i] == A_GAIN ? VM_GAIN :
                actions[i] == A_EXPRESSION ? VM_SUB_EXPRESSION :
                m->extended && m->engine == VM_SUBTRACTIVE ? VM_SUB_BEND : VM_PITCH;
            int updated_on = 0;
            for (q = 0; q < m->queue_count; ++q)
                if (m->commands[q].voice == i && m->commands[q].kind == VM_ON &&
                    m->commands[q].generation == v->generation) {
                    command_for(m, i, VM_ON, &m->commands[q]); updated_on = 1;
                }
            if ((!updated_on || actions[i] == A_EXPRESSION) &&
                (updated_on || (v->backend_live && v->backend.generation == v->generation))) {
                command_for(m, i, kind, &m->commands[m->queue_count++]);
            }
            if (actions[i] == A_RESET &&
                (updated_on || (v->backend_live && v->backend.generation == v->generation)))
                command_for(m, i, VM_SUB_EXPRESSION, &m->commands[m->queue_count++]);
        }
    }
}

static unsigned pick_voice(const voice_manager_t *m)
{
    unsigned i, best = 0;
    unsigned best_rank = 3;
    for (i = 0; i < m->voice_count; ++i) {
        const vm_voice_t *v = &m->voices[i];
        unsigned rank = v->state == VM_IDLE ? 0u : v->state == VM_RELEASE ? 1u : 2u;
        if (rank < best_rank || (rank == best_rank && v->age < m->voices[best].age)) {
            best = i; best_rank = rank;
        }
    }
    return best;
}

static vm_result_t note_on(voice_manager_t *m, const control_midi_event_t *e,
                           unsigned channel)
{
    uint8_t actions[VM_MAX_VOICES];
    uint32_t pitch = pitch_for(m, e->data1, m->channels[channel].bend);
    unsigned best;
    vm_voice_t *v;
    zero_bytes(actions, sizeof(actions));
    if (e->data1 < caps[m->engine].minimum_note || !legal_pitch(m, pitch)) return VM_IGNORED;
    if (m->generation == UINT32_MAX) return VM_GENERATION_EXHAUSTED;
    best = pick_voice(m); actions[best] = A_START;
    if (!actions_fit(m, actions)) return VM_BACKPRESSURE;
    discard_actions(m, actions);
    v = &m->voices[best];
    if (v->backend_on) {
        control_voice_command_t off;
        copy_command(&off, &v->backend);
        off.kind = VM_OFF; off.aux = 0; append(m, &off);
    }
    v->generation = ++m->generation; v->age = ++m->age;
    v->source = e->source; v->channel = e->status & 15u;
    v->note = e->data1; v->velocity = e->data2; v->pitch = pitch;
    v->state = VM_HELD; v->key_down = 1;
    command_for(m, best, VM_ON, &m->commands[m->queue_count++]);
    if (m->extended && m->engine == VM_SUBTRACTIVE)
        command_for(m, best, VM_SUB_EXPRESSION, &m->commands[m->queue_count++]);
    return VM_OK;
}

static vm_result_t note_off(voice_manager_t *m, const control_midi_event_t *e,
                            unsigned channel)
{
    uint8_t actions[VM_MAX_VOICES];
    unsigned i, best = VM_MAX_VOICES;
    zero_bytes(actions, sizeof(actions));
    for (i = 0; i < m->voice_count; ++i) {
        const vm_voice_t *v = &m->voices[i];
        if (v->state == VM_HELD && v->key_down && owner_index(v) == channel &&
            v->note == e->data1 && (best == VM_MAX_VOICES || v->age < m->voices[best].age)) best = i;
    }
    if (best == VM_MAX_VOICES) return VM_IGNORED;
    /* Subtractive WAIT Off cancels preparation even if the pedal is down. */
    if (!m->channels[channel].sustain ||
        (m->engine == VM_SUBTRACTIVE && queued_on(m, best))) actions[best] = A_RELEASE;
    if (!actions_fit(m, actions)) return VM_BACKPRESSURE;
    m->voices[best].key_down = 0;
    apply_updates(m, actions);
    return VM_OK;
}

static vm_result_t controller(voice_manager_t *m, const control_midi_event_t *e,
                              unsigned channel)
{
    uint8_t actions[VM_MAX_VOICES];
    vm_channel_t changed;
    unsigned i;
    zero_bytes(actions, sizeof(actions));
    copy_channel(&changed, &m->channels[channel]);
    switch (e->data1) {
    case 1: case 71: case 74:
        if (!m->extended || m->engine != VM_SUBTRACTIVE) return VM_IGNORED;
        if (e->data1 == 1) changed.wheel = (uint8_t)(e->data2 + (e->data2 >> 6));
        else if (e->data1 == 71) changed.touch_x = (uint8_t)(e->data2 ^ 0x40u);
        else changed.touch_y = (uint8_t)(e->data2 ^ 0x40u);
        break;
    case 7: case 10: case 11:
        if (!caps[m->engine].has_gain_cc) return VM_IGNORED;
        if (e->data1 == 7) changed.volume = e->data2;
        else if (e->data1 == 10) changed.pan = e->data2;
        else changed.expression = e->data2;
        break;
    case 64: changed.sustain = (uint8_t)(e->data2 >= 64u); break;
    case 120: case 123: changed.sustain = 0; break;
    case 121:
        if (m->engine != VM_SUBTRACTIVE) return VM_IGNORED;
        changed.sustain = 0; changed.bend = 8192; break;
    default: return VM_IGNORED;
    }
    for (i = 0; i < m->voice_count; ++i) {
        const vm_voice_t *v = &m->voices[i];
        if (v->state == VM_IDLE || owner_index(v) != channel) continue;
        switch (e->data1) {
        case 1: case 71: case 74: actions[i] = A_EXPRESSION; break;
        case 7: case 10: case 11: actions[i] = A_GAIN; break;
        case 120: actions[i] = A_KILL; break;
        case 123: actions[i] = A_RELEASE; break;
        case 64: case 121:
            if (!changed.sustain && v->state == VM_HELD && !v->key_down) actions[i] = A_RELEASE;
            else if (e->data1 == 121) actions[i] = m->extended ? A_RESET : A_PITCH;
            break;
        default: break;
        }
    }
    if (e->data1 == 121 && m->extended) changed.wheel = changed.touch_x = changed.touch_y = 0;
    if (!actions_fit(m, actions)) return VM_BACKPRESSURE;
    copy_channel(&m->channels[channel], &changed);
    if (e->data1 == 121)
        for (i = 0; i < m->voice_count; ++i)
            if (owner_index(&m->voices[i]) == channel && m->voices[i].state != VM_IDLE)
                m->voices[i].pitch = pitch_for(m, m->voices[i].note, changed.bend);
    apply_updates(m, actions);
    return VM_OK;
}

static vm_result_t bend(voice_manager_t *m, const control_midi_event_t *e,
                        unsigned channel)
{
    uint8_t actions[VM_MAX_VOICES];
    uint16_t value = (uint16_t)((uint16_t)e->data2 * 128u + e->data1);
    unsigned i;
    zero_bytes(actions, sizeof(actions));
    if (!caps[m->engine].has_bend) return VM_IGNORED;
    for (i = 0; i < m->voice_count; ++i) {
        const vm_voice_t *v = &m->voices[i];
        if (v->state != VM_IDLE && owner_index(v) == channel &&
            legal_pitch(m, pitch_for(m, v->note, value))) actions[i] = A_PITCH;
    }
    if (!actions_fit(m, actions)) return VM_BACKPRESSURE;
    m->channels[channel].bend = value;
    for (i = 0; i < m->voice_count; ++i)
        if (actions[i] == A_PITCH) m->voices[i].pitch = pitch_for(m, m->voices[i].note, value);
    apply_updates(m, actions);
    return VM_OK;
}

vm_result_t vm_submit(voice_manager_t *m, const control_midi_event_t *e)
{
    unsigned channel;
    uint8_t group;
    if (!valid_manager(m) || e == NULL || e->source >= VM_MIDI_SOURCES || e->status < 128u)
        return VM_INVALID;
    group = e->status >> 4;
    if (group != 8u && group != 9u && group != 11u && group != 14u) return VM_IGNORED;
    if (e->nbytes != 3u || e->data1 >= 128u || e->data2 >= 128u) return VM_INVALID;
    if (!m->adapter_ready) return VM_NOT_READY;
    channel = (unsigned)e->source * 16u + (e->status & 15u);
    if ((group == 8u || group == 9u) && e->data1 < caps[m->engine].minimum_note) return VM_IGNORED;
    if (group == 9u && e->data2 != 0u) return note_on(m, e, channel);
    if (group == 8u || group == 9u) return note_off(m, e, channel);
    if (group == 11u) return controller(m, e, channel);
    return bend(m, e, channel);
}

static int same_command(const control_voice_command_t *a, const control_voice_command_t *b)
{
    return a->engine == b->engine && a->voice == b->voice && a->kind == b->kind &&
           a->source == b->source && a->channel == b->channel && a->note == b->note &&
           a->velocity == b->velocity && a->volume == b->volume && a->pan == b->pan &&
           a->generation == b->generation && a->pitch == b->pitch && a->aux == b->aux;
}

vm_result_t vm_command_peek(const voice_manager_t *m, control_voice_command_t *c)
{
    if (!valid_manager(m) || c == NULL) return VM_INVALID;
    if (m->queue_count == 0u) return VM_IGNORED;
    copy_command(c, &m->commands[0]);
    return VM_OK;
}

vm_result_t vm_command_consume(voice_manager_t *m, const control_voice_command_t *c)
{
    vm_voice_t *v;
    unsigned i;
    if (!valid_manager(m) || c == NULL) return VM_INVALID;
    if (m->queue_count == 0u || !same_command(c, &m->commands[0])) return VM_STALE;
    v = &m->voices[c->voice];
    if (c->kind == VM_ON) {
        if (v->generation != c->generation) return VM_STALE;
        copy_command(&v->backend, c); v->backend_on = 1; v->backend_live = 1; v->backend_age = v->age;
    } else {
        if (!v->backend_live || v->backend.generation != c->generation) return VM_STALE;
        if (c->kind == VM_OFF || c->kind == VM_KILL) v->backend_on = 0;
        else if (c->kind == VM_PITCH) v->backend.pitch = c->pitch;
#if ENABLE_AE350_FDC
        else if (c->kind == VM_SENSOR) v->backend.pitch = c->pitch;
#endif
        else if (c->kind == VM_GAIN) { v->backend.volume = c->volume; v->backend.pan = c->pan; }
        else if (c->kind == VM_SUB_BEND) v->backend.aux = (uint16_t)(0x8000u | c->pitch);
    }
    for (i = 1; i < m->queue_count; ++i) copy_command(&m->commands[i - 1u], &m->commands[i]);
    --m->queue_count;
    return VM_OK;
}

vm_result_t vm_feedback_idle(voice_manager_t *m, uint8_t voice, uint32_t generation)
{
    uint8_t actions[VM_MAX_VOICES];
    vm_voice_t *v;
    unsigned i;
    zero_bytes(actions, sizeof(actions));
    if (!valid_manager(m) || voice >= m->voice_count || generation == 0u) return VM_INVALID;
    v = &m->voices[voice];
    /* A plucked/zero-sustain envelope can finish while its key is held. The
     * real adapter arms feedback for this occupancy; its generation is the
     * authority. Legacy candidates keep their release-only contract. */
    if ((v->state != VM_RELEASE && !(m->extended && v->state == VM_HELD)) ||
        !v->backend_live || v->generation != generation ||
        v->backend.generation != generation) return VM_STALE;
    for (i = 0; i < m->queue_count; ++i)
        if (m->commands[i].voice == voice &&
            (m->commands[i].kind == VM_ON || m->commands[i].kind == VM_OFF || m->commands[i].kind == VM_KILL))
            return VM_STALE;
    actions[voice] = A_RELEASE;
    discard_actions(m, actions); /* Discard redundant queued expression on a finished tail. */
    v->state = VM_IDLE; v->key_down = 0; v->backend_live = 0; v->backend_on = 0;
    return VM_OK;
}

vm_result_t vm_flush_sources(voice_manager_t *m, uint8_t source_mask)
{
    uint8_t actions[VM_MAX_VOICES];
    unsigned i;
    zero_bytes(actions, sizeof(actions));
    if (!valid_manager(m) || (source_mask & ~7u) != 0u) return VM_INVALID;
    if (source_mask == 0u) return VM_IGNORED;
    for (i = 0; i < m->voice_count; ++i)
        if (m->voices[i].state != VM_IDLE && (source_mask & (1u << m->voices[i].source)))
            actions[i] = A_RELEASE;
    if (!actions_fit(m, actions)) return VM_BACKPRESSURE;
    for (i = 0; i < VM_CHANNELS; ++i)
        if (source_mask & (1u << (i / 16u))) channel_default(&m->channels[i]);
    apply_updates(m, actions);
    return VM_OK;
}

vm_result_t vm_reset(voice_manager_t *m)
{
    uint8_t actions[VM_MAX_VOICES];
    unsigned i;
    zero_bytes(actions, sizeof(actions));
    if (!valid_manager(m)) return VM_INVALID;
    for (i = 0; i < m->voice_count; ++i)
        if (m->voices[i].state != VM_IDLE || m->voices[i].backend_live) actions[i] = A_KILL;
    if (!actions_fit(m, actions)) return VM_BACKPRESSURE;
    for (i = 0; i < VM_CHANNELS; ++i) channel_default(&m->channels[i]);
    apply_updates(m, actions);
    return VM_OK;
}

size_t vm_pending_commands(const voice_manager_t *m)
{
    return valid_manager(m) ? m->queue_count : 0u;
}

#if ENABLE_AE350_FDC
static int sensor_values(uint16_t pitch14,uint32_t gain_q16)
{
    return pitch14<=16383u && gain_q16<=65536u;
}
static int sensor_handle(const voice_manager_t *m,const vm_sensor_handle_t *h)
{
    return h && h->voice<m->voice_count && h->generation &&
        m->voices[h->voice].source==3u &&
        m->voices[h->voice].generation==h->generation &&
        m->voices[h->voice].state==VM_HELD && m->voices[h->voice].key_down;
}
vm_result_t vm_sensor_claim(voice_manager_t *m,uint16_t pitch14,
    uint32_t gain_q16,uint8_t transport_busy,vm_sensor_handle_t *h)
{
    uint8_t actions[VM_MAX_VOICES]; unsigned slot,count;
    vm_voice_t *v;
    if (!valid_manager(m) || m->engine!=VM_SUBTRACTIVE || !h || !sensor_values(pitch14,gain_q16))
        return VM_INVALID;
    if (transport_busy || !m->adapter_ready) return VM_NOT_READY;
    if (m->generation==UINT32_MAX) return VM_GENERATION_EXHAUSTED;
    slot=pick_voice(m); zero_bytes(actions,sizeof(actions)); actions[slot]=A_START;
    /* Reuse A_START's exact cancellation and backend-off reservation, but a
     * sensor ON has no second MIDI expression job. Do not mutate to predict. */
    count=m->queue_count;
    for (unsigned i=0;i<m->queue_count;i++)
        if (remove_command(m,&m->commands[i],actions[m->commands[i].voice])) --count;
    count+=m->voices[slot].backend_on ? 2u:1u;
    if (count>m->queue_limit) return VM_BACKPRESSURE;
    discard_actions(m,actions); v=&m->voices[slot];
    if (v->backend_on) {
        control_voice_command_t off;copy_command(&off,&v->backend);
        off.kind=VM_OFF;off.aux=0;append(m,&off);
    }
    v->generation=++m->generation;v->age=++m->age;
    v->source=3;v->channel=0;v->note=69;v->velocity=127;
    v->pitch=(uint32_t)pitch14 | (gain_q16<<14);
    v->state=VM_HELD;v->key_down=1;
    command_for(m,slot,VM_ON,&m->commands[m->queue_count++]);
    h->voice=(uint8_t)slot;h->generation=v->generation;
    return VM_OK;
}
vm_result_t vm_sensor_update(voice_manager_t *m,const vm_sensor_handle_t *h,
    uint16_t pitch14,uint32_t gain_q16,uint8_t transport_busy)
{
    unsigned has_on=0,removed=0,out=0;vm_voice_t *v;
    if (!valid_manager(m) || m->engine!=VM_SUBTRACTIVE || !sensor_values(pitch14,gain_q16)) return VM_INVALID;
    if (transport_busy) return VM_NOT_READY;
    if (!sensor_handle(m,h)) return VM_STALE;
    v=&m->voices[h->voice];
    for (unsigned i=0;i<m->queue_count;i++) {
        const control_voice_command_t *c=&m->commands[i];
        if (c->voice==h->voice && c->generation==h->generation) {
            if (c->kind==VM_ON) has_on=1;
            if (c->kind==VM_SENSOR) removed++;
        }
    }
    if (!has_on && (!v->backend_live || !v->backend_on || v->backend.generation!=h->generation)) return VM_STALE;
    if (m->queue_count-removed+(!has_on)>m->queue_limit) return VM_BACKPRESSURE;
    v->pitch=(uint32_t)pitch14 | (gain_q16<<14);
    for (unsigned i=0;i<m->queue_count;i++) {
        control_voice_command_t *c=&m->commands[i];
        if (c->voice==h->voice && c->generation==h->generation && c->kind==VM_SENSOR) continue;
        if (c->voice==h->voice && c->generation==h->generation && c->kind==VM_ON) command_for(m,h->voice,VM_ON,c);
        if (out!=i) copy_command(&m->commands[out],c);
        out++;
    }
    m->queue_count=(uint8_t)out;
    if (!has_on) command_for(m,h->voice,VM_SENSOR,&m->commands[m->queue_count++]);
    return VM_OK;
}
vm_result_t vm_sensor_release(voice_manager_t *m,const vm_sensor_handle_t *h,
    uint8_t kill,uint8_t transport_busy)
{
    uint8_t actions[VM_MAX_VOICES];
    if (!valid_manager(m) || m->engine!=VM_SUBTRACTIVE || kill>1u) return VM_INVALID;
    if (transport_busy) return VM_NOT_READY;
    if (!sensor_handle(m,h)) return VM_STALE;
    zero_bytes(actions,sizeof(actions));actions[h->voice]=kill?A_KILL:A_RELEASE;
    if (!actions_fit(m,actions)) return VM_BACKPRESSURE;
    apply_updates(m,actions);return VM_OK;
}
vm_result_t vm_sensor_flush(voice_manager_t *m,uint8_t kill,uint8_t transport_busy)
{
    uint8_t actions[VM_MAX_VOICES];
    if (!valid_manager(m) || m->engine!=VM_SUBTRACTIVE || kill>1u) return VM_INVALID;
    if (transport_busy) return VM_NOT_READY;
    zero_bytes(actions,sizeof(actions));
    for (unsigned i=0;i<m->voice_count;i++)
        if (m->voices[i].source==3u &&
            (m->voices[i].state!=VM_IDLE || m->voices[i].backend_live))
            actions[i]=kill?A_KILL:A_RELEASE;
    if (!actions_fit(m,actions)) return VM_BACKPRESSURE;
    apply_updates(m,actions);return VM_OK;
}
#endif
