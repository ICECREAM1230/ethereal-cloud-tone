// Private implementation included by engine_link.c: no second runtime SPI task.
static engine_ks_state_t s_ks;
static struct {
    engine_ks_note_t note;
    uint32_t generation;
    bool on, off, gain, loss;
} s_ks_desired[ENGINE_KS_VOICES];
static struct {
    uint32_t sequence, generation, reject_before;
    int64_t sent_at;
    uint8_t op, voice;
    bool mode;
} s_ks_inflight;
static uint32_t s_ks_sequence, s_ks_mode_generation, s_ks_mode_sent;
static bool s_ks_panic_pending;
static unsigned s_ks_cursor;
static bool s_ks_seen, s_ks_need_probe, s_ks_owner_pending, s_ks_wait_bank_reset;
static int64_t s_ks_prepare_at;
static bool s_ks_quarantined;
static int64_t s_ks_probe_at;
static int64_t s_ks_read_at;
#define KS_IDLE_PROBE_US 20000LL
#define KS_CONFIRM_TIMEOUT_US 50000LL
#define KS_PREPARE_TIMEOUT_US 1000000LL
enum { KS_FAULT_TIMEOUT=1, KS_FAULT_READBACK, KS_FAULT_RESET, KS_FAULT_PREPARE_TIMEOUT };
static void ks_silence_pending(uint32_t *gates, uint32_t *shadow)
{
    *gates=*shadow=0;
    for(unsigned v=0;v<32;v++) {
        unsigned bus=GATE_BASE+v;
        if(s_bus_seq[bus]!=s_bus_done[bus] || s_bus[bus])*gates|=1u<<v;
    }
    if(s_bus_seq[1023]!=s_bus_done[1023] || s_bus[1023])(*shadow)++;
    for(unsigned e=0;e<ENGINE_NUM_ELEMENTS;e++) {
        unsigned bit=e*ENGINE_WORDS_PER_ELEMENT+4;
        if(s_desired[e][4] || s_image[e][4] || (s_pending[bit>>5]&(1u<<(bit&31))))(*shadow)++;
    }
}

void engine_link_owner_panic_ack(void)
{
    portENTER_CRITICAL(&s_lock);s_ks_owner_pending=false;portEXIT_CRITICAL(&s_lock);
}
static bool ks_can_isolate_locked(void)
{
    if(CONFIG_FPGA_FM_ONLY || s_ks_inflight.sequence || s_ks_panic_pending || s_ks_owner_pending)return false;
    bool local=backend_local_quiet_locked(1) && (!s_ks.valid || !s_ks.applied_mode);
    bool host=backend_host_scope_locked() && !s_ks.requested_mode && !s_ks.applied_mode &&
        !s_ks.switching && !s_ks.recovering && !s_ks.busy && s_ks_mode_generation==s_ks_mode_sent &&
        (s_ks_quarantined || (s_ks.available && s_ks.valid));
    return local || host;
}
static void ks_recover(unsigned fault)
{
    portENTER_CRITICAL(&s_lock);bool isolate=ks_can_isolate_locked();portEXIT_CRITICAL(&s_lock);
    if(isolate && backend_common_alive()) {
        portENTER_CRITICAL(&s_lock);
        if(ks_can_isolate_locked()) {
            if(!s_ks_quarantined)s_ks.recovery_count++;
            s_ks_quarantined=true;s_ks_probe_at=esp_timer_get_time()+KS_IDLE_PROBE_US;
            s_ks.available=s_ks.valid=s_ks.switching=s_ks.recovering=false;
            s_ks.requested_mode=s_ks.applied_mode=false;s_ks.fault=fault;s_ks_need_probe=true;
            for(unsigned v=0;v<ENGINE_KS_VOICES;v++) {
                s_ks_desired[v].generation++;
                s_ks_desired[v].on=s_ks_desired[v].off=s_ks_desired[v].gain=s_ks_desired[v].loss=false;
                s_ks.note_sequence[v]=0;
            }
            portEXIT_CRITICAL(&s_lock);return;
        }
        portEXIT_CRITICAL(&s_lock);
    }
    s_ks_quarantined=false;
    if(s_local.supported && s_local.local_active) {
        portENTER_CRITICAL(&s_lock);
        s_ks.valid=false;s_ks.recovering=true;s_ks.fault=fault;s_local_health_fault=true;
        portEXIT_CRITICAL(&s_lock);raw_midi_pause(true);return;
    }
    portENTER_CRITICAL(&s_lock);
    if(fault==KS_FAULT_PREPARE_TIMEOUT && (s_ks.recovering || !s_ks.switching ||
       s_ks_inflight.sequence || esp_timer_get_time()-s_ks_prepare_at<KS_PREPARE_TIMEOUT_US)) {
        portEXIT_CRITICAL(&s_lock);return;
    }
    // One fault episode publishes one owner panic. A lost recovery command
    // can restart the transport without requiring the owner to panic again.
    bool notify=!s_ks.recovering;
    if(!notify && !s_ks_inflight.sequence && fault!=KS_FAULT_RESET) {
        portEXIT_CRITICAL(&s_lock);return;
    }
    bool rebuild=notify || fault==KS_FAULT_RESET;
    s_ks.fault_sequence=s_ks_inflight.sequence;s_ks.fault_op=s_ks_inflight.sequence?s_ks_inflight.op:0;
    s_ks.recovering=true;s_ks.switching=true;s_ks.requested_mode=false;
    s_ks.valid=false;s_ks.fault=fault;s_ks.recovery_count++;
    if(fault==KS_FAULT_TIMEOUT || fault==KS_FAULT_PREPARE_TIMEOUT)s_ks.timeout_count++;
    s_ks_prepare_at=esp_timer_get_time();
    s_ks_mode_generation++;s_ks_mode_sent=0;s_ks_inflight.sequence=0;
    s_ks_panic_pending=true;s_ks_need_probe=true;
    s_ks_owner_pending|=notify;
    for(unsigned v=0;v<ENGINE_KS_VOICES;v++) {
        s_ks_desired[v].generation++;
        s_ks_desired[v].on=s_ks_desired[v].off=false;
        s_ks_desired[v].gain=s_ks_desired[v].loss=false;
        s_ks.note_sequence[v]=0;
    }
    // A new fault/reset may have erased or interrupted a DDS bank swap.
    // Rebuild both banks from muted desired state; retain patch parameters.
    // Retrying a lost recovery acknowledgement must preserve this progress:
    // a full render may legitimately take longer than a mailbox timeout.
    if(rebuild) {
        s_ks_wait_bank_reset=true;
        s_flushing=false;s_swap_requested=false;
        memset(s_dirty_now,0,sizeof(s_dirty_now));memset(s_dirty_prev,0,sizeof(s_dirty_prev));
        memset(s_pdirty_now,0,sizeof(s_pdirty_now));memset(s_pdirty_prev,0,sizeof(s_pdirty_prev));
        memset(s_pending,0xff,sizeof(s_pending));memset(s_ppending,0xff,sizeof(s_ppending));
        for(unsigned e=0;e<ENGINE_NUM_ELEMENTS;e++)s_desired[e][4]=0;
        for(unsigned v=0;v<32;v++) {
            s_notes[v].on=s_notes[v].ready=false;s_notes[v].seq++;s_notes[v].snapshot_seq=0;
            s_bus[GATE_BASE+v]=0;
        }
        s_bus[1023]=0;
        for(unsigned bus=1;bus<1024;bus++) {s_bus_seq[bus]++;mark(s_bus_dirty,bus);}
    }
    portEXIT_CRITICAL(&s_lock);
    if(notify) {
        ESP_LOGW(TAG,"KS recovery fault=%u op=%u sequence=%lu",fault,
                 (unsigned)s_ks.fault_op,(unsigned long)s_ks.fault_sequence);
        // Never publish while holding the engine lock: the owner will clear
        // MIDI/web held keys and acknowledge before safe DDS mode completes.
        const evt_t event={.kind=EVT_PANIC};event_bus_publish(&event);
    }
}

void engine_link_ks_state(engine_ks_state_t *state)
{
    engine_fm_status_t fm;engine_link_fm_status(&fm);
    int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&s_lock); *state=s_ks;
    state->mode_generation=s_ks_mode_generation;state->mode_sent_generation=s_ks_mode_sent;
    state->inflight_sequence=s_ks_inflight.sequence;state->inflight_op=s_ks_inflight.sequence?s_ks_inflight.op:0;
    state->owner_pending=s_ks_owner_pending;state->bank_pending=s_ks_wait_bank_reset || s_swap_requested;
    state->flushing=s_flushing;state->control_status=s_control_status;
    state->control_status_valid=s_control_status_valid;
    ks_silence_pending(&state->gate_off_pending,&state->shadow_gate_pending);
    state->wait_reason=KS_WAIT_NONE;state->wait_ms=0;
    if(s_ks.switching) {
        int64_t elapsed=(now-s_ks_prepare_at)/1000;
        state->wait_ms=elapsed>UINT32_MAX?UINT32_MAX:elapsed>0?(uint32_t)elapsed:0;
        state->wait_reason=s_ks_inflight.sequence?KS_WAIT_ACK:s_ks.busy?KS_WAIT_BUSY:
            s_ks_owner_pending?KS_WAIT_OWNER:state->bank_pending?KS_WAIT_BANK:
            (state->gate_off_pending || state->shadow_gate_pending || s_flushing)?KS_WAIT_DDS:
            (s_ks.requested_mode && (fm.selected || fm.applied_backend || fm.switching || fm.recovering || fm.needs_select))?KS_WAIT_FM:KS_WAIT_MODE;
    }
    state->pending_mask=0;
    for(unsigned v=0;v<ENGINE_KS_VOICES;v++) if(s_ks_desired[v].on || s_ks_desired[v].off ||
       (s_ks_inflight.sequence && s_ks_inflight.voice==v)) state->pending_mask|=1u<<v;
    portEXIT_CRITICAL(&s_lock);
}
bool engine_link_ks_note_on(unsigned voice, const engine_ks_note_t *note)
{
    if(voice>=ENGINE_KS_VOICES || !note || note->length<2 || note->length>4096 ||
       note->tuning==INT16_MIN || note->loss>32767 || note->amplitude>32767) return false;
    portENTER_CRITICAL(&s_lock);
    bool allowed=s_ready && s_ks.available && s_ks.valid && s_ks.requested_mode &&
                 s_ks.applied_mode && !s_ks.switching;
    if(allowed) {
        s_ks_desired[voice].generation++;
        s_ks_desired[voice].note=*note;
        s_ks_desired[voice].on=true;
        s_ks_desired[voice].gain=s_ks_desired[voice].loss=false;
    }
    portEXIT_CRITICAL(&s_lock);
    return allowed;
}
void engine_link_ks_note_off(unsigned voice)
{
    if(voice>=ENGINE_KS_VOICES)return;
    portENTER_CRITICAL(&s_lock);
    s_ks_desired[voice].generation++;
    s_ks_desired[voice].on=false;
    s_ks_desired[voice].gain=s_ks_desired[voice].loss=false;
    s_ks_desired[voice].off=true;
    portEXIT_CRITICAL(&s_lock);
}
void engine_link_ks_panic(void)
{
    portENTER_CRITICAL(&s_lock);
    for(unsigned v=0;v<ENGINE_KS_VOICES;v++) {
        s_ks_desired[v].generation++;
        s_ks_desired[v].on=s_ks_desired[v].off=false;
        s_ks_desired[v].gain=s_ks_desired[v].loss=false;
    }
    s_ks_panic_pending=s_ks.available;
    portEXIT_CRITICAL(&s_lock);
}
bool engine_link_ks_mode(bool mode)
{
    portENTER_CRITICAL(&s_lock);
    if(!mode && s_ks_quarantined && ks_can_isolate_locked()) {
        portEXIT_CRITICAL(&s_lock);return true;
    }
    if(s_ks.recovering || (mode && !s_ks.available)) {portEXIT_CRITICAL(&s_lock);return false;}
    s_ks.requested_mode=mode;
    s_ks.switching=s_ks.available;
    s_ks_mode_generation++;
    s_ks_prepare_at=esp_timer_get_time();
    portEXIT_CRITICAL(&s_lock);
    engine_link_ks_panic();
    return true;
}
void engine_link_ks_gain(unsigned voice, uint16_t left, uint16_t right)
{
    if(voice>=ENGINE_KS_VOICES)return;
    portENTER_CRITICAL(&s_lock);
    s_ks_desired[voice].note.gain_left=left;s_ks_desired[voice].note.gain_right=right;
    s_ks_desired[voice].gain=true;
    portEXIT_CRITICAL(&s_lock);
}
void engine_link_ks_loss(unsigned voice, uint16_t loss)
{
    if(voice>=ENGINE_KS_VOICES || loss>32767)return;
    portENTER_CRITICAL(&s_lock);
    s_ks_desired[voice].note.loss=loss;s_ks_desired[voice].loss=true;
    portEXIT_CRITICAL(&s_lock);
}
static void ks_probe(void)
{
    uint32_t words[2];
    bool available=fpga_word_read_burst(0x0500,words,2) &&
        words[0]==ENGINE_KS_CAPABILITY && words[1]==ENGINE_KS_VERSION;
    portENTER_CRITICAL(&s_lock);
    s_ks.available=available;
    if(available) {s_ks.switching=true;s_ks_mode_generation=1;s_ks_panic_pending=true;s_ks_prepare_at=esp_timer_get_time();}
    portEXIT_CRITICAL(&s_lock);
}
static bool ks_readback(void)
{
    uint32_t words[6], masks[3], started[ENGINE_KS_VOICES];
    if(!fpga_word_read_burst(0x0502,words,6) || !fpga_word_read_burst(0x0508,masks,3) ||
       !fpga_word_read_burst(0x0540,started,8) ||
       !fpga_word_read_burst(0x0548,started+8,8)) {
        ks_recover(KS_FAULT_READBACK);
        portENTER_CRITICAL(&s_lock);s_ks.valid=false;portEXIT_CRITICAL(&s_lock);return false;
    }
    portENTER_CRITICAL(&s_lock);
    // ESP may reboot while the FPGA keeps running. Start beyond its last
    // accepted command so our own boot Panic is not mistaken for a reset.
    if(!s_ks_seen && !s_ks_sequence && !s_ks_inflight.sequence)s_ks_sequence=words[1];
    // Modulo subtraction permits natural 32-bit wrap. A backward counter or
    // command sequence is evidence of reset, rather than an old On to replay.
    bool mixed=s_local.supported;
    bool accepted_back=(int32_t)(words[1]-s_ks.accepted_sequence)<0;
    bool mode_back=(int32_t)(words[3]-s_ks.mode_sequence)<0;
    if(mixed) {
        if(((words[1]^s_ks.accepted_sequence)&0x80000000u) || words[1]==s_ks_inflight.sequence)accepted_back=false;
        if(((words[3]^s_ks.mode_sequence)&0x80000000u) || words[3]==s_ks_inflight.sequence)mode_back=false;
    }
    bool reset=s_ks_seen && (accepted_back || mode_back || (int32_t)(words[4]-s_ks.frame_count)<0);
    s_ks_seen=true;
    s_ks.valid=true;s_ks.applied_mode=(words[0]&1)!=0;s_ks.busy=(words[0]&2)!=0;
    s_ks.active_mask=(uint16_t)masks[0];s_ks.init_mask=(uint16_t)masks[1];s_ks.release_mask=(uint16_t)masks[2];
    s_ks.accepted_sequence=words[1];s_ks.reject_count=words[2];s_ks.mode_sequence=words[3];
    s_ks.frame_count=words[4];s_ks.deadline_count=words[5];
    s_ks_read_at=esp_timer_get_time();
    memcpy(s_ks.started_sequence,started,sizeof(started));
    if(reset) {portEXIT_CRITICAL(&s_lock);ks_recover(KS_FAULT_RESET);return true;}
    if(s_ks_inflight.sequence && words[1]==s_ks_inflight.sequence) {
        unsigned v=s_ks_inflight.voice;
        if(s_ks_inflight.op==KS_NOTE_ON) {
            s_ks.note_sequence[v]=s_ks_inflight.sequence;
            if(s_ks_desired[v].generation==s_ks_inflight.generation)s_ks_desired[v].on=false;
        } else if(s_ks_inflight.op==KS_NOTE_OFF) {
            if(s_ks_desired[v].generation==s_ks_inflight.generation)s_ks_desired[v].off=false;
        } else if(s_ks_inflight.op==KS_MODE) {
            s_ks_mode_sent=s_ks_inflight.generation;
        }
        // MODE needs its applied sequence as well as mailbox acceptance.
        if(s_ks_inflight.op!=KS_MODE || words[3]==s_ks_inflight.sequence) s_ks_inflight.sequence=0;
    } else if(s_ks_inflight.sequence && !(words[0]&2) && words[2]!=s_ks_inflight.reject_before) {
        // A rejected COMMIT never replaced the pending mailbox. Retain the
        // desired command for retry after the actual busy owner has cleared.
        if(s_ks_inflight.op==KS_PANIC)s_ks_panic_pending=true;
        if(s_ks_inflight.op==KS_GAIN)s_ks_desired[s_ks_inflight.voice].gain=true;
        if(s_ks_inflight.op==KS_LOSS)s_ks_desired[s_ks_inflight.voice].loss=true;
        s_ks_inflight.sequence=0;
    }
    if(!s_ks_inflight.sequence && s_ks_mode_sent==s_ks_mode_generation &&
       s_ks.applied_mode==s_ks.requested_mode && !s_ks_owner_pending) {
        s_ks.switching=false;s_ks.recovering=false;
    }
    portEXIT_CRITICAL(&s_lock);
    return true;
}
static bool ks_bank_ready(void)
{
    if(!s_ks_wait_bank_reset)return true;
    uint32_t status;
    if(!control_status(&status) || (status&2u))return false;
    s_ks_wait_bank_reset=false;
    return true;
}
static bool ks_dds_silent(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t gates,shadow;ks_silence_pending(&gates,&shadow);
    bool silent=!s_flushing && !gates && !shadow;
    portEXIT_CRITICAL(&s_lock);
    return silent;
}
static void ks_service(void)
{
    if(s_ks_quarantined) {
        int64_t now=esp_timer_get_time();if(now<s_ks_probe_at)return;
        s_ks_probe_at=now+KS_IDLE_PROBE_US;
        uint32_t caps[2];
        if(!fpga_word_read_burst(0x0500,caps,2) || caps[0]!=ENGINE_KS_CAPABILITY || caps[1]!=ENGINE_KS_VERSION)return;
        if(!ks_readback())return;
        portENTER_CRITICAL(&s_lock);
        bool quiet=s_ks.valid && !s_ks.applied_mode && !s_ks.busy;
        if(quiet) {
            s_ks.available=true;s_ks.fault=0;s_ks_need_probe=s_ks_quarantined=false;
            s_ks_sequence=s_local.supported?s_ks.accepted_sequence&0x7fffffffu:s_ks.accepted_sequence;
        }
        portEXIT_CRITICAL(&s_lock);
        if(!quiet)ks_recover(KS_FAULT_READBACK);
        return; // health recovery never replays an old mode/note command
    }
    if(!s_ks.available && !s_ks.recovering)return;
    if(s_ks_need_probe) {
        uint32_t caps[2];
        if(!fpga_word_read_burst(0x0500,caps,2))return;
        portENTER_CRITICAL(&s_lock);
        s_ks.available=caps[0]==ENGINE_KS_CAPABILITY && caps[1]==ENGINE_KS_VERSION;
        if(s_ks.available)s_ks_need_probe=false;
        portEXIT_CRITICAL(&s_lock);
        if(!s_ks.available)return;
    }
    // With no native command pending, repeated status reads cannot advance
    // a MODE waiting for DDS mute. Leave time for that bank/live work while
    // retaining 20ms health reads. Panic/Off, BUSY and unknown ACKs stay urgent.
    portENTER_CRITICAL(&s_lock);
    bool preparing=s_ks.switching && s_ks.valid && !s_ks.busy && !s_ks_inflight.sequence &&
        !s_ks_panic_pending && s_ks_mode_sent!=s_ks_mode_generation;
    bool idle=s_ks.valid && !s_ks.busy && !s_ks.init_mask && !s_ks.release_mask &&
        !s_ks.recovering && !s_ks.switching &&
        !s_ks_inflight.sequence && !s_ks_panic_pending && !s_ks_owner_pending &&
        s_ks_mode_sent==s_ks_mode_generation;
    for(unsigned v=0;v<ENGINE_KS_VOICES;v++) {
        if(s_ks_desired[v].off)preparing=false;
        if(s_ks_desired[v].on || s_ks_desired[v].off || s_ks_desired[v].gain || s_ks_desired[v].loss)idle=false;
    }
    int64_t now=esp_timer_get_time();
    bool recent=now-s_ks_read_at<KS_IDLE_PROBE_US;
    bool overdue=!s_ks.recovering && now-s_ks_prepare_at>=KS_PREPARE_TIMEOUT_US;
    portEXIT_CRITICAL(&s_lock);
    // Idle health cannot consume every control tick before template upload.
    // Commands, unknown ACKs, BUSY, recovery and invalid observations bypass
    // this cache, as do the existing urgent Off/Panic paths above.
    if(idle && recent)return;
    if(preparing && recent && !overdue && !ks_dds_silent())return;
    if(!ks_readback())return;
    // A busy mailbox or an unacknowledged DDS mute can prevent COMMIT from
    // ever being sent. The normal 50 ms inflight timer cannot cover that.
    if(s_ks.switching && !s_ks.recovering && !s_ks_inflight.sequence &&
       esp_timer_get_time()-s_ks_prepare_at>=KS_PREPARE_TIMEOUT_US)
        ks_recover(KS_FAULT_PREPARE_TIMEOUT);
    if(s_ks_inflight.sequence && esp_timer_get_time()-s_ks_inflight.sent_at>=KS_CONFIRM_TIMEOUT_US)
        ks_recover(KS_FAULT_TIMEOUT);
    portENTER_CRITICAL(&s_lock);
    bool waiting=s_ks.busy || s_ks_inflight.sequence!=0;
    portEXIT_CRITICAL(&s_lock);
    if(waiting)return;
    bool dds_silent=ks_dds_silent();
    engine_fm_status_t fm;engine_link_fm_status(&fm);
    bool fm_silent=!fm.selected && !fm.applied_backend && !fm.switching && !fm.recovering && !fm.needs_select;
    uint32_t shadow[6]={0};
    unsigned op=0,voice=0;
    uint32_t generation=0;
    bool mode=false;
    portENTER_CRITICAL(&s_lock);
    if(s_ks_panic_pending) {op=KS_PANIC;s_ks_panic_pending=false;}
    else if(s_ks.switching && dds_silent && (!s_ks.requested_mode || fm_silent) &&
            !s_ks_owner_pending && s_ks_mode_sent!=s_ks_mode_generation) {
        op=KS_MODE;mode=s_ks.requested_mode;generation=s_ks_mode_generation;
    } else {
        // Every Off outranks every On, including a canceled initialization.
        for(unsigned n=0;n<ENGINE_KS_VOICES;n++) {unsigned v=(s_ks_cursor+n)%ENGINE_KS_VOICES;
            if(s_ks_desired[v].off) {op=KS_NOTE_OFF;voice=v;break;}}
        if(!op && !s_ks.switching) for(unsigned n=0;n<ENGINE_KS_VOICES;n++) {unsigned v=(s_ks_cursor+n)%ENGINE_KS_VOICES;
            if(s_ks_desired[v].on) {op=KS_NOTE_ON;voice=v;break;}
            if(s_ks_desired[v].gain) {op=KS_GAIN;voice=v;s_ks_desired[v].gain=false;break;}
            if(s_ks_desired[v].loss) {op=KS_LOSS;voice=v;s_ks_desired[v].loss=false;break;}}
        if(op)generation=s_ks_desired[voice].generation;
    }
    if(!op) {portEXIT_CRITICAL(&s_lock);return;}
    engine_ks_note_t note=s_ks_desired[voice].note;
    shadow[0]=op|(voice<<8)|((uint32_t)mode<<16);shadow[1]=note.length;
    shadow[2]=(uint16_t)note.tuning|((uint32_t)note.loss<<16);shadow[3]=note.amplitude;
    shadow[4]=note.gain_left|((uint32_t)note.gain_right<<16);shadow[5]=note.seed;
    portEXIT_CRITICAL(&s_lock);
    fpga_word_write_burst(0x0510,shadow,6);
    portENTER_CRITICAL(&s_lock);
    // Producer Off/Panic may arrive between shadow and COMMIT. Do not send
    // a stale On. Off arriving after this serialization point remains
    // queued behind the committed On and cancels its FPGA initialization.
    if(op==KS_NOTE_ON && (!s_ks_desired[voice].on ||
       s_ks_desired[voice].generation!=generation || s_ks_panic_pending || s_ks.switching)) {
        portEXIT_CRITICAL(&s_lock);return;
    }
    if(s_local.supported)s_ks_sequence=(s_ks_sequence&0x7fffffffu)==0x7fffffffu?1:(s_ks_sequence&0x7fffffffu)+1;
    else if(++s_ks_sequence==0)++s_ks_sequence;
    s_ks_inflight.sequence=s_ks_sequence;s_ks_inflight.op=op;s_ks_inflight.voice=voice;
    s_ks_inflight.generation=generation;s_ks_inflight.mode=mode;s_ks_inflight.reject_before=s_ks.reject_count;
    s_ks_inflight.sent_at=esp_timer_get_time();
    uint32_t sequence=s_ks_sequence;
    s_ks_cursor=(voice+1)%ENGINE_KS_VOICES;
    portEXIT_CRITICAL(&s_lock);
    fpga_word_write(0x0516,sequence);
}
