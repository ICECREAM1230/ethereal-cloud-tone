#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine_link.h"
#include "fdc_control.h"
#include "ae350_control.h"
#include "spi_regs.h"
#include "esp_timer.h"
extern int64_t fake_time;
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static engine_cpu_mode_state_t s_cpu_mode;
static uint32_t status;
static bool io=true,stop_during_send;
static bool raw_supported,raw_torn,raw_data_error;
static uint32_t raw_peer[4];
static unsigned reads,writes;
static uint16_t address[32];static uint32_t data[32][6];static size_t counts[32];
static fpga_template_write_result_t send_result=FPGA_TEMPLATE_SENT;
bool fpga_word_read_burst_observe(uint16_t a,uint32_t *out,size_t n)
{
    if(a==FDC_STATUS_REG){assert(n==1);reads++;*out=status;return io;}
    if(a==FDC_RAW_MAGIC_REG){assert(n==1);*out=raw_supported?FDC_RAW_MAGIC:0;return io;}
    assert(a==FDC_RAW_DATA_REG && (n==1 || n==4));
    if(raw_data_error)return false;
    if(n==4)memcpy(out,raw_peer,sizeof raw_peer);
    else *out=raw_peer[0]+(raw_torn?1u:0u);
    return io;
}
fpga_template_write_result_t fpga_template_write_burst(uint16_t a,const uint32_t *w,size_t n)
{
    assert(ae350_control_write_allowed(a,n));
    if(send_result==FPGA_TEMPLATE_DEFERRED)return send_result;
    assert(writes<32 && n<=6);address[writes]=a;counts[writes]=n;memcpy(data[writes],w,n*4);writes++;
    if(stop_during_send){stop_during_send=false;engine_link_fdc_stop();}
    return send_result;
}
void fpga_cpu_link_authorize(bool good){(void)good;}
#include "engine_fdc.inc.h"
static void tick(void){fake_time+=1000;fdc_service(fake_time+700,true);}
static fdc_request_t job(unsigned op,unsigned id)
{
    fdc_request_t r={.op=op,.event={.client=0,.token=10,.sequence=id,.context_epoch=1,.c=id,.expires=fake_time+1200000}};
    if(op==FDC_CALIBRATE){r.words[0]=1000;r.words[1]=2000;r.words[2]=3000;r.words[3]=2000;}
    return r;
}
static void reset(void)
{
    fake_time=100;raw_supported=raw_torn=raw_data_error=false;memset(raw_peer,0,sizeof raw_peer);io=true;status=FDC_STATUS_TAG|3u;reads=writes=0;stop_during_send=false;send_result=FPGA_TEMPLATE_SENT;
    s_cpu_mode=(engine_cpu_mode_state_t){.supported=true,.valid=true,.ready=true};fdc_init();tick();
}
static engine_fdc_state_t state(void){engine_fdc_state_t s;engine_link_fdc_state(&s);return s;}
int main(void)
{
    reset();fdc_request_t r=job(FDC_ENABLE,1);status=0;fake_time+=20000;tick();
    assert(!state().available && !engine_link_fdc_request(&r) && !writes);
    unsigned before=reads;tick();assert(reads==before); /* absent capability backs off */
    reset();assert(engine_link_fdc_request(&r));send_result=FPGA_TEMPLATE_DEFERRED;tick();assert(!writes && state().pending);
    send_result=FPGA_TEMPLATE_SENT;tick();assert(writes==1 && address[0]==0xb500 && data[0][0]==1);
    assert(state().result==FDC_SENT && state().completed_request_id==1 && !state().pending);
    assert(!(state().word&8u)); /* transport sent cannot invent driver_enabled */
    engine_link_fdc_cancel(1,true);tick();assert(writes==2 && !data[1][0]);
    reset();r=job(FDC_CALIBRATE,2);assert(engine_link_fdc_request(&r));tick();assert(writes==1 && counts[0]==6 && address[0]==0xb510);
    engine_link_fdc_stop();tick();assert(writes==2 && address[1]==0xb500 && !data[1][0]);
    for(unsigned i=0;i<writes;i++)assert(address[i]!=0xb51e);
    assert(state().result==FDC_CANCELLED);
    reset();r=job(FDC_CALIBRATE,3);assert(engine_link_fdc_request(&r));tick();tick();
    assert(writes==2 && address[1]==0xb51e && data[1][0]==1 && state().result==FDC_SENT);
    reset();r=job(FDC_ENABLE,4);assert(engine_link_fdc_request(&r));send_result=FPGA_TEMPLATE_UNCERTAIN;tick();
    assert(writes==1 && state().result==FDC_UNKNOWN && !state().pending);
    send_result=FPGA_TEMPLATE_SENT;tick();assert(writes==2 && !data[1][0]);tick();assert(writes==2); /* no enable replay */
    reset();r=job(FDC_ENABLE,5);assert(engine_link_fdc_request(&r));s_cpu_mode.applied=s_cpu_mode.desired=2;status&=~2u;fake_time+=20000;tick();tick();
    assert(writes==1 && address[0]==0xb500 && !data[0][0] && state().result==FDC_CANCELLED);
    r=job(FDC_DISABLE,6);assert(engine_link_fdc_request(&r));tick();assert(state().completed_request_id==6);
    reset();s_cpu_mode.supported=s_cpu_mode.valid=false;status|=0x20;fake_time+=20000;tick();
    r=job(FDC_RESTART,9);assert(engine_link_fdc_request(&r));tick();assert(writes==1 && address[0]==0xb501);
    reset();s_cpu_mode.supported=s_cpu_mode.valid=false;r=job(FDC_ENABLE,10);assert(engine_link_fdc_request(&r));tick();assert(writes==1 && data[0][0]==1);
    reset();r=job(FDC_ENABLE,7);assert(engine_link_fdc_request(&r));stop_during_send=true;tick();
    assert(state().result==FDC_CANCELLED && state().disable_pending);tick();assert(writes==2 && !data[1][0]);
    reset();engine_link_fdc_stop();stop_during_send=true;tick();assert(state().disable_pending);tick();assert(!state().disable_pending && writes==2);
    reset();fake_time+=FDC_FRESH_US+1;tick();assert(!state().fresh);r=job(FDC_ENABLE,8);assert(!engine_link_fdc_request(&r));
    status+=0x100;fake_time+=20000;tick();assert(state().fresh);r=job(FDC_ENABLE,8);assert(engine_link_fdc_request(&r));
    io=false;fake_time+=20000;tick();assert(!state().valid);tick();assert(!state().pending && state().result==FDC_CANCELLED);
    fdc_unavailable();assert(!state().available);
    puts("PASS actual FDC SPI-owner: capability/liveness, bounded frames, defer, immutable6+commit, mode fence, priority STOP/races, uncertain no replay");
    return 0;
}
