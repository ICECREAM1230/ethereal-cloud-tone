// Use the real FMC discovery service, never a hand-filled FM status fixture.
#define main original_engine_main
#include "test_ae350_engine.c"
#undef main
int main(int argc,char **argv)
{
    assert(argc==2);FILE *out=fopen(argv[1],"wb");assert(out);
    for(unsigned global=0;global<3;global++)for(unsigned fmc=0;fmc<2;fmc++) {
        memset(regs,0,sizeof(regs));fake_time=1000000;io_ok=authorized=s_ready=true;
        fm_init();cpu_fm_init();
        s_cpu_mode=(engine_cpu_mode_state_t){.supported=true,.valid=true,.ready=true,.channel_routes=true,.desired=global,.applied=global};
        regs[FMC_STATUS_SPI]=fmc?FMC_MAGIC:0;regs[AE350_TEMPLATE_CREDIT_REG]=64;
        fake_time+=25000;cpu_fm_service(fake_time+700);
        engine_fm_state_t view;engine_link_fm_state(&view);
        assert(view.status.available==fmc && view.status.selected==fmc);
        if(fmc)assert(view.status.requested_backend==FM_BACKEND_JT51_OPM && view.status.applied_backend==FM_BACKEND_JT51_OPM && view.parameter_count==46);
        assert(s_cpu_mode.desired==global && s_cpu_mode.applied==global);
        assert(fwrite(&view,sizeof(view),1,out)==1);
        printf("ACTUAL cpu_fm_service global=%u FMC_MAGIC=%u available=%u selected=%u requested_backend=%u applied_backend=%u parameters=%u\n",global,fmc,view.status.available,view.status.selected,view.status.requested_backend,view.status.applied_backend,view.parameter_count);
    }
    assert(!fclose(out));puts("PASS captured six complete engine_fm_state_t snapshots from actual engine_link.c/cpu_fm_service; no hand-filled FM owner state");return 0;
}
