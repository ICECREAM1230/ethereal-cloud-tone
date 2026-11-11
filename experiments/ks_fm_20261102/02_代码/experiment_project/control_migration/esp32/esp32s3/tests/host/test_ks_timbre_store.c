#include "ks_timbre_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    ks_timbre_store_init();
    ks_timbre_request_t r={.op=KST_SAVE,.slot=7,.has_slot=true,.event={.c=1},.setting={.decay_ms=2000,.name="Harp slot"}};
    assert(ks_timbre_factory(1,&r.setting.preset));bool ok;
    assert(ks_timbre_store_request(&r));assert(!ks_timbre_store_result(1,&ok));
    ks_timbre_store_host_step();assert(ks_timbre_store_result(1,&ok)&&ok);
    ks_timbre_setting_t first,read;assert(ks_timbre_slot_read(7,&first));
    r.event.c=2;r.setting.decay_ms=4000;ks_timbre_store_host_fail(true);
    assert(ks_timbre_store_request(&r));ks_timbre_store_host_step();assert(ks_timbre_store_result(2,&ok)&&!ok);
    assert(ks_timbre_slot_read(7,&read)&&!memcmp(&first,&read,sizeof read));
    ks_timbre_store_host_fail(false);r.event.c=3;assert(ks_timbre_store_request(&r));ks_timbre_store_host_step();assert(ks_timbre_store_result(3,&ok)&&ok);
    ks_timbre_request_t boot={.op=KST_STARTUP,.enabled=true,.has_slot=true,.slot=7,.event={.c=4}};
    assert(ks_timbre_store_request(&boot));ks_timbre_store_host_step();assert(ks_timbre_store_result(4,&ok)&&ok);
    assert(ks_timbre_startup_setting(&read)&&read.decay_ms==4000&&read.preset.instrument_id==1);
    boot.event.c=5;boot.enabled=false;assert(ks_timbre_store_request(&boot));ks_timbre_store_host_step();assert(!ks_timbre_startup_setting(&read));
    r.event.c=6;r.slot=8;assert(ks_timbre_store_request(&r));ks_timbre_store_host_step();assert(ks_timbre_store_result(6,&ok)&&!ok);
    puts("KS actual persistence worker: asynchronous results, 8-slot bounds, failed save retains RAM state, startup slot and disabled fallback PASS (flash IO remains on-board)");
}
