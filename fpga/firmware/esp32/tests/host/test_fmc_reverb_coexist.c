/* Both production owners use the same fake SPI bus and register table.
 * The hardware map is deliberately literal, independent of the FW macro. */
#define main original_ae350_engine_main
#include "test_ae350_engine.c"
#undef main

enum { HARDWARE_FMC_STATUS = 0x07c0 };

static void coexist_tick(void)
{
    fake_time += 1000;
    cpu_fm_service(fake_time + 700);
    reverb_service(fake_time + 700);
}

static void coexist_ticks(unsigned count)
{ while(count--) coexist_tick(); }

static void assert_owners_ready(void)
{
    if(!s_fm.status.available || !s_reverb.available || !s_reverb.valid) {
        fprintf(stderr,"FMC_RVR_COEXISTENCE: fm_available=%u reverb_available=%u valid=%u fault=%u; FMC read=%04x hardware=%04x RVR=%04x\n",
            s_fm.status.available,s_reverb.available,s_reverb.valid,s_reverb.fault,
            FMC_STATUS_SPI,HARDWARE_FMC_STATUS,REVERB_REG_MAGIC);
    }
    assert(s_fm.status.available && s_fm.status.valid && s_fm.status.ready);
    assert(s_reverb.available && s_reverb.valid && !s_reverb.fault);
}

int main(int argc,char **argv)
{
    bool collision=argc==2 && !strcmp(argv[1],"--legacy-collision");
    assert(argc==1 || collision);
    memset(regs,0,sizeof(regs));memset(&s_local,0,sizeof(s_local));
    fake_time=1000000;io_ok=authorized=s_ready=true;
    fm_init();cpu_fm_init();reverb_init();
    regs[AE350_TEMPLATE_CREDIT_REG]=64;
    regs[REVERB_REG_MAGIC]=REVERB_MAGIC;regs[REVERB_REG_ABI]=REVERB_ABI;
    regs[REVERB_REG_STATUS]=REVERB_STATUS_READY;
    regs[REVERB_REG_APPLIED_PARAMS]=(8192u<<16)|7000u;
    regs[REVERB_REG_APPLIED_SEQUENCE]=41;regs[REVERB_REG_GENERATION]=10;
    regs[HARDWARE_FMC_STATUS]=FMC_MAGIC;
    if(collision) {
        /* Reproduce the old FPGA priority decode: FMC shadows RVR at 0720. */
        for(unsigned i=0;i<FMC_STATUS_WORDS;i++)
            regs[REVERB_REG_MAGIC+i]=regs[HARDWARE_FMC_STATUS+i];
        memset(&regs[HARDWARE_FMC_STATUS],0,FMC_STATUS_WORDS*sizeof(uint32_t));
    }
    coexist_ticks(40);assert_owners_ready();
    assert(s_fm.status.voice_count==32 && s_fm.parameter_count==46);
    assert(FMC_DEFAULT_PRESET_ID==3 && s_cpu_fm_keys==15);
    for(unsigned i=0;i<32;i++)assert(s_fm.operators_per_voice[i]==4);
    uint32_t initial_fmc[FMC_STATUS_WORDS];
    memcpy(initial_fmc,&regs[HARDWARE_FMC_STATUS],sizeof(initial_fmc));
    assert(engine_link_reverb_request(9012,11345,101));
    assert(engine_link_fm_preset(4)==FM_RESULT_OK);
    for(unsigned i=0;i<80 && (!s_reverb_job.committed || s_cpu_fm_job.phase!=3);i++)coexist_tick();
    assert(s_reverb_job.committed && s_reverb.pending && s_cpu_fm_job.active && s_cpu_fm_job.phase==3);
    assert(regs[REVERB_REG_SHADOW_PARAMS]==((9012u<<16)|11345u));
    assert(regs[REVERB_REG_COMMIT]==42 && s_reverb_job.sequence==42);
    assert(s_reverb.applied_sequence==41 && s_reverb.completed_request_id==0);
    assert(!memcmp(initial_fmc,&regs[HARDWARE_FMC_STATUS],sizeof(initial_fmc)));
    assert(regs[FMC_BASE]==FMC_MAGIC && regs[FMC_BASE+1]==s_cpu_fm_job.sequence);
    assert(regs[FMC_BASE+12]==fmc_crc(&regs[FMC_BASE],12));
    assert(regs[FMC_COMMIT]==s_cpu_fm_job.sequence);
    /* Hardware accepted both independent writes; neither owner is complete. */
    uint32_t fm_sequence=s_cpu_fm_job.sequence,fm_crc=s_cpu_fm_job.words[12];
    regs[REVERB_REG_STATUS]=REVERB_STATUS_READY|REVERB_STATUS_BUSY;
    regs[REVERB_REG_GENERATION]++;
    regs[HARDWARE_FMC_STATUS+1]=fm_sequence;
    coexist_ticks(6);
    assert(s_fm.status.accepted.sequence_id==fm_sequence);
    assert(s_fm.status.completed.sequence_id!=fm_sequence && s_cpu_fm_job.active);
    assert(s_reverb.pending && s_reverb.applied_sequence==41 && !s_reverb.completed_request_id);
    uint32_t rvr_before[8];memcpy(rvr_before,&regs[REVERB_REG_MAGIC],sizeof(rvr_before));
    regs[HARDWARE_FMC_STATUS+2]=fm_sequence;
    regs[HARDWARE_FMC_STATUS+3]=FMC_OK;regs[HARDWARE_FMC_STATUS+4]=fm_crc;
    coexist_ticks(6);
    assert(!s_cpu_fm_job.active && s_fm.status.result.code==FM_RESULT_OK);
    assert(s_fm.status.completed.sequence_id==fm_sequence && s_fm.parameter_count==46);
    assert(!memcmp(rvr_before,&regs[REVERB_REG_MAGIC],sizeof(rvr_before)));
    assert(s_reverb.pending && s_reverb.applied_sequence==41);
    uint32_t fmc_before[FMC_STATUS_WORDS];
    memcpy(fmc_before,&regs[HARDWARE_FMC_STATUS],sizeof(fmc_before));
    regs[REVERB_REG_APPLIED_PARAMS]=regs[REVERB_REG_SHADOW_PARAMS];
    regs[REVERB_REG_APPLIED_SEQUENCE]=regs[REVERB_REG_COMMIT];
    regs[REVERB_REG_STATUS]=REVERB_STATUS_READY;regs[REVERB_REG_GENERATION]++;
    coexist_ticks(12);assert_owners_ready();
    assert(!s_reverb.pending && s_reverb.completed_request_id==101 && s_reverb.applied_sequence==42);
    assert(s_reverb.wet_q15==9012 && s_reverb.damp_q15==11345);
    assert(!memcmp(fmc_before,&regs[HARDWARE_FMC_STATUS],sizeof(fmc_before)));
    /* A wrong FMC CRC faults its transaction while RVR stays usable. */
    assert(engine_link_fm_preset(3)==FM_RESULT_OK);
    for(unsigned i=0;i<40 && (!s_cpu_fm_job.active || s_cpu_fm_job.phase!=3);i++)coexist_tick();
    assert(s_cpu_fm_job.active && s_cpu_fm_job.phase==3);
    fm_sequence=s_cpu_fm_job.sequence;
    regs[HARDWARE_FMC_STATUS+1]=regs[HARDWARE_FMC_STATUS+2]=fm_sequence;
    regs[HARDWARE_FMC_STATUS+4]=s_cpu_fm_job.words[12]^1u;
    coexist_ticks(6);
    assert(!s_cpu_fm_job.active && s_fm.status.result.code==FM_RESULT_BACKEND_FAULT);
    assert(s_reverb.available && s_reverb.valid && !s_reverb.fault && s_reverb.completed_request_id==101);
    assert(engine_link_reverb_request(4567,6789,102));
    puts("PASS FMC_RVR_COEXISTENCE: real engine_link owners, one shared SPI table, FMC@07c0/RVR@0720, concurrent shadow/COMMIT/accepted/completed/CRC, no cross-owner pollution; boot EP/32x4/46 parameters retained");
    return 0;
}
