// Prepared AE350 FM configuration. Called only by engine_link's SPI owner.
#include "../contract/fm/fm_config_v1.h"
static struct {
    uint32_t words[FMC_WORDS],sequence,cancel_revision;
    engine_fm_parameter_t parameters[ENGINE_FM_PARAMETER_SLOTS];
    unsigned count,phase;
    uint8_t keys;
    bool active;
    int64_t expires;
} s_cpu_fm_job;
static uint32_t s_cpu_fm_floor,s_cpu_fm_observed;
static uint8_t s_cpu_fm_keys;
static int64_t s_cpu_fm_poll;

static void cpu_fm_preset_cache(uint32_t preset,engine_fm_parameter_t *values,unsigned *count,uint8_t *keys)
{
    const fm_preset_t *p=fm_preset(1u,preset);
    *count=0;if(!p)return;
    *keys=(uint8_t)p->key_on_logical_mask;
    for(unsigned i=0;i<p->values_count && i<ENGINE_FM_PARAMETER_SLOTS;i++){
        const fm_preset_value_t *v=&p->values[i];uint32_t target;
        if(!fm_pack_target(FM_TARGET_NONE,v->operator_index,&target)){*count=0;return;}
        values[(*count)++]=(engine_fm_parameter_t){v->parameter_id,target,v->value_u32,0};
    }
}
static bool cpu_fm_value(const engine_fm_parameter_t *p,unsigned count,unsigned id,unsigned op,uint32_t *out)
{
    uint32_t target;if(!fm_pack_target(FM_TARGET_NONE,op,&target))return false;
    for(unsigned i=0;i<count;i++)if(p[i].parameter_id==id && p[i].target_word==target){*out=p[i].value_u32;return true;}
    return false;
}
static bool cpu_fm_encode(void)
{
    uint32_t *w=s_cpu_fm_job.words,v;
    memset(w,0,sizeof(s_cpu_fm_job.words));
    w[0]=FMC_MAGIC;w[1]=s_cpu_fm_job.sequence;w[2]=FM_BACKEND_JT51_OPM;
    if(!cpu_fm_value(s_cpu_fm_job.parameters,s_cpu_fm_job.count,1,FM_TARGET_NONE,&v))return false;
    w[3]=v;
    if(!cpu_fm_value(s_cpu_fm_job.parameters,s_cpu_fm_job.count,2,FM_TARGET_NONE,&v))return false;
    w[3]|=(v<<3)|((uint32_t)s_cpu_fm_job.keys<<8);
    for(unsigned op=0;op<4;op++){
        uint32_t x[11];for(unsigned i=0;i<11;i++)
            if(!cpu_fm_value(s_cpu_fm_job.parameters,s_cpu_fm_job.count,16+i,op,&x[i]))return false;
        uint8_t bytes[6]={(uint8_t)((x[7]<<4)|x[0]),(uint8_t)x[1],(uint8_t)((x[9]<<6)|x[2]),
            (uint8_t)((x[10]<<7)|x[3]),(uint8_t)((x[8]<<6)|x[4]),(uint8_t)((x[5]<<4)|x[6])};
        for(unsigned j=0;j<6;j++)w[4+2*op+j/4]|=(uint32_t)bytes[j]<<((j%4)*8);
    }
    w[12]=fmc_crc(w,12);return true;
}
static void cpu_fm_unavailable(void)
{
    portENTER_CRITICAL(&s_lock);
    fm_clear_intents();
    s_fm.status.available=s_fm.status.valid=s_fm.status.ready=false;
    s_fm.status.selected=false;s_fm.status.reason=FM_LINK_READBACK;
    s_fm.parameter_count=0;s_cpu_fm_job.active=false;s_cpu_fm_floor=s_cpu_fm_observed=0;
    s_fm.status.switching=false;s_fm.status.flags=0;
    portEXIT_CRITICAL(&s_lock);
}
static void cpu_fm_finish(uint32_t result,bool apply)
{
    engine_fm_identity_t identity={s_fm.status.session_id,s_fm.status.engine_epoch,s_cpu_fm_job.sequence};
    s_fm.status.completed=identity;s_fm.status.result=(engine_fm_result_t){identity,result};
    if(apply){
        s_fm.parameter_count=s_cpu_fm_job.count;s_cpu_fm_keys=s_cpu_fm_job.keys;
        memcpy(s_fm.parameters,s_cpu_fm_job.parameters,sizeof(s_fm.parameters));
        for(unsigned i=0;i<s_fm.parameter_count;i++)s_fm.parameters[i].sequence_id=s_cpu_fm_job.sequence;
    }
    s_cpu_fm_job.active=false;s_fm.status.switching=s_fm_preset_pending;
    s_fm.status.flags=FM_FLAG_READY|FM_FLAG_SELECTED;
}
static void cpu_fm_service(int64_t deadline)
{
    if(!CONFIG_FPGA_AE350_CONTROL || esp_timer_get_time()>=deadline)return;
    int64_t now=esp_timer_get_time();
    if(now<s_cpu_fm_poll)return;
    s_cpu_fm_poll=now+(s_cpu_fm_job.active?1000:20000);
    uint32_t first[FMC_STATUS_WORDS],status[FMC_STATUS_WORDS];
    if(!fpga_word_read_burst_observe(FMC_STATUS_SPI,first,FMC_STATUS_WORDS) ||
       !fpga_word_read_burst_observe(FMC_STATUS_SPI,status,FMC_STATUS_WORDS)){
        portENTER_CRITICAL(&s_lock);s_fm.status.valid=s_fm.status.ready=false;portEXIT_CRITICAL(&s_lock);return;
    }
    if(memcmp(first,status,sizeof(status)))return;
    if(status[0]!=FMC_MAGIC){cpu_fm_unavailable();return;}
    portENTER_CRITICAL(&s_lock);
    if(!s_fm.status.available){
        memset(&s_fm,0,sizeof(s_fm));
        s_fm_nonce+=2u;s_fm.status.session_id=s_fm_nonce|1u;s_fm.status.engine_epoch=1;s_fm.status.topology_generation=1;
        s_fm.status.backend_id=FM_BACKEND_JT51_OPM;s_fm.status.profile_id=1u;
        s_fm.status.profile_version=fm_profile(1)->profile_version;s_fm.status.voice_count=ENGINE_CPU_FM_VOICES;
        s_fm.status.available=s_fm.status.valid=s_fm.status.ready=s_fm.status.selected=true;
        s_fm.status.requested_backend=s_fm.status.applied_backend=FM_BACKEND_JT51_OPM;
        s_fm.status.phase=FM_PHASE_READY;s_fm.status.flags=FM_FLAG_READY|FM_FLAG_SELECTED;
        s_fm.status.features=FM_FEATURE_SET_PARAMETER|FM_FEATURE_APPLY_PRESET|FM_FEATURE_NEXT_NOTE_PARAMETERS|FM_FEATURE_NATIVE_STEREO_PAN;
        // Current PCM_Audio_PLL: 50MHz *233/474, 256 cycles/frame.
        // Prepared JT51 produces one PCM frame per audio request.
        s_fm.source_numerator=s_fm.output_numerator=91015625u;
        s_fm.source_denominator=s_fm.output_denominator=948u;
        for(unsigned i=0;i<ENGINE_CPU_FM_VOICES;i++)s_fm.operators_per_voice[i]=ENGINE_CPU_FM_OPERATORS;
        if(!status[1] && !status[2])cpu_fm_preset_cache(FMC_DEFAULT_PRESET_ID,s_fm.parameters,&s_fm.parameter_count,&s_cpu_fm_keys);
        s_cpu_fm_floor=status[1];
    }
    s_fm.status.valid=s_fm.status.ready=true;
    if(s_cpu_fm_observed && !status[1]){
        portEXIT_CRITICAL(&s_lock);cpu_fm_unavailable();return;
    }
    s_cpu_fm_observed=status[1];
    if(!s_cpu_fm_floor || (status[1]-s_cpu_fm_floor<UINT32_C(0x80000000)))s_cpu_fm_floor=status[1];
    if(s_cpu_fm_job.active){
        if(status[1]==s_cpu_fm_job.sequence)
            s_fm.status.accepted=(engine_fm_identity_t){s_fm.status.session_id,s_fm.status.engine_epoch,status[1]};
        if(status[2]==s_cpu_fm_job.sequence){
            bool same=s_cpu_fm_job.cancel_revision==s_fm_cancel_revision;
            uint32_t result=status[3]==FMC_OK?FM_RESULT_OK:status[3]==FMC_UNSUPPORTED?FM_RESULT_UNSUPPORTED:FM_RESULT_BAD_VALUE;
            if(result==FM_RESULT_OK && status[4]!=s_cpu_fm_job.words[12])result=FM_RESULT_BACKEND_FAULT;
            if(!same){result=FM_RESULT_CANCELLED;s_fm.parameter_count=0;}
            if(result==FM_RESULT_BACKEND_FAULT)s_fm.parameter_count=0;
            cpu_fm_finish(result,result==FM_RESULT_OK);
        }else if(now>=s_cpu_fm_job.expires){
            s_fm.parameter_count=0;cpu_fm_finish(FM_RESULT_BACKEND_FAULT,false);
        }
    }
    if(!s_cpu_fm_job.active){
        bool dirty=s_fm_preset_pending;for(unsigned i=0;i<ENGINE_FM_PARAMETER_SLOTS;i++)dirty|=s_fm_parameters[i].dirty;
        if(dirty){
            memset(&s_cpu_fm_job,0,sizeof(s_cpu_fm_job));
            s_cpu_fm_job.count=s_fm.parameter_count;s_cpu_fm_job.keys=s_cpu_fm_keys;
            memcpy(s_cpu_fm_job.parameters,s_fm.parameters,sizeof(s_fm.parameters));
            if(s_fm_preset_pending){
                cpu_fm_preset_cache(s_fm_preset_id,s_cpu_fm_job.parameters,&s_cpu_fm_job.count,&s_cpu_fm_job.keys);
                s_fm_preset_pending=false;
            }
            for(unsigned i=0;i<ENGINE_FM_PARAMETER_SLOTS;i++)if(s_fm_parameters[i].dirty){
                for(unsigned j=0;j<s_cpu_fm_job.count;j++)if(s_cpu_fm_job.parameters[j].parameter_id==s_fm_parameters[i].value.parameter_id &&
                    s_cpu_fm_job.parameters[j].target_word==s_fm_parameters[i].value.target_word)
                    s_cpu_fm_job.parameters[j]=s_fm_parameters[i].value;
                s_fm_parameters[i].dirty=false;
            }
            s_cpu_fm_job.sequence=++s_cpu_fm_floor;if(!s_cpu_fm_job.sequence)s_cpu_fm_job.sequence=++s_cpu_fm_floor;
            s_cpu_fm_job.cancel_revision=s_fm_cancel_revision;
            if(cpu_fm_encode()){
                s_cpu_fm_job.active=true;s_cpu_fm_job.expires=now+1000000;
                s_fm.status.switching=true;s_fm.status.flags|=FM_FLAG_BUSY;
            }else s_fm.status.switching=false;
        }
    }
    bool send=s_cpu_fm_job.active && s_cpu_fm_job.phase<3;
    unsigned phase=s_cpu_fm_job.phase;
    portEXIT_CRITICAL(&s_lock);
    if(!send || esp_timer_get_time()>=deadline)return;
    uint16_t address=phase==0?FMC_BASE:phase==1?FMC_BASE+12:FMC_COMMIT;
    const uint32_t *data=phase==0?s_cpu_fm_job.words:phase==1?s_cpu_fm_job.words+12:&s_cpu_fm_job.sequence;
    fpga_template_write_result_t result=fpga_template_write_burst(address,data,phase==0?12:1);
    portENTER_CRITICAL(&s_lock);
    if(result==FPGA_TEMPLATE_SENT)s_cpu_fm_job.phase++;
    else if(result==FPGA_TEMPLATE_UNCERTAIN){
        if(phase==2)s_cpu_fm_job.phase=3; // Observe immutable COMMIT; never resend it.
        else cpu_fm_finish(FM_RESULT_BACKEND_FAULT,false); // No COMMIT after an uncertain body.
    }else if(result!=FPGA_TEMPLATE_DEFERRED)cpu_fm_finish(FM_RESULT_BACKEND_FAULT,false);
    portEXIT_CRITICAL(&s_lock);
}

static void cpu_fm_init(void)
{
    memset(&s_cpu_fm_job,0,sizeof(s_cpu_fm_job));
    s_cpu_fm_floor=s_cpu_fm_observed=0;s_cpu_fm_keys=1;s_cpu_fm_poll=0;
}
