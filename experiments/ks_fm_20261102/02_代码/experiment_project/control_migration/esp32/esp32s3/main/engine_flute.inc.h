// FLU1 transactions share the existing single SPI owner. Public APIs are RAM-only.
static engine_flute_state_t s_flute;
static bool s_flute_probe_done;
static uint32_t s_flute_sequence, s_flute_pending_word;
static int64_t s_flute_started, s_flute_sent_at, s_flute_read_at;
static bool flute_readback(void)
{
    uint32_t w[9];
    s_flute_read_at=esp_timer_get_time();
    if(!fpga_word_read_burst(0x07b0,w,9))return false;
    if(w[0]!=ENGINE_FLUTE_CAPABILITY || w[1]!=ENGINE_FLUTE_VERSION)return false;
    if(w[4]&~31u || w[5]!=1u)return false;
    portENTER_CRITICAL(&s_lock);
    s_flute.available=s_flute.valid=true;s_flute.accepted_word=w[3];s_flute.flags=w[4];
    s_flute.ready=(w[4]&1u)!=0;s_flute.applied_mode=(w[4]&2u)!=0;
    s_flute.busy=(w[4]&4u)!=0;s_flute.active=(w[4]&8u)!=0;
    s_flute.preset=w[5];s_flute.reject_count=w[6];s_flute.deadline_count=w[7];s_flute.frame_count=w[8];
    if(w[4]&16u)s_flute.fault=1;
    portEXIT_CRITICAL(&s_lock);
    s_flute_read_at=esp_timer_get_time();return true;
}
static void flute_probe(void)
{
    if(s_flute_probe_done)return;
    uint32_t caps[2];
    if(!fpga_word_read_burst(0x07b0,caps,2))return;
    if(caps[0]==ENGINE_FLUTE_CAPABILITY && caps[1]==ENGINE_FLUTE_VERSION) {
        if(flute_readback()) {s_flute_sequence=s_flute.accepted_word>>1;s_flute_probe_done=true;}
    } else s_flute_probe_done=true;
}
void engine_link_flute_state(engine_flute_state_t *state)
{
    if(!state)return;
    portENTER_CRITICAL(&s_lock);*state=s_flute;portEXIT_CRITICAL(&s_lock);
}
bool engine_link_flute_mode(bool bind)
{
    portENTER_CRITICAL(&s_lock);
    bool ok=!bind || (s_flute.available && !CONFIG_FPGA_FM_ONLY);
    if(ok && (bind!=s_flute.requested_mode || bind!=s_flute.applied_mode || (bind && s_flute.fault))) {
        s_flute.requested_mode=bind;s_flute.switching=true;s_flute.fault=0;
        s_flute_pending_word=0;s_flute_started=esp_timer_get_time();s_control_idle=false;
        raw_enable_locked(false);
    }
    portEXIT_CRITICAL(&s_lock);return ok;
}
static bool flute_blocked_locked(void)
{
    return s_flute.switching || ((s_flute.requested_mode || s_flute.applied_mode) &&
        (s_flute.fault || !s_flute.valid || !s_flute.ready || s_flute.requested_mode!=s_flute.applied_mode));
}
static void flute_service(void)
{
    flute_probe();
    if(!s_flute.available)return;
    // A confirmed inactive backend has no performance ownership. Leave bus
    // time to the selected engine and refresh FLU1 on its next selection.
    if(!s_flute.switching && !s_flute.requested_mode && !s_flute.applied_mode)return;
    // Before MODE is sent, legacy preparation needs the shared bus budget.
    // Only an actual outstanding ACK is urgent; a bind waiting on bank mute
    // must not consume a nine-word read on every tick and starve that mute.
    bool urgent=s_flute_pending_word!=0;
    if(!urgent && esp_timer_get_time()-s_flute_read_at<20000)return;
    if(!flute_readback()) {
        portENTER_CRITICAL(&s_lock);s_flute.valid=false;s_flute.fault=2;portEXIT_CRITICAL(&s_lock);return;
    }
    bool dds_silent=ks_dds_silent();
    portENTER_CRITICAL(&s_lock);
    if(s_flute_pending_word && s_flute.accepted_word==s_flute_pending_word &&
       s_flute.applied_mode==s_flute.requested_mode && s_flute.ready && !s_flute.busy && (!s_flute.requested_mode || !s_flute.fault)) {
        s_flute_pending_word=0;s_flute.switching=false;
    }
    bool send=s_flute.switching && !s_flute_pending_word && (!s_flute.requested_mode || !s_flute.fault);
    bool bind=s_flute.requested_mode;
    // A bind follows legacy owner release, static gate mute and all bank ACKs.
    if(bind && (s_ks.applied_mode || s_ks.switching || s_ks.recovering ||
       s_fm.status.selected || s_fm.status.switching || s_fm.status.recovering ||
       !dds_silent || s_flushing || s_swap_requested))send=false;
    uint32_t word=0;
    if(send) {
        s_flute_sequence=(s_flute_sequence+1u)&0x7fffffffu;
        if(!s_flute_sequence)s_flute_sequence=1;
        word=(s_flute_sequence<<1)|bind;s_flute_pending_word=word;s_flute_sent_at=esp_timer_get_time();
    }
    // A complete two-bank rebuild can take seconds on the measured slow SPI
    // transport. Its bounded preparation is separate from a sent MODE's ACK.
    bool expired=s_flute_pending_word ? esp_timer_get_time()-s_flute_sent_at>500000 :
        esp_timer_get_time()-s_flute_started>5000000;
    if(s_flute.switching && expired) {
        s_flute.fault=3;s_flute.switching=false;send=false;
    }
    portEXIT_CRITICAL(&s_lock);
    if(send)fpga_word_write(0x07b2,word);
}
