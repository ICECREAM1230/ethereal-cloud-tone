#include <assert.h>
#include <stdio.h>
#include "control_source.h"

static control_source_ref_t claim(control_source_kind_t kind, int64_t now)
{
    control_source_ref_t source;
    assert(control_source_claim(kind,now,&source));
    assert(source.kind==kind && source.epoch && control_source_valid(source));
    return source;
}
static bool reserve(control_source_ref_t source,uint32_t token,uint32_t sequence,
                    int64_t now,control_source_activity_t activity)
{return control_source_reserve(source,token,sequence,now,now+CONTROL_SOURCE_LEASE_US,activity);}

int main(void)
{
    control_source_init();
    control_source_ref_t wifi=claim(CONTROL_SOURCE_WIFI,1000),same=claim(CONTROL_SOURCE_WIFI,1001),ble;
    assert(control_source_same(wifi,same));
    assert(!control_source_valid((control_source_ref_t){0}));
    assert(!control_source_claim(CONTROL_SOURCE_NONE,1000,&ble));
    assert(!control_source_claim(CONTROL_SOURCE_WIFI,-1,&ble));
    assert(!control_source_claim(CONTROL_SOURCE_WIFI,1000,NULL));
    // Opening a session or sending empty idle heartbeats cannot lock a mode.
    assert(reserve(wifi,10,1,1001,CONTROL_SOURCE_IDLE));
    ble=claim(CONTROL_SOURCE_BLE_CTRL,1002);
    assert(!control_source_valid(wifi));
    assert(!reserve(wifi,10,2,1003,CONTROL_SOURCE_KEYS));
    control_source_revoke(wifi);assert(control_source_valid(ble));

    wifi=claim(CONTROL_SOURCE_WIFI,1100);
    assert(reserve(wifi,10,2,1100,CONTROL_SOURCE_KEYS));
    assert(!control_source_claim(CONTROL_SOURCE_BLE_CTRL,1101,&ble));
    control_source_complete(wifi,10,2,CONTROL_SOURCE_KEYS,true);
    assert(!control_source_claim(CONTROL_SOURCE_BLE_CTRL,1102,&ble));
    assert(reserve(wifi,10,3,1103,CONTROL_SOURCE_IDLE));
    assert(!control_source_claim(CONTROL_SOURCE_BLE_CTRL,1104,&ble));
    control_source_complete(wifi,10,3,CONTROL_SOURCE_IDLE,true);
    ble=claim(CONTROL_SOURCE_BLE_CTRL,1105);

    // A queued future On is still reserved when an earlier Off is adopted.
    assert(reserve(ble,20,1,1200,CONTROL_SOURCE_KEYS));
    assert(reserve(ble,20,2,1201,CONTROL_SOURCE_IDLE));
    assert(reserve(ble,20,3,1202,CONTROL_SOURCE_KEYS));
    control_source_complete(ble,20,2,CONTROL_SOURCE_IDLE,true);
    control_source_release(ble,20,2);
    assert(!control_source_claim(CONTROL_SOURCE_WIFI,1203,&wifi));
    control_source_complete(ble,20,3,CONTROL_SOURCE_KEYS,false);
    wifi=claim(CONTROL_SOURCE_WIFI,1204);

    // Four same-transport sessions retain independent performance holds.
    for(unsigned i=0;i<CONTROL_SOURCE_CLIENTS;i++)
        assert(reserve(wifi,30+i,1,1300,CONTROL_SOURCE_KEYS));
    assert(!reserve(wifi,99,1,1300,CONTROL_SOURCE_KEYS));
    control_source_release(wifi,30,1);
    assert(!control_source_claim(CONTROL_SOURCE_BLE_CTRL,1301,&ble));
    for(unsigned i=1;i<CONTROL_SOURCE_CLIENTS;i++)control_source_release(wifi,30+i,1);
    ble=claim(CONTROL_SOURCE_BLE_CTRL,1302);

    // Parameters block takeover until owner adoption; anonymous DDS does not
    // consume a session slot and completes against its immutable job ID.
    for(unsigned i=0;i<CONTROL_SOURCE_CLIENTS;i++)
        assert(reserve(ble,40+i,1,1400,CONTROL_SOURCE_OUTPUT));
    assert(reserve(ble,0,77,1400,CONTROL_SOURCE_OUTPUT));
    assert(!reserve(ble,0,78,1400,CONTROL_SOURCE_OUTPUT));
    for(unsigned i=0;i<CONTROL_SOURCE_CLIENTS;i++)
        control_source_complete(ble,40+i,1,CONTROL_SOURCE_OUTPUT,true);
    control_source_complete(ble,0,76,CONTROL_SOURCE_OUTPUT,true);
    assert(!control_source_claim(CONTROL_SOURCE_WIFI,1401,&wifi));
    control_source_complete(ble,0,77,CONTROL_SOURCE_OUTPUT,true);
    wifi=claim(CONTROL_SOURCE_WIFI,1402);

    assert(reserve(wifi,50,1,1500,CONTROL_SOURCE_RAW));
    control_source_complete(wifi,50,1,CONTROL_SOURCE_RAW,true);
    assert(reserve(wifi,50,2,1501,CONTROL_SOURCE_IDLE));
    control_source_complete(wifi,50,2,CONTROL_SOURCE_IDLE,true);
    assert(!control_source_claim(CONTROL_SOURCE_BLE_CTRL,1502,&ble));
    control_source_release(wifi,50,2);
    ble=claim(CONTROL_SOURCE_BLE_CTRL,1503);

    // A dead transport cannot be kept alive by GETs; expiry is absolute.
    assert(reserve(ble,60,1,1600,CONTROL_SOURCE_KEYS));
    assert(!control_source_claim(CONTROL_SOURCE_WIFI,1600+CONTROL_SOURCE_LEASE_US-1,&wifi));
    wifi=claim(CONTROL_SOURCE_WIFI,1600+CONTROL_SOURCE_LEASE_US);
    assert(!control_source_valid(ble));
    assert(!control_source_reserve(wifi,1,1,10,10,CONTROL_SOURCE_OUTPUT));
    assert(!control_source_reserve(wifi,1,1,10,11+CONTROL_SOURCE_LEASE_US,CONTROL_SOURCE_OUTPUT));
    assert(!reserve(wifi,0,1,2000,CONTROL_SOURCE_KEYS));
    assert(!reserve(wifi,1,0,2000,CONTROL_SOURCE_KEYS));
    assert(!reserve(wifi,1,1,2000,(control_source_activity_t)-1));
    control_source_revoke(wifi);
    assert(!control_source_valid(wifi));
    ble=claim(CONTROL_SOURCE_BLE_CTRL,2001);
    assert(ble.epoch>wifi.epoch);
    uint32_t first_ticket,next_ticket;
    assert(control_source_reserve_job(ble,2001,2001+CONTROL_SOURCE_LEASE_US,&first_ticket));
    assert(!control_source_reserve_job(ble,2001,2001+CONTROL_SOURCE_LEASE_US,&next_ticket));
    control_source_complete(ble,0,first_ticket,CONTROL_SOURCE_OUTPUT,true);
    assert(control_source_reserve_job(ble,2002,2002+CONTROL_SOURCE_LEASE_US,&next_ticket));
    assert(next_ticket>first_ticket);
    control_source_complete(ble,0,first_ticket,CONTROL_SOURCE_OUTPUT,false);
    assert(!control_source_claim(CONTROL_SOURCE_WIFI,2003,&wifi));
    control_source_complete(ble,0,next_ticket,CONTROL_SOURCE_OUTPUT,true);
    assert(control_source_claim(CONTROL_SOURCE_WIFI,2004,&wifi));
    puts("Control source: idle takeover, bounded performance/output holds, four clients, RAW release, stale/revoked fences PASS");
    return 0;
}
