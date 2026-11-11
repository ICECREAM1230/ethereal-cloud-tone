// Replay fresh real-FMC service views through the production Web owner.
#define main unused_original_raw_owner_main
#include "test_raw_web_owner.c"
#undef main
extern engine_fm_state_t fake_fm_state;
static engine_cpu_mode_state_t mode_view;
void engine_link_cpu_mode_state(engine_cpu_mode_state_t *out){*out=mode_view;}
static const char *scenario;
static bool is(const char *name){return !strcmp(scenario,name);}
int main(int argc,char **argv)
{
    assert(argc==3);scenario=argv[1];setvbuf(stdout,NULL,_IONBF,0);
    FILE *in=fopen(argv[2],"rb");assert(in);
    const uint8_t controls[]={73,75,79,72,102,103,104,105,7,10};
    unsigned total=0;
    // These expected bytes are the MIDI control contract, independent of the
    // parameter mapper and SUB1 compiler used by the production owner.
    const uint8_t expected_adsr[]={180,180,74,180,180,180,74,180};
    unsigned globals=is("dds")?3:1;
    for(unsigned global=0;global<globals;global++)for(unsigned found=is("dds")?0:1;found<2;found++) {
        reset_owner();observed.cpu_control=true;template_state.sub_patch_supported=true;
        mode_view=(engine_cpu_mode_state_t){.supported=true,.valid=true,.ready=true,.channel_routes=!is("single-editor"),.desired=global,.applied=global};
        assert(!fseek(in,(long)((global*2+found)*sizeof(fake_fm_state)),SEEK_SET));
        assert(fread(&fake_fm_state,sizeof(fake_fm_state),1,in)==1);
        assert(fake_fm_state.status.available==found && fake_fm_state.status.selected==found);
        owner_cpu_mode_sync();start(0,111);
        unsigned route=is("route-ks")?FM_ENGINE_KS:is("route-fm")?FM_ENGINE_FM:FM_ENGINE_DDS;
        web_v2_event_t e={.client=0,.token=111,.sequence=1,.context_epoch=s_control_epoch,
            .op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=route,.expires=fake_time+WEB_LEASE_US};
        if(mode_view.channel_routes){handle_web_v2(&e,fake_time);assert(s_v2_result_code[0]==FM_RESULT_OK && s_web[0].midi_channel==route);}
        if(is("route-fault")){mode_view.fault=true;owner_cpu_mode_sync();start(0,111);}
        if(is("desired3")){mode_view.desired=3;owner_cpu_mode_sync();start(0,111);}
        if(is("applied3")){mode_view.applied=3;owner_cpu_mode_sync();start(0,111);}
        if(is("applied42")){mode_view.applied=42;owner_cpu_mode_sync();start(0,111);}
        if(is("desired3") || is("applied3") || is("applied42")) {
            e.context_epoch=s_control_epoch;e.sequence=1;e.op=FM_HTTP_OPCODE_ENGINE_SELECT;e.a=FM_ENGINE_DDS;
            handle_web_v2(&e,fake_time);assert(s_v2_result_code[0]==FM_RESULT_OK && s_web[0].midi_channel==FM_ENGINE_DDS);
        }
        if(is("cap-invalid"))template_state.sub_patch_valid=false;
        if(is("cap-unsupported"))template_state.sub_patch_supported=false;
        if(is("source-revoked")) {
            control_source_ref_t source;assert(control_source_claim(CONTROL_SOURCE_WIFI,fake_time,&source));s_web[0].source=source;
            control_source_revoke(source);
        }
        uint32_t epoch=s_control_epoch;e.context_epoch=epoch;
        if(is("context-stale"))e.context_epoch=epoch-1;
        for(unsigned i=0;i<sizeof controls;i++) {
            patch_t before=g_patch;uint32_t old_parameter=s_web[0].parameter_sequence,old_result_seq=s_v2_result_sequence[0];
            e.op=FM_HTTP_OPCODE_CC;e.a=controls[i];e.b=37;e.sequence++;e.expires=fake_time+WEB_LEASE_US;
            handle_web_v2(&e,fake_time);
            bool illegal_mode=is("desired3") || is("applied3") || is("applied42");
            bool legacy_block=found && i<8 && (!mode_view.channel_routes || route!=FM_ENGINE_DDS || illegal_mode);
            uint32_t expected=is("context-stale")?FM_RESULT_STALE_EPOCH:
                is("route-fault")?FM_RESULT_BUSY:
                ((is("cap-invalid") || is("cap-unsupported")) && sub_patch1_required_cc(e.a))?FM_RESULT_UNSUPPORTED:
                legacy_block?FM_RESULT_UNSUPPORTED:FM_RESULT_OK;
            if(is("source-revoked")) {
                assert(s_v2_result_sequence[0]==old_result_seq && s_web[0].parameter_sequence==old_parameter && !memcmp(&before,&g_patch,sizeof before));
            } else {
                if(s_v2_result_code[0]!=expected)fprintf(stderr,"%s global=%u FMC=%u route=%u cc=%u expected=%u actual=%u\n",scenario,global,found,route,e.a,expected,s_v2_result_code[0]);
                assert(s_v2_result_code[0]==expected);
                if(expected!=FM_RESULT_OK)assert(!memcmp(&before,&g_patch,sizeof before) && s_web[0].parameter_sequence==old_parameter);
                else {
                    assert(s_web[0].parameter_sequence==e.sequence);
                    if(i<8) {
                        assert(memcmp(&before,&g_patch,sizeof before));
                        uint8_t actual_adsr[]={g_patch.env[0].attack,g_patch.env[0].decay,
                            g_patch.env[0].sustain,g_patch.env[0].release,g_patch.env[1].attack,
                            g_patch.env[1].decay,g_patch.env[1].sustain,g_patch.env[1].release};
                        assert(actual_adsr[i]==expected_adsr[i]);
                        fake_time+=30000;owner_templates_update();
                        assert(s_template_compiled && s_template_full_sub);
                        assert(published_image.elements[2][5]==patch_adsr_rate1(&g_patch.env[0]));
                        assert(published_image.elements[2][6]==patch_adsr_rate2(&g_patch.env[0]));
                        assert(published_image.elements[2][7]==patch_adsr_rate1(&g_patch.env[1]));
                        assert(published_image.elements[3][5]==patch_adsr_rate2(&g_patch.env[1]));
                        assert(!template_state.applied_generation);
                    }
                }
            }
            assert(s_control_epoch==epoch);
            if(!illegal_mode)assert(mode_view.desired==global && mode_view.applied==global);
            total++;
        }
        if(is("dds") && found) {
            uint32_t target;assert(fm_pack_target(FM_TARGET_NONE,0,&target));
            e.sequence++;e.op=FM_HTTP_OPCODE_FM_PARAMETER;e.a=17;e.b=target;e.c=42;
            handle_web_v2(&e,fake_time);assert(s_v2_result_code[0]==FM_RESULT_OK);
            e.sequence++;e.c=128;handle_web_v2(&e,fake_time);assert(s_v2_result_code[0]==FM_RESULT_BAD_VALUE);
            e.sequence++;e.op=FM_HTTP_OPCODE_FM_PRESET;e.a=0;e.b=e.c=0;handle_web_v2(&e,fake_time);assert(s_v2_result_code[0]==FM_RESULT_OK);
            assert(s_control_epoch==epoch && s_web[0].midi_channel==FM_ENGINE_DDS);
        }
    }
    assert(!fclose(in));printf("PASS %s %u CC events: ADSR/shared result and source/context/SUB1/route fences match expected; CPU editor/context preserved; FM dedicated operations unchanged\n",scenario,total);return 0;
}
