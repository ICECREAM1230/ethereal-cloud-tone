#include "platform.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nimble/nimble_npl.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

/* Standard BLE-MIDI UUIDs, little-endian. A's existing GATT server exposes
 * these interfaces; no A firmware sources are compiled into this program. */
static const ble_uuid128_t service_uuid=BLE_UUID128_INIT(
    0x00,0xc7,0xc4,0x4e,0xe3,0x6c,0x51,0xa7,0x33,0x4b,0xe8,0xed,0x5a,0x0e,0xb8,0x03);
static const ble_uuid128_t data_uuid=BLE_UUID128_INIT(
    0xf3,0x6b,0x10,0x9d,0x66,0xf2,0xa9,0xa1,0x12,0x41,0x68,0x38,0xdb,0xe5,0x72,0x77);
static const char *TAG="b_ble";
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static uint16_t connection=BLE_HS_CONN_HANDLE_NONE, value_handle;
static uint16_t service_start,service_end;
static uint32_t generation=1,write_errors;
static bool ready,busy;
static struct ble_npl_callout retry;
static struct ble_npl_event tx_event;
enum { TX_IDLE, TX_QUEUED, TX_DONE };
static struct {
    unsigned state;
    uint32_t epoch;
    harp_midi_t message;
    bool success;
} tx;

static int gap_event(struct ble_gap_event *,void *);
static void scan(void);

static bool current_connection(uint16_t conn,uint32_t epoch)
{
    portENTER_CRITICAL(&lock);
    bool current=connection==conn && generation==epoch;
    portEXIT_CRITICAL(&lock);
    return current;
}

static void retry_later(void)
{
    int rc=ble_npl_callout_reset(&retry,ble_npl_time_ms_to_ticks32(1000));
    if(rc) ESP_LOGE(TAG,"Cannot schedule reconnect: %d",rc);
}

static void close_connection(uint16_t conn)
{
    int rc=ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);
    if(rc) {
        ESP_LOGW(TAG,"Terminate rc=%d",rc);
        if(rc==BLE_HS_ENOTCONN) {
            portENTER_CRITICAL(&lock);
            ready=false;connection=BLE_HS_CONN_HANDLE_NONE;
            if(++generation==0) ++generation;
            portEXIT_CRITICAL(&lock);
            busy=false;retry_later();
        }
    }
}

static int on_characteristic(uint16_t conn,const struct ble_gatt_error *error,
                              const struct ble_gatt_chr *chr,void *arg)
{
    uint32_t epoch=(uint32_t)(uintptr_t)arg;
    if(!current_connection(conn,epoch)) return 0;
    if(error->status==0 && chr) {
        if(chr->properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP) value_handle=chr->val_handle;
        return 0;
    }
    if(error->status==BLE_HS_EDONE && value_handle) {
        portENTER_CRITICAL(&lock);ready=true;portEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG,"A board MIDI ready, handle=%u",value_handle);
    } else {
        ESP_LOGW(TAG,"MIDI characteristic discovery failed: %d",error->status);
        close_connection(conn);
    }
    return 0;
}

static int on_service(uint16_t conn,const struct ble_gatt_error *error,
                      const struct ble_gatt_svc *svc,void *arg)
{
    uint32_t epoch=(uint32_t)(uintptr_t)arg;
    if(!current_connection(conn,epoch)) return 0;
    if(error->status==0 && svc) {
        if(!service_start) { service_start=svc->start_handle;service_end=svc->end_handle; }
        return 0;
    }
    if(error->status==BLE_HS_EDONE && service_start && service_end>service_start) {
        int rc=ble_gattc_disc_chrs_by_uuid(conn,service_start,service_end,&data_uuid.u,
                                         on_characteristic,arg);
        if(!rc) return 0;
        ESP_LOGW(TAG,"Start characteristic discovery rc=%d",rc);
    } else ESP_LOGW(TAG,"MIDI service not found, status=%d",error->status);
    close_connection(conn);
    return 0;
}

static void retry_event(struct ble_npl_event *event)
{
    (void)event;scan();
}

static void scan(void)
{
    if(busy || connection!=BLE_HS_CONN_HANDLE_NONE) return;
    uint8_t address_type;
    int rc=ble_hs_id_infer_auto(0,&address_type);
    if(rc) { ESP_LOGW(TAG,"Address selection rc=%d",rc);retry_later();return; }
    struct ble_gap_disc_params params={0};
    /* A puts its full name in Scan Response, not in the primary advertisement. */
    params.passive=0;params.filter_duplicates=1;
    rc=ble_gap_disc(address_type,BLE_HS_FOREVER,&params,gap_event,NULL);
    if(rc && rc!=BLE_HS_EALREADY) { ESP_LOGW(TAG,"Scan rc=%d",rc);retry_later(); }
}

static int gap_event(struct ble_gap_event *event,void *arg)
{
    (void)arg;
    switch(event->type) {
    case BLE_GAP_EVENT_DISC: {
        if(busy || connection!=BLE_HS_CONN_HANDLE_NONE) break;
        struct ble_hs_adv_fields fields;
        if(ble_hs_adv_parse_fields(&fields,event->disc.data,event->disc.length_data)) break;
        const char *name=CONFIG_B_A_BOARD_BLE_NAME;
        if(!fields.name || fields.name_len!=strlen(name) || memcmp(fields.name,name,fields.name_len)) break;
        uint8_t address_type;
        if(ble_hs_id_infer_auto(0,&address_type)) break;
        busy=true;
        int rc=ble_gap_disc_cancel();
        if(rc) { busy=false;ESP_LOGW(TAG,"Cancel scan rc=%d",rc);retry_later();break; }
        struct ble_gap_conn_params params={.scan_itvl=16,.scan_window=16,.itvl_min=6,
            .itvl_max=12,.latency=0,.supervision_timeout=400,.min_ce_len=0,.max_ce_len=0};
        rc=ble_gap_connect(address_type,&event->disc.addr,10000,&params,gap_event,NULL);
        if(rc) { busy=false;ESP_LOGW(TAG,"Connect rc=%d",rc);retry_later(); }
        break;
    }
    case BLE_GAP_EVENT_CONNECT:
        busy=false;
        if(event->connect.status) {
            ESP_LOGW(TAG,"Connect failed: %d",event->connect.status);retry_later();break;
        }
        portENTER_CRITICAL(&lock);
        connection=event->connect.conn_handle;ready=false;
        if(++generation==0) ++generation;
        uint32_t epoch=generation;
        portEXIT_CRITICAL(&lock);
        value_handle=0;service_start=0;service_end=0;
        {
            int rc=ble_gattc_disc_svc_by_uuid(connection,&service_uuid.u,on_service,(void *)(uintptr_t)epoch);
            if(rc) { ESP_LOGW(TAG,"Start MIDI service discovery rc=%d",rc);close_connection(connection); }
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        portENTER_CRITICAL(&lock);
        ready=false;connection=BLE_HS_CONN_HANDLE_NONE;
        if(++generation==0) ++generation;
        portEXIT_CRITICAL(&lock);
        value_handle=0;busy=false;
        ESP_LOGI(TAG,"A disconnected, reason=%d",event->disconnect.reason);
        retry_later();break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if(!busy && connection==BLE_HS_CONN_HANDLE_NONE) retry_later();
        break;
    default: break;
    }
    return 0;
}

/* Execute all GATT writes on the host event queue. GAP events and this write
 * are serialized there, so a stale handle cannot be reused across epochs. */
static void transmit_event(struct ble_npl_event *event)
{
    (void)event;
    harp_midi_t message;uint32_t epoch;uint16_t conn,attribute;
    portENTER_CRITICAL(&lock);
    if(tx.state!=TX_QUEUED) { portEXIT_CRITICAL(&lock);return; }
    message=tx.message;epoch=tx.epoch;conn=connection;attribute=value_handle;
    bool allowed=ready && generation==epoch;
    portEXIT_CRITICAL(&lock);
    uint8_t packet[5];
    size_t size=harp_ble_encode(&message,(uint32_t)(esp_timer_get_time()/1000),packet);
    int rc=BLE_HS_ENOTCONN;
    if(allowed && size) rc=ble_gattc_write_no_rsp_flat(conn,attribute,packet,(uint16_t)size);
    portENTER_CRITICAL(&lock);
    tx.success=rc==0;tx.state=TX_DONE;
    if(rc) ++write_errors;
    portEXIT_CRITICAL(&lock);
}

bool b_ble_send(void *arg,const harp_midi_t *message,uint32_t epoch)
{
    (void)arg;
    portENTER_CRITICAL(&lock);
    if(!ready || generation!=epoch) { portEXIT_CRITICAL(&lock);return false; }
    if(tx.state==TX_QUEUED) { portEXIT_CRITICAL(&lock);return false; }
    if(tx.state==TX_DONE) {
        bool matches=tx.epoch==epoch && tx.message.length==message->length &&
                     memcmp(tx.message.data,message->data,message->length)==0;
        bool accepted=matches && tx.success;
        tx.state=TX_IDLE;
        portEXIT_CRITICAL(&lock);
        if(accepted) return true;
        /* Retry the same pending MIDI message on the next owner tick. */
        return false;
    }
    tx.message=*message;tx.epoch=epoch;tx.state=TX_QUEUED;
    portEXIT_CRITICAL(&lock);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&tx_event);
    return false;
}

bool b_ble_ready(uint32_t *epoch)
{
    portENTER_CRITICAL(&lock);bool result=ready;*epoch=generation;portEXIT_CRITICAL(&lock);
    return result;
}

uint32_t b_ble_write_errors(void)
{
    portENTER_CRITICAL(&lock);uint32_t n=write_errors;portEXIT_CRITICAL(&lock);return n;
}

static void on_sync(void)
{
    int rc=ble_hs_util_ensure_addr(0);
    if(rc) { ESP_LOGE(TAG,"No BLE identity address: %d",rc);return; }
    scan();
}

static void on_reset(int reason)
{
    portENTER_CRITICAL(&lock);
    ready=false;connection=BLE_HS_CONN_HANDLE_NONE;
    if(++generation==0) ++generation;
    portEXIT_CRITICAL(&lock);
    busy=false;ESP_LOGW(TAG,"Host reset: %d",reason);
}

static void host_task(void *arg)
{
    (void)arg;nimble_port_run();nimble_port_freertos_deinit();
}

esp_err_t b_ble_init(void)
{
    esp_err_t rc=nimble_port_init();
    if(rc!=ESP_OK) return rc;
    ble_hs_cfg.reset_cb=on_reset;ble_hs_cfg.sync_cb=on_sync;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding=0;ble_hs_cfg.sm_mitm=0;ble_hs_cfg.sm_sc=1;
    ble_npl_callout_init(&retry,nimble_port_get_dflt_eventq(),retry_event,NULL);
    ble_npl_event_init(&tx_event,transmit_event,NULL);
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG,"Central scans for A board: %s",CONFIG_B_A_BOARD_BLE_NAME);
    return ESP_OK;
}
