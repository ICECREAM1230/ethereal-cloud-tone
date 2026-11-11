#define main previous_dds_owner_entry
#include "test_dds_owner.c"
#undef main

static control_source_ref_t dds_source(void)
{
    control_source_ref_t source;assert(control_source_claim(CONTROL_SOURCE_WIFI,fake_time,&source));return source;
}
static uint32_t source_load(control_source_ref_t source)
{
    uint32_t id;char error[DDS_ERROR_MAX];
    assert(dds_presets_request_source(source,DDS_LOAD,"Patches/Factory/Pads/Galaxy.toml",NULL,0,0,&id,error));
    return id;
}
int main(void)
{
    control_source_init();event_bus_init();dds_presets_init();patch_default(&g_patch);fake_time=1000;
    ks_state.control_status_valid=true;s_control_epoch=1;dds_presets_owner_context(1);
    patch_t old=g_patch;control_source_ref_t source=dds_source(),ble;
    uint32_t id=source_load(source);
    assert(!control_source_claim(CONTROL_SOURCE_BLE_CTRL,fake_time,&ble));
    control_source_revoke(source);dds_owner_request(id);status_is(DDS_FAILED);
    assert(!memcmp(&old,&g_patch,sizeof(old)));
    assert(xQueueReceive(s_work,&id,0)==pdFALSE);

    source=dds_source();id=source_load(source);dds_owner_request(id);status_is(DDS_WORKING);
    control_source_revoke(source);
    dds_presets_owner_cancel(id,s_control_epoch,"test connection revoked");
    status_is(DDS_WORKING); // worker owns buffers until terminal, so no reuse
    uint32_t rejected;char error[DDS_ERROR_MAX];
    assert(!dds_presets_request(DDS_LIST,"",NULL,0,0,&rejected,error));
    run_worker();status_is(DDS_FAILED);assert(!memcmp(&old,&g_patch,sizeof(old)));

    source=dds_source();id=source_load(source);dds_owner_request(id);run_worker();status_is(DDS_READY);
    control_source_revoke(source);assert(!dds_owner_poll(fake_time));status_is(DDS_FAILED);
    assert(!memcmp(&old,&g_patch,sizeof(old)) && !gates_off);

    source=dds_source();id=source_load(source);dds_owner_request(id);run_worker();
    gate_ack=false;assert(!dds_owner_poll(fake_time));status_is(DDS_CLOSING);
    control_source_revoke(source);gate_ack=true;
    assert(dds_owner_poll(fake_time));status_is(DDS_FAILED);
    assert(!memcmp(&old,&g_patch,sizeof(old)) && !s_dds_switch_id);

    source=dds_source();id=source_load(source);dds_owner_request(id);run_worker();
    fake_time+=CONTROL_SOURCE_LEASE_US;
    assert(!dds_owner_poll(fake_time));status_is(DDS_FAILED);
    assert(!memcmp(&old,&g_patch,sizeof(old)));
    assert(control_source_claim(CONTROL_SOURCE_BLE_CTRL,fake_time,&ble));
    source=dds_source();
    char *owned=malloc(10);assert(owned);strcpy(owned,"format=1\n");char *payload=owned;
    assert(dds_presets_request_owned_source(source,DDS_IMPORT,"Patches/User/Pads/Cancelled.toml",&owned,9,0,&id,error));
    assert(!owned && s_job.input==payload);
    dds_owner_request(id);status_is(DDS_WORKING);
    control_source_revoke(source);dds_presets_owner_cancel(id,s_control_epoch,"test owned buffer cancellation");
    assert(s_job.input==payload);status_is(DDS_WORKING);
    run_worker();status_is(DDS_FAILED);assert(!s_job.input && !s_job.input_len);
    puts("Control source DDS: waiting/working/prepared/closing and deadline fences, old patch and worker lifetime preserved PASS");
    return 0;
}
