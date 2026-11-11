/* Production owner decisions with SPI observations supplied by host fixtures. */
#define main previous_raw_owner_main
#include "test_raw_web_owner.c"
#undef main

extern engine_fm_state_t fake_fm_state;
extern engine_local_state_t fake_local_state;
static engine_ks_state_t isolation_ks;
static unsigned isolation_mode_calls;

/* Only the SPI observation boundary is replaced; dispatch and RAW encoding
 * below are the production owner. Unsupported SELECT must never reach it. */
void engine_link_ks_state(engine_ks_state_t *state){*state=isolation_ks;}
bool engine_link_ks_note_on(unsigned voice,const engine_ks_note_t *note)
{(void)voice;(void)note;return false;}
void engine_link_ks_note_off(unsigned voice){(void)voice;}
void engine_link_ks_panic(void){}
bool engine_link_ks_mode(bool mode)
{
    isolation_mode_calls++;
    if(isolation_ks.recovering || (mode && !isolation_ks.available))return false;
    isolation_ks.requested_mode=mode;
    isolation_ks.switching=mode!=isolation_ks.applied_mode;
    return true;
}
void engine_link_ks_gain(unsigned voice,uint16_t left,uint16_t right)
{(void)voice;(void)left;(void)right;}
void engine_link_ks_loss(unsigned voice,uint16_t loss){(void)voice;(void)loss;}
void engine_link_owner_panic_ack(void){}

static void isolation_setup(unsigned active)
{
    memset(&fake_fm_state,0,sizeof(fake_fm_state));
    memset(&fake_local_state,0,sizeof(fake_local_state));
    memset(&s_remote_mode,0,sizeof(s_remote_mode));
    s_owner_local_generation=s_owner_local_adopted=0;
    reset_owner();
    s_requested_engine=active;s_panic_revision=0;
    s_dirty=s_apply_active=0;isolation_mode_calls=0;
    isolation_ks=(engine_ks_state_t){.available=true,.valid=true,
        .requested_mode=active==FM_ENGINE_KS,.applied_mode=active==FM_ENGINE_KS};
    fake_fm_state.status=(engine_fm_status_t){.available=true,.valid=true,.ready=true,
        .backend_id=1,.profile_id=1,.voice_count=8,.flags=FM_FLAG_READY|FM_FLAG_MUTED};
    if(active==FM_ENGINE_FM) {
        fake_fm_state.status.selected=true;
        fake_fm_state.status.requested_backend=fake_fm_state.status.applied_backend=1;
        fake_fm_state.status.flags=FM_FLAG_READY|FM_FLAG_SELECTED;
        fake_fm_state.status.session_id=42;fake_fm_state.status.engine_epoch=4;
        fake_fm_state.status.topology_generation=1;
    }
}

static void offline_other_engines(unsigned active)
{
    /* engine_link has proved these blocks inactive. A bare recovering=true
     * observation is deliberately tested separately as a blocking fence. */
    if(active!=FM_ENGINE_KS)isolation_ks=(engine_ks_state_t){.fault=2,.recovery_count=1};
    if(active!=FM_ENGINE_FM)fake_fm_state.status=(engine_fm_status_t){.reason=FM_LINK_READBACK};
}

static void offline_engines_preserve_live_input(void)
{
    for(unsigned active=FM_ENGINE_DDS;active<=FM_ENGINE_FM;active++) {
        isolation_setup(active);
        owner_templates_update();
        web_v2_event_t event=keys(0,111,1,1);handle_web_v2(&event,fake_time);
        assert(stream_length==3 && s_web[0].keys==1);
        uint32_t context=s_control_epoch,panic=s_panic_revision;
        offline_other_engines(active);
        owner_templates_update();publish_web_state();
        web_control_state_t snapshot;web_control_read(&snapshot);
        assert(owner_raw_ready() && snapshot.inputs.ready);
        assert(snapshot.requested_engine==active && snapshot.applied_engine==active);
        assert(!snapshot.mode_switching && !snapshot.mode_recovering);
        if(active!=FM_ENGINE_FM)assert(!snapshot.fm.status.available && snapshot.fm.status.reason==FM_LINK_READBACK);
        if(active!=FM_ENGINE_KS)assert(!snapshot.engine.available && snapshot.engine.fault==2);
        assert(s_web[0].keys==1 && s_web[0].armed);
        assert(dds_selected()==(active==FM_ENGINE_DDS));
        const uint8_t bend[]={0xe0,0,64};
        event=raw_event(0,111,2,bend,sizeof(bend));handle_web_raw(&event,fake_time);
        assert(s_web[0].sequence==2 && stream_length==6);
        assert(!memcmp(stream+3,bend,sizeof(bend)));
        event=keys(0,111,3,3);handle_web_v2(&event,fake_time);
        assert(s_v2_result_code[0]==FM_RESULT_OK && s_web[0].keys==3 && stream_length==9);
        assert(s_control_epoch==context && s_panic_revision==panic && !global_flushes);
        for(unsigned source=0;source<4;source++)assert(!source_flushes[source]);
    }
}

static void offline_select_preserves_held_keys(void)
{
    isolation_setup(FM_ENGINE_DDS);offline_other_engines(FM_ENGINE_DDS);
    web_v2_event_t event=keys(0,111,1,1);handle_web_v2(&event,fake_time);
    uint32_t context=s_control_epoch,panic=s_panic_revision;
    for(unsigned target=FM_ENGINE_KS;target<=FM_ENGINE_FM;target++) {
        event=(web_v2_event_t){.client=0,.token=111,.sequence=target+1,.context_epoch=context,
            .op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=target,.b=target==FM_ENGINE_FM?1:0,
            .expires=fake_time+WEB_LEASE_US};
        handle_web_v2(&event,fake_time);
        assert(s_v2_result_code[0]==FM_RESULT_UNSUPPORTED && !s_web[0].parameter_sequence);
        assert(s_requested_engine==FM_ENGINE_DDS && s_web[0].keys==1 && s_web[0].armed);
        assert(stream_length==3 && !isolation_mode_calls && !global_flushes);
        assert(s_control_epoch==context && s_panic_revision==panic);
    }
}

static void current_or_unconfirmed_fault_blocks_input(void)
{
    for(unsigned cause=0;cause<3;cause++) {
        isolation_setup(cause==0?FM_ENGINE_FM:FM_ENGINE_DDS);
        if(cause==0)fake_fm_state.status.valid=false;
        if(cause==1)isolation_ks.recovering=true;
        if(cause==2)fake_fm_state.status.recovering=fake_fm_state.status.needs_select=true;
        assert(!owner_raw_ready());
        web_v2_event_t event=keys(0,111,1,1);handle_web_v2(&event,fake_time);
        assert(s_v2_result_code[0]==FM_RESULT_BUSY && !stream_length);
        const uint8_t on[]={0x90,60,100};
        event=raw_event(1,222,1,on,sizeof(on));handle_web_raw(&event,fake_time);
        assert(s_web[1].sequence==1 && !s_web[1].armed && !stream_length);
    }
}

static void shared_reset_keeps_global_recovery(void)
{
    isolation_setup(FM_ENGINE_DDS);offline_other_engines(FM_ENGINE_DDS);
    owner_templates_update();
    web_v2_event_t event=keys(0,111,1,1);handle_web_v2(&event,fake_time);
    uint32_t context=s_control_epoch,panic=s_panic_revision;
    observed.ready=false;observed.fault=RAW_MIDI_IO;
    assert(!owner_raw_ready());
    event=keys(0,111,2,3);handle_web_v2(&event,fake_time);
    assert(s_v2_result_code[0]==FM_RESULT_BUSY && stream_length==3);
    observed.reset_count++;owner_templates_update();
    assert(s_control_epoch!=context && s_panic_revision==panic+1 && global_flushes==1);
    assert(!s_web[0].armed && !s_web[0].keys && !observed.stop_requested);
}

static void unknown_previous_fm_owner(void)
{
    isolation_setup(FM_ENGINE_DDS);
    memset(&fake_fm_state,0,sizeof(fake_fm_state));
    fake_fm_state.status.applied_backend=1;
    assert(!owner_raw_ready() && "unconfirmed FM owner must block raw MIDI");
    assert(!dds_selected() && "unconfirmed FM owner must block DDS preset adoption");
    engine_fm_status_t fm;
    assert(fm_active(&fm) && "applied FM routing survives a missing status read");
    const uint8_t on[]={0x90,60,100};
    web_v2_event_t event=raw_event(0,111,1,on,sizeof(on));handle_web_raw(&event,fake_time);
    assert(s_web[0].sequence==1 && !s_web[0].armed && !stream_length);
}

static void begin_remote_selection(unsigned target)
{
    isolation_setup(FM_ENGINE_DDS);
    fake_local_state=(engine_local_state_t){.supported=true,.valid=true,.local_active=true,
        .engine_mask=15,.requested=FM_ENGINE_DDS,.applied=FM_ENGINE_DDS};
    web_v2_event_t event={.client=0,.token=111,.sequence=1,.context_epoch=s_control_epoch,
        .op=FM_HTTP_OPCODE_ENGINE_SELECT,.a=target,.expires=fake_time+WEB_LEASE_US};
    handle_web_v2(&event,fake_time);
    assert(s_remote_mode.active && !s_remote_mode.applying);
    fake_local_state.local_active=fake_local_state.blocked=fake_local_state.remote_pending=false;
    fake_local_state.remote_ready=true;
    fake_fm_state.status.ready=false;
}

static void dormant_unready_fm_does_not_block_remote_selection(void)
{
    for(unsigned target=FM_ENGINE_DDS;target<=FM_ENGINE_KS;target++) {
        begin_remote_selection(target);
        assert(owner_remote_poll(fake_time) && s_remote_mode.applying &&
            "unowned FM readiness must not stall a DDS/KS handoff");
        assert(s_requested_engine==target && !s_web[0].parameter_sequence);
        isolation_ks.requested_mode=isolation_ks.applied_mode=target==FM_ENGINE_KS;
        isolation_ks.switching=false;
        assert(owner_remote_poll(fake_time) && !s_remote_mode.active);
        assert(s_web[0].parameter_sequence==1 && s_v2_result_code[0]==FM_RESULT_OK);
    }
    for(unsigned cause=0;cause<4;cause++) {
        begin_remote_selection(FM_ENGINE_DDS);
        if(cause==0)fake_fm_state.status.applied_backend=1;
        if(cause==1)fake_fm_state.status.recovering=true;
        if(cause==2)fake_fm_state.status.needs_select=true;
        if(cause==3)fake_fm_state.status.switching=true;
        uint32_t context=s_control_epoch;
        assert(!owner_remote_poll(fake_time) && s_remote_mode.active && !s_remote_mode.applying);
        assert(s_control_epoch==context && !global_flushes && !s_panic_revision);
    }
}

/* A removed backend must not gate ordinary patch edits on any surviving
 * route. These use the actual web owner and its immutable-template publish. */
static void three_engine_parameters_preserve_held_context(void)
{
    for(unsigned active=FM_ENGINE_DDS;active<=FM_ENGINE_FM;active++) {
        isolation_setup(active);owner_templates_update();
        web_v2_event_t event=keys(0,111,1,1);handle_web_v2(&event,fake_time);
        assert(s_web[0].keys==1 && stream_length==3);
        uint32_t context=s_control_epoch,panic=s_panic_revision;
        event=keys(0,111,2,0);event.op=FM_HTTP_OPCODE_CC;event.a=7;event.b=99;
        handle_web_v2(&event,fake_time);
        assert(g_patch.volume==198 && s_web[0].parameter_sequence==2 && !s_v2_result_code[0]);
        fake_time+=20000;owner_templates_update();
        event.sequence=3;event.a=10;event.b=87;
        handle_web_v2(&event,fake_time);
        assert(g_patch.pan==23 && s_web[0].parameter_sequence==3 && !s_v2_result_code[0]);
        event.sequence=4;
        if(active==FM_ENGINE_KS) {
            event.op=FM_HTTP_OPCODE_KS_DECAY;event.a=6500;event.b=0;
            handle_web_v2(&event,fake_time);assert(ks_voice_decay_ms()==6500);
        } else if(active==FM_ENGINE_FM) {
            event.op=FM_HTTP_OPCODE_FM_PARAMETER;event.a=18;event.b=0xffff0000u;event.c=12;
            handle_web_v2(&event,fake_time);
        } else {
            event.a=71;event.b=64;handle_web_v2(&event,fake_time);
            assert(s_reso_off+g_patch.filter.resonance==2838);
        }
        assert(s_web[0].parameter_sequence==4 && s_v2_result_code[0]==FM_RESULT_OK);
        fake_time+=20000;owner_templates_update();publish_web_state();
        assert(s_control_epoch==context && s_panic_revision==panic && !global_flushes);
        assert(s_web[0].keys==1 && s_web[0].armed && stream_length==3);
        assert(owner_raw_ready() && !performance_false && publishes>=2);
        for(unsigned source=0;source<4;source++)assert(!source_flushes[source]);
    }
}

static void requested_route_mismatch_still_blocks_parameters_and_raw(void)
{
    isolation_setup(FM_ENGINE_DDS);owner_templates_update();
    web_v2_event_t event=keys(0,111,1,1);handle_web_v2(&event,fake_time);
    uint32_t context=s_control_epoch;unsigned volume=g_patch.volume;
    s_requested_engine=FM_ENGINE_KS; // Hardware is still DDS, without a settled MODE.
    assert(owner_route_blocked() && !owner_raw_ready());
    event=keys(0,111,2,0);event.op=FM_HTTP_OPCODE_CC;event.a=7;event.b=99;
    handle_web_v2(&event,fake_time);
    assert(s_v2_result_code[0]==FM_RESULT_BUSY && g_patch.volume==volume);
    owner_templates_update();assert(performance_false && publishes==1);
    assert(s_web[0].keys==1 && s_control_epoch==context && !global_flushes);
}

/* Closing permission for an unsettled route must not become a durable
 * mute once the same, already compiled template is safe again. */
static void cached_template_permission_recovers_after_route_restore(void)
{
    isolation_setup(FM_ENGINE_DDS);owner_templates_update();
    assert(publishes==1 && performance_true && s_template_valid);
    uint32_t context=s_control_epoch,panic=s_panic_revision;
    unsigned compiled=publishes;
    s_requested_engine=FM_ENGINE_KS;
    owner_templates_update();
    assert(owner_route_blocked() && performance_false && publishes==compiled);
    unsigned authorized=performance_true;
    isolation_ks.requested_mode=isolation_ks.applied_mode=true;
    assert(!owner_route_blocked());
    for(unsigned i=0;i<100;i++) {fake_time+=1000;owner_templates_update();}
    assert(performance_true>authorized &&
        "a restored route must reauthorize its unchanged compiled template");
    assert(s_template_valid && publishes==compiled);
    assert(s_control_epoch==context && s_panic_revision==panic && !global_flushes);
    assert(!memcmp(&g_patch,&s_template_snapshot.patch,sizeof(g_patch)));
    assert(ks_voice_decay_ms()==s_template_decay);
}

/* s_template_valid also caches rejected compiler inputs to avoid a retry on
 * every tick; a restored route cannot turn that negative cache into consent. */
static void rejected_template_stays_closed_after_route_restore(void)
{
    isolation_setup(FM_ENGINE_DDS);g_patch.voice_struct=(voice_struct_t)99;
    uint32_t context=s_control_epoch,panic=s_panic_revision;
    owner_templates_update();
    assert(s_template_valid && performance_false && !performance_true && !publishes);
    s_requested_engine=FM_ENGINE_KS;owner_templates_update();
    assert(owner_route_blocked());
    isolation_ks.requested_mode=isolation_ks.applied_mode=true;
    assert(!owner_route_blocked());
    for(unsigned i=0;i<100;i++) {fake_time+=1000;owner_templates_update();}
    assert(!performance_true && !publishes &&
        "a rejected cached template must never reopen performance permission");
    assert(s_template_valid && s_control_epoch==context && s_panic_revision==panic && !global_flushes);
}

int main(void)
{
    unknown_previous_fm_owner();
    offline_engines_preserve_live_input();offline_select_preserves_held_keys();
    current_or_unconfirmed_fault_blocks_input();shared_reset_keeps_global_recovery();
    isolation_setup(FM_ENGINE_DDS);stop_receipts_and_input_snapshot();
    dormant_unready_fm_does_not_block_remote_selection();
    three_engine_parameters_preserve_held_context();
    requested_route_mismatch_still_blocks_parameters_and_raw();
    rejected_template_stays_closed_after_route_restore();
    cached_template_permission_recovers_after_route_restore();
    puts("owner engine isolation: inactive FM/KS, live DDS/KS/FM parameters/RAW/keys, stable held context, route mismatch, old-owner/shared fences and STOP PASS");
    return 0;
}
