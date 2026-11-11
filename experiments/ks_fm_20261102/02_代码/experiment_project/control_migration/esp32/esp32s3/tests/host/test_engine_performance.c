#define main legacy_engine_main
#include "test_engine_order.c"
#undef main
#include "raw_midi.h"
#include "midi_template_link.h"

static raw_midi_state_t raw_state;
static midi_template_link_state_t template_state;
static unsigned enable_calls,disable_calls,template_inits,upload_opportunities;
static int64_t expected_deadline;
static bool inject_reset, hold_disable;

void raw_midi_request_enable(bool enabled)
{
    if(enabled)enable_calls++;else disable_calls++;
    if(!enabled && hold_disable) {
        raw_state.initialized=false;raw_state.inflight=true;raw_state.source[0].flushing=true;
    } else {
        raw_state.enabled=enabled;
        if(!enabled)raw_state.inflight=false;
    }
}
void raw_midi_pause(bool paused) { (void)paused; }
void raw_midi_state(raw_midi_state_t *state) { *state=raw_state; }
void raw_midi_service(int64_t deadline)
{
    assert(deadline<=expected_deadline && deadline<=fake_time+200);
    fake_time+=20;
    if(inject_reset) {
        inject_reset=false;raw_state.reset_count++;
        raw_state.fault=0; // fault already cleared by ENABLE0 acknowledgement
    }
}
void midi_template_link_init(void)
{
    template_inits++;template_state=(midi_template_link_state_t){0};
}
void midi_template_link_invalidate(void)
{
    template_state.ready=template_state.busy=false;
    template_state.fault=MIDI_TEMPLATE_LINK_RESET;
}
void midi_template_link_state(midi_template_link_state_t *state) { *state=template_state; }
void midi_template_link_service(int64_t deadline,bool idle,bool available)
{
    assert(deadline<=expected_deadline);
    if(!available && template_state.available) {
        template_state.ready=template_state.busy=false;
        template_state.fault=MIDI_TEMPLATE_LINK_UNAVAILABLE;
    }
    template_state.available=available;
    if(idle && available)upload_opportunities++;
}
static void tick_control(void) { expected_deadline=fake_time+700;engine_service(); }

int main(void)
{
    // MID1 success followed by capability/status IO failure leaves every
    // readiness bit false. reset_count must still fence legacy static writes.
    raw_state.reset_count=1;raw_state.fault=RAW_MIDI_IO;
    engine_link_init();assert(template_inits==1);
    engine_cmd_t interrupted={.elem=1,.word=1,.value=77};assert(engine_link_send(&interrupted));
    s_fm_poll_at=0;
    for(unsigned i=0;i<3;i++)tick_control();
    assert(s_raw_seen && s_image[1][1]==0 && !s_fm_poll_at);
    raw_state.available=raw_state.initialized=true;
    raw_state.fault=0;
    raw_state.enabled=false;
    template_state.available=template_state.ready=true;
    for(unsigned i=0;i<10;i++)tick_control();
    assert(!enable_calls && engine_link_control_idle());
    // Even after the owner grants permission, the first template must ACK.
    template_state.ready=false;template_state.busy=true;
    engine_link_performance_ready(true);
    for(unsigned i=0;i<10;i++)tick_control();
    assert(!enable_calls);
    template_state.ready=true;template_state.busy=false;tick_control();
    assert(enable_calls==1 && raw_state.enabled);
    // A later template upload/dual-ACK wait refreshes HELD without ENABLE=0.
    template_state.ready=false;template_state.busy=true;
    raw_state.inflight=true; // continuous MIDI must not starve template upload
    unsigned uploads=upload_opportunities;
    for(unsigned i=0;i<10;i++)tick_control();
    assert(!disable_calls && enable_calls==1 && upload_opportunities>uploads);
    template_state.ready=true;template_state.busy=false;tick_control();
    assert(!disable_calls && enable_calls==1);
    engine_cmd_t edit={.elem=1,.word=1,.value=11};assert(engine_link_send(&edit));
    assert(!engine_link_control_idle());
    for(unsigned i=0;i<20;i++)tick_control();
    assert(engine_link_control_idle() && !disable_calls && enable_calls==1);
    // Read-only FM health/query traffic and local musical BUSY do not switch
    // the engine, and must not repeatedly revoke its own raw MIDI ingress.
    s_fm_query_pending=true;s_fm.status.flags|=FM_FLAG_BUSY;
    tick_control();assert(!disable_calls);s_fm_query_pending=false;
    template_state.ready=false;template_state.busy=false;template_state.fault=MIDI_TEMPLATE_LINK_IO;
    for(unsigned i=0;i<10;i++)tick_control();assert(disable_calls==1);
    template_state.fault=0;template_state.ready=true;tick_control();assert(enable_calls==2);
    engine_link_performance_ready(false);assert(disable_calls==2);
    for(unsigned i=0;i<10;i++)tick_control();assert(enable_calls==2 && disable_calls==2);
    engine_link_performance_ready(true);s_ks.switching=true;tick_control();assert(enable_calls==2);
    s_ks.switching=false;tick_control();assert(enable_calls==3);
    // Source-local rejection/timeout must not silence unrelated sources.
    raw_state.fault=RAW_MIDI_TIMEOUT;tick_control();assert(disable_calls==2);
    raw_state.fault=RAW_MIDI_REJECTED;tick_control();assert(disable_calls==2);
    raw_state.fault=0;
    // Simulate RESET and its ENABLE0 ACK clearing transient fault inside the
    // same raw slice: the persistent reset counter still invalidates images.
    inject_reset=true;tick_control();
    assert(disable_calls==3 && !template_state.ready);
    for(unsigned i=0;i<10;i++)tick_control();assert(enable_calls==3);
    // Only a new owner publish/dual ACK may re-arm the template after reset.
    template_state.fault=0;template_state.ready=true;tick_control();assert(enable_calls==4);
    raw_state.available=false;
    for(unsigned i=0;i<10;i++)tick_control();assert(disable_calls==4);
    raw_state.available=raw_state.initialized=true;raw_state.inflight=false;
    template_state.ready=true;template_state.fault=0;
    tick_control();assert(enable_calls==5);
    // ENABLE0 may remain busy behind the native USB barrier. Neither static
    // control nor mode service may run until its actual ACK and global flush.
    hold_disable=true;engine_link_performance_ready(false);assert(raw_state.enabled);
    edit.value=22;assert(engine_link_send(&edit));
    uploads=upload_opportunities;s_fm_poll_at=0;
    for(unsigned i=0;i<10;i++)tick_control();
    assert(s_image[1][1]==11 && !s_fm_poll_at && upload_opportunities==uploads);
    // The ACK-to-FLUSH_ALL gap can have no in-flight command: source flushing
    // still proves that the global disable boundary has not completed.
    raw_state.enabled=false;raw_state.initialized=true;raw_state.inflight=false;
    tick_control();assert(s_image[1][1]==11 && !s_fm_poll_at);
    raw_state.source[0].flushing=false;hold_disable=false;
    for(unsigned i=0;i<20;i++)tick_control();
    assert(s_image[1][1]==22 && engine_link_control_idle());
    engine_link_performance_ready(true);tick_control();assert(enable_calls==6);
    // A normal BYTE or source-specific flush while enabled is independent of
    // mode/static control and must not impose the global-disable barrier.
    raw_state.inflight=true;raw_state.source[1].flushing=true;
    edit.value=33;assert(engine_link_send(&edit));
    for(unsigned i=0;i<20;i++)tick_control();
    assert(s_image[1][1]==33 && disable_calls==5);
    // An ID/read failure cannot make a previously identified MID1 device
    // fall back to the legacy control path while ENABLE0 is unconfirmed.
    hold_disable=true;raw_state.available=false;raw_state.initialized=false;
    raw_state.reset_count++;raw_state.fault=RAW_MIDI_IO;
    edit.value=44;assert(engine_link_send(&edit));s_fm_poll_at=0;
    for(unsigned i=0;i<10;i++)tick_control();
    assert(s_image[1][1]==33 && !s_fm_poll_at && raw_state.enabled);
    raw_state.available=true;tick_control();assert(s_image[1][1]==33);
    raw_state.enabled=false;raw_state.initialized=true;raw_state.inflight=false;
    raw_state.fault=0;tick_control();assert(s_image[1][1]==33);
    for(unsigned source=0;source<4;source++)raw_state.source[source].flushing=false;
    hold_disable=false;
    for(unsigned i=0;i<20;i++)tick_control();
    assert(s_image[1][1]==44 && engine_link_control_idle());
    puts("engine performance: default/initial owner fence, held parameter/template refresh, edge disable, mode/fault/reset fences, shared 700us deadline PASS");
}
