#include <assert.h>
#include <stdio.h>
#include "control_source.c"
int main(void)
{
    control_source_init();s_next_epoch=UINT32_MAX;
    control_source_ref_t last,next;
    assert(control_source_claim(CONTROL_SOURCE_WIFI,1,&last));
    assert(last.epoch==UINT32_MAX && control_source_valid(last));
    assert(control_source_claim(CONTROL_SOURCE_WIFI,2,&next));
    assert(control_source_same(last,next));
    assert(!control_source_claim(CONTROL_SOURCE_BLE_CTRL,3,&next));
    assert(control_source_valid(last));
    control_source_revoke(last);
    assert(!control_source_claim(CONTROL_SOURCE_WIFI,4,&next));
    assert(!control_source_valid(last));
    control_source_init();s_next_job=UINT32_MAX;
    assert(control_source_claim(CONTROL_SOURCE_WIFI,5,&last));
    uint32_t ticket;
    assert(control_source_reserve_job(last,5,100,&ticket) && ticket==UINT32_MAX);
    control_source_complete(last,0,ticket,CONTROL_SOURCE_OUTPUT,true);
    assert(!control_source_reserve_job(last,6,100,&ticket));
    puts("Control source epoch exhaustion: no recycling or stale-reference resurrection PASS");
    return 0;
}
