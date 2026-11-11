#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine_link.h"
#include "raw_midi.h"
#include "midi_template_link.h"
#include "midi_feedback.h"
#include "ble_midi.h"
#include "web_raw_queue.h"
extern int64_t fake_time;
static raw_midi_state_t observed;
static uint8_t stream[4096];
static size_t stream_length;
static unsigned source_flushes[4],global_flushes,renews,publishes;
static midi_template_image_t published_image;
static bool fail_submit;
static unsigned performance_true,performance_false;
static midi_template_link_state_t template_state;
static unsigned template_reads,invalidate_cap_after;

bool engine_link_send(const engine_cmd_t *c){(void)c;return true;}
bool engine_link_bus_write(uint16_t b,uint32_t v){(void)b;(void)v;return true;}
bool engine_link_prod_write(uint8_t e,uint8_t w,uint32_t v){(void)e;(void)w;(void)v;return true;}
bool engine_link_note_on(const engine_note_t *n){(void)n;assert(!"legacy DDS NoteOn in production owner");return false;}
uint32_t engine_link_drops(void){return 0;}
uint32_t engine_link_prod_committed(uint8_t e,uint8_t w){(void)e;(void)w;return 0;}
uint32_t engine_link_release_coefficient(uint8_t e,uint32_t *r){(void)e;*r=0;return 0;}
bool engine_link_bus_committed(uint16_t b,uint32_t v){(void)b;(void)v;return true;}
void engine_link_performance_ready(bool ready){if(ready)performance_true++;else performance_false++;}
void raw_midi_state(raw_midi_state_t *state){*state=observed;}
bool raw_midi_claim(uint8_t source,uint64_t session)
{
    assert(source==MIDI_RAW_SOURCE_WEB);
    if(!observed.ready || (observed.source[source].session && observed.source[source].session!=session))return false;
    observed.source[source].session=session;return true;
}
bool raw_midi_renew(uint8_t source,uint64_t session,int64_t expires)
{
    assert(expires>fake_time);
    if(!observed.ready || observed.source[source].session!=session)return false;
    renews++;return true;
}
bool raw_midi_submit(uint8_t source,uint64_t session,const uint8_t *bytes,size_t length,int64_t expires)
{
    assert(source==MIDI_RAW_SOURCE_WEB && session==observed.source[source].session);
    assert(expires>fake_time);
    if(fail_submit)return false;
    assert(stream_length+length<=sizeof(stream));
    memcpy(stream+stream_length,bytes,length);stream_length+=length;return true;
}
void raw_midi_flush(uint8_t source)
{
    if(source==MIDI_RAW_SOURCE_ALL){global_flushes++;for(unsigned i=0;i<4;i++)observed.source[i].session=0;}
    else {assert(source<4);source_flushes[source]++;observed.source[source].session=0;}
}
uint32_t raw_midi_request_stop_all(void)
{
    if(!++observed.stop_requested)++observed.stop_requested;
    observed.stop_pending=true;observed.ready=false;
    return observed.stop_requested;
}
void midi_template_link_publish(const midi_template_image_t *image){assert(image);published_image=*image;publishes++;}
void midi_template_link_state(midi_template_link_state_t *state)
{*state=template_state;if(++template_reads==invalidate_cap_after)template_state.sub_patch_valid=false;}
void midi_feedback_init(void){}
bool midi_feedback_publish(const web_control_state_t *state,uint8_t except_source,uint64_t epoch)
{(void)state;(void)except_source;(void)epoch;return true;}
bool ble_midi_feedback_sync_pending(uint64_t *epoch){(void)epoch;return false;}
void ble_midi_feedback_sync_complete(uint64_t epoch){(void)epoch;}
#include "voice_alloc.c"

static web_v2_event_t keys(unsigned client,uint32_t token,uint32_t seq,uint32_t mask)
{
    return (web_v2_event_t){.client=(uint8_t)client,.token=token,.sequence=seq,
        .context_epoch=s_control_epoch,.op=FM_HTTP_OPCODE_KEYS_UPDATE,.a=mask,.b=100,
        .expires=fake_time+WEB_LEASE_US};
}
static void start(unsigned client,uint32_t token)
{
    web_event_t event={.client=(uint8_t)client,.token=token,.op=WEB_START};
    handle_web(&event,fake_time);
}
static void reset_owner(void)
{
    fake_time=1000;patch_default(&g_patch);
    memset(s_web,0,sizeof(s_web));memset(s_voices,0,sizeof(s_voices));
    memset(&observed,0,sizeof(observed));memset(&template_state,0,sizeof(template_state));
    template_state.ready=true;
    template_state.sub_patch_valid=true;
    template_reads=invalidate_cap_after=0;
    memset(source_flushes,0,sizeof(source_flushes));
    memset(s_v2_result_code,0,sizeof(s_v2_result_code));
    s_control_epoch=1;s_requested_engine=UINT32_MAX;s_dds_switch_id=0;
    s_template_valid=s_template_compiled=false;s_template_reset_count=0;s_template_last_publish=0;
    s_template_fault=s_template_fault_generation=0;
    stream_length=global_flushes=renews=publishes=0;
    performance_true=performance_false=0;fail_submit=false;
    web_raw_queue_reset();
    observed.available=observed.initialized=observed.enabled=observed.ready=true;
    observed.input_capabilities_known=observed.independent_usb=true;
    start(0,111);start(1,222);
}
static web_v2_event_t raw_event(unsigned client,uint32_t token,uint32_t sequence,
                              const uint8_t *bytes,size_t length)
{
    web_raw_job_t job={.client=(uint8_t)client,.expires=fake_time+WEB_LEASE_US,
        .request={.token=token,.sequence=sequence,.context_epoch=s_control_epoch,
            .length=length,.has_binding=true}};
    memcpy(job.request.bytes,bytes,length);
    uint32_t id;assert(web_raw_queue_push(&job,&id));
    return (web_v2_event_t){.client=(uint8_t)client,.token=token,.sequence=sequence,
        .context_epoch=s_control_epoch,.expires=job.expires,.a=id};
}
static void ordering_and_renewal(void)
{
    reset_owner();
    web_v2_event_t event=keys(0,111,1,1);
    handle_web_v2(&event,fake_time);
    const uint8_t on[]={0x90,48,100};
    assert(stream_length==3 && !memcmp(stream,on,3));
    assert(s_web[0].keys==1 && s_web[0].desired_keys==1);
    const uint8_t bend[]={0xe0,0,96};
    event=raw_event(0,111,2,bend,sizeof(bend));handle_web_raw(&event,fake_time);
    assert(s_web[0].sequence==2 && s_v2_result_sequence[0]==1 && s_v2_result_code[0]==FM_RESULT_OK);
    event=keys(0,111,3,2);handle_web_v2(&event,fake_time);
    const uint8_t expected[]={0x90,48,100,0xe0,0,96,0x80,48,0,0x90,49,100};
    assert(stream_length==sizeof(expected) && !memcmp(stream,expected,sizeof(expected)));
    fake_time+=1000;event=keys(0,111,4,2);handle_web_v2(&event,fake_time);
    assert(stream_length==sizeof(expected) && renews==1);
    assert(s_web[0].expires==event.expires && s_web[0].keys==2);
    event=keys(0,111,5,0);handle_web_v2(&event,fake_time);
    assert(stream_length==sizeof(expected)+3 && observed.source[1].session==111);
    assert(!source_flushes[1] && !s_web[0].keys);
    event=keys(0,111,6,0);handle_web_v2(&event,fake_time);
    assert(renews==2 && !source_flushes[1]);
    // Repeated/old descriptors cannot append another On.
    const uint8_t extra[]={0x90,60,100};
    event=raw_event(0,111,5,extra,sizeof(extra));handle_web_raw(&event,fake_time);
    assert(stream_length==sizeof(expected)+3);
}
static void isolated_sessions(void)
{
    reset_owner();observed.source[2].session=999;
    web_v2_event_t event=keys(0,111,1,1);handle_web_v2(&event,fake_time);
    start(1,333);assert(!source_flushes[1] && s_web[0].keys==1);
    event=keys(1,333,1,0);handle_web_v2(&event,fake_time);
    assert(!global_flushes && !source_flushes[1] && s_web[0].keys==1);
    assert(observed.source[1].session==111 && observed.source[2].session==999);
    start(1,444);event=keys(1,444,1,0);handle_web_v2(&event,fake_time);
    assert(observed.source[1].session==111 && !source_flushes[1]);
    s_web[1].expires=fake_time-1;assert(web_expire(fake_time));
    assert(s_web[0].keys==1 && !source_flushes[1]);
    start(1,555);event=keys(1,555,1,1);handle_web_v2(&event,fake_time);
    assert(s_v2_result_code[1]==FM_RESULT_BUSY && stream_length==3);
    assert(s_web[0].keys==1 && observed.source[1].session==111);
    uint32_t old_context=s_control_epoch;
    const uint8_t on[]={0x90,60,100};
    web_v2_event_t old_raw=raw_event(0,111,2,on,sizeof(on));
    event=(web_v2_event_t){.client=1,.token=555,.sequence=2,.context_epoch=s_control_epoch,
        .op=FM_HTTP_OPCODE_STOP};handle_web_v2(&event,fake_time);
    assert(!source_flushes[1] && global_flushes==1 && !observed.source[2].session);
    assert(!s_web[0].armed && !s_web[0].keys && s_control_epoch!=old_context);
    // Owner adoption is not the FPGA's acknowledgement of both input clears.
    assert(s_web[1].stop_sequence==0 && s_v2_result_code[1]==FM_RESULT_OK);
    assert(observed.stop_pending && observed.stop_requested==1);
    publish_web_state();
    assert(!s_web[1].stop_sequence);
    observed.stop_completed=observed.stop_requested;observed.stop_pending=false;observed.ready=true;
    publish_web_state();
    assert(s_web[1].stop_sequence==2);
    handle_web_raw(&old_raw,fake_time);assert(stream_length==3);
    event=keys(0,111,2,1);handle_web_v2(&event,fake_time);
    assert(stream_length==3 && !s_web[0].keys); // no old held-key replay
    start(1,666);event=keys(1,666,1,1);handle_web_v2(&event,fake_time);
    assert(observed.source[1].session==666 && stream_length==6);
}
static void stale_and_faults(void)
{
    reset_owner();
    const uint8_t on[]={0x90,60,100};
    web_v2_event_t event=raw_event(0,111,1,on,sizeof(on));event.context_epoch++;
    handle_web_raw(&event,fake_time);assert(!stream_length);
    // Event and descriptor match, but both refer to an old context.
    event=raw_event(0,111,1,on,sizeof(on));s_control_epoch++;
    handle_web_raw(&event,fake_time);
    assert(!stream_length && !s_web[0].armed && !s_v2_result_sequence[0] && !s_v2_result_code[0]);
    reset_owner();event=raw_event(0,111,1,on,sizeof(on));
    fake_time=event.expires;handle_web_raw(&event,fake_time);
    assert(!stream_length && !s_web[0].armed);
    reset_owner();event=raw_event(0,111,1,on,sizeof(on));
    web_raw_queue_reset();web_v2_event_t fresh=raw_event(0,111,2,on,sizeof(on));
    handle_web_raw(&event,fake_time);assert(!stream_length);
    handle_web_raw(&fresh,fake_time);assert(stream_length==3);
    // Overflow/failure cancels only this Web token, never retries old intent.
    fail_submit=true;event=keys(0,111,3,1);handle_web_v2(&event,fake_time);
    assert(!s_web[0].armed && !s_web[0].keys && source_flushes[1]==1);
    fail_submit=false;event=keys(0,111,4,1);handle_web_v2(&event,fake_time);
    assert(stream_length==3 && !s_web[0].keys);
    event=keys(0,111,5,0);handle_web_v2(&event,fake_time);
    event=keys(0,111,6,1);handle_web_v2(&event,fake_time);assert(stream_length==6);
    fake_time=s_web[0].expires;assert(web_expire(fake_time));
    assert(!s_web[0].armed && !s_web[0].keys && source_flushes[1]==2);
    // A long-idle empty session is free to begin a fresh raw lease.
    reset_owner();event=keys(0,111,1,0);handle_web_v2(&event,fake_time);
    fake_time=event.expires+1;
    event=raw_event(0,111,2,on,sizeof(on));handle_web_raw(&event,fake_time);
    assert(stream_length==3);
}
static void templates_and_panic(void)
{
    reset_owner();
    owner_templates_update();assert(publishes==1 && performance_true==1);
    for(unsigned i=0;i<10;i++)owner_templates_update();
    assert(publishes==1);
    g_patch.pan=12;owner_templates_update();assert(publishes==1);
    fake_time+=20000;owner_templates_update();assert(publishes==2);
    g_patch.pan=13;g_patch.pan=14;fake_time+=20000;owner_templates_update();
    assert(publishes==3 && s_template_snapshot.patch.pan==14);
    web_v2_event_t event=keys(0,111,1,1);handle_web_v2(&event,fake_time);
    unsigned permission_requests=performance_true;
    uint32_t old_context=s_control_epoch;all_sound_off();
    assert(global_flushes==1 && performance_false && s_control_epoch!=old_context);
    assert(!s_web[0].armed && !s_web[0].keys);
    owner_templates_update();assert(publishes==4 && performance_true==permission_requests+1);
    event=keys(0,111,2,1);handle_web_v2(&event,fake_time);
    assert(stream_length==3 && !s_web[0].keys);
    observed.reset_count++;
    owner_templates_update();assert(publishes==5 && global_flushes==2);
    assert(!observed.stop_requested); // Automatic recovery must not clear USB.
    owner_templates_update();assert(publishes==5);
    template_state.fault=MIDI_TEMPLATE_LINK_IO;template_state.generation=9;
    owner_templates_update();assert(publishes==6 && global_flushes==3);
    assert(!observed.stop_requested);
    owner_templates_update();assert(publishes==6);
}
static void stop_receipts_and_input_snapshot(void)
{
    reset_owner();observed.usb_status=1;
    web_v2_event_t stop={.client=0,.token=111,.sequence=3,
        .context_epoch=s_control_epoch,.op=FM_HTTP_OPCODE_STOP};
    handle_web_v2(&stop,fake_time);
    assert(s_web[0].sequence==3 && s_v2_result_sequence[0]==3);
    assert(!s_web[0].stop_sequence && observed.stop_pending);
    uint32_t first=observed.stop_requested;
    stop=(web_v2_event_t){.client=1,.token=222,.sequence=7,
        .context_epoch=s_control_epoch,.op=FM_HTTP_OPCODE_STOP};
    handle_web_v2(&stop,fake_time);
    uint32_t second=observed.stop_requested;assert(first!=second);
    observed.stop_completed=first;observed.stop_fault=RAW_MIDI_IO;
    publish_web_state();
    assert(s_web[0].stop_sequence==3 && !s_web[1].stop_sequence);
    web_control_state_t snapshot;web_control_read(&snapshot);
    assert(snapshot.ready && !snapshot.inputs.ready && snapshot.inputs.stop_pending);
    assert(snapshot.inputs.version==1 && snapshot.inputs.available && snapshot.inputs.initialized);
    assert(snapshot.inputs.capabilities_known && snapshot.inputs.independent_usb);
    assert(snapshot.inputs.usb_configured && !snapshot.inputs.usb_error);
    assert(snapshot.inputs.stop_requested==second && snapshot.inputs.stop_completed==first);
    assert(snapshot.inputs.stop_fault==RAW_MIDI_IO);
    web_v2_event_t on=keys(1,222,8,1);handle_web_v2(&on,fake_time);
    assert(!stream_length && s_v2_result_code[1]==FM_RESULT_BUSY);
    // A new session must never inherit an old token's delayed STOP receipt.
    start(1,333);
    observed.stop_completed=second;observed.stop_pending=false;observed.ready=true;
    publish_web_state();assert(!s_web[1].stop_sequence);
    // Lost link makes USB enumeration unknown even if a stale cache says 1.
    observed.available=false;observed.fault=RAW_MIDI_IO;
    publish_web_state();web_control_read(&snapshot);
    assert(snapshot.ready && !snapshot.inputs.available && !snapshot.inputs.usb_configured);
    assert(!snapshot.inputs.ready && snapshot.inputs.fault==RAW_MIDI_IO);
}
static void playback_readiness_is_not_http_or_usb_readiness(void)
{
    reset_owner();publish_web_state();
    web_control_state_t snapshot;web_control_read(&snapshot);
    assert(snapshot.ready && snapshot.inputs.ready && !snapshot.inputs.usb_configured);
    observed.usb_status=1;template_state.ready=false;
    publish_web_state();web_control_read(&snapshot);
    assert(snapshot.ready && snapshot.inputs.usb_configured && !snapshot.inputs.ready);
    template_state.ready=true;template_state.fault=MIDI_TEMPLATE_LINK_IO;
    publish_web_state();web_control_read(&snapshot);assert(!snapshot.inputs.ready);
    template_state.fault=0;observed.initialized=false;
    publish_web_state();web_control_read(&snapshot);assert(!snapshot.inputs.ready);
    observed.initialized=true;observed.input_capabilities_known=false;
    publish_web_state();web_control_read(&snapshot);assert(!snapshot.inputs.ready);
    observed.input_capabilities_known=true;observed.fault=RAW_MIDI_IO;
    publish_web_state();web_control_read(&snapshot);assert(!snapshot.inputs.ready);
}
static void confirmed_template_hot_update_preserves_held_input(void)
{
    reset_owner();web_v2_event_t event=keys(0,111,1,1);handle_web_v2(&event,fake_time);
    assert(s_web[0].keys==1 && stream_length==3);
    template_state.ready=false;template_state.busy=true;template_state.applied_generation=7;
    publish_web_state();web_control_state_t snapshot;web_control_read(&snapshot);
    assert(snapshot.inputs.ready && s_web[0].keys==1);
    event=keys(0,111,2,1);handle_web_v2(&event,fake_time);
    assert(stream_length==3 && renews==1 && !source_flushes[1]);
    template_state.fault=MIDI_TEMPLATE_LINK_IO;
    publish_web_state();web_control_read(&snapshot);assert(!snapshot.inputs.ready);
}
static void no_legacy_performance(void)
{
    reset_owner();
    midi_message_t message={.status=0x90,.len=2,.data={60,100}};
    handle_midi_source(&message,MIDI_SOURCE_PANEL);
    handle_midi_source(&message,MIDI_SOURCE_DIN);
    handle_midi_source(&message,MIDI_SOURCE_BLE);
    for(unsigned i=0;i<NUM_VOICES;i++)assert(s_voices[i].state==V_IDLE);
    assert(!stream_length && !global_flushes);
    message=(midi_message_t){.status=0xb0,.len=2,.data={7,63}};
    handle_midi_source(&message,MIDI_SOURCE_DEFAULT);assert(g_patch.volume==126);
    message.data[1]=22;handle_midi_source(&message,MIDI_SOURCE_DIN);
    assert(g_patch.volume==126);
}
static void full_chord_and_init(void)
{
    reset_owner();
    web_v2_event_t event=keys(0,111,1,WEB_KEYS_MASK);
    handle_web_v2(&event,fake_time);
    assert(stream_length==WEB_KEYS*3 && s_web[0].keys==WEB_KEYS_MASK);
    for(unsigned key=0;key<WEB_KEYS;key++) {
        assert(stream[key*3]==0x90 && stream[key*3+1]==WEB_FIRST_NOTE+key);
        assert(stream[key*3+2]==100);
    }
    start(0,777); // New slot token has no inherited expired lease.
    assert(s_web[0].armed && !s_web[0].expires && !s_web[0].keys);
    reset_owner();event_bus_init();voice_alloc_init();
    assert(publishes==1 && performance_true==1 && performance_false==1);
}
static void rejected_template_does_not_spin(void)
{
    reset_owner();g_patch.voice_struct=255;
    owner_templates_update();owner_templates_update();owner_templates_update();
    assert(!publishes && performance_false==1 && !performance_true);
    g_patch.voice_struct=VOICE_2_PLAIN;fake_time+=20000;owner_templates_update();
    assert(publishes==1 && performance_true==1);
}
static void preset_load_keeps_gate_closed(void)
{
    reset_owner();owner_templates_update();
    assert(publishes==1 && performance_true==1);
    all_sound_off();s_dds_switch_id=123;
    owner_templates_update();
    assert(publishes==1 && performance_true==1 && performance_false>=2);
    // The DDS worker's adopted patch is published only after closing ends.
    g_patch.pan=33;s_dds_switch_id=0;owner_templates_update();
    assert(publishes==2 && performance_true==2 && s_template_snapshot.patch.pan==33);
}
static void raw_failure_preserves_snapshot_receipt(void)
{
    reset_owner();
    web_v2_event_t event=keys(0,111,1,1);handle_web_v2(&event,fake_time);
    const uint8_t bend[]={0xe0,0,96};
    event=raw_event(0,111,2,bend,sizeof(bend));fail_submit=true;
    handle_web_raw(&event,fake_time);
    assert(s_web[0].sequence==2 && !s_web[0].armed && !s_web[0].keys);
    assert(s_v2_result_sequence[0]==1 && s_v2_result_code[0]==FM_RESULT_OK);
    assert(s_v2_result_context[0]==s_control_epoch);
}
int main(void)
{
    playback_readiness_is_not_http_or_usb_readiness();
    confirmed_template_hot_update_preserves_held_input();
    ordering_and_renewal();isolated_sessions();stale_and_faults();
    templates_and_panic();no_legacy_performance();full_chord_and_init();
    rejected_template_does_not_spin();
    preset_load_keeps_gate_closed();
    raw_failure_preserves_snapshot_receipt();
    stop_receipts_and_input_snapshot();
    puts("production raw Web owner: FIFO order, isolated binding, leases, failure fences, template coalescing and no S3 notes PASS");
    return 0;
}
