// Optional RVR1 effect control, serviced only by this SPI task. The proposed
// FPGA interface is documented in contract/reverb; absent capability is RO.
#include "reverb_control.h"
#define REVERB_HEALTH_US 20000LL
#define REVERB_TIMEOUT_US 100000LL
typedef enum { RV_CAP, RV_FIRST, RV_SAMPLE, RV_LAST, RV_SHADOW, RV_COMMIT } reverb_phase_t;
static engine_reverb_state_t s_reverb;
static struct {
    uint32_t id,packed,sequence,mode_generation;
    bool active,committed,cancelled,verified;
    int64_t sent_at;
    control_source_ref_t source;
} s_reverb_job;
static reverb_phase_t s_reverb_phase;
static uint32_t s_reverb_first,s_reverb_sample[3],s_reverb_status;
static int64_t s_reverb_next,s_reverb_coherent_at;
static bool reverb_values(uint32_t wet,uint32_t damp)
{return wet<=REVERB_WET_MAX && damp<=REVERB_DAMP_MAX;}
static void reverb_init(void)
{
    memset(&s_reverb,0,sizeof(s_reverb));memset(&s_reverb_job,0,sizeof(s_reverb_job));
    s_reverb.fault=REVERB_LINK_UNAVAILABLE;s_reverb_phase=RV_CAP;
    s_reverb_next=s_reverb_coherent_at=0;s_reverb_status=0;
}
void engine_link_reverb_state(engine_reverb_state_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&s_lock);*out=s_reverb;portEXIT_CRITICAL(&s_lock);
}
static bool reverb_request(control_source_ref_t source,uint32_t wet,uint32_t damp,uint32_t id)
{
    if(CONFIG_FPGA_FM_ONLY || !id || !reverb_values(wet,damp))return false;
    portENTER_CRITICAL(&s_lock);
    bool ok=s_reverb.available && s_reverb.valid && !s_reverb.pending &&
        !s_reverb.fault && !s_reverb_job.active && s_reverb_status==REVERB_STATUS_READY &&
        (!source.epoch || control_source_valid(source));
    if(ok) {
        s_reverb_job.id=id;s_reverb_job.packed=(wet<<16)|damp;
        s_reverb_job.mode_generation=s_local.generation;
        s_reverb_job.source=source;
        s_reverb_job.active=true;s_reverb_job.committed=s_reverb_job.cancelled=s_reverb_job.verified=false;
        s_reverb.pending=true;s_reverb.request_id=id;s_reverb.completed_request_id=0;
    }
    portEXIT_CRITICAL(&s_lock);return ok;
}
bool engine_link_reverb_request(uint32_t wet,uint32_t damp,uint32_t id)
{return reverb_request((control_source_ref_t){0},wet,damp,id);}
bool engine_link_reverb_request_source(control_source_ref_t source,uint32_t wet,uint32_t damp,uint32_t id)
{return source.epoch && reverb_request(source,wet,damp,id);}
void engine_link_reverb_cancel(uint32_t id)
{
    portENTER_CRITICAL(&s_lock);
    if(s_reverb_job.active && (!id || id==s_reverb_job.id))s_reverb_job.cancelled=true;
    portEXIT_CRITICAL(&s_lock);
}
static void reverb_error(uint32_t fault,bool absent)
{
    portENTER_CRITICAL(&s_lock);
    s_reverb.valid=false;s_reverb.fault=fault;s_reverb.completed_request_id=0;
    if(absent)s_reverb.available=false;
    if(s_reverb_job.active) {
        s_reverb_job.cancelled=true;
        if(!s_reverb_job.committed)s_reverb_job.active=false;
    }
    s_reverb.pending=s_reverb_job.active;
    portEXIT_CRITICAL(&s_lock);
    s_reverb_phase=RV_CAP;s_reverb_next=esp_timer_get_time()+REVERB_HEALTH_US;
}
static bool reverb_read(uint16_t address,uint32_t *words,size_t count)
{
    if(fpga_word_read_burst(address,words,count))return true;
    reverb_error(REVERB_LINK_IO,true);return false;
}
static void reverb_service(int64_t deadline)
{
    // FMV1 is a fixed diagnostic ABI, outside the production RVR1 contract.
    if(CONFIG_FPGA_FM_ONLY)return;
    int64_t now=esp_timer_get_time();
    if(now>=deadline)return;
    portENTER_CRITICAL(&s_lock);
    if(s_reverb_job.active && s_reverb_job.mode_generation!=s_local.generation)s_reverb_job.cancelled=true;
    if(s_reverb_job.active && s_reverb_job.source.epoch && !control_source_valid(s_reverb_job.source))s_reverb_job.cancelled=true;
    if(s_reverb_job.active && s_reverb_job.cancelled && !s_reverb_job.committed) {
        s_reverb_job.active=false;s_reverb.pending=false;s_reverb.completed_request_id=0;
        s_reverb.fault=REVERB_LINK_CANCELLED;s_reverb_phase=RV_CAP;s_reverb_next=now;
    }
    bool active=s_reverb_job.active,committed=s_reverb_job.committed;
    if(active && committed && now-s_reverb_job.sent_at>=REVERB_TIMEOUT_US) {
        s_reverb.fault=REVERB_LINK_TIMEOUT;s_reverb.valid=false;
        s_reverb_job.cancelled=true; // late ACK cannot become a timed-out success
    }
    if(active && !committed && !s_reverb_job.verified) {s_reverb_phase=RV_CAP;s_reverb_next=now;}
    portEXIT_CRITICAL(&s_lock);
    if(now<s_reverb_next)return;
    // Exactly one SPI frame per invocation; RAW/STOP and music ran first.
    if(s_reverb_phase==RV_CAP) {
        uint32_t caps[2];if(!reverb_read(REVERB_REG_MAGIC,caps,2))return;
        if(caps[0]!=REVERB_MAGIC || caps[1]!=REVERB_ABI) {
            reverb_error(REVERB_LINK_UNAVAILABLE,true);return;
        }
        portENTER_CRITICAL(&s_lock);
        s_reverb.available=true;
        if(s_reverb_job.active)s_reverb_job.verified=true;
        portEXIT_CRITICAL(&s_lock);
        s_reverb_phase=RV_FIRST;s_reverb_next=0;return;
    }
    if(s_reverb_phase==RV_FIRST) {
        if(reverb_read(REVERB_REG_GENERATION,&s_reverb_first,1))s_reverb_phase=RV_SAMPLE;
        return;
    }
    if(s_reverb_phase==RV_SAMPLE) {
        if(reverb_read(REVERB_REG_STATUS,s_reverb_sample,3))s_reverb_phase=RV_LAST;
        return;
    }
    if(s_reverb_phase==RV_LAST) {
        uint32_t last;if(!reverb_read(REVERB_REG_GENERATION,&last,1))return;
        if(last!=s_reverb_first) {
            s_reverb_phase=RV_FIRST;
            if(now-s_reverb_coherent_at>=REVERB_TIMEOUT_US)reverb_error(REVERB_LINK_IO,false);
            return;
        }
        uint32_t status=s_reverb_sample[0],packed=s_reverb_sample[1],sequence=s_reverb_sample[2];
        if((status&~7u) || (status&REVERB_STATUS_FAULT) || sequence>REVERB_SEQUENCE_MAX ||
           !reverb_values(packed>>16,packed&0xffffu)) {reverb_error(REVERB_LINK_PROTOCOL,false);return;}
        if(!(status&REVERB_STATUS_READY)) {reverb_error(REVERB_LINK_UNAVAILABLE,false);return;}
        portENTER_CRITICAL(&s_lock);
        bool reset=s_reverb_coherent_at && ((int32_t)(last-s_reverb.generation)<0 ||
            (s_reverb.applied_sequence && !sequence));
        if(reset) {
            s_reverb_job.active=false;s_reverb.completed_request_id=0;
            s_reverb.pending=false;s_reverb.valid=false;s_reverb.fault=REVERB_LINK_RESET;
            s_reverb.applied_sequence=sequence;s_reverb.generation=last;
            portEXIT_CRITICAL(&s_lock);s_reverb_phase=RV_CAP;s_reverb_next=now+REVERB_HEALTH_US;return;
        }
        s_reverb_status=status;s_reverb.wet_q15=packed>>16;s_reverb.damp_q15=packed&0xffffu;
        s_reverb.applied_sequence=sequence;s_reverb.generation=last;s_reverb.valid=true;
        s_reverb_coherent_at=now;
        active=s_reverb_job.active;committed=s_reverb_job.committed;
        bool busy=(status&REVERB_STATUS_BUSY)!=0;
        if(active && committed) {
            if(!busy && sequence==s_reverb_job.sequence) {
                bool matches=packed==s_reverb_job.packed;
                s_reverb.valid=matches;
                s_reverb.fault=!matches?REVERB_LINK_PROTOCOL:s_reverb_job.cancelled?REVERB_LINK_CANCELLED:REVERB_LINK_OK;
                s_reverb.completed_request_id=matches && !s_reverb_job.cancelled?s_reverb_job.id:0;
                s_reverb_job.active=false;s_reverb.pending=false;
            } else {
                s_reverb.pending=true;
                if(now-s_reverb_job.sent_at>=REVERB_TIMEOUT_US) {
                    s_reverb.valid=false;s_reverb.fault=REVERB_LINK_TIMEOUT;
                }
            }
        } else {
            s_reverb.pending=active || busy;s_reverb.fault=REVERB_LINK_OK;
        }
        bool upload=s_reverb_job.active && !s_reverb_job.committed && !s_reverb_job.cancelled && !busy;
        bool wait=s_reverb_job.active;
        portEXIT_CRITICAL(&s_lock);
        s_reverb_phase=upload?RV_SHADOW:wait?RV_FIRST:RV_CAP;
        s_reverb_next=wait?0:now+REVERB_HEALTH_US;return;
    }
    if(s_reverb_phase==RV_SHADOW) {
        portENTER_CRITICAL(&s_lock);
        bool current=s_reverb_job.active && !s_reverb_job.cancelled &&
            s_reverb_job.mode_generation==s_local.generation &&
            (!s_reverb_job.source.epoch || control_source_valid(s_reverb_job.source));
        uint32_t packed=s_reverb_job.packed;
        portEXIT_CRITICAL(&s_lock);
        if(current) {fpga_word_write(REVERB_REG_SHADOW_PARAMS,packed);s_reverb_phase=RV_COMMIT;}
        return;
    }
    portENTER_CRITICAL(&s_lock);
    bool current=s_reverb_job.active && !s_reverb_job.cancelled &&
        s_reverb_job.mode_generation==s_local.generation &&
        (!s_reverb_job.source.epoch || control_source_valid(s_reverb_job.source));
    uint32_t sequence=s_reverb.applied_sequence==REVERB_SEQUENCE_MAX?1:s_reverb.applied_sequence+1;
    if(current) {
        s_reverb_job.sequence=sequence;s_reverb_job.committed=true;s_reverb_job.sent_at=now;
    }
    portEXIT_CRITICAL(&s_lock);
    if(current)fpga_word_write(REVERB_REG_COMMIT,sequence);
    s_reverb_phase=RV_FIRST;
}
