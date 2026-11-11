#include "ks_timbre_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern int64_t fake_time;
static control_source_ref_t claim_source(void)
{control_source_ref_t source;assert(control_source_claim(CONTROL_SOURCE_WIFI,fake_time,&source));return source;}
int main(void)
{
    control_source_init();ks_timbre_store_init();fake_time=1000;bool ok;
    ks_timbre_request_t job={.op=KST_SAVE,.slot=7,.has_slot=true,.event={.c=1},
        .setting={.decay_ms=2000,.name="Original"}};
    assert(ks_timbre_factory(1,&job.setting.preset));
    assert(ks_timbre_store_request(&job));ks_timbre_store_host_step();
    assert(ks_timbre_store_result(1,&ok)&&ok);
    ks_timbre_setting_t original,read;assert(ks_timbre_slot_read(7,&original));
    const unsigned ops[]={KST_SAVE,KST_IMPORT,KST_STARTUP};
    for(unsigned i=0;i<3;i++) {
        job.op=ops[i];job.event.c++;job.event.expires=fake_time+CONTROL_SOURCE_LEASE_US;
        job.source=claim_source();job.setting.decay_ms=4000;job.enabled=true;
        assert(ks_timbre_store_request(&job));control_source_revoke(job.source);
        ks_timbre_store_host_step();assert(ks_timbre_store_result(job.event.c,&ok)&&!ok);
        assert(ks_timbre_slot_read(7,&read)&&!memcmp(&original,&read,sizeof original));
        assert(!ks_timbre_startup_setting(&read));
    }
    job.op=KST_IMPORT;job.event.c++;job.event.expires=fake_time+CONTROL_SOURCE_LEASE_US;job.source=claim_source();
    assert(ks_timbre_store_request(&job));fake_time=job.event.expires;
    ks_timbre_store_host_step();assert(ks_timbre_store_result(job.event.c,&ok)&&!ok);
    assert(ks_timbre_slot_read(7,&read)&&!memcmp(&original,&read,sizeof original));
    puts("PASS source KS store: queued SAVE/IMPORT/STARTUP revoked or expired before worker cannot persist or report success");
    return 0;
}
