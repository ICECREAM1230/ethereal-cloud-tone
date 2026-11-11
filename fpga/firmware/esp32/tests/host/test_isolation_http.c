/* Real HTTP handlers and their existing SDK/session boundary fixture. */
#define HTTP_V2_TEST_MAIN previous_http_v2_main
#include "test_http_v2.c"
#undef HTTP_V2_TEST_MAIN

static web_control_state_t isolation_snapshot;

static void isolation_setup(unsigned active)
{
    now=100;publication_count=random_call=0;
    control_source_init();memset(s_sessions,0,sizeof(s_sessions));
    memset(&s_http_performance,0,sizeof(s_http_performance));web_raw_queue_reset();
    memset(&isolation_snapshot,0,sizeof(isolation_snapshot));
    isolation_snapshot.ready=true;isolation_snapshot.context_epoch=9;
    isolation_snapshot.panic_revision=7;
    isolation_snapshot.requested_engine=isolation_snapshot.applied_engine=active;
    patch_default(&isolation_snapshot.patch);
    isolation_snapshot.engine=(engine_ks_state_t){.available=true,.valid=true,
        .requested_mode=active==FM_ENGINE_KS,.applied_mode=active==FM_ENGINE_KS};
    isolation_snapshot.fm.status=(engine_fm_status_t){.available=true,.valid=true,.ready=true,
        .backend_id=1,.profile_id=1,.voice_count=8,.flags=FM_FLAG_READY|FM_FLAG_MUTED};
    if(active==FM_ENGINE_FM)isolation_snapshot.fm.status=(engine_fm_status_t){
        .available=true,.valid=true,.ready=true,.selected=true,.backend_id=1,.profile_id=1,
        .voice_count=8,.flags=FM_FLAG_READY|FM_FLAG_SELECTED,.requested_backend=1,.applied_backend=1,
        .session_id=42,.engine_epoch=4,.topology_generation=1};
    isolation_snapshot.inputs=(web_input_state_t){.version=1,.available=true,.initialized=true,
        .enabled=true,.ready=true,.capabilities_known=true,.independent_usb=true};
    http_raw_state=(raw_midi_state_t){.available=true,.initialized=true,.enabled=true,.ready=true,
        .input_capabilities_known=true,.independent_usb=true};
    web_control_store(&isolation_snapshot);
    httpd_req_t req=request("","1");
    assert(v2_session(&req)==ESP_OK && !strcmp(req.status,"201 Created"));
    assert(publication_count==1 && published[0].kind==EVT_WEB && published[0].web.op==WEB_START);
}

static void isolation_command(unsigned sequence,unsigned op,unsigned a,unsigned b,
                              const char *status_code)
{
    char body[128];
    snprintf(body,sizeof(body),"%u %u 9 %u %u %u 0",s_sessions[0].token,sequence,op,a,b);
    expect(v2_command,body,"1",status_code);
}

static void isolation_raw(unsigned sequence,const char *status_code)
{
    char body[180];
    snprintf(body,sizeof(body),"{\"source\":\"web\",\"bytes\":[224,0,64],\"token\":%u,\"sequence\":%u,\"context_epoch\":9}",
        s_sessions[0].token,sequence);
    unsigned before=publication_count;
    httpd_req_t req=request(body,"1");
    assert(midi_raw_post(&req)==ESP_OK);
    if(strcmp(req.status,status_code))fprintf(stderr,"isolated RAW expected %s got %s: %s\n",status_code,req.status,req.response);
    assert(!strcmp(req.status,status_code));
    if(status_code[0]!='2')assert(publication_count==before);
}

static void offline_other_engines(unsigned active)
{
    /* These observations contain the SPI owner's proof of inactivity: the
     * diagnostics survive, but requested/applied/inflight ownership is empty. */
    if(active!=FM_ENGINE_KS)isolation_snapshot.engine=(engine_ks_state_t){.fault=2,.recovery_count=1};
    if(active!=FM_ENGINE_FM)isolation_snapshot.fm.status=(engine_fm_status_t){.reason=FM_LINK_READBACK};
    web_control_store(&isolation_snapshot);
}

static void offline_engines_preserve_session_and_ingress(void)
{
    for(unsigned active=FM_ENGINE_DDS;active<=FM_ENGINE_FM;active++) {
        isolation_setup(active);
        isolation_command(1,FM_HTTP_OPCODE_KEYS_UPDATE,1,100,"202 Accepted");
        uint32_t token=s_sessions[0].token;
        offline_other_engines(active);
        isolation_raw(2,"202 Accepted");
        isolation_command(3,FM_HTTP_OPCODE_KEYS_UPDATE,3,100,"202 Accepted");
        assert(s_sessions[0].token==token && s_sessions[0].context_epoch==9 && s_sessions[0].sequence==3);
        assert(publication_count==4 && published[1].kind==EVT_WEB_V2 &&
            published[2].kind==EVT_WEB_RAW && published[3].kind==EVT_WEB_V2);
        web_raw_job_t job;assert(web_raw_queue_take(published[2].web_v2.a,&job));
        const uint8_t bend[]={0xe0,0,64};
        assert(job.request.token==token && job.request.context_epoch==9 && job.request.length==3);
        assert(!memcmp(job.request.bytes,bend,sizeof(bend)));
        web_control_state_t observed;web_control_read(&observed);
        assert(observed.context_epoch==9 && observed.panic_revision==7);
        assert(!observed.mode_switching && !observed.mode_recovering && observed.inputs.ready);
        httpd_req_t state_req=request("",NULL);
        assert(v2_state(&state_req)==ESP_OK);
        char mode[100];
        snprintf(mode,sizeof(mode),"\"mode\":{\"requested\":%u,\"applied\":%u,\"switching\":false,\"recovering\":false}",active,active);
        assert(strstr(state_req.response,mode));
        assert(strstr(state_req.response,"\"context_epoch\":9"));
    }
}

static void offline_targets_do_not_consume_sequence(void)
{
    isolation_setup(FM_ENGINE_DDS);offline_other_engines(FM_ENGINE_DDS);
    isolation_command(1,FM_HTTP_OPCODE_KEYS_UPDATE,1,100,"202 Accepted");
    isolation_command(2,FM_HTTP_OPCODE_ENGINE_SELECT,FM_ENGINE_FM,1,"503 Service Unavailable");
    isolation_command(2,FM_HTTP_OPCODE_ENGINE_SELECT,FM_ENGINE_KS,0,"503 Service Unavailable");
    isolation_command(2,FM_HTTP_OPCODE_ENGINE_SELECT,3,0,"400 Bad Request");
    assert(s_sessions[0].sequence==1 && publication_count==2);
    isolation_raw(2,"202 Accepted");
    assert(s_sessions[0].sequence==2 && publication_count==3);
}

static void active_and_shared_fences_keep_stop_available(void)
{
    isolation_setup(FM_ENGINE_FM);
    isolation_snapshot.fm.status.recovering=true;
    isolation_snapshot.fm.status.valid=false;isolation_snapshot.inputs.ready=false;
    isolation_snapshot.mode_recovering=true;web_control_store(&isolation_snapshot);
    isolation_command(1,FM_HTTP_OPCODE_KEYS_UPDATE,1,100,"503 Service Unavailable");
    isolation_raw(1,"409 Conflict");
    isolation_command(1,FM_HTTP_OPCODE_STOP,0,0,"202 Accepted");
    assert(published[1].kind==EVT_WEB_V2 && published[1].web_v2.op==FM_HTTP_OPCODE_STOP);

    isolation_setup(FM_ENGINE_DDS);offline_other_engines(FM_ENGINE_DDS);
    http_raw_state.ready=false;http_raw_state.fault=RAW_MIDI_IO;
    isolation_command(1,FM_HTTP_OPCODE_KEYS_UPDATE,1,100,"409 Conflict");
    isolation_raw(1,"409 Conflict");
    isolation_command(1,FM_HTTP_OPCODE_STOP,0,0,"202 Accepted");
    assert(s_sessions[0].sequence==1 && publication_count==2);
}

static void local_owner_snapshot_keeps_raw_and_stop_available(void)
{
    isolation_setup(FM_ENGINE_DDS);offline_other_engines(FM_ENGINE_DDS);
    isolation_snapshot.local=(engine_local_state_t){.supported=true,.valid=true,.local_active=true,
        .adopted=true,.ready=true,.requested=FM_ENGINE_DDS,.applied=FM_ENGINE_DDS,.engine_mask=15};
    web_control_store(&isolation_snapshot);
    isolation_command(1,FM_HTTP_OPCODE_KEYS_UPDATE,1,100,"202 Accepted");
    isolation_raw(2,"202 Accepted");
    isolation_command(3,FM_HTTP_OPCODE_STOP,0,0,"202 Accepted");
    assert(publication_count==4 && published[3].web_v2.op==FM_HTTP_OPCODE_STOP);
    isolation_command(4,FM_HTTP_OPCODE_ENGINE_SELECT,3,0,"400 Bad Request");
    assert(s_sessions[0].sequence==3 && publication_count==4);
}

static void unknown_previous_fm_owner(void)
{
    isolation_setup(FM_ENGINE_DDS);
    memset(&isolation_snapshot.fm.status,0,sizeof(isolation_snapshot.fm.status));
    isolation_snapshot.fm.status.applied_backend=1;
    assert(http_fm_active(&isolation_snapshot) && "unconfirmed FM owner must require API v2");
    web_control_store(&isolation_snapshot);
    expect(command,"1001 1 3 1 100","1","503 Service Unavailable");
    isolation_command(1,FM_HTTP_OPCODE_KEYS_UPDATE,1,100,"503 Service Unavailable");
    assert(!s_sessions[0].sequence && publication_count==1);
}

int main(void)
{
    unknown_previous_fm_owner();
    offline_engines_preserve_session_and_ingress();offline_targets_do_not_consume_sequence();
    active_and_shared_fences_keep_stop_available();local_owner_snapshot_keeps_raw_and_stop_available();
    puts("HTTP engine isolation: inactive diagnostics preserve v2 RAW/keys/session, refused targets, old-owner/shared fences, local input and STOP PASS");
    return 0;
}
