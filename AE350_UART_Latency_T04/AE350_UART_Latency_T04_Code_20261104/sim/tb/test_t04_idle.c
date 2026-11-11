#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "control_service.h"
static cs_service_t s;
static uint32_t hardware,template_meta;
static unsigned reads,writes;
static int get(void *p,uint32_t off,uint32_t *v){
    (void)p;reads++;
    assert(off==0x0c || off==PC_TEMPLATE_META);
    *v=off==0x0c?hardware:template_meta;return 1;
}
static int put(void *p,uint32_t off,uint32_t v){(void)p;(void)off;(void)v;writes++;return 1;}
static void ready(void){
    memset(&s,0,sizeof(s));s.stats.state=CS_RUNNING;s.stats.ready=1;
    cr_init(&s.runtime,7);
    s.compiler.boot_done=1;s.io.read32=get;s.io.write32=put;
    hardware=template_meta=reads=writes=0;
}
int main(void){
    unsigned checks=0;
#define CHECK(x) do{assert(x);checks++;}while(0)
    CHECK(!cs_is_idle(0));ready();CHECK(cs_is_idle(&s));CHECK(reads==2 && writes==0);
    const unsigned bits[]={1,4,32,128,256,1024};
    for(unsigned i=0;i<sizeof(bits)/sizeof(bits[0]);i++){
        ready();hardware=bits[i];CHECK(!cs_is_idle(&s));CHECK(reads==1 && writes==0);
    }
    ready();template_meta=0x80008000u;CHECK(!cs_is_idle(&s));CHECK(reads==2 && writes==0);
#define BUSY(field) do{ready();s.field=1;CHECK(!cs_is_idle(&s));CHECK(reads==0 && writes==0);}while(0)
    BUSY(fatal);BUSY(mode_phase);BUSY(rx_held);BUSY(flush_started);BUSY(need_recovery);
    BUSY(runtime.flush_mask);BUSY(runtime.in_flight_valid);BUSY(runtime.event_pending);
    BUSY(tx_committed);BUSY(compiler.pending);BUSY(compiler.refresh_phase);BUSY(compiler.count);
    BUSY(runtime.parser.sources[0].event_count);BUSY(runtime.parser.sources[0].sysex_queued);
    for(unsigned e=0;e<3;e++){ready();s.runtime.manager[e].queue_count=1;CHECK(!cs_is_idle(&s));CHECK(reads==0);}
    ready();s.stats.state=CS_SWITCHING;CHECK(!cs_is_idle(&s));
    ready();s.stats.ready=0;CHECK(!cs_is_idle(&s));
    ready();s.compiler.boot_done=0;CHECK(!cs_is_idle(&s));
    ready();CHECK(cs_diagnostics_idle(&s));CHECK(reads==2);
    ready();s.stats.state=CS_FAILED;s.fatal=1;s.stats.fault=CS_FAULT_BACKEND;s.tx_committed=1;
    CHECK(!cs_is_idle(&s));CHECK(cs_diagnostics_idle(&s));CHECK(reads==0 && writes==0);
    CHECK(s.stats.fault==CS_FAULT_BACKEND && s.tx_committed==1 && s.fatal==1);
    ready();s.stats.state=CS_FAILED;CHECK(!cs_diagnostics_idle(&s));
    printf("PASS T04 actual service idle predicate: %u checks, no diagnostic writes\n",checks);
    return 0;
}
