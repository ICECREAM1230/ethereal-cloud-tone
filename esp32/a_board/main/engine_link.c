// Copyright (c) 2026 ICECREAM1230
// Desired images → bounded shadow commit → acknowledged live gates.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
#include "engine_link.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spi_regs.h"
#include "event_bus.h"
#include "status_led.h"
#include "fm_engine.h"
#include "raw_midi.h"
#include "midi_template_link.h"
#include "fm_only_contract.h"

#define TAG "engine_link"
#define ENGINE_TASK_STACK 4096
#define ENGINE_TASK_PRIO 6
#define ENGINE_TICK_US 1000
#define ENGINE_BATCH_US 700
#define ELEM_BASE 0x2000
#define ELEM_STRIDE 64
#define PROD_BASE 0x0100
#define GATE_BASE 80
#define DIRTY_WORDS ((ENGINE_NUM_ELEMENTS * ENGINE_WORDS_PER_ELEMENT + 31) / 32)
#define PDIRTY_WORDS (ENGINE_NUM_PRODUCERS * 4 / 32)

// Producers only change desired state under the lock. The SPI owner takes a
// snapshot at a bank boundary; subsequent changes remain pending. There is
// no finite command queue to overflow during a full patch render.
static uint32_t s_desired[ENGINE_NUM_ELEMENTS][ENGINE_WORDS_PER_ELEMENT];
static uint32_t s_image[ENGINE_NUM_ELEMENTS][ENGINE_WORDS_PER_ELEMENT];
static uint32_t s_prod_desired[ENGINE_NUM_PRODUCERS][4];
static uint32_t s_prod[ENGINE_NUM_PRODUCERS][4];
static uint32_t s_prod_committed[ENGINE_NUM_PRODUCERS][4];
static uint32_t s_release_revision[ENGINE_NUM_PRODUCERS];
static uint32_t s_pending[DIRTY_WORDS], s_ppending[PDIRTY_WORDS];
static uint32_t s_dirty_now[DIRTY_WORDS], s_dirty_prev[DIRTY_WORDS];
static uint32_t s_pdirty_now[PDIRTY_WORDS], s_pdirty_prev[PDIRTY_WORDS];
static uint32_t s_bus[1024], s_bus_seq[1024], s_bus_done[1024], s_bus_dirty[32];
static struct {
    uint32_t seq, snapshot_seq;
    bool on, ready;
    uint8_t phase;             // 0 needs Off, 1 awaits completed pass
    uint16_t pass;
} s_notes[ENGINE_NUM_VOICES];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;
static bool s_ready, s_flushing, s_swap_requested;
static unsigned s_elem_cursor, s_prod_cursor, s_live_cursor, s_gate_cursor, s_note_cursor;
static uint32_t s_drops;
static unsigned s_expected_bank;
static uint32_t s_control_status;
static bool s_control_status_valid;
static bool s_control_idle, s_performance_ready, s_raw_enable_requested;
static uint32_t s_raw_reset_count;
static bool s_raw_seen;
static engine_local_state_t s_local;
static bool s_local_probed, s_local_fm_adopting, s_local_release_sent;
static bool s_local_health_fault, s_local_adopt_pending;
static bool s_local_polling;
static uint32_t s_local_owner_ack;
static bool s_backend_boot_pending;
static bool s_cpu_verified;
static int64_t s_cpu_probe_at;
#include "engine_cpu_mode.inc.h"
#include "engine_audio_routes.inc.h"
#include "fdc_control.h"
#include "engine_fdc.inc.h"

static bool local_mode_poll(void);
static void raw_enable_locked(bool enabled);

static bool control_status(uint32_t *status)
{
    bool valid=fpga_control_status(status);
    portENTER_CRITICAL(&s_lock);
    s_control_status_valid=valid;
    if(valid)s_control_status=*status;
    portEXIT_CRITICAL(&s_lock);
    return valid;
}

// A completed ENG1 route proves which backend is audible even when an idle
// backend cannot answer. Handoff, an incomplete generation and shared faults
// never grant this proof. Call with s_lock held.
static bool backend_local_quiet_locked(unsigned mode)
{
    return s_local.supported && s_local.local_active && s_local.valid &&
        !s_local_health_fault && !s_local.remote_pending && !s_local.busy && !s_local.fault &&
        s_local.ready && s_local.revision && s_local.revision==s_local.completed &&
        s_local.requested==s_local.applied && s_local.applied!=mode;
}
static bool backend_host_scope_locked(void)
{
    return !s_local.supported || (s_local.valid && s_local.remote_ready &&
        !s_local.local_active && !s_local.remote_pending);
}
static bool backend_common_alive(void)
{
    // Legacy images without the shared RAW interface retain conservative
    // recovery. A backend-only read failure must not mask a dead SPI bus.
    if(CONFIG_FPGA_FM_ONLY || (!s_raw_seen && !s_local.supported))return false;
    uint32_t magic;
    return fpga_word_read_burst(MIDI_RAW_REG_MAGIC,&magic,1) && magic==MIDI_RAW_ABI_MAGIC;
}

static void mark(uint32_t *bits, unsigned bit) { bits[bit>>5] |= 1u << (bit&31); }
static void set_element(const engine_cmd_t *c)
{
    if (s_desired[c->elem][c->word] != c->value) {
        s_desired[c->elem][c->word] = c->value;
        mark(s_pending, c->elem * ENGINE_WORDS_PER_ELEMENT + c->word);
    }
}
static void set_prod(unsigned entry, unsigned word, uint32_t value)
{
    if (s_prod_desired[entry][word] != value) {
        s_prod_desired[entry][word] = value;
        mark(s_ppending, entry*4+word);
    }
}
bool engine_link_send(const engine_cmd_t *c)
{
    if(CONFIG_FPGA_AE350_CONTROL)return false;
    if (!s_ready || !c || c->word >= ENGINE_WORDS_PER_ELEMENT) return false;
    portENTER_CRITICAL(&s_lock); set_element(c); portEXIT_CRITICAL(&s_lock);
    return true;
}
bool engine_link_prod_write(uint8_t entry, uint8_t word, uint32_t value)
{
    if(CONFIG_FPGA_AE350_CONTROL)return false;
    if (!s_ready || word >= 4) return false;
    portENTER_CRITICAL(&s_lock); set_prod(entry, word, value); portEXIT_CRITICAL(&s_lock);
    return true;
}
bool engine_link_bus_write(uint16_t bus, uint32_t value)
{
    if(CONFIG_FPGA_AE350_CONTROL)return false;
    if (!s_ready || bus == 0 || bus >= 1024) return false;
    portENTER_CRITICAL(&s_lock);
    if (bus >= GATE_BASE && bus < GATE_BASE+ENGINE_NUM_VOICES) {
        // Gates must use the transaction interface for On. Off is always
        // accepted and invalidates a snapshot before it can re-gate.
        if (value != 0) { portEXIT_CRITICAL(&s_lock); s_drops++; return false; }
        unsigned v = bus-GATE_BASE;
        s_notes[v].on = false; s_notes[v].ready = false; s_notes[v].seq++;
        s_notes[v].phase = 0;
    }
    s_bus[bus] = value & 0x3FFFFu; s_bus_seq[bus]++; mark(s_bus_dirty,bus);
    portEXIT_CRITICAL(&s_lock);
    return true;
}
bool engine_link_note_on(const engine_note_t *n)
{
    if(CONFIG_FPGA_AE350_CONTROL)return false;
    if (!s_ready || !n || n->voice >= ENGINE_NUM_VOICES || n->count > ENGINE_NOTE_WORDS)
        return false;
    for (unsigned i=0; i<n->count; i++)
        if (n->words[i].word >= ENGINE_WORDS_PER_ELEMENT) return false;
    portENTER_CRITICAL(&s_lock);
    for (unsigned i=0; i<n->count; i++) set_element(&n->words[i]);
    set_prod(n->amp_entry, 2, n->amp_depth);
    set_prod(n->mod_entry, 2, n->mod_depth);
    unsigned cut=48+n->voice;
    s_bus[cut]=n->cutoff & 0x3FFFF; s_bus_seq[cut]++; mark(s_bus_dirty,cut);
    s_notes[n->voice].seq++; s_notes[n->voice].on=true;
    s_notes[n->voice].ready=false; s_notes[n->voice].phase=0;
    // Coalescing of the bank image is safe, but gate transitions are fenced:
    // every accepted replacement first exposes low to a complete CSP pass.
    unsigned gate=GATE_BASE+n->voice;
    s_bus[gate]=0; s_bus_seq[gate]++; mark(s_bus_dirty,gate);
    portEXIT_CRITICAL(&s_lock);
    return true;
}
uint32_t engine_link_drops(void) { return s_drops; }
uint32_t engine_link_prod_committed(uint8_t entry, uint8_t word)
{
    if (word>=4) return 0;
    portENTER_CRITICAL(&s_lock);
    uint32_t value=s_prod_committed[entry][word];
    portEXIT_CRITICAL(&s_lock);
    return value;
}
uint32_t engine_link_release_coefficient(uint8_t entry, uint32_t *revision)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t k=(s_prod_committed[entry][3]>>4)&0x3FFFFu;
    *revision=s_release_revision[entry];
    portEXIT_CRITICAL(&s_lock);
    return k;
}
bool engine_link_bus_committed(uint16_t bus, uint32_t value)
{
    if (bus>=1024) return false;
    portENTER_CRITICAL(&s_lock);
    bool done=s_bus_seq[bus]==s_bus_done[bus] && s_bus[bus]==(value&0x3FFFFu);
    portEXIT_CRITICAL(&s_lock);
    return done;
}

static bool live_one(unsigned bus);

static void prepare_notes(int64_t deadline)
{
    for (unsigned n=0;n<ENGINE_NUM_VOICES && esp_timer_get_time()<deadline;n++) {
        unsigned v=s_note_cursor++ % ENGINE_NUM_VOICES;
        portENTER_CRITICAL(&s_lock);
        bool pending=s_notes[v].on && !s_notes[v].ready;
        uint32_t seq=s_notes[v].seq;
        unsigned phase=s_notes[v].phase;
        uint16_t pass=s_notes[v].pass;
        portEXIT_CRITICAL(&s_lock);
        if (!pending) continue;
        uint32_t status;
        if (phase==0) {
            if (!live_one(48+v) || !fpga_live_write(GATE_BASE+v,0) ||
                !control_status(&status)) break;
            portENTER_CRITICAL(&s_lock);
            if (s_notes[v].seq==seq) {
                s_notes[v].pass=status>>16; s_notes[v].phase=1;
                unsigned gate=GATE_BASE+v;
                s_bus_done[gate]=s_bus_seq[gate];
                s_bus_dirty[gate>>5] &= ~(1u<<(gate&31));
            }
            portEXIT_CRITICAL(&s_lock);
        } else {
            if (!control_status(&status)) break;
            // A pass in flight at Off ack may already have read old gate.
            // Two completions cover an entire subsequent pass, across wrap.
            if ((uint16_t)((status>>16)-pass)<2u) continue;
            portENTER_CRITICAL(&s_lock);
            if (s_notes[v].seq==seq) s_notes[v].ready=true;
            portEXIT_CRITICAL(&s_lock);
        }
    }
}
static bool live_one(unsigned bus)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t seq=s_bus_seq[bus], done=s_bus_done[bus], value=s_bus[bus];
    portEXIT_CRITICAL(&s_lock);
    if (seq==done) return true;
    if (!fpga_live_write(bus,value)) return false;
    portENTER_CRITICAL(&s_lock);
    // A newer update during SPI remains pending; never mark it committed.
    s_bus_done[bus]=seq;
    if(s_bus_seq[bus]==seq) s_bus_dirty[bus>>5] &= ~(1u<<(bus&31));
    portEXIT_CRITICAL(&s_lock);
    return true;
}
static void begin_flush(void)
{
    portENTER_CRITICAL(&s_lock);
    memcpy(s_image,s_desired,sizeof(s_image)); memcpy(s_prod,s_prod_desired,sizeof(s_prod));
    memcpy(s_dirty_now,s_pending,sizeof(s_pending)); memset(s_pending,0,sizeof(s_pending));
    memcpy(s_pdirty_now,s_ppending,sizeof(s_ppending)); memset(s_ppending,0,sizeof(s_ppending));
    bool any=false;
    for (unsigned i=0;i<DIRTY_WORDS;i++) any |= (s_dirty_now[i]|s_dirty_prev[i]) != 0;
    for (unsigned i=0;i<PDIRTY_WORDS;i++) any |= (s_pdirty_now[i]|s_pdirty_prev[i]) != 0;
    for (unsigned v=0;v<ENGINE_NUM_VOICES;v++) {
        s_notes[v].snapshot_seq=s_notes[v].ready ? s_notes[v].seq : 0;
        any |= s_notes[v].snapshot_seq != 0;
    }
    s_flushing=any;
    portEXIT_CRITICAL(&s_lock);
    s_elem_cursor=0; s_prod_cursor=0; s_swap_requested=false;
}
static void finish_flush(void)
{
    // dirty_now UNION dirty_prev was written. After the confirmed swap, the
    // next shadow is one generation behind; retain only this batch's delta.
    memcpy(s_dirty_prev,s_dirty_now,sizeof(s_dirty_prev));
    memcpy(s_pdirty_prev,s_pdirty_now,sizeof(s_pdirty_prev));
    s_flushing=false;
    portENTER_CRITICAL(&s_lock);
    for(unsigned p=0;p<ENGINE_NUM_PRODUCERS;p++)
        if (((s_prod_committed[p][3]^s_prod[p][3]) & 0x3FFFF0u)!=0) s_release_revision[p]++;
    memcpy(s_prod_committed,s_prod,sizeof(s_prod_committed));
    portEXIT_CRITICAL(&s_lock);
    for (unsigned v=0;v<ENGINE_NUM_VOICES;v++) {
        portENTER_CRITICAL(&s_lock);
        bool on=s_notes[v].on && s_notes[v].ready &&
                s_notes[v].seq==s_notes[v].snapshot_seq;
        if (on) {
            unsigned gate=GATE_BASE+v;
            s_bus[gate]=1; s_bus_seq[gate]++; mark(s_bus_dirty,gate);
            // The desired bus entry will retry GateOn on any SPI failure.
            // A later Off supersedes it before the next live service.
            s_notes[v].ready=false; s_notes[v].on=false;
        }
        portEXIT_CRITICAL(&s_lock);
    }
}
// Return the next dirty non-gate bus, scanning words rather than all 1023
// addresses. Rotate after each dispatched address even if a producer keeps
// updating the first bus, so expression streams share the slice fairly.
static unsigned next_live_bus(void)
{
    portENTER_CRITICAL(&s_lock);
    for(unsigned n=0;n<33;n++) {
        unsigned start=(s_live_cursor+n*32)&1023u;
        unsigned word=start>>5;
        uint32_t bits=s_bus_dirty[word];
        if(word==0) bits &= ~1u;
        if(word==2) bits &= 0x0000FFFFu; // gate 80..95
        if(word==3) bits &= 0xFFFF0000u; // gate 96..111
        if(n==0) bits &= ~0u<<(s_live_cursor&31);
        else if(n==32) bits &= (1u<<(s_live_cursor&31))-1u;
        if(bits) {
            unsigned bus=word*32+__builtin_ctz(bits);
            s_live_cursor=(bus+1)&1023u;
            portEXIT_CRITICAL(&s_lock);
            return bus;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return 0;
}
#include "engine_ks.inc.h"
#include "engine_fm.inc.h"
#include "engine_cpu_fm.inc.h"
#include "fm_diagnostics.inc.h"
#include "engine_flute.inc.h"
#include "engine_local.inc.h"
#include "engine_reverb.inc.h"

// Called with s_lock held. Only ESP control transactions participate here:
// FPGA-local FM music BUSY and the idle voice-query cursor must not disable
// their own MIDI input. SELECT/Panic/recovery and pending patch commands do.
static bool control_stable_locked(void)
{
    if(!CONFIG_FPGA_FM_ONLY && (s_ks.switching || s_ks.recovering || s_ks_owner_pending || s_ks_panic_pending ||
       s_ks_inflight.sequence || (s_ks.available && !s_ks.valid)))return false;
    if(flute_blocked_locked())return false;
    if(CONFIG_FPGA_FM_ONLY && !engine_fm_playable(&s_fm.status))return false;
    if(s_fm.status.switching || s_fm.status.recovering || s_fm.status.needs_select ||
       s_fm_owner_pending || s_fm_mode_pending || s_fm_panic_pending || s_fm_panic.sequence_id ||
       s_fm_inflight.active || s_fm_preset_pending || (engine_fm_control_active(&s_fm.status) &&
       (!s_fm.status.available || !s_fm.status.valid || !s_fm.status.ready)))return false;
    for(unsigned v=0;!CONFIG_FPGA_FM_ONLY && v<ENGINE_KS_VOICES;v++)
        if(s_ks_desired[v].on || s_ks_desired[v].off || s_ks_desired[v].gain || s_ks_desired[v].loss)return false;
    for(unsigned v=0;v<ENGINE_FM_VOICES;v++)
        if(s_fm_notes[v].on || s_fm_notes[v].off || s_fm_notes[v].pitch_dirty || s_fm_notes[v].gain_dirty)return false;
    for(unsigned p=0;p<ENGINE_FM_PARAMETER_SLOTS;p++)if(s_fm_parameters[p].dirty)return false;
    return true;
}
static bool control_pending_locked(void)
{
    // The shared control owner may still render its DDS RAM image. That image
    // has no hardware consumer in this explicitly identified validation build.
    if(CONFIG_FPGA_FM_ONLY)return false;
    if(s_flushing || s_swap_requested || s_ks_wait_bank_reset)return true;
    for(unsigned i=0;i<DIRTY_WORDS;i++)if(s_pending[i] || s_dirty_prev[i])return true;
    for(unsigned i=0;i<PDIRTY_WORDS;i++)if(s_ppending[i] || s_pdirty_prev[i])return true;
    for(unsigned i=0;i<32;i++)if(s_bus_dirty[i])return true;
    for(unsigned v=0;v<ENGINE_NUM_VOICES;v++)if(s_notes[v].on || s_notes[v].ready)return true;
    return false;
}
bool engine_link_control_idle(void)
{
    portENTER_CRITICAL(&s_lock);
    // The cached owner observation fences owner-only cursors; the live
    // pending bits also catch edits queued since that observation.
    bool idle=s_ready && s_control_idle && !control_pending_locked() && control_stable_locked();
    portEXIT_CRITICAL(&s_lock);return idle;
}
static void raw_enable_locked(bool enabled)
{
    if(s_raw_enable_requested!=enabled) {
        s_raw_enable_requested=enabled;
        // RAM-only; raw_midi never acquires s_lock or calls engine_link.
        // Keep the owner fence and request atomic without holding SPI locks.
        raw_midi_request_enable(enabled);
    }
}
void engine_link_performance_ready(bool ready)
{
    portENTER_CRITICAL(&s_lock);
    s_performance_ready=ready;
    if(!ready && !(s_local.supported && (s_local.local_active || s_local.blocked || s_local.remote_pending)))raw_enable_locked(false);
    portEXIT_CRITICAL(&s_lock);
}
static bool raw_sources_flushing(const raw_midi_state_t *raw)
{
    if(raw->stop_pending)return true;
    for(unsigned source=0;source<4;source++)if(raw->source[source].flushing)return true;
    return false;
}
static void performance_enable(const raw_midi_state_t *raw,const midi_template_link_state_t *image)
{
    portENTER_CRITICAL(&s_lock);
    bool transition=flute_blocked_locked() || (!CONFIG_FPGA_FM_ONLY && (s_ks.switching || s_ks.recovering || s_ks_owner_pending || s_ks_panic_pending ||
        (s_ks.available && !s_ks.valid))) || s_fm.status.switching || s_fm.status.recovering ||
        s_fm.status.needs_select || s_fm_owner_pending || s_fm_mode_pending || s_fm_panic_pending ||
        s_fm_panic.sequence_id || (engine_fm_control_active(&s_fm.status) &&
        (!s_fm.status.available || !s_fm.status.valid || !s_fm.status.ready));
    bool safe=s_performance_ready && s_ready &&
        raw->available && raw->initialized && !image->fault &&
        raw->fault!=RAW_MIDI_RESET && raw->fault!=RAW_MIDI_IO && raw->fault!=RAW_MIDI_UNAVAILABLE && !transition &&
        (!CONFIG_FPGA_FM_ONLY || engine_fm_playable(&s_fm.status));
    // First enable needs complete static/template readiness. Once running,
    // ordinary parameter writes and later template commits must preserve HELD
    // voices. ENABLE=0 is destructive and belongs only to an unsafe episode.
    bool enabled=safe && (s_raw_enable_requested || (image->ready && s_control_idle &&
        !raw->enabled && !raw->inflight && !raw_sources_flushing(raw) &&
        !control_pending_locked() && control_stable_locked()));
    raw_enable_locked(enabled);
    portEXIT_CRITICAL(&s_lock);
}
static void performance_reset_fence(const raw_midi_state_t *raw)
{
    // Capability discovery and a failing status read may share one raw slice,
    // leaving both snapshots unavailable. Initialization/reset history also
    // proves that this lifecycle identified MID1 and cannot use legacy SPI.
    if(raw->available || raw->initialized || raw->reset_count)s_raw_seen=true;
    if(raw->reset_count==s_raw_reset_count)return;
    s_raw_reset_count=raw->reset_count;
    portENTER_CRITICAL(&s_lock);raw_enable_locked(false);portEXIT_CRITICAL(&s_lock);
    // The reset counter survives a RESET -> ENABLE0 ACK in one raw slice.
    // Explicit invalidation also drops a first pending image which has never
    // reached service, while preserving an unknown COMMIT's safety fence.
    midi_template_link_invalidate();
}
static void control_service(int64_t deadline)
{
    // Dedicated slices keep release, note preparation, expression and shadow
    // work making progress under continuous traffic in any one category.
    int64_t gate_deadline=esp_timer_get_time()+200;
    if(gate_deadline>deadline)gate_deadline=deadline;
    for (unsigned n=0;n<ENGINE_NUM_VOICES && esp_timer_get_time()<gate_deadline;n++) {
        unsigned bus=GATE_BASE+s_gate_cursor++%ENGINE_NUM_VOICES;
        if (!live_one(bus)) break;
    }
    int64_t note_deadline=esp_timer_get_time()+150;
    if(note_deadline>deadline) note_deadline=deadline;
    prepare_notes(note_deadline);
    int64_t live_deadline=esp_timer_get_time()+100;
    if(live_deadline>deadline) live_deadline=deadline;
    while(esp_timer_get_time()<live_deadline) {
        unsigned bus=next_live_bus();
        if(!bus || !live_one(bus)) break;
    }
    // A reset/lost acknowledgement abandons the old bank transaction. Wait
    // for its hardware pending flag before rebuilding both muted images.
    if(!ks_bank_ready())return;
    if (!s_flushing) {
        if (esp_timer_get_time()>=deadline) return;
        // Pending gates that have not yet observed low are excluded from the
        // On snapshot; they cannot block prepared notes or ordinary updates.
        begin_flush();
    }
    if (!s_flushing) return;
    while (s_elem_cursor<ENGINE_NUM_ELEMENTS && esp_timer_get_time()<deadline) {
        unsigned e=s_elem_cursor++; bool dirty=false;
        for(unsigned w=0;w<ENGINE_WORDS_PER_ELEMENT;w++) {
            unsigned bit=e*ENGINE_WORDS_PER_ELEMENT+w;
            dirty |= ((s_dirty_now[bit>>5]|s_dirty_prev[bit>>5]) & (1u<<(bit&31)))!=0;
        }
        if(dirty) fpga_word_write_burst(ELEM_BASE+e*ELEM_STRIDE,s_image[e],ENGINE_WORDS_PER_ELEMENT);
    }
    while(s_elem_cursor==ENGINE_NUM_ELEMENTS && s_prod_cursor<ENGINE_NUM_PRODUCERS*4 &&
          esp_timer_get_time()<deadline) {
        unsigned bit=s_prod_cursor++;
        if((s_pdirty_now[bit>>5]|s_pdirty_prev[bit>>5]) & (1u<<(bit&31)))
            fpga_word_write(PROD_BASE+bit,s_prod[bit/4][bit%4]);
    }
    if(s_prod_cursor<ENGINE_NUM_PRODUCERS*4 || esp_timer_get_time()>=deadline) return;
    uint32_t status;
    if (!control_status(&status)) return;
    if (!s_swap_requested) {
        if(status&2u) return; // another request must complete before reuse
        s_expected_bank=(status&1u)^1u;
        fpga_word_write(0x0002,1);
        s_swap_requested=true;
        return;
    }
    if (!(status&2u) && (status&1u)==s_expected_bank) finish_flush();
}
// CPU1 owns all note parsing, voice allocation and route changes. The S3
// owner retains only bounded RAW transport and independent template uploads.
static void engine_cpu_service(int64_t deadline)
{
    int64_t now=esp_timer_get_time();
    if(!s_cpu_verified || now>=s_cpu_probe_at) {
        s_cpu_probe_at=now+20000;
        s_cpu_verified=ae350_control_identity();
        if(!s_cpu_verified) {
            cpu_mode_unavailable();
            cpu_fm_unavailable();
            audio_routes_unavailable();
            fdc_unavailable();
            raw_midi_pause(true);
            portENTER_CRITICAL(&s_lock);raw_enable_locked(false);s_control_idle=false;portEXIT_CRITICAL(&s_lock);
            return; // Fail closed: never run ENG1/legacy probes or writers.
        }
    }
    raw_midi_pause(false);
    int64_t raw_deadline=esp_timer_get_time()+200;
    if(raw_deadline>deadline)raw_deadline=deadline;
    raw_midi_service(raw_deadline);
    raw_midi_state_t raw;raw_midi_state(&raw);
    cpu_mode_service(deadline);
    if(raw.initialized && !raw.stop_pending)cpu_fm_service(deadline);
    fdc_service(deadline,raw.initialized && !raw.stop_pending);
    if(raw.reset_count!=s_raw_reset_count) {
        audio_routes_unavailable();
        s_raw_reset_count=raw.reset_count;
        cpu_fm_unavailable();
        midi_template_link_invalidate();
        portENTER_CRITICAL(&s_lock);raw_enable_locked(false);portEXIT_CRITICAL(&s_lock);
    }
    audio_routes_service(deadline,raw.available && raw.initialized);
    bool available=raw.available && raw.cpu_control && raw.fault!=RAW_MIDI_IO &&
        raw.fault!=RAW_MIDI_RESET && raw.fault!=RAW_MIDI_UNAVAILABLE;
    // Only the old static-bank fence is retired. Native COMMIT ACK/fence and
    // STOP scope completion remain in their existing services unchanged.
    midi_template_link_service(deadline,raw.initialized && !raw.stop_pending,available);
    midi_template_link_state_t image;midi_template_link_state(&image);
    portENTER_CRITICAL(&s_lock);
    s_control_idle=s_cpu_verified;
    bool parallel=s_audio_routes.available && s_audio_routes.valid && s_cpu_mode.applied<=2;
    bool mode_ready=!s_cpu_mode.supported || (s_cpu_mode.valid && !s_cpu_mode.fault &&
        (parallel || (s_cpu_mode.ready && !s_cpu_mode.busy && !s_cpu_mode.pending)));
    bool safe=s_performance_ready && available && raw.initialized && !image.fault && mode_ready;
    bool enabled=safe && (s_raw_enable_requested || (image.ready &&
        !raw.inflight && !raw_sources_flushing(&raw)));
    raw_enable_locked(enabled);
    portEXIT_CRITICAL(&s_lock);
    if(!raw.stop_pending)reverb_service(deadline);
}
static void engine_service(void)
{
    int64_t deadline=esp_timer_get_time()+ENGINE_BATCH_US;
    if(CONFIG_FPGA_AE350_CONTROL){engine_cpu_service(deadline);return;}
    // Observe physical ownership before any backend/probe/static write.
    local_mode_poll();
    engine_local_state_t local;engine_link_local_state(&local);
    if(local.supported && (local.local_active || local.blocked || local.remote_pending)) {
        raw_midi_pause(local.blocked);
        if(local.adopted && !local.blocked) {
            portENTER_CRITICAL(&s_lock);raw_enable_locked(true);portEXIT_CRITICAL(&s_lock);
        }
        int64_t raw_deadline=esp_timer_get_time()+200;
        if(raw_deadline>deadline)raw_deadline=deadline;
        raw_midi_service(raw_deadline); // source releases and explicit STOP remain live
        raw_midi_state_t raw;raw_midi_state(&raw);
        if(!raw.stop_pending)reverb_service(deadline);
        return;
    }
    if(!s_local_probed)return;
    if(s_backend_boot_pending) {
        ks_probe();
        portENTER_CRITICAL(&s_lock);
        memset(s_pending,0xff,sizeof(s_pending));memset(s_ppending,0xff,sizeof(s_ppending));
        s_backend_boot_pending=false;
        portEXIT_CRITICAL(&s_lock);
    }
    raw_midi_pause(false);
    raw_midi_state_t raw;
    midi_template_link_state_t image;
    raw_midi_state(&raw);performance_reset_fence(&raw);midi_template_link_state(&image);
    performance_enable(&raw,&image);
    // Raw ingress gets the first bounded slice, sharing the original overall
    // 700us budget with all existing control and template work.
    int64_t raw_deadline=esp_timer_get_time()+200;
    if(raw_deadline>deadline)raw_deadline=deadline;
    raw_midi_service(raw_deadline);
    raw_midi_state(&raw);performance_reset_fence(&raw);midi_template_link_state(&image);
    performance_enable(&raw,&image);
    // Requesting ENABLE0 does not finish its native-USB barrier. Once MID1
    // has been identified, stop mode/static/template dispatch until hardware
    // confirms disabled and the subsequent global flush is acknowledged.
    // Normal enabled BYTE/source flush traffic imposes no such barrier.
    raw_midi_state(&raw);
    portENTER_CRITICAL(&s_lock);
    bool stopping=s_raw_seen && !s_raw_enable_requested &&
        (!raw.available || raw.enabled || !raw.initialized || raw.inflight || raw_sources_flushing(&raw));
    if(stopping)s_control_idle=false;
    portEXIT_CRITICAL(&s_lock);
    if(stopping)return;
    if(!CONFIG_FPGA_FM_ONLY && esp_timer_get_time()<deadline)flute_service();
    // Unbind FLU1 before another backend can select itself. Binding waits for
    // the existing KS/FM preparation and static-bank quiet fences below.
    if(s_flute.switching && !s_flute.requested_mode)return;
    if(!CONFIG_FPGA_FM_ONLY && esp_timer_get_time()<deadline)ks_service();
    if(esp_timer_get_time()<deadline)
        fm_service(CONFIG_FPGA_FM_ONLY || (!s_ks.applied_mode && !s_ks.switching && ks_dds_silent()));
    if(!CONFIG_FPGA_FM_ONLY && esp_timer_get_time()<deadline)control_service(deadline);
    portENTER_CRITICAL(&s_lock);
    s_control_idle=!control_pending_locked() && control_stable_locked();
    portEXIT_CRITICAL(&s_lock);
    raw_midi_state(&raw);
    performance_reset_fence(&raw);
    bool idle=engine_link_control_idle();
    // Initial raw initialization acknowledges ENABLE=0. Later template commits
    // deliberately run while enabled to refresh HELD voices atomically; raw
    // byte commands and template writes share this one serialized SPI owner.
    bool upload_idle=idle && raw.initialized;
    bool template_available=raw.available && raw.fault!=RAW_MIDI_RESET && raw.fault!=RAW_MIDI_IO &&
        (!CONFIG_FPGA_FM_ONLY || (s_fm.status.available && s_fm.status.valid));
    midi_template_link_service(deadline,upload_idle,template_available);
    midi_template_link_state(&image);
    performance_enable(&raw,&image);
    // Only spare time after all musical work may serve diagnostic reads.
    if(!raw.stop_pending)reverb_service(deadline);
    if(CONFIG_FPGA_FM_ONLY) {
        bool diagnostic_idle=!raw.inflight && !image.busy && !s_fm_inflight.active &&
            !s_fm_panic_pending && !s_fm_panic.sequence_id;
        for(unsigned i=0;i<4;i++)if(raw.source[i].queued_bytes)diagnostic_idle=false;
        fm_diagnostics_service(deadline,diagnostic_idle);
    }
}
static void tick_cb(void *arg) { (void)arg; xTaskNotifyGive(s_task); }
static void engine_task(void *arg)
{
    (void)arg;
    unsigned led_ticks = 0;
    while(1) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        engine_service();
        if (++led_ticks >= 50) {
            status_led_engine_heartbeat();
            led_ticks = 0;
        }
        // Drop accumulated periodic notifications, then block until a future
        // timer wake. Each service is bounded in µs, never rounded to a 10 ms
        // vTaskDelay(1), and cannot monopolize the single-core idle task.
        ulTaskNotifyTake(pdTRUE,0);
    }
}
void engine_link_init(void)
{
    s_ready=false;s_cpu_verified=false;s_cpu_probe_at=0;
    fpga_cpu_link_authorize(false);
    fm_diagnostics_init();
    uint32_t identity[2]={0};
    bool identity_ok=fpga_word_read_burst(FM_ONLY_REG_IDENTITY,identity,2);
    if(CONFIG_FPGA_FM_ONLY) {
        if(!identity_ok || identity[0]!=FM_ONLY_IDENTITY || identity[1]!=FM_ONLY_VERSION) {
            ESP_LOGE(TAG,"FM_ONLY requires FMV1 validation image ABI 1.0; engine unavailable");
            return;
        }
    } else if(identity_ok && identity[0]==FM_ONLY_IDENTITY) {
        ESP_LOGE(TAG,"FMV1 validation image requires explicit FM_ONLY firmware; engine unavailable");
        return;
    }
    midi_template_link_init();
    cpu_mode_init();
    audio_routes_init();
    fdc_init();
    fm_init();
    cpu_fm_init();
    s_ks_quarantined=false;s_ks_probe_at=s_ks_read_at=0;
    memset(&s_local,0,sizeof(s_local));
    s_local_probed=s_local_fm_adopting=s_local_release_sent=false;s_local_owner_ack=0;
    s_local_health_fault=s_local_adopt_pending=false;s_local_health_at=0;
    s_local_polling=false;
    reverb_init();
    memset(&s_flute,0,sizeof(s_flute));s_flute_pending_word=0;s_flute_probe_done=false;
    if(!CONFIG_FPGA_AE350_CONTROL) {
        local_mode_poll();
        if(!CONFIG_FPGA_FM_ONLY)flute_probe();
    }
    s_control_idle=s_performance_ready=s_raw_enable_requested=false;
    raw_midi_state_t raw;
    raw_midi_state(&raw);s_raw_reset_count=raw.reset_count;
    s_raw_seen=raw.available || raw.initialized || raw.reset_count;
    // CPU1 owns the legacy banks. Keeping their unused boot initialization
    // alive also retains 14 KiB of S3 RAM needed by file parsing and Wi-Fi.
    if(!CONFIG_FPGA_AE350_CONTROL) {
        memset(s_desired,0,sizeof(s_desired));
        for(unsigned e=0;e<ENGINE_NUM_ELEMENTS;e++) s_desired[e][2]=0x40000000;
        memcpy(s_image,s_desired,sizeof(s_image));
    }
    bool initialize_now=!CONFIG_FPGA_AE350_CONTROL && !CONFIG_FPGA_FM_ONLY && s_local_probed &&
        !(s_local.supported && (s_local.local_active || s_local.blocked));
    for(unsigned bank=0;initialize_now && bank<2;bank++) {
        for(unsigned e=0;e<ENGINE_NUM_ELEMENTS;e++)
            fpga_word_write_burst(ELEM_BASE+e*ELEM_STRIDE,s_image[e],ENGINE_WORDS_PER_ELEMENT);
        if (!fpga_swap_checked()) {
            ESP_LOGE(TAG,"boot bank swap was not acknowledged; engine unavailable");
            return;
        }
    }
    s_ready=true;
    s_backend_boot_pending=!CONFIG_FPGA_AE350_CONTROL && !CONFIG_FPGA_FM_ONLY && !initialize_now;
    if(initialize_now)ks_probe();
    if(xTaskCreate(engine_task,"engine_link",ENGINE_TASK_STACK,NULL,ENGINE_TASK_PRIO,&s_task)!=pdPASS) {
        s_ready=false; ESP_LOGE(TAG,"failed to create task"); return;
    }
    const esp_timer_create_args_t args={.callback=tick_cb,.name="engine_tick"};
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&args,&timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer,ENGINE_TICK_US));
    ESP_LOGI(TAG,"bounded 1 kHz control service running");
}
