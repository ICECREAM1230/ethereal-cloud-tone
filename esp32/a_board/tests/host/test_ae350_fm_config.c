#define main original_ae350_engine_main
#include "test_ae350_engine.c"
#undef main
static void config_tick(void){fake_time+=25000;cpu_fm_service(fake_time+700);}
static void finish_config(void){
    regs[FMC_STATUS_SPI+1]=s_cpu_fm_job.sequence;config_tick();
    assert(s_fm.status.accepted.sequence_id==s_cpu_fm_job.sequence);
    regs[FMC_STATUS_SPI+2]=s_cpu_fm_job.sequence;regs[FMC_STATUS_SPI+3]=FMC_OK;
    regs[FMC_STATUS_SPI+4]=s_cpu_fm_job.words[12];config_tick();
}
static uint32_t cached(unsigned id,unsigned op){
    uint32_t value=0;assert(cpu_fm_value(s_fm.parameters,s_fm.parameter_count,id,op,&value));return value;
}
int main(void){
    memset(regs,0,sizeof(regs));fake_time=1000000;io_ok=authorized=s_ready=true;
    fm_init();cpu_fm_init();regs[FMC_STATUS_SPI]=FMC_MAGIC;regs[AE350_TEMPLATE_CREDIT_REG]=64;
    config_tick();assert(s_fm.status.available&&s_fm.parameter_count==46);
    assert(s_fm.status.voice_count==32 && ENGINE_FM_VOICES==8);
    for(unsigned voice=0;voice<32;voice++)assert(s_fm.operators_per_voice[voice]==4);
    assert(engine_link_fm_mode(FM_BACKEND_JT51_OPM)&&!engine_link_fm_mode(UINT32_C(2)));
    assert(cached(1,FM_TARGET_NONE)==4);
    unsigned before=writes;assert(engine_link_fm_preset(0)==FM_RESULT_OK&&writes==before);
    config_tick();assert(s_cpu_fm_job.active&&s_cpu_fm_job.phase==1&&s_fm.parameter_count==46);
    assert(cached(1,FM_TARGET_NONE)==4);assert(regs[FMC_BASE+3]==0x107);
    config_tick();assert(s_cpu_fm_job.phase==2);config_tick();assert(s_cpu_fm_job.phase==3);
    assert(!s_fm.status.completed.sequence_id&&cached(1,FM_TARGET_NONE)==4);
    finish_config();assert(!s_cpu_fm_job.active&&s_fm.status.result.code==FM_RESULT_OK);
    assert(cached(1,FM_TARGET_NONE)==7&&cached(17,0)==8);
    assert(engine_link_fm_preset(3)==FM_RESULT_OK);config_tick();config_tick();config_tick();finish_config();
    assert(cached(1,FM_TARGET_NONE)==4&&cached(17,0)==32);
    uint32_t target;assert(fm_pack_target(FM_TARGET_NONE,0,&target));
    before=writes;assert(engine_link_fm_parameter(17,target,42)==FM_RESULT_OK&&writes==before);
    config_tick();assert(cached(17,0)==32&&((regs[FMC_BASE+4]>>8)&127u)==42);
    config_tick();config_tick();finish_config();assert(cached(17,0)==42);
    assert(engine_link_fm_parameter(17,target,128)==FM_RESULT_BAD_VALUE);
    assert(engine_link_fm_preset(4)==FM_RESULT_OK);regs[AE350_TEMPLATE_CREDIT_REG]=1;before=writes;
    config_tick();assert(s_cpu_fm_job.active&&s_cpu_fm_job.phase==0&&writes==before);
    regs[AE350_TEMPLATE_CREDIT_REG]=64;config_tick();config_tick();config_tick();finish_config();
    assert(cached(24,0)==1&&cached(16,3)==2);
    assert(engine_link_fm_preset(3)==FM_RESULT_OK);config_tick();config_tick();config_tick();
    regs[FMC_STATUS_SPI+1]=s_cpu_fm_job.sequence;regs[FMC_STATUS_SPI+2]=s_cpu_fm_job.sequence;
    regs[FMC_STATUS_SPI+4]=0;config_tick();assert(s_fm.status.result.code==FM_RESULT_BACKEND_FAULT&&!s_fm.parameter_count);
    assert(engine_link_fm_parameter(17,target,42)==FM_RESULT_STALE_EPOCH);
    assert(engine_link_fm_preset(4)==FM_RESULT_OK);config_tick();config_tick();config_tick();finish_config();
    assert(engine_link_fm_preset(3)==FM_RESULT_OK);config_tick();engine_link_fm_panic();
    config_tick();config_tick();finish_config();assert(s_fm.status.result.code==FM_RESULT_CANCELLED&&!s_fm.parameter_count);
    regs[FMC_STATUS_SPI]=0;config_tick();assert(!s_fm.status.available);
    regs[FMC_STATUS_SPI]=FMC_MAGIC;regs[FMC_STATUS_SPI+1]=regs[FMC_STATUS_SPI+2]=0;config_tick();
    assert(s_fm.status.available&&cached(1,FM_TARGET_NONE)==4);
    assert(FMC_DEFAULT_PRESET_ID==3u&&s_cpu_fm_keys==15u&&cached(17,0)==32);
    // Every current catalog voice is explicitly selectable and remains next-note.
    for(unsigned preset=0;preset<5;preset++){
        const fm_preset_t *p=fm_preset(1,preset);assert(p&&p->values_count==46);
        engine_fm_parameter_t previous[ENGINE_FM_PARAMETER_SLOTS];memcpy(previous,s_fm.parameters,sizeof previous);
        unsigned previous_count=s_fm.parameter_count,before_writes=writes;
        assert(engine_link_fm_preset(preset)==FM_RESULT_OK&&writes==before_writes);
        config_tick();config_tick();config_tick();
        assert(s_cpu_fm_job.active&&s_cpu_fm_job.phase==3);
        assert(s_fm.parameter_count==previous_count&&!memcmp(previous,s_fm.parameters,sizeof previous));
        assert(s_cpu_fm_job.words[12]==fmc_crc(s_cpu_fm_job.words,12));
        finish_config();assert(!s_cpu_fm_job.active&&s_fm.status.result.code==FM_RESULT_OK);
        assert(s_cpu_fm_keys==p->key_on_logical_mask);
        for(unsigned v=0;v<p->values_count;v++)assert(cached(p->values[v].parameter_id,p->values[v].operator_index)==p->values[v].value_u32);
    }
    // An ESP-only restart cannot pretend the CPU's existing custom image is boot EP.
    regs[FMC_STATUS_SPI+1]=regs[FMC_STATUS_SPI+2]=19;
    fm_init();cpu_fm_init();unsigned before_restart=writes;config_tick();
    assert(s_fm.status.available&&s_fm.parameter_count==0&&!s_cpu_fm_job.active&&writes==before_restart);
    // First configuration may already be accepted when ESP restarts; unknown in-flight work is not boot state.
    regs[FMC_STATUS_SPI+1]=21;regs[FMC_STATUS_SPI+2]=0;
    fm_init();cpu_fm_init();config_tick();
    assert(s_fm.status.available&&s_fm.parameter_count==0&&!s_cpu_fm_job.active&&writes==before_restart);
    regs[FMC_STATUS_SPI]=0;config_tick();regs[FMC_STATUS_SPI]=FMC_MAGIC;
    regs[FMC_STATUS_SPI+1]=regs[FMC_STATUS_SPI+2]=0;config_tick();
    assert(s_fm.parameter_count==46&&cached(1,FM_TARGET_NONE)==4&&cached(17,0)==32&&!s_cpu_fm_job.active);
    puts("PASS ESP32 AE350 FMC1: real engine owner serializer, no early cache ACK, boot electric piano, all5 presets next-note, operator values, credit, CRC, cancel/reset and ESP-only restart no overwrite");
}
