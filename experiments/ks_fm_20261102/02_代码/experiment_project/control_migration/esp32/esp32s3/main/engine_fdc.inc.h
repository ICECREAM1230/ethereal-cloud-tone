/* 071F is the CPU-published FDC APB170 status through its own32-bit held CDC.
 * It is not an ACK slot. Revision is liveness only, never a command receipt. */
static engine_fdc_state_t s_fdc;
static struct {fdc_request_t request;bool active;unsigned phase;int64_t expires;} s_fdc_job;
static int64_t s_fdc_poll,s_fdc_changed;
static uint32_t s_fdc_stop_revision;
/* Read-only FDR1 observation. Only the existing engine task touches SPI.
 * One bounded read per idle service invocation; commands retain priority. */
static unsigned s_fdc_raw_phase;
static int64_t s_fdc_raw_poll,s_fdc_raw_changed,s_fdc_raw_started;
static uint32_t s_fdc_raw_words[4];
static void fdc_raw_reset(void)
{
    memset(&s_fdc.raw,0,sizeof s_fdc.raw);
    s_fdc_raw_phase=0;s_fdc_raw_poll=s_fdc_raw_changed=s_fdc_raw_started=0;
}
static bool fdc_raw_fresh(int64_t now)
{
    return s_fdc.raw.valid && s_fdc.raw.sample_fresh &&
        now>=s_fdc.raw.observed_us && now-s_fdc.raw.observed_us<=FDC_FRESH_US &&
        now>=s_fdc_raw_changed && now-s_fdc_raw_changed<=FDC_FRESH_US;
}
static void fdc_raw_service(int64_t now)
{
    uint32_t words[4]={0};unsigned phase=s_fdc_raw_phase;
    if(!phase && now<s_fdc_raw_poll)return;
    uint16_t address=phase?FDC_RAW_DATA_REG:FDC_RAW_MAGIC_REG;
    size_t count=phase==1?4:1;
    bool ok=fpga_word_read_burst_observe(address,words,count);
    portENTER_CRITICAL(&s_lock);
    if(!ok) {
        s_fdc.raw.valid=s_fdc.raw.fresh=false;
        s_fdc_raw_phase=0;s_fdc_raw_poll=now+100000;
    } else if(!phase) {
        s_fdc.raw.supported=words[0]==FDC_RAW_MAGIC;
        if(s_fdc.raw.supported)s_fdc_raw_phase=1;
        else {
            s_fdc.raw.valid=s_fdc.raw.fresh=false;
            s_fdc_raw_poll=now+1000000;
        }
    } else if(phase==1) {
        memcpy(s_fdc_raw_words,words,sizeof words);s_fdc_raw_started=now;s_fdc_raw_phase=2;
    } else {
        const uint32_t *w=s_fdc_raw_words;
        /* A32-bit publication serial is the commit guard. FDR1 data can change
         * only with this guard; bound acquisition to500ms, far below its
         * wrap even at the maximum APB rate. No mixed burst becomes valid. */
        bool valid=now>=s_fdc_raw_started && now-s_fdc_raw_started<=FDC_FRESH_US && words[0]==w[0] && w[0]!=0 &&
            (w[3]&UINT32_C(0x1ffff))<=65536 && !(w[3]&UINT32_C(0x00e00000)) &&
            ((w[1]^w[2])&UINT32_C(0xc0000000))==0;
        if(valid) {
            engine_fdc_raw_state_t *t=&s_fdc.raw;
            if(!t->valid || t->revision!=w[0])s_fdc_raw_changed=now;
            t->valid=true;t->revision=w[0];t->observed_us=now;
            t->raw0=w[1]&UINT32_C(0x0fffffff);t->raw1=w[2]&UINT32_C(0x0fffffff);
            t->quality0=(w[1]>>28)&3u;t->quality1=(w[2]>>28)&3u;
            t->sample_valid=!!(w[1]&(UINT32_C(1)<<30));
            t->sample_fresh=!!(w[1]&(UINT32_C(1)<<31));
            t->gain_q16=w[3]&UINT32_C(0x1ffff);t->sequence=w[3]>>24;
            t->enabled=!!(w[3]&(1u<<17));t->muted=!!(w[3]&(1u<<18));
            t->pitch_stale=!!(w[3]&(1u<<19));t->volume_stale=!!(w[3]&(1u<<20));
            t->fresh=fdc_raw_fresh(now);
        } else s_fdc.raw.valid=s_fdc.raw.fresh=false;
        s_fdc_raw_phase=0;s_fdc_raw_poll=now+100000;
    }
    portEXIT_CRITICAL(&s_lock);
}

static void fdc_init(void)
{
    memset(&s_fdc,0,sizeof s_fdc);memset(&s_fdc_job,0,sizeof s_fdc_job);
    s_fdc_poll=s_fdc_changed=0;s_fdc_stop_revision=0;fdc_raw_reset();
}
static bool fdc_fresh_locked(int64_t now)
{return s_fdc.valid && now>=s_fdc.observed_us && now-s_fdc.observed_us<=FDC_FRESH_US &&
    now>=s_fdc_changed && now-s_fdc_changed<=FDC_FRESH_US;}
void engine_link_fdc_state(engine_fdc_state_t *out)
{
    portENTER_CRITICAL(&s_lock);*out=s_fdc;out->fresh=fdc_fresh_locked(esp_timer_get_time());out->raw.fresh=fdc_raw_fresh(esp_timer_get_time());
    portEXIT_CRITICAL(&s_lock);
}
static void fdc_finish_locked(uint32_t result)
{
    s_fdc.completed_request_id=s_fdc_job.request.event.c;s_fdc.result=result;
    s_fdc_job.active=false;s_fdc.pending=false;
}
bool engine_link_fdc_request(const fdc_request_t *request)
{
    if(!CONFIG_FPGA_AE350_CONTROL || !fdc_request_valid(request) || !request->event.c)return false;
    portENTER_CRITICAL(&s_lock);
    bool ready=fdc_fresh_locked(esp_timer_get_time()) && s_fdc.available;
    bool ok=ready && (!s_fdc_job.active || request->op==FDC_DISABLE) &&
        (!s_fdc.disable_pending || request->op==FDC_DISABLE);
    if(request->op!=FDC_DISABLE)ok=ok && !(s_fdc.word&0x10u);
    if(request->op==FDC_ENABLE || request->op==FDC_CALIBRATE)ok=ok && (s_fdc.word&2u) && !(s_fdc.word&0x20u);
    if(ok) {
        if(s_fdc_job.active)fdc_finish_locked(FDC_CANCELLED);
        s_fdc_job.request=*request;s_fdc_job.active=true;s_fdc_job.phase=0;
        s_fdc_job.expires=request->event.expires;s_fdc.pending=true;
        s_fdc.request_id=request->event.c;s_fdc.completed_request_id=0;
        if(request->op==FDC_DISABLE){s_fdc.disable_pending=true;s_fdc_stop_revision++;}
    }
    portEXIT_CRITICAL(&s_lock);return ok;
}
void engine_link_fdc_cancel(uint32_t id,bool disable)
{
    portENTER_CRITICAL(&s_lock);
    bool matches=s_fdc_job.active && (!id || s_fdc_job.request.event.c==id);
    if(matches)fdc_finish_locked(FDC_CANCELLED);
    if(disable && (!id || matches || s_fdc.request_id==id)){s_fdc.disable_pending=true;s_fdc_stop_revision++;}
    portEXIT_CRITICAL(&s_lock);
}
void engine_link_fdc_stop(void){engine_link_fdc_cancel(0,true);}
static void fdc_unavailable(void)
{
    portENTER_CRITICAL(&s_lock);s_fdc.valid=s_fdc.available=false;fdc_raw_reset();
    if(s_fdc_job.active){fdc_finish_locked(FDC_CANCELLED);s_fdc.disable_pending=true;}
    portEXIT_CRITICAL(&s_lock);
}
static void fdc_service(int64_t deadline,bool can_arm)
{
    int64_t now=esp_timer_get_time();
    if(!CONFIG_FPGA_AE350_CONTROL || now>=deadline)return;
    portENTER_CRITICAL(&s_lock);
    if(s_fdc_job.active && now>=s_fdc_job.expires){fdc_finish_locked(FDC_CANCELLED);s_fdc.disable_pending=true;}
    bool read=now>=s_fdc_poll;
    portEXIT_CRITICAL(&s_lock);
    /* One read or credit-guarded write invocation. Every write still uses the
     * existing template credit API; no bypass and no retry of uncertain data. */
    if(read) {
        uint32_t word;bool good=fpga_word_read_burst_observe(FDC_STATUS_REG,&word,1);
        portENTER_CRITICAL(&s_lock);
        if(good && (word&UINT32_C(0xffff0000))==FDC_STATUS_TAG) {
            if(!s_fdc.valid || word!=s_fdc.word)s_fdc_changed=now;
            s_fdc.word=word;s_fdc.valid=true;s_fdc.available=(word&1u)!=0;s_fdc.observed_us=now;
        } else {s_fdc.valid=s_fdc.available=false;fdc_raw_reset();}
        if((!s_fdc.valid || !s_fdc.available) && s_fdc_job.active) {
            fdc_finish_locked(FDC_CANCELLED);s_fdc.disable_pending=true;
        }
        s_fdc_poll=now+20000;
        portEXIT_CRITICAL(&s_lock);return;
    }
    portENTER_CRITICAL(&s_lock);
    bool disable=s_fdc.disable_pending;uint32_t stop_revision=s_fdc_stop_revision;
    bool active=s_fdc_job.active;
    fdc_request_t request=s_fdc_job.request;unsigned phase=s_fdc_job.phase;
    bool available=fdc_fresh_locked(now) && s_fdc.available;
    bool arm=available && can_arm && !(s_fdc.word&0x10u) &&
        (request.op==FDC_RESTART || ((s_fdc.word&2u) && !(s_fdc.word&0x20u)));
    if(active && request.op!=FDC_DISABLE && !arm) {
        fdc_finish_locked(FDC_CANCELLED);s_fdc.disable_pending=true;disable=true;active=false;
    }
    portEXIT_CRITICAL(&s_lock);
    if(!available)return;
    if(!disable && (!active || !can_arm)){fdc_raw_service(now);return;}
    uint16_t address;uint32_t scalar=1;const uint32_t *words=&scalar;size_t count=1;
    if(disable){address=0xb500;scalar=0;}
    else if(request.op==FDC_CALIBRATE && !phase){address=0xb510;words=request.words;count=6;}
    else address=request.op==FDC_ENABLE?0xb500:request.op==FDC_RESTART?0xb501:0xb51e;
    if(esp_timer_get_time()>=deadline)return;
    portENTER_CRITICAL(&s_lock);
    bool retired=!disable && (!s_fdc_job.active || s_fdc_job.request.event.c!=request.event.c || s_fdc.disable_pending);
    portEXIT_CRITICAL(&s_lock);
    if(retired)return;
    fpga_template_write_result_t result=fpga_template_write_burst(address,words,count);
    if(result==FPGA_TEMPLATE_DEFERRED)return;
    portENTER_CRITICAL(&s_lock);
    /* A STOP arriving while SPI was outside the RAM lock remains latched.
     * Its disable is serviced next; a canceled job never becomes sent-success. */
    if(disable) {
        if(s_fdc_stop_revision==stop_revision)s_fdc.disable_pending=false;
        if(s_fdc_job.active && s_fdc_job.request.op==FDC_DISABLE &&
           s_fdc_job.request.event.c==request.event.c)
            fdc_finish_locked(result==FPGA_TEMPLATE_SENT?FDC_SENT:FDC_UNKNOWN);
    } else if(s_fdc_job.active && s_fdc_job.request.event.c==request.event.c) {
        if(result==FPGA_TEMPLATE_SENT) {
            if(request.op==FDC_CALIBRATE && !phase)s_fdc_job.phase=1;
            else fdc_finish_locked(FDC_SENT);
        } else {fdc_finish_locked(result==FPGA_TEMPLATE_UNCERTAIN?FDC_UNKNOWN:FDC_FAILED);s_fdc.disable_pending=true;}
    }
    portEXIT_CRITICAL(&s_lock);
}
