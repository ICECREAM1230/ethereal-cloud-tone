// AOR1: public functions touch RAM only; the existing engine task owns SPI.
static engine_audio_routes_state_t s_audio_routes;
static struct {uint32_t command,baseline_sequence,baseline_generation,ticket;bool sent;int64_t expires,poll_at;control_source_ref_t source;} s_audio_routes_job;
static void audio_routes_finish_locked(bool success)
{
    control_source_complete(s_audio_routes_job.source,0,s_audio_routes_job.ticket,CONTROL_SOURCE_OUTPUT,success);
    s_audio_routes.pending=false;s_audio_routes.error=!success;
    s_audio_routes_job.source=(control_source_ref_t){0};s_audio_routes_job.ticket=0;
}
static void audio_routes_init(void)
{
    memset(&s_audio_routes,0,sizeof(s_audio_routes));
    memset(&s_audio_routes_job,0,sizeof(s_audio_routes_job));
}
void engine_link_audio_routes_state(engine_audio_routes_state_t *state)
{portENTER_CRITICAL(&s_lock);*state=s_audio_routes;portEXIT_CRITICAL(&s_lock);}
static bool audio_routes_request(control_source_ref_t source,uint32_t hp,uint32_t pcm_a)
{
    portENTER_CRITICAL(&s_lock);
    bool ok=CONFIG_FPGA_AE350_CONTROL && hp<4 && pcm_a<4 && s_audio_routes.available &&
        s_audio_routes.valid && s_audio_routes.ready && !s_audio_routes.pending && !s_audio_routes.busy;
    uint32_t ticket=0;int64_t now=esp_timer_get_time();
    if(ok && source.epoch)ok=control_source_reserve_job(source,now,now+1000000,&ticket);
    if(ok) {
        uint32_t next=(s_audio_routes.sequence+1u)&UINT32_C(0x0fffffff);if(!next)next=1;
        s_audio_routes_job.command=(next<<4)|(pcm_a<<2)|hp;s_audio_routes_job.sent=false;
        s_audio_routes_job.baseline_sequence=s_audio_routes.sequence;
        s_audio_routes_job.baseline_generation=s_audio_routes.generation;
        s_audio_routes_job.source=source;s_audio_routes_job.ticket=ticket;
        s_audio_routes_job.expires=esp_timer_get_time()+1000000;s_audio_routes_job.poll_at=0;
        s_audio_routes.pending=true;s_audio_routes.error=false;
    }
    portEXIT_CRITICAL(&s_lock);return ok;
}
bool engine_link_audio_routes_request(uint32_t hp,uint32_t pcm_a)
{return audio_routes_request((control_source_ref_t){0},hp,pcm_a);}
bool engine_link_audio_routes_request_source(control_source_ref_t source,uint32_t hp,uint32_t pcm_a)
{return source.epoch && audio_routes_request(source,hp,pcm_a);}
static void audio_routes_unavailable(void)
{
    portENTER_CRITICAL(&s_lock);
    s_audio_routes.valid=s_audio_routes.ready=false;
    if(s_audio_routes.pending)audio_routes_finish_locked(false);
    portEXIT_CRITICAL(&s_lock);
}
static void audio_routes_service(int64_t deadline,bool transport_ready)
{
    int64_t now=esp_timer_get_time();
    if(!CONFIG_FPGA_AE350_CONTROL || now>=deadline)return;
    if(!transport_ready){audio_routes_unavailable();return;}
    portENTER_CRITICAL(&s_lock);
    if(s_audio_routes.pending && (now>=s_audio_routes_job.expires ||
       (s_audio_routes_job.source.epoch && !control_source_valid(s_audio_routes_job.source))))audio_routes_finish_locked(false);
    bool pending=s_audio_routes.pending;
    int64_t poll_at=s_audio_routes_job.poll_at;
    portEXIT_CRITICAL(&s_lock);
    if(now<poll_at)return;
    uint32_t word[8],generation;
    if(!fpga_word_read_burst_observe(UINT16_C(0x07b7),&generation,1) ||
       !fpga_word_read_burst_observe(UINT16_C(0x07b0),word,8)){audio_routes_unavailable();return;}
    // The audio edge can occur during a serial burst. Never combine two
    // generations into a fabricated application/reset receipt.
    if(word[7]!=generation)return;
    bool valid=word[0]==UINT32_C(0x414f5231) && word[1]==UINT32_C(0x00010000) &&
        !(word[2]&~3u) && !(word[3]&~15u) && !(word[4]&UINT32_C(0xf0000000));
    portENTER_CRITICAL(&s_lock);
    s_audio_routes_job.poll_at=now+(pending?1000:20000);
    s_audio_routes.available=valid;s_audio_routes.valid=valid;
    if(!valid){
        if(s_audio_routes.pending)audio_routes_finish_locked(false);
        portEXIT_CRITICAL(&s_lock);return;
    }
    s_audio_routes.ready=(word[2]&1u)!=0;s_audio_routes.busy=(word[2]&2u)!=0;
    s_audio_routes.pt8211=word[3]&3u;s_audio_routes.pcm_a=(word[3]>>2)&3u;
    s_audio_routes.sequence=word[4];s_audio_routes.generation=word[7];
    uint32_t command=s_audio_routes_job.command;
    if(s_audio_routes.pending) {
        bool unchanged=word[4]==s_audio_routes_job.baseline_sequence && word[7]==s_audio_routes_job.baseline_generation;
        bool applied=s_audio_routes_job.sent && word[4]==(command>>4) &&
            word[7]==s_audio_routes_job.baseline_generation+1u && word[3]==(command&15u);
        if(s_audio_routes_job.source.epoch && !control_source_valid(s_audio_routes_job.source))audio_routes_finish_locked(false);
        else if(applied && s_audio_routes.ready && !s_audio_routes.busy)audio_routes_finish_locked(true);
        else if(!unchanged && !applied)audio_routes_finish_locked(false);
    }
    bool send=s_audio_routes.pending && !s_audio_routes_job.sent && s_audio_routes.ready && !s_audio_routes.busy;
    portEXIT_CRITICAL(&s_lock);
    if(!send || esp_timer_get_time()>=deadline)return;
    fpga_template_write_result_t result=fpga_template_write_burst(UINT16_C(0x07b5),&command,1);
    portENTER_CRITICAL(&s_lock);
    if(s_audio_routes.pending && s_audio_routes_job.command==command) {
        if(result==FPGA_TEMPLATE_SENT || result==FPGA_TEMPLATE_UNCERTAIN)s_audio_routes_job.sent=true;
        else if(result!=FPGA_TEMPLATE_DEFERRED)audio_routes_finish_locked(false);
    }
    portEXIT_CRITICAL(&s_lock);
}
