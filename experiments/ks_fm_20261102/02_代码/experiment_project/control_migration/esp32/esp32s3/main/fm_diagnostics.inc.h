// Included only by engine_link.c. No diagnostic path writes the SPI bus or
// changes music validity, permissions, queues or recovery state.
static portMUX_TYPE s_fm_diagnostic_lock=portMUX_INITIALIZER_UNLOCKED;
static engine_fm_diagnostics_t s_fm_diagnostic;
#if CONFIG_FPGA_FM_ONLY
static engine_fm_diagnostics_t s_fm_diagnostic_work;
static bool s_fm_diagnostic_started;
static unsigned s_fm_diagnostic_block;
#endif

static void fm_diagnostics_init(void)
{
    portENTER_CRITICAL(&s_fm_diagnostic_lock);
    memset(&s_fm_diagnostic,0,sizeof(s_fm_diagnostic));
    portEXIT_CRITICAL(&s_fm_diagnostic_lock);
#if CONFIG_FPGA_FM_ONLY
    s_fm_diagnostic_started=false;s_fm_diagnostic_block=0;
#endif
}
uint32_t engine_link_fm_diagnostics_request(void)
{
#if CONFIG_FPGA_FM_ONLY
    int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&s_fm_diagnostic_lock);
    if(s_ready && !s_fm_diagnostic.pending && (!s_fm_diagnostic.request_id ||
       now-s_fm_diagnostic.requested_at_us>=ENGINE_FM_DIAGNOSTIC_INTERVAL_US)) {
        if(!++s_fm_diagnostic.request_id)++s_fm_diagnostic.request_id;
        s_fm_diagnostic.requested_at_us=now;s_fm_diagnostic.pending=true;
    }
    uint32_t id=s_fm_diagnostic.request_id;
    portEXIT_CRITICAL(&s_fm_diagnostic_lock);
    return id;
#else
    return 0;
#endif
}
void engine_link_fm_diagnostics_state(engine_fm_diagnostics_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&s_fm_diagnostic_lock);*out=s_fm_diagnostic;portEXIT_CRITICAL(&s_fm_diagnostic_lock);
    out->supported=CONFIG_FPGA_FM_ONLY;
    if(!CONFIG_FPGA_FM_ONLY)out->error=FM_DIAGNOSTIC_UNSUPPORTED;
    else if(!s_ready) {out->valid=false;out->error=FM_DIAGNOSTIC_NOT_READY;}
}
#if CONFIG_FPGA_FM_ONLY
static engine_fm_diagnostic_context_t fm_diagnostic_context(void)
{
    engine_fm_status_t fm;engine_link_fm_status(&fm);
    return (engine_fm_diagnostic_context_t){fm.valid,fm.session_id,fm.engine_epoch,fm.topology_generation};
}
static void fm_diagnostic_finish(unsigned error)
{
    s_fm_diagnostic_work.completed_at_us=esp_timer_get_time();
    s_fm_diagnostic_work.context_after=fm_diagnostic_context();
    raw_midi_state(&s_fm_diagnostic_work.raw_cache);
    s_fm_diagnostic_work.raw_cached_at_us=esp_timer_get_time();
    s_fm_diagnostic_work.raw_cache_valid=true;
    if(!error) {
        engine_fm_diagnostic_context_t a=s_fm_diagnostic_work.context_before,b=s_fm_diagnostic_work.context_after;
        if(a.valid!=b.valid || a.session_id!=b.session_id || a.engine_epoch!=b.engine_epoch ||
           a.topology_generation!=b.topology_generation)error=FM_DIAGNOSTIC_CONTEXT;
    }
    s_fm_diagnostic_work.error=error;s_fm_diagnostic_work.valid=!error;
    s_fm_diagnostic_work.pending=false;
    portENTER_CRITICAL(&s_fm_diagnostic_lock);
    s_fm_diagnostic_work.error_count=s_fm_diagnostic.error_count+(error!=FM_DIAGNOSTIC_OK);
    s_fm_diagnostic=s_fm_diagnostic_work;
    portEXIT_CRITICAL(&s_fm_diagnostic_lock);
    s_fm_diagnostic_started=false;
}
#endif
static void fm_diagnostics_service(int64_t deadline,bool idle)
{
#if CONFIG_FPGA_FM_ONLY
    int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&s_fm_diagnostic_lock);
    bool pending=s_fm_diagnostic.pending;
    uint32_t id=s_fm_diagnostic.request_id;
    int64_t requested=s_fm_diagnostic.requested_at_us;
    portEXIT_CRITICAL(&s_fm_diagnostic_lock);
    if(!pending)return;
    // A busy music path may postpone every block. Bound the pending lifetime
    // without issuing SPI or touching the music fault/recovery path.
    if(!s_fm_diagnostic_started) {
        if(now-requested<ENGINE_FM_DIAGNOSTIC_TIMEOUT_US &&
           (!idle || deadline-now<ENGINE_FM_DIAGNOSTIC_RESERVE_US))return;
        memset(&s_fm_diagnostic_work,0,sizeof(s_fm_diagnostic_work));
        s_fm_diagnostic_work.supported=true;s_fm_diagnostic_work.request_id=id;
        s_fm_diagnostic_work.sample_request_id=id;s_fm_diagnostic_work.requested_at_us=requested;
        s_fm_diagnostic_work.started_at_us=now;s_fm_diagnostic_work.context_before=fm_diagnostic_context();
        static const uint16_t addresses[]={FM_ONLY_REG_IDENTITY,FM_ONLY_REG_LOCAL_ACCEPTED,MIDI_RAW_REG_MAGIC,FM_ONLY_REG_IDENTITY};
        static const uint8_t counts[]={11,3,7,2};
        for(unsigned i=0;i<ENGINE_FM_DIAGNOSTIC_BLOCKS;i++) {
            s_fm_diagnostic_work.blocks[i].address=addresses[i];s_fm_diagnostic_work.blocks[i].count=counts[i];
        }
        s_fm_diagnostic_block=0;s_fm_diagnostic_started=true;
    }
    if(now-requested>=ENGINE_FM_DIAGNOSTIC_TIMEOUT_US) {fm_diagnostic_finish(FM_DIAGNOSTIC_TIMEOUT);return;}
    if(!idle || deadline-esp_timer_get_time()<ENGINE_FM_DIAGNOSTIC_RESERVE_US)return;
    engine_fm_diagnostic_block_t *block=&s_fm_diagnostic_work.blocks[s_fm_diagnostic_block];
    block->started_at_us=esp_timer_get_time();
    block->read_ok=fpga_word_read_burst_observe(block->address,block->words,block->count);
    block->completed_at_us=esp_timer_get_time();
    if(!block->read_ok) {fm_diagnostic_finish(FM_DIAGNOSTIC_IO);return;}
    if((s_fm_diagnostic_block==0 || s_fm_diagnostic_block==3) &&
       (block->words[0]!=FM_ONLY_IDENTITY || block->words[1]!=FM_ONLY_VERSION)) {
        fm_diagnostic_finish(FM_DIAGNOSTIC_IDENTITY);return;
    }
    if(s_fm_diagnostic_block==2 && block->words[0]!=MIDI_RAW_ABI_MAGIC) {
        fm_diagnostic_finish(FM_DIAGNOSTIC_IDENTITY);return;
    }
    if(++s_fm_diagnostic_block==ENGINE_FM_DIAGNOSTIC_BLOCKS)fm_diagnostic_finish(FM_DIAGNOSTIC_OK);
#else
    (void)deadline;(void)idle;
#endif
}
