#define S3_FDC_HOST_TEST 1
#include "fdc_control.h"
static engine_fdc_state_t fake_fdc;
static unsigned fdc_requests,fdc_cancels,fdc_stops;
void engine_link_fdc_state(engine_fdc_state_t *out){*out=fake_fdc;}
bool engine_link_fdc_request(const fdc_request_t *r)
{fdc_requests++;fake_fdc.pending=true;fake_fdc.request_id=r->event.c;fake_fdc.completed_request_id=0;return true;}
void engine_link_fdc_cancel(uint32_t id,bool disable)
{(void)id;fdc_cancels++;fake_fdc.pending=false;if(disable)fake_fdc.disable_pending=true;}
void engine_link_fdc_stop(void){fdc_stops++;fake_fdc.disable_pending=true;}
#define main previous_owner_main
#include "../test_raw_web_owner.c"
#undef main
static void fixture(void)
{
    fdc_request_t waiting;fdc_control_cancel_reserved(&waiting);reset_owner();
    memset(&s_fdc_owner,0,sizeof s_fdc_owner);memset(&s_fdc_result,0,sizeof s_fdc_result);
    fake_fdc=(engine_fdc_state_t){.available=true,.valid=true,.fresh=true,.word=FDC_STATUS_TAG|3,.observed_us=fake_time};
    fdc_requests=fdc_cancels=fdc_stops=0;
}
static fdc_request_t reserve(unsigned op,unsigned seq)
{
    fdc_request_t r={.op=op,.event={.client=0,.token=111,.sequence=seq,.context_epoch=s_control_epoch,.expires=fake_time+WEB_LEASE_US}};
    assert(fdc_control_reserve(&r,fake_time));return r;
}
int main(void)
{
    fixture();fdc_request_t r=reserve(FDC_ENABLE,1);handle_web_fdc(&r.event,fake_time);
    assert(fdc_requests==1 && s_fdc_owner.active && !s_web[0].parameter_sequence);
    fake_fdc.pending=false;fake_fdc.completed_request_id=r.event.c;fake_fdc.result=FDC_SENT;
    owner_fdc_poll(fake_time);assert(!s_fdc_owner.active && s_fdc_result.code==FDC_SENT && !s_web[0].parameter_sequence);
    fixture();r=reserve(FDC_ENABLE,1);web_stop(0,2);handle_web_fdc(&r.event,fake_time);
    assert(!fdc_requests && fdc_stops==1 && s_fdc_result.code==FDC_CANCELLED && observed.stop_pending);
    assert(!s_web[0].stop_sequence); /* original MIDI STOP ACK is still pending */
    fixture();r=reserve(FDC_ENABLE,1);handle_web_fdc(&r.event,fake_time);s_control_epoch++;
    owner_fdc_poll(fake_time);assert(fdc_cancels==1 && s_fdc_result.code==FDC_CANCELLED && !s_web[0].parameter_sequence);
    fixture();r=reserve(FDC_ENABLE,1);handle_web_fdc(&r.event,fake_time);s_web[0].token++;
    owner_fdc_poll(fake_time);assert(fdc_cancels==1 && s_fdc_result.code==FDC_CANCELLED);
    fixture();r=reserve(FDC_ENABLE,1);handle_web_fdc(&r.event,fake_time);web_release(0);
    assert(!fdc_stops && !fdc_cancels); /* ordinary source-local release stays local */
    fdc_request_t off=reserve(FDC_DISABLE,2);handle_web_fdc(&off.event,fake_time);
    assert(fdc_cancels==1 && fdc_requests==2 && s_fdc_owner.request.op==FDC_DISABLE);
    puts("PASS actual FDC owner: immutable reservation, transport-only result, stale session/context retirement, predispatch global STOP and unchanged source/MIDI receipts");
}
