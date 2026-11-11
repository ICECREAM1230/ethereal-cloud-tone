// Private implementation: this file never creates another SPI task.
#include "fm_engine.h"
#include "esp_random.h"
// Bit 31 belongs to FPGA-local performance, never to an SPI control identity.
#define FM_SPI_SEQUENCE_MAX UINT32_C(0x7fffffff)
#define FM_CONFIRM_TIMEOUT_US 100000LL
#define FM_PROBE_INTERVAL_US 20000LL
#define FM_IDLE_POLL_INTERVAL_US 20000LL
#define FM_KNOWN_FLAGS (FM_FLAG_READY|FM_FLAG_SELECTED|FM_FLAG_MUTED|FM_FLAG_BUSY|FM_FLAG_FAULT)
#define FM_REQUIRED_FEATURES (FM_FEATURE_NOTE_ON|FM_FEATURE_NOTE_OFF|FM_FEATURE_SET_PITCH|FM_FEATURE_SET_GAIN_PAN|FM_FEATURE_SET_PARAMETER|FM_FEATURE_APPLY_PRESET|FM_FEATURE_RELEASE_ALL|FM_FEATURE_PRIORITY_PANIC|FM_FEATURE_NEXT_NOTE_PARAMETERS|FM_FEATURE_NATIVE_STEREO_PAN)
static engine_fm_state_t s_fm;
static struct {
    uint32_t pitch, revision;
    uint8_t velocity, volume, pan;
    bool on, off, pitch_dirty, gain_dirty, sounding;
} s_fm_notes[ENGINE_FM_VOICES];
static struct { engine_fm_parameter_t value; uint32_t revision; bool dirty; } s_fm_parameters[ENGINE_FM_PARAMETER_SLOTS];
static struct {
    fm_command_t command;
    uint32_t revision, cancel_revision;
    int64_t sent_at;
    unsigned slot;
    bool active, accepted, retry, cancelled;
} s_fm_inflight;
static engine_fm_identity_t s_fm_panic;
static bool s_fm_panic_pending, s_fm_owner_pending, s_fm_mode_pending, s_fm_preset_pending;
static bool s_fm_fault_panic;
static uint32_t s_fm_sequence, s_fm_panic_sequence, s_fm_mode_revision, s_fm_preset_id;
static uint32_t s_fm_cancel_revision, s_fm_preset_revision;
static uint32_t s_fm_nonce, s_fm_query_session, s_fm_query_epoch, s_fm_query_topology;
static unsigned s_fm_cursor, s_fm_query_voice;
static bool s_fm_query_pending;
static int64_t s_fm_probe_at, s_fm_query_at, s_fm_panic_at, s_fm_coherent_at;
static int64_t s_fm_poll_at;
static bool s_fm_quarantined;
static uint32_t s_fm_status_generation;
static bool fm_can_isolate_locked(void);
static bool fm_identity_equal(engine_fm_identity_t a, engine_fm_identity_t b)
{return a.sequence_id && a.sequence_id==b.sequence_id && a.session_id==b.session_id && a.engine_epoch==b.engine_epoch;}
static engine_fm_identity_t fm_identity(const fm_command_t *c)
{return (engine_fm_identity_t){c->session_id,c->engine_epoch,c->sequence_id};}
static void fm_clear_intents(void)
{
    s_fm_cancel_revision++;
    for(unsigned v=0;v<ENGINE_FM_VOICES;v++) {
        s_fm_notes[v].revision++;
        s_fm_notes[v].on=s_fm_notes[v].off=s_fm_notes[v].pitch_dirty=s_fm_notes[v].gain_dirty=s_fm_notes[v].sounding=false;
    }
    for(unsigned p=0;p<ENGINE_FM_PARAMETER_SLOTS;p++)s_fm_parameters[p].dirty=false;
    s_fm_preset_pending=false;s_fm_query_pending=false;
}
void engine_link_fm_status(engine_fm_status_t *state)
{
    portENTER_CRITICAL(&s_lock);*state=s_fm.status;state->pending_mask=0;
    for(unsigned v=0;v<ENGINE_FM_VOICES;v++)if(s_fm_notes[v].on||s_fm_notes[v].off)state->pending_mask|=1u<<v;
    if(s_fm_inflight.active && s_fm_inflight.command.scope_id==FM_SCOPE_VOICE)
        state->pending_mask|=1u<<fm_target_voice(s_fm_inflight.command.target_word);
    portEXIT_CRITICAL(&s_lock);
}
void engine_link_fm_state(engine_fm_state_t *state)
{portENTER_CRITICAL(&s_lock);*state=s_fm;portEXIT_CRITICAL(&s_lock);}
static bool fm_can_play(void)
{
    return s_ready && engine_fm_playable(&s_fm.status) && !s_fm_panic_pending && !s_fm_panic.sequence_id;
}
bool engine_link_fm_mode(uint32_t backend)
{
    if(CONFIG_FPGA_AE350_CONTROL){
        portENTER_CRITICAL(&s_lock);
        bool ok=backend==FM_BACKEND_JT51_OPM && s_fm.status.available && s_fm.status.valid && s_fm.status.ready;
        portEXIT_CRITICAL(&s_lock);return ok;
    }
    portENTER_CRITICAL(&s_lock);
    if(backend==FM_BACKEND_UNBOUND && s_fm_quarantined && fm_can_isolate_locked()) {
        portEXIT_CRITICAL(&s_lock);return true;
    }
    bool allowed=engine_fm_selectable(&s_fm.status) &&
        (backend==FM_BACKEND_UNBOUND || backend==s_fm.status.backend_id);
    if(allowed) {
        fm_clear_intents();s_fm.status.requested_backend=backend;s_fm.status.switching=true;
        s_fm.status.needs_select=false;s_fm_mode_pending=true;s_fm_mode_revision++;
        if((s_fm.status.selected || s_fm_inflight.active) && !s_fm_panic.sequence_id)s_fm_panic_pending=true;
    }
    portEXIT_CRITICAL(&s_lock);return allowed;
}
bool engine_link_fm_note_on(unsigned voice,uint32_t pitch,uint8_t velocity,uint8_t volume,uint8_t pan)
{
    if(CONFIG_FPGA_AE350_CONTROL)return false;
    if(voice>=ENGINE_FM_VOICES || !velocity || velocity>127 || volume>127 || pan>127 || pitch>0x7fffffu)return false;
    portENTER_CRITICAL(&s_lock);bool allowed=fm_can_play() && voice<s_fm.status.voice_count;
    if(allowed) {
        // A steal must send Off before the next generation On.
        s_fm_notes[voice].off|=s_fm_notes[voice].sounding ||
            (s_fm_inflight.active && s_fm_inflight.command.opcode_id==FM_SPI_OPCODE_NOTE_ON && fm_target_voice(s_fm_inflight.command.target_word)==voice);
        s_fm_notes[voice].revision++;s_fm_notes[voice].on=true;
        s_fm_notes[voice].pitch=pitch;s_fm_notes[voice].velocity=velocity;
        s_fm_notes[voice].volume=volume;s_fm_notes[voice].pan=pan;
        s_fm_notes[voice].pitch_dirty=s_fm_notes[voice].gain_dirty=false;
    }
    portEXIT_CRITICAL(&s_lock);return allowed;
}
void engine_link_fm_note_off(unsigned voice)
{
    if(CONFIG_FPGA_AE350_CONTROL)return;
    if(voice>=ENGINE_FM_VOICES)return;
    portENTER_CRITICAL(&s_lock);s_fm_notes[voice].revision++;
    s_fm_notes[voice].on=s_fm_notes[voice].pitch_dirty=s_fm_notes[voice].gain_dirty=false;
    s_fm_notes[voice].off|=s_fm_notes[voice].sounding ||
        (s_fm_inflight.active && s_fm_inflight.command.opcode_id==FM_SPI_OPCODE_NOTE_ON && fm_target_voice(s_fm_inflight.command.target_word)==voice);
    portEXIT_CRITICAL(&s_lock);
}
void engine_link_fm_pitch(unsigned voice,uint32_t pitch)
{
    if(CONFIG_FPGA_AE350_CONTROL)return;
    if(voice>=ENGINE_FM_VOICES || pitch>0x7fffffu)return;
    portENTER_CRITICAL(&s_lock);if(fm_can_play()) {
        s_fm_notes[voice].pitch=pitch;
        s_fm_notes[voice].pitch_dirty=s_fm_notes[voice].sounding || s_fm_notes[voice].on;
    }portEXIT_CRITICAL(&s_lock);
}
void engine_link_fm_gain(unsigned voice,uint8_t volume,uint8_t pan)
{
    if(CONFIG_FPGA_AE350_CONTROL)return;
    if(voice>=ENGINE_FM_VOICES || volume>127 || pan>127)return;
    portENTER_CRITICAL(&s_lock);if(fm_can_play()) {
        s_fm_notes[voice].volume=volume;s_fm_notes[voice].pan=pan;
        s_fm_notes[voice].gain_dirty=s_fm_notes[voice].sounding || s_fm_notes[voice].on;
    }portEXIT_CRITICAL(&s_lock);
}
uint32_t engine_link_fm_parameter(uint32_t id,uint32_t target,uint32_t value)
{
    portENTER_CRITICAL(&s_lock);
    if(CONFIG_FPGA_AE350_CONTROL && !s_fm.parameter_count){portEXIT_CRITICAL(&s_lock);return FM_RESULT_STALE_EPOCH;}
    const fm_parameter_t *p=fm_parameter(s_fm.status.profile_id,id);
    uint32_t result=!fm_can_play()?FM_RESULT_BUSY:!p?FM_RESULT_UNSUPPORTED:
        !fm_validate_parameter(s_fm.status.profile_id,id,p->scope_id,target,value)?FM_RESULT_BAD_VALUE:FM_RESULT_OK;
    if(result==FM_RESULT_OK) {
        unsigned slot=0;
        while(slot<ENGINE_FM_PARAMETER_SLOTS && s_fm_parameters[slot].value.parameter_id &&
             (s_fm_parameters[slot].value.parameter_id!=id || s_fm_parameters[slot].value.target_word!=target))slot++;
        if(slot==ENGINE_FM_PARAMETER_SLOTS)result=FM_RESULT_BUSY;
        else {
            s_fm_parameters[slot].value=(engine_fm_parameter_t){id,target,value,0};
            s_fm_parameters[slot].revision++;s_fm_parameters[slot].dirty=true;
        }
    }
    portEXIT_CRITICAL(&s_lock);return result;
}
uint32_t engine_link_fm_preset(uint32_t id)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t result=!(s_fm.status.available && s_fm.status.valid && s_fm.status.selected &&
        s_fm.status.requested_backend && !s_fm.status.needs_select && !s_fm_mode_pending)?FM_RESULT_BUSY:
        !fm_preset(s_fm.status.profile_id,id)?FM_RESULT_BAD_VALUE:FM_RESULT_OK;
    if(!result){fm_clear_intents();s_fm_preset_id=id;s_fm_preset_revision++;s_fm_preset_pending=true;s_fm.status.switching=true;}
    portEXIT_CRITICAL(&s_lock);return result;
}
void engine_link_fm_panic(void)
{
    if(CONFIG_FPGA_AE350_CONTROL){
        portENTER_CRITICAL(&s_lock);fm_clear_intents();s_fm.status.switching=false;portEXIT_CRITICAL(&s_lock);return;
    }
    portENTER_CRITICAL(&s_lock);fm_clear_intents();s_fm_mode_revision++;s_fm_mode_pending=false;
    s_fm.status.requested_backend=s_fm.status.applied_backend;
    if(s_fm.status.available && (s_fm.status.selected || s_fm_inflight.active || s_fm.status.session_id)) {
        if(!s_fm_panic.sequence_id)s_fm_panic_pending=true;
        s_fm.status.switching=true;
    }
    portEXIT_CRITICAL(&s_lock);
}
void engine_link_fm_owner_ack(void)
{portENTER_CRITICAL(&s_lock);s_fm_owner_pending=false;portEXIT_CRITICAL(&s_lock);}
static bool fm_can_isolate_locked(void)
{
    if(CONFIG_FPGA_FM_ONLY || s_fm_inflight.active || s_fm_mode_pending ||
       s_fm_panic.sequence_id || s_fm_panic_pending || s_fm_owner_pending)return false;
    bool local=backend_local_quiet_locked(2) && (!s_fm.status.valid || !s_fm.status.selected);
    bool host=backend_host_scope_locked() && !engine_fm_control_active(&s_fm.status) &&
        (s_fm_quarantined || (s_fm.status.valid && (s_fm.status.flags&FM_FLAG_MUTED)));
    return local || host;
}
static void fm_fault(unsigned reason)
{
    // A failed read may leave a previous local generation's selected cache.
    // A coherent malformed read that explicitly reports SELECTED is different:
    // it contradicts the completed ENG1 route and must retain the global fence.
    portENTER_CRITICAL(&s_lock);
    bool isolate=fm_can_isolate_locked() && !(reason==FM_LINK_BACKEND && s_fm.status.selected);
    portEXIT_CRITICAL(&s_lock);
    if(isolate && backend_common_alive()) {
        portENTER_CRITICAL(&s_lock);
        if(fm_can_isolate_locked() && !(reason==FM_LINK_BACKEND && s_fm.status.selected)) {
            fm_clear_intents();s_fm_quarantined=true;
            s_fm.status.available=s_fm.status.valid=s_fm.status.ready=s_fm.status.selected=false;
            s_fm.status.requested_backend=s_fm.status.applied_backend=FM_BACKEND_UNBOUND;
            s_fm.status.switching=s_fm.status.recovering=s_fm.status.needs_select=false;
            s_fm.status.reason=reason;s_fm.parameter_count=0;
            s_fm_probe_at=esp_timer_get_time();
            portEXIT_CRITICAL(&s_lock);return;
        }
        portEXIT_CRITICAL(&s_lock);
    }
    s_fm_quarantined=false;
    if(s_local.supported && s_local.local_active) {
        portENTER_CRITICAL(&s_lock);
        fm_clear_intents();s_fm_inflight.active=false;
        s_fm.status.valid=s_fm.status.ready=false;
        s_fm.status.needs_select=s_fm.status.recovering=true;s_fm.status.reason=reason;
        s_local_health_fault=true;
        portEXIT_CRITICAL(&s_lock);raw_midi_pause(true);return;
    }
    portENTER_CRITICAL(&s_lock);bool notify=!s_fm.status.needs_select;
    uint32_t op=s_fm_inflight.command.opcode_id,seq=s_fm_inflight.command.sequence_id;
    uint32_t panic_seq=s_fm_panic.sequence_id,epoch=s_fm.status.engine_epoch;
    engine_fm_identity_t panic_wanted=s_fm_panic,panic_done=s_fm.status.panic_completed;
    int64_t now=esp_timer_get_time();
    long long normal_age=s_fm_inflight.active?now-s_fm_inflight.sent_at:-1;
    long long panic_age=panic_seq?now-s_fm_panic_at:-1;
    long long query_age=s_fm_query_pending?now-s_fm_query_at:-1;
    fm_clear_intents();s_fm_inflight.active=false;s_fm_mode_pending=false;
    s_fm_fault_panic=s_fm.status.selected || s_fm.status.session_id;
    s_fm_panic_pending=s_fm_fault_panic;s_fm_panic.sequence_id=0;
    s_fm.status.available=s_fm.status.valid=s_fm.status.ready=false;
    s_fm.status.needs_select=s_fm.status.recovering=s_fm.status.switching=true;
    if(!s_fm.status.selected)s_fm.status.applied_backend=FM_BACKEND_UNBOUND;
    s_fm.status.reason=reason;s_fm_owner_pending|=notify;s_fm_probe_at=0;
    s_fm.parameter_count=0;memset(s_fm_parameters,0,sizeof(s_fm_parameters));
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG,"FM recovery reason=%u epoch=%lu op=%lu seq=%lu normal_us=%lld panic_seq=%lu panic_us=%lld query_us=%lld coherent_us=%lld wanted=%lu/%lu/%lu done=%lu/%lu/%lu",
        reason,(unsigned long)epoch,(unsigned long)op,(unsigned long)seq,normal_age,
        (unsigned long)panic_seq,panic_age,query_age,(long long)(now-s_fm_coherent_at),
        (unsigned long)panic_wanted.session_id,(unsigned long)panic_wanted.engine_epoch,(unsigned long)panic_wanted.sequence_id,
        (unsigned long)panic_done.session_id,(unsigned long)panic_done.engine_epoch,(unsigned long)panic_done.sequence_id);
    if(notify){const evt_t e={.kind=EVT_PANIC};event_bus_publish(&e);}
}
static bool fm_probe(void)
{
    uint32_t caps[6], descriptor[7], sha[8];unsigned reason=FM_LINK_NO_CAPABILITY;
    if(!fpga_word_read_burst(FM_REG_CAP_MAGIC,caps,6))reason=FM_LINK_READBACK;
    else if(caps[0]==FM_CAP_MAGIC) {
        reason=FM_LINK_ABI;
        if(fm_abi_compatible(caps[1])) {
            reason=FM_LINK_PROFILE;
            // Wire backend ID and catalog profile ID are independent keys.
            const fm_profile_t *p=NULL;
            bool descriptor_ok=fpga_word_read_burst(FM_REG_SOURCE_RATE_NUMERATOR,descriptor,7) &&
                fpga_word_read_burst(FM_REG_PROFILE_SHA256_0,sha,8) &&
                !memcmp(sha,fm_profiles_sha256_words,sizeof(sha));
            if(descriptor_ok)for(size_t i=0;i<sizeof(fm_profiles)/sizeof(fm_profiles[0]);i++)
                if(fm_profiles[i].backend_id==caps[2] && fm_profiles[i].profile_version==descriptor[4] &&
                   !strcmp(fm_profiles[i].profile_sha256,FM_PROFILES_SHA256)){p=&fm_profiles[i];break;}
            if(p && caps[2]==FM_BACKEND_JT51_OPM && caps[3]==p->voice_count && caps[3]<=ENGINE_FM_VOICES &&
               caps[4]==FM_PROFILE_JT51_OPM_MAX_OPERATOR_COUNT && caps[5]==FM_REQUIRED_FEATURES &&
               descriptor[0] && descriptor[1] && descriptor[2] && descriptor[3] &&
               descriptor[4]==p->profile_version && descriptor[5]==p->parameter_count &&
               descriptor[6]==p->preset_count) {
                portENTER_CRITICAL(&s_lock);
                // A quarantined backend stays unavailable until the following
                // coherent read confirms it is still unselected and muted.
                if(!s_fm_quarantined)s_fm.status.available=true;
                if(!s_fm.status.needs_select && !s_fm_quarantined)s_fm.status.reason=0;
                s_fm.status.backend_id=p->backend_id;
                s_fm.status.profile_id=p->id;s_fm.status.profile_version=p->profile_version;
                s_fm.status.voice_count=caps[3];s_fm.status.features=caps[5];
                // The verified profile describes topology before SELECT.
                // Voice queries still fence generation and check this count;
                // leaving it zero makes HTTP clients unable to select FM.
                for(unsigned v=0;v<ENGINE_FM_VOICES;v++)
                    s_fm.operators_per_voice[v]=v<p->voice_count?p->operators_per_voice[v]:0;
                s_fm.source_numerator=descriptor[0];s_fm.source_denominator=descriptor[1];
                s_fm.output_numerator=descriptor[2];s_fm.output_denominator=descriptor[3];
                portEXIT_CRITICAL(&s_lock);
                // Bound the first coherent status read as well as later ones.
                s_fm_coherent_at=esp_timer_get_time();
                return true;
            }
        }
    }
    portENTER_CRITICAL(&s_lock);s_fm.status.reason=reason;portEXIT_CRITICAL(&s_lock);return false;
}
static void fm_cache_parameter(const fm_command_t *c)
{
    unsigned p=0;
    while(p<s_fm.parameter_count && (s_fm.parameters[p].parameter_id!=c->parameter_id || s_fm.parameters[p].target_word!=c->target_word))p++;
    if(p<ENGINE_FM_PARAMETER_SLOTS) {
        s_fm.parameters[p]=(engine_fm_parameter_t){c->parameter_id,c->target_word,c->value_u32,c->sequence_id};
        if(p==s_fm.parameter_count)s_fm.parameter_count++;
    }
}
static void fm_finish(void)
{
    const fm_command_t *c=&s_fm_inflight.command;
    bool ok=s_fm.status.result.code==FM_RESULT_OK;
    unsigned v=fm_target_voice(c->target_word),slot=s_fm_inflight.slot;
    if(c->opcode_id==FM_SPI_OPCODE_SELECT_BACKEND) {
        s_fm_sequence=0;s_fm_panic_sequence=0;s_fm.parameter_count=0;
        memset(s_fm_parameters,0,sizeof(s_fm_parameters));
        memset(s_fm.voice_generations,0,sizeof(s_fm.voice_generations));
        if(ok) {
            s_fm.status.applied_backend=c->value_u32;
            if(!s_fm_mode_pending)s_fm.status.requested_backend=c->value_u32;
        }
        if(ok && s_fm_inflight.revision==s_fm_mode_revision) {
            s_fm_mode_pending=false;s_fm.status.recovering=false;
            s_fm.status.reason=0;
        }
    } else if(c->opcode_id==FM_SPI_OPCODE_APPLY_PRESET) {
        if(ok) {
            const fm_preset_t *preset=fm_preset(s_fm.status.profile_id,c->value_u32);
            s_fm.parameter_count=0;
            if(preset)for(unsigned p=0;p<preset->values_count;p++) {
                fm_command_t item=*c;item.parameter_id=preset->values[p].parameter_id;
                fm_pack_target(FM_TARGET_NONE,preset->values[p].operator_index,&item.target_word);
                item.value_u32=preset->values[p].value_u32;
                fm_cache_parameter(&item);
            }
        }
        if(s_fm_inflight.revision==s_fm_preset_revision)s_fm_preset_pending=false;
    } else if(c->opcode_id==FM_SPI_OPCODE_SET_PARAMETER) {
        if(ok)fm_cache_parameter(c);
        if(s_fm_parameters[slot].revision==s_fm_inflight.revision)s_fm_parameters[slot].dirty=false;
    } else if(v<ENGINE_FM_VOICES) {
        if(c->opcode_id==FM_SPI_OPCODE_NOTE_ON) {
            s_fm_notes[v].sounding=ok;
            if(ok)s_fm.voice_generations[v]=c->voice_generation;
            if(s_fm_notes[v].revision==s_fm_inflight.revision)s_fm_notes[v].on=false;
        } else if(c->opcode_id==FM_SPI_OPCODE_NOTE_OFF)s_fm_notes[v].off=s_fm_notes[v].sounding=false;
        else if(c->opcode_id==FM_SPI_OPCODE_SET_PITCH && s_fm_notes[v].pitch==c->value_u32)s_fm_notes[v].pitch_dirty=false;
        else if(c->opcode_id==FM_SPI_OPCODE_SET_GAIN_PAN && s_fm_notes[v].volume==c->value_u32 && s_fm_notes[v].pan==c->arg0_u32)s_fm_notes[v].gain_dirty=false;
    }
    s_fm_inflight.active=false;
}
static bool fm_readback(void)
{
    uint32_t first, core[10], identity[6], panic[6], last;
    if(!fpga_word_read_burst(FM_REG_STATUS_GENERATION,&first,1) ||
       !fpga_word_read_burst(FM_REG_EPOCH,core,10) ||
       !fpga_word_read_burst(FM_REG_RESULT_SESSION,identity,6) ||
       !fpga_word_read_burst(FM_REG_PANIC_ACCEPTED_SEQUENCE,panic,6) ||
       !fpga_word_read_burst(FM_REG_STATUS_GENERATION,&last,1)) {fm_fault(FM_LINK_READBACK);return false;}
    if(first!=last) {
        // FPGA-local music may advance status between SPI frames. Keep the
        // complete verified image; never publish fields from this torn read.
        // fm_service bounds its age and dispatches no command on this path.
        return false;
    }
    s_fm_coherent_at=esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    s_fm_status_generation=last;
    uint32_t old_session=s_fm.status.session_id, old_epoch=s_fm.status.engine_epoch, old_topo=s_fm.status.topology_generation;
    engine_fm_status_t *s=&s_fm.status;
    s->engine_epoch=core[0];s->phase=core[1];s->topology_generation=core[6];s->flags=core[8];s->session_id=core[9];
    s->accepted=(engine_fm_identity_t){identity[2],identity[3],core[2]};
    s->completed=(engine_fm_identity_t){identity[4],identity[5],core[3]};
    s->result=(engine_fm_result_t){{identity[0],identity[1],core[4]},core[5]};
    s->panic_accepted=(engine_fm_identity_t){panic[4],panic[5],panic[0]};
    s->panic_completed=(engine_fm_identity_t){panic[4],panic[5],panic[1]};
    s->panic_result=(engine_fm_result_t){{panic[4],panic[5],panic[2]},panic[3]};
    s->selected=(s->flags&FM_FLAG_SELECTED)!=0;s->ready=(s->flags&FM_FLAG_READY)!=0;
    if(s->flags&FM_FLAG_MUTED) {
        // Cancel musical intent without disturbing explicit SELECT/preset,
        // priority Panic or safe Off. Unmuting never replays an old On/value.
        for(unsigned v=0;v<ENGINE_FM_VOICES;v++) {
            if(s_fm_notes[v].on || s_fm_notes[v].pitch_dirty || s_fm_notes[v].gain_dirty)s_fm_notes[v].revision++;
            s_fm_notes[v].on=s_fm_notes[v].pitch_dirty=s_fm_notes[v].gain_dirty=false;
        }
        for(unsigned p=0;p<ENGINE_FM_PARAMETER_SLOTS;p++) {
            if(s_fm_parameters[p].dirty)s_fm_parameters[p].revision++;
            s_fm_parameters[p].dirty=false;
        }
        uint32_t op=s_fm_inflight.command.opcode_id;
        if(s_fm_inflight.active && (op==FM_SPI_OPCODE_NOTE_ON || op==FM_SPI_OPCODE_SET_PITCH ||
            op==FM_SPI_OPCODE_SET_GAIN_PAN || op==FM_SPI_OPCODE_SET_PARAMETER))s_fm_inflight.cancelled=true;
    }
    bool valid=!(s->flags&~FM_KNOWN_FLAGS) && s->engine_epoch && s->topology_generation && s->phase<=FM_PHASE_BLOCKED;
    bool changed=old_epoch && (old_session!=s->session_id || old_epoch!=s->engine_epoch || old_topo!=s->topology_generation);
    bool expected=(s_local_fm_adopting && (!old_topo || old_topo==s->topology_generation)) ||
        (s_fm_inflight.active && s_fm_inflight.command.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND &&
        s->session_id==s_fm_inflight.command.arg0_u32 && s->engine_epoch==(s_fm_inflight.command.engine_epoch==UINT32_MAX?1:s_fm_inflight.command.engine_epoch+1) && s->topology_generation==old_topo) ||
        (s_fm_panic.sequence_id && s->session_id==s_fm_panic.session_id && s->engine_epoch==(s_fm_panic.engine_epoch==UINT32_MAX?1:s_fm_panic.engine_epoch+1) && s->topology_generation==old_topo);
    s->valid=valid;
    bool fault=!valid || (s->flags&FM_FLAG_FAULT) || (changed && !expected) ||
        (!old_epoch && s->selected && !s_fm_mode_pending && !s->needs_select && !s_local_fm_adopting);
    if(!fault && s_fm_inflight.active) {
        engine_fm_identity_t wanted=fm_identity(&s_fm_inflight.command);
        uint32_t code=s->result.code;
        if(fm_identity_equal(wanted,s->result.identity) && code!=FM_RESULT_OK && code!=FM_RESULT_BUSY &&
           (s_fm_inflight.command.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND || code==FM_RESULT_STALE_EPOCH ||
            code==FM_RESULT_ABI_MISMATCH || code==FM_RESULT_BACKEND_FAULT || code==FM_RESULT_SEQUENCE_CONFLICT ||
            code==FM_RESULT_STALE_SESSION || code==FM_RESULT_STALE_TOPOLOGY))fault=true;
        s_fm_inflight.accepted|=fm_identity_equal(wanted,s->accepted);
        if(!fault && fm_identity_equal(wanted,s->completed) && fm_identity_equal(wanted,s->result.identity))fm_finish();
        else if(!fault && fm_identity_equal(wanted,s->result.identity) && s->result.code!=FM_RESULT_OK) {
            if(s->result.code==FM_RESULT_BUSY && !s_fm_inflight.accepted)s_fm_inflight.retry=true;
            else if(!s_fm_inflight.accepted)fm_finish();
        }
    }
    // The priority channel shares one result identity with its sequence
    // markers. A reused sequence may still be the previous completion while
    // this Panic is accepted and BUSY; wait for the backend cancellation fence.
    if(!fault && !(s->flags&FM_FLAG_BUSY) && s_fm_panic.sequence_id && fm_identity_equal(s_fm_panic,s->panic_completed) &&
       fm_identity_equal(s_fm_panic,s->panic_result.identity) && s->panic_result.code==FM_RESULT_OK) {
        s_fm_panic.sequence_id=0;s_fm_sequence=s_fm_panic_sequence=0;s_fm_query_pending=false;
        s_fm_fault_panic=false;
        memset(s_fm.voice_generations,0,sizeof(s_fm.voice_generations));
    }
    if(!fault && !s_fm_inflight.active && !s_fm_panic.sequence_id && !s_fm_panic_pending &&
       !s_fm_mode_pending && !s_fm_preset_pending && !s_fm_owner_pending && !s->needs_select && s->ready && !(s->flags&FM_FLAG_BUSY))
        s->switching=false;
    portEXIT_CRITICAL(&s_lock);
    if(fault) {
        // A key can arrive after the service's first ENG1 read. Recheck its
        // generation before converting the new context into a hardware Panic.
        if(!s_local_fm_adopting && s_local.supported && local_mode_poll())return false;
        fm_fault(valid?FM_LINK_CONTEXT:FM_LINK_BACKEND);return false;
    }
    return true;
}
static bool fm_query(unsigned voice)
{
    if(!s_fm_query_pending) {
        s_fm_query_voice=voice;s_fm_query_session=s_fm.status.session_id;s_fm_query_epoch=s_fm.status.engine_epoch;
        s_fm_query_topology=s_fm.status.topology_generation;s_fm_query_at=esp_timer_get_time();
        s_fm_query_pending=true;fpga_word_write(FM_REG_VOICE_QUERY_INDEX,voice);return false;
    }
    uint32_t a,b,words[3];
    if(!fpga_word_read_burst(FM_REG_STATUS_GENERATION,&a,1) ||
       !fpga_word_read_burst(FM_REG_VOICE_QUERY_INDEX,words,3) ||
       !fpga_word_read_burst(FM_REG_STATUS_GENERATION,&b,1)){fm_fault(FM_LINK_READBACK);return false;}
    if(s_fm_query_session!=s_fm.status.session_id || s_fm_query_epoch!=s_fm.status.engine_epoch || s_fm_query_topology!=s_fm.status.topology_generation) {s_fm_query_pending=false;return false;}
    if(a!=b || a!=s_fm_status_generation || words[0]!=s_fm_query_voice) {
        if(esp_timer_get_time()-s_fm_query_at>=FM_CONFIRM_TIMEOUT_US)fm_fault(FM_LINK_TIMEOUT);
        return false;
    }
    uint32_t expected;
    if(!fm_profile_operator_count(s_fm.status.profile_id,s_fm_query_voice,&expected) || words[2]!=expected){fm_fault(FM_LINK_PROFILE);return false;}
    portENTER_CRITICAL(&s_lock);s_fm.voice_generations[s_fm_query_voice]=words[1];s_fm.operators_per_voice[s_fm_query_voice]=words[2];portEXIT_CRITICAL(&s_lock);
    bool matches=s_fm_query_voice==voice;s_fm_query_pending=false;return matches;
}
static void fm_commit(void)
{
    uint32_t words[FM_COMMAND_WORD_COUNT];fm_command_t c=s_fm_inflight.command;
    if(!fm_serialize_command(&c,words,FM_COMMAND_WORD_COUNT))return;
    fpga_word_write_burst(FM_REG_CMD_OPCODE_ID,words,FM_COMMAND_WORD_COUNT);
    portENTER_CRITICAL(&s_lock);
    bool cancel=s_fm_inflight.cancelled || s_fm_inflight.cancel_revision!=s_fm_cancel_revision ||
        (c.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND && (!s_fm_mode_pending || s_fm_inflight.revision!=s_fm_mode_revision)) ||
        (c.opcode_id==FM_SPI_OPCODE_NOTE_ON &&
        (!s_fm_notes[s_fm_inflight.slot].on || s_fm_notes[s_fm_inflight.slot].revision!=s_fm_inflight.revision)) || s_fm_panic_pending;
    if(cancel){s_fm_inflight.active=false;portEXIT_CRITICAL(&s_lock);return;}
    if(!s_fm_inflight.sent_at)s_fm_inflight.sent_at=esp_timer_get_time();
    s_fm_inflight.retry=false;
    portEXIT_CRITICAL(&s_lock);
    fpga_word_write(FM_REG_CMD_COMMIT,c.sequence_id);
}
static bool fm_poll_due(bool other_engines_silent,int64_t now)
{
    portENTER_CRITICAL(&s_lock);
    bool urgent=s_fm_inflight.active || s_fm_panic.sequence_id || s_fm_query_pending ||
        (s_fm_panic_pending && (!s_fm.status.needs_select || s_fm_fault_panic)) ||
        (s_fm_mode_pending && (other_engines_silent || !s_fm.status.requested_backend)) ||
        s_fm_preset_pending;
    for(unsigned v=0;v<ENGINE_FM_VOICES && !urgent;v++)
        urgent=s_fm_notes[v].on || s_fm_notes[v].off || s_fm_notes[v].pitch_dirty || s_fm_notes[v].gain_dirty;
    for(unsigned p=0;p<ENGINE_FM_PARAMETER_SLOTS && !urgent;p++)urgent=s_fm_parameters[p].dirty;
    // A SELECT awaiting DDS silence must leave time for that silence to be
    // committed. Musical commands and their acknowledgement keep the 1 kHz
    // service; idle health/query traffic runs at 50 Hz, even when selected.
    bool due=urgent || !s_fm_poll_at || now-s_fm_poll_at>=FM_IDLE_POLL_INTERVAL_US;
    if(due)s_fm_poll_at=now;
    portEXIT_CRITICAL(&s_lock);
    return due;
}
static void fm_service(bool other_engines_silent)
{
    int64_t now=esp_timer_get_time();
    if(!fm_poll_due(other_engines_silent,now))return;
    if(!s_fm.status.available) {
        if(s_fm_probe_at && now-s_fm_probe_at<FM_PROBE_INTERVAL_US)return;
        s_fm_probe_at=now;if(!fm_probe())return;
    }
    if(!fm_readback()) {
        if(s_fm.status.available && now-s_fm_coherent_at>=FM_CONFIRM_TIMEOUT_US)fm_fault(FM_LINK_TIMEOUT);
        return;
    }
    if(s_fm_quarantined) {
        portENTER_CRITICAL(&s_lock);
        bool quiet=s_fm.status.valid && !s_fm.status.selected && (s_fm.status.flags&FM_FLAG_MUTED);
        if(quiet) {
            s_fm.status.available=true;s_fm.status.reason=0;s_fm_quarantined=false;
        }
        portEXIT_CRITICAL(&s_lock);
        if(!quiet)fm_fault(FM_LINK_CONTEXT);
        return;
    }
    if(s_fm_panic.sequence_id) {
        if(now-s_fm_panic_at>=FM_CONFIRM_TIMEOUT_US)fm_fault(FM_LINK_TIMEOUT);
        return;
    }
    portENTER_CRITICAL(&s_lock);
    if(s_fm_fault_panic && !s_fm.status.session_id && !s_fm.status.selected) {
        // Native reset already cleared the backend. Keep the explicit SELECT
        // fence, without writing a Panic into the fresh unbound session.
        s_fm_fault_panic=s_fm_panic_pending=false;
    }
    bool panic=s_fm_panic_pending && (!s_fm.status.needs_select || s_fm_fault_panic);
    if(panic) {
        s_fm_panic_pending=false;s_fm_inflight.active=false;
        if(++s_fm_panic_sequence==0)++s_fm_panic_sequence;
        s_fm_panic=(engine_fm_identity_t){s_fm.status.session_id,s_fm.status.engine_epoch,s_fm_panic_sequence};s_fm_panic_at=now;
    }
    portEXIT_CRITICAL(&s_lock);
    if(panic) {
        // This path never waits on normal mailbox BUSY.
        uint32_t words[2]={s_fm_panic.session_id,s_fm_panic.engine_epoch};
        fpga_word_write_burst(FM_REG_PANIC_SESSION,words,2);fpga_word_write(FM_REG_PANIC_SEQUENCE,s_fm_panic.sequence_id);return;
    }
    if(s_fm_inflight.active) {
        if(now-s_fm_inflight.sent_at>=FM_CONFIRM_TIMEOUT_US){fm_fault(FM_LINK_TIMEOUT);return;}
        if(s_fm_inflight.retry && !(s_fm.status.flags&FM_FLAG_BUSY))fm_commit();
        return;
    }
    if(s_fm.status.needs_select || s_fm_owner_pending || !s_fm.status.ready || (s_fm.status.flags&FM_FLAG_BUSY))return;
    fm_command_t c={.scope_id=FM_SCOPE_ENGINE,.target_word=UINT32_MAX,
        .session_id=s_fm.status.session_id,.engine_epoch=s_fm.status.engine_epoch,.topology_generation=s_fm.status.topology_generation};
    unsigned slot=0;uint32_t revision=0,cancel_revision;bool found=false;
    portENTER_CRITICAL(&s_lock);
    cancel_revision=s_fm_cancel_revision;
    if(s_fm_mode_pending && (other_engines_silent || !s_fm.status.requested_backend)) {
        c.opcode_id=FM_SPI_OPCODE_SELECT_BACKEND;c.value_u32=s_fm.status.requested_backend;
        do{s_fm_nonce=esp_random();}while(!s_fm_nonce || s_fm_nonce==s_fm.status.session_id);
        c.arg0_u32=s_fm_nonce;revision=s_fm_mode_revision;found=true;
    } else if(!s_fm_mode_pending && s_fm.status.selected) {
        if(s_fm_preset_pending && engine_fm_bound_ready(&s_fm.status)) {c.opcode_id=FM_SPI_OPCODE_APPLY_PRESET;c.scope_id=FM_SCOPE_PATCH;c.value_u32=s_fm_preset_id;revision=s_fm_preset_revision;found=true;}
        else {
            // Off for any voice precedes all NoteOn and parameter work.
            for(unsigned n=0;n<ENGINE_FM_VOICES;n++) {unsigned v=(s_fm_cursor+n)%ENGINE_FM_VOICES;
                if(s_fm_notes[v].off){slot=v;c.opcode_id=FM_SPI_OPCODE_NOTE_OFF;found=true;break;}}
            if(!found && !s_fm_preset_pending && fm_can_play())for(unsigned n=0;n<ENGINE_FM_VOICES;n++) {unsigned v=(s_fm_cursor+n)%ENGINE_FM_VOICES;
                if(s_fm_notes[v].on){slot=v;c.opcode_id=FM_SPI_OPCODE_NOTE_ON;found=true;break;}
                if(s_fm_notes[v].pitch_dirty){slot=v;c.opcode_id=FM_SPI_OPCODE_SET_PITCH;found=true;break;}
                if(s_fm_notes[v].gain_dirty){slot=v;c.opcode_id=FM_SPI_OPCODE_SET_GAIN_PAN;found=true;break;}}
            if(found) {
                c.scope_id=FM_SCOPE_VOICE;fm_pack_target(slot,FM_TARGET_NONE,&c.target_word);revision=s_fm_notes[slot].revision;
                if(c.opcode_id==FM_SPI_OPCODE_NOTE_ON) {c.value_u32=s_fm_notes[slot].pitch;c.arg0_u32=s_fm_notes[slot].velocity|((uint32_t)s_fm_notes[slot].volume<<8);c.arg1_u32=s_fm_notes[slot].pan;}
                if(c.opcode_id==FM_SPI_OPCODE_SET_PITCH)c.value_u32=s_fm_notes[slot].pitch;
                if(c.opcode_id==FM_SPI_OPCODE_SET_GAIN_PAN){c.value_u32=s_fm_notes[slot].volume;c.arg0_u32=s_fm_notes[slot].pan;}
            } else if(!s_fm_preset_pending && fm_can_play())for(unsigned p=0;p<ENGINE_FM_PARAMETER_SLOTS;p++)if(s_fm_parameters[p].dirty) {
                slot=p;c.opcode_id=FM_SPI_OPCODE_SET_PARAMETER;c.parameter_id=s_fm_parameters[p].value.parameter_id;
                c.scope_id=fm_parameter(s_fm.status.profile_id,c.parameter_id)->scope_id;
                c.target_word=s_fm_parameters[p].value.target_word;c.value_u32=s_fm_parameters[p].value.value_u32;
                revision=s_fm_parameters[p].revision;found=true;break;
            }
        }
    }
    portEXIT_CRITICAL(&s_lock);
    if(!found) {
        // Refresh each generation through the applied query fence while idle.
        if(s_fm.status.selected && fm_query(s_fm_cursor))s_fm_cursor=(s_fm_cursor+1)%ENGINE_FM_VOICES;
        return;
    }
    if(c.scope_id==FM_SCOPE_VOICE) {
        if(!fm_query(slot))return;
        c.voice_generation=s_fm.voice_generations[slot];
        if(c.opcode_id==FM_SPI_OPCODE_NOTE_ON && ++c.voice_generation==0)c.voice_generation=1;
    }
    // Reserve the high-bit namespace for FPGA-local performance. At exhaustion,
    // use the existing recovery/Panic fence and require a new explicit SELECT;
    // never reuse a sequence in the current session/epoch.
    if(s_fm_sequence>=FM_SPI_SEQUENCE_MAX){fm_fault(FM_LINK_CONTEXT);return;}
    portENTER_CRITICAL(&s_lock);
    if(cancel_revision!=s_fm_cancel_revision || s_fm_panic_pending ||
       (c.opcode_id==FM_SPI_OPCODE_SELECT_BACKEND && (!s_fm_mode_pending || revision!=s_fm_mode_revision))) {
        portEXIT_CRITICAL(&s_lock);return;
    }
    c.sequence_id=++s_fm_sequence;
    s_fm_inflight.command=c;s_fm_inflight.revision=revision;s_fm_inflight.slot=slot;
    s_fm_inflight.active=true;s_fm_inflight.accepted=s_fm_inflight.retry=s_fm_inflight.cancelled=false;
    s_fm_inflight.cancel_revision=cancel_revision;
    s_fm_inflight.sent_at=0;
    portEXIT_CRITICAL(&s_lock);s_fm_cursor=(slot+1)%ENGINE_FM_VOICES;fm_commit();
}
static void fm_init(void)
{
    memset(&s_fm,0,sizeof(s_fm));memset(s_fm_notes,0,sizeof(s_fm_notes));memset(s_fm_parameters,0,sizeof(s_fm_parameters));
    memset(&s_fm_inflight,0,sizeof(s_fm_inflight));memset(&s_fm_panic,0,sizeof(s_fm_panic));
    s_fm_panic_pending=s_fm_owner_pending=s_fm_mode_pending=s_fm_preset_pending=s_fm_query_pending=false;
    s_fm_fault_panic=false;
    s_fm_quarantined=false;
    s_fm_sequence=s_fm_panic_sequence=s_fm_mode_revision=s_fm_cursor=0;s_fm_probe_at=0;
    s_fm_cancel_revision=s_fm_preset_revision=0;
    s_fm_coherent_at=s_fm_poll_at=0;
}
