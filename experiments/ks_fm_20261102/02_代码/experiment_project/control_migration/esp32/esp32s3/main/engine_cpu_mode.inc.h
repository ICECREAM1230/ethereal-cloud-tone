// B401/071D v1: existing CPU FIFO and authoritative route readback.
// Only the existing SPI owner executes this service; public APIs are RAM-only.
#include <string.h>
#define CPU_MODE_SEQUENCE_MASK UINT32_C(0x000fffff)
#define CPU_MODE_STATUS_REG UINT16_C(0x071d)
#define CPU_MODE_REQUEST_REG UINT16_C(0xb401)
static engine_cpu_mode_state_t s_cpu_mode;
static struct {uint32_t id,mode,sequence,floor;bool sent;int64_t expires;} s_cpu_mode_job;
static uint32_t s_cpu_mode_word;
static int64_t s_cpu_mode_poll;
static bool cpu_mode_newer(uint32_t a,uint32_t b)
{uint32_t delta=(a-b)&CPU_MODE_SEQUENCE_MASK;return delta && delta<UINT32_C(0x80000);}
static void cpu_mode_finish_locked(uint32_t result)
{
    s_cpu_mode.pending=false;s_cpu_mode.completed_request_id=s_cpu_mode_job.id;s_cpu_mode.result=result;
    s_cpu_mode_job.id=0;s_cpu_mode_job.sent=false;
}
static void cpu_mode_init(void)
{
    memset(&s_cpu_mode,0,sizeof(s_cpu_mode));memset(&s_cpu_mode_job,0,sizeof(s_cpu_mode_job));
    s_cpu_mode.applied=3;s_cpu_mode_word=0;s_cpu_mode_poll=0;
}
void engine_link_cpu_mode_state(engine_cpu_mode_state_t *state)
{portENTER_CRITICAL(&s_lock);*state=s_cpu_mode;portEXIT_CRITICAL(&s_lock);}
bool engine_link_cpu_mode_request(uint32_t mode,uint32_t request_id)
{
    portENTER_CRITICAL(&s_lock);
    bool ok=CONFIG_FPGA_AE350_CONTROL && mode<=2 && request_id && s_cpu_mode.supported &&
        s_cpu_mode.valid && !s_cpu_mode.fault && !s_cpu_mode.pending;
    if(ok) {
        s_cpu_mode_job.id=request_id;s_cpu_mode_job.mode=mode;s_cpu_mode_job.sent=false;
        s_cpu_mode_job.expires=esp_timer_get_time()+1000000;
        s_cpu_mode.pending=true;s_cpu_mode.request_id=request_id;s_cpu_mode.completed_request_id=0;
        s_cpu_mode.result=FM_RESULT_BUSY;s_cpu_mode_poll=0;
    }
    portEXIT_CRITICAL(&s_lock);return ok;
}
static void cpu_mode_unavailable(void)
{
    portENTER_CRITICAL(&s_lock);
    if(s_cpu_mode.valid){s_cpu_mode.valid=false;s_cpu_mode.revision++;}
    portEXIT_CRITICAL(&s_lock);
}
static void cpu_mode_service(int64_t deadline)
{
    int64_t now=esp_timer_get_time();
    if(!CONFIG_FPGA_AE350_CONTROL || now>=deadline)return;
    portENTER_CRITICAL(&s_lock);
    if(s_cpu_mode.pending && now>=s_cpu_mode_job.expires)cpu_mode_finish_locked(FM_RESULT_BACKEND_FAULT);
    bool pending=s_cpu_mode.pending;
    portEXIT_CRITICAL(&s_lock);
    if(now<s_cpu_mode_poll)return;
    s_cpu_mode_poll=now+(pending?1000:20000);
    uint32_t word;
    if(!fpga_word_read_burst_observe(CPU_MODE_STATUS_REG,&word,1)){cpu_mode_unavailable();return;}
    unsigned desired=word&3u,applied=(word>>2)&3u;
    bool ready=(word&32u)!=0,busy=(word&16u)!=0,fault=(word&64u)!=0;
    unsigned version=word>>28;
    bool valid=(version==1 || version==2) && (word&128u) && desired<=2 &&
        (!ready || (!busy && !fault && applied<=2 && desired==applied));
    portENTER_CRITICAL(&s_lock);
    if(!valid) {
        if(s_cpu_mode.valid){s_cpu_mode.valid=false;s_cpu_mode.revision++;}
        portEXIT_CRITICAL(&s_lock);return;
    }
    uint32_t sequence=(word>>8)&CPU_MODE_SEQUENCE_MASK;
    bool reset=s_cpu_mode.supported && s_cpu_mode.sequence && !sequence;
    if(reset) {
        s_cpu_mode.reset_count++;s_cpu_mode_job.floor=0;
        if(s_cpu_mode.pending)cpu_mode_finish_locked(FM_RESULT_CANCELLED);
    }
    if(word!=s_cpu_mode_word || !s_cpu_mode.valid)s_cpu_mode.revision++;
    s_cpu_mode_word=word;s_cpu_mode.supported=s_cpu_mode.valid=true;
    s_cpu_mode.channel_routes=version==2;
    s_cpu_mode.sequence=sequence;s_cpu_mode.desired=desired;s_cpu_mode.applied=applied;
    s_cpu_mode.ready=ready;s_cpu_mode.busy=busy;s_cpu_mode.fault=fault;
    if(!s_cpu_mode_job.floor || cpu_mode_newer(sequence,s_cpu_mode_job.floor))s_cpu_mode_job.floor=sequence;
    if(s_cpu_mode.pending && s_cpu_mode_job.sent) {
        if(sequence==s_cpu_mode_job.sequence) {
            if(desired!=s_cpu_mode_job.mode)cpu_mode_finish_locked(FM_RESULT_CANCELLED);
            else if(fault)cpu_mode_finish_locked(FM_RESULT_BACKEND_FAULT);
            else if(ready && !busy && applied==s_cpu_mode_job.mode)cpu_mode_finish_locked(FM_RESULT_OK);
        } else if(cpu_mode_newer(sequence,s_cpu_mode_job.sequence))cpu_mode_finish_locked(FM_RESULT_CANCELLED);
    }
    bool send=s_cpu_mode.pending && !s_cpu_mode_job.sent && !fault;
    uint32_t request_id=s_cpu_mode_job.id,mode=s_cpu_mode_job.mode;
    uint32_t next=(s_cpu_mode_job.floor+1)&CPU_MODE_SEQUENCE_MASK;if(!next)next=1;
    portEXIT_CRITICAL(&s_lock);
    if(!send || esp_timer_get_time()>=deadline)return;
    uint32_t request=UINT32_C(0x10000000)|(next<<8)|mode;
    fpga_template_write_result_t result=fpga_template_write_burst(CPU_MODE_REQUEST_REG,&request,1);
    portENTER_CRITICAL(&s_lock);
    if(s_cpu_mode.pending && s_cpu_mode_job.id==request_id) {
        if(result==FPGA_TEMPLATE_SENT || result==FPGA_TEMPLATE_UNCERTAIN) {
            // An uncertain frame is observed, never resent or declared done.
            s_cpu_mode_job.sequence=next;s_cpu_mode_job.floor=next;s_cpu_mode_job.sent=true;
        } else if(result!=FPGA_TEMPLATE_DEFERRED)cpu_mode_finish_locked(FM_RESULT_BACKEND_FAULT);
    }
    portEXIT_CRITICAL(&s_lock);
}
