#include "ble_control.h"
#include "ble_control_codec.h"
#include "control_api.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "nimble/nimble_port.h"
#include "os/os_mbuf.h"

#if !MYNEWT_VAL(BLE_GATT_NOTIFY)
#error "Private BLE control requires NimBLE notifications"
#endif

#define TAG "ble_control"
#define CONTROL_RX_DEPTH 4u
// AE350 shared-core KS/TOML path: 5952 target-compiled bytes + 1024 reserve.
// The worker remains lazy; the standard MIDI RX task keeps its own budget.
#define CONTROL_TASK_STACK 8192u
#define CONTROL_TASK_PRIORITY 3u
#define CONTROL_STALL_US 1000000LL
#define CONTROL_RPC_US 10000000LL
#define CONTROL_IDLE_US 30000000LL
#define CONTROL_MBUF_RESERVE 8
#define CONTROL_TX_BURST_MAX 16u
#define CONTROL_TX_BUSY_US 2000LL

typedef struct {
    uint64_t epoch;
    uint16_t connection, length, packet_limit;
    uint8_t bytes[BLE_CONTROL_PACKET_MAX];
} control_rx_t;
typedef struct {
    ble_control_decoder_t decoder;
    ble_control_encoder_t encoder;
    control_rx_t receive;
    QueueHandle_t queue;
    uint64_t epoch;
    uint16_t connection;
    unsigned borrowers;
    uint32_t request_barrier;
    int64_t first_received, last_received, deadline;
    int64_t burst_started;
    unsigned burst_packets;
} control_runtime_t;
_Static_assert(sizeof(control_runtime_t)<=5632,"Private BLE runtime exceeds lazy RAM budget");
enum {MAIL_IDLE,MAIL_PENDING,MAIL_RUNNING,MAIL_DONE,MAIL_CANCELED};
typedef struct {
    uint8_t bytes[BLE_CONTROL_PACKET_MAX];
    uint64_t epoch, token;
    uint16_t connection, length;
    unsigned state;
    int result;
} control_mailbox_t;

// Idle: one 514-byte TX mailbox + one 514-byte startup packet, two NPL events
// and a static semaphore. Private CCCD use lazily adds ~5.6 KiB runtime,
// four RX slots (~2.2 KiB) and the measured/budgeted worker stack. Disconnect,
// fatal failure or 30 s private inactivity releases every lazy allocation.
// Standard MIDI alone never allocates a private worker or decoder.
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static control_runtime_t *s_runtime;
static control_mailbox_t s_mail;
static control_rx_t s_bootstrap;
// The old worker checks its epoch before using this wake item's payload.
// Constant storage avoids adding a 528-byte callback stack frame.
static const control_rx_t s_cancel_receive={0};
static bool s_bootstrap_valid, s_start_pending, s_initialized;
static bool s_live, s_subscribed;
static uint64_t s_epoch;
static uint16_t s_connection=BLE_HS_CONN_HANDLE_NONE,s_mtu=BLE_ATT_MTU_DFLT;
static uint64_t s_terminate_epoch;
static uint16_t s_terminate_connection;
static struct ble_npl_event s_notify_event,s_terminate_event;
static StaticSemaphore_t s_done_storage;
static SemaphoreHandle_t s_done;
static uint16_t s_response_handle;

static void start_deferred(void *unused,uint32_t unused_value);
static void control_worker(void *argument);

static control_api_peer_t peer(uint64_t epoch)
{return (control_api_peer_t){.transport=CONTROL_API_BLE_CONTROL,.connection_epoch=epoch};}
static uint32_t packet_id(const uint8_t *p)
{return ((uint32_t)p[4]<<24)|((uint32_t)p[5]<<16)|((uint32_t)p[6]<<8)|p[7];}
static bool current_locked(uint64_t epoch,uint16_t connection)
{return ble_control_link_matches(epoch,connection,s_epoch,s_connection,s_live,s_subscribed);}
static bool current(uint64_t epoch,uint16_t connection)
{
    portENTER_CRITICAL(&s_lock);bool live=current_locked(epoch,connection);
    portEXIT_CRITICAL(&s_lock);return live;
}
// Caller holds s_lock. LAST acceptance opens ingress for exactly the next
// RPC, even while the worker finishes this response. An old completion must
// never clear a newer request's barrier.
static void response_queued_locked(void)
{
    control_runtime_t *r=s_runtime;
    if(s_mail.state==MAIL_RUNNING&&(s_mail.bytes[3]&2)&&
        current_locked(s_mail.epoch,s_mail.connection)&&r&&r->epoch==s_mail.epoch&&
        r->request_barrier==packet_id(s_mail.bytes))r->request_barrier=0;
}
static bool request_start(void)
{
    portENTER_CRITICAL(&s_lock);
    bool ready=s_initialized&&s_live&&s_subscribed;
    bool schedule=ready&&!s_runtime&&!s_start_pending;
    if(schedule)s_start_pending=true;
    portEXIT_CRITICAL(&s_lock);
    if(schedule&&xTimerPendFunctionCall(start_deferred,NULL,0,0)!=pdPASS){
        portENTER_CRITICAL(&s_lock);s_start_pending=false;portEXIT_CRITICAL(&s_lock);
        return false;
    }
    return ready;
}
static void close_link(uint64_t epoch,uint16_t connection,bool terminate)
{
    bool invalidate=false;
    control_runtime_t *wake=NULL;
    portENTER_CRITICAL(&s_lock);
    if(epoch&&s_epoch==epoch&&s_connection==connection){
        invalidate=s_live;s_live=false;s_subscribed=false;s_bootstrap_valid=false;
        if(s_mail.epoch==epoch&&s_mail.state!=MAIL_IDLE)s_mail.state=MAIL_CANCELED;
        if(terminate){s_terminate_epoch=epoch;s_terminate_connection=connection;}
        if(s_runtime&&s_runtime->epoch==epoch){wake=s_runtime;wake->borrowers++;}
    }
    portEXIT_CRITICAL(&s_lock);
    // No allocation, common mutex, owner wait or SPI. The core tombstone
    // prevents an already-running old prepare from recreating this peer.
    if(invalidate)control_api_peer_invalidate(peer(epoch));
    if(wake){
        // Zero-wait cancellation also wakes an idle xQueueReceive. A full
        // queue already wakes it; no callback waits for worker/core cleanup.
        (void)xQueueSend(wake->queue,&s_cancel_receive,0);
        portENTER_CRITICAL(&s_lock);wake->borrowers--;portEXIT_CRITICAL(&s_lock);
    }
    if(terminate&&invalidate)
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&s_terminate_event);
    if(s_done)xSemaphoreGive(s_done);
}
static void terminate_event(struct ble_npl_event *event)
{
    (void)event;
    portENTER_CRITICAL(&s_lock);
    bool same=s_terminate_epoch&&s_terminate_epoch==s_epoch&&
        s_terminate_connection==s_connection&&!s_live;
    uint16_t connection=s_terminate_connection;s_terminate_epoch=0;
    portEXIT_CRITICAL(&s_lock);
    if(same)(void)ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM);
}
static void notify_event(struct ble_npl_event *event)
{
    (void)event;
    // This copy also makes cancellation/reuse safe if the worker times out
    // while this host event is already running on the other CPU.
    uint8_t bytes[BLE_CONTROL_PACKET_MAX];uint16_t connection=0,length=0;
    uint64_t epoch=0,token=0;bool send=false;
    portENTER_CRITICAL(&s_lock);
    if(s_mail.state==MAIL_PENDING){
        token=s_mail.token;epoch=s_mail.epoch;connection=s_mail.connection;
        length=s_mail.length;send=current_locked(epoch,connection);
        if(send){memcpy(bytes,s_mail.bytes,length);s_mail.state=MAIL_RUNNING;}
    }
    portEXIT_CRITICAL(&s_lock);
    int result=BLE_HS_ENOTCONN;
    if(send&&current(epoch,connection)){
        if(os_msys_num_free()<CONTROL_MBUF_RESERVE)result=BLE_HS_ENOMEM;
        else{
            struct os_mbuf *om=ble_hs_mbuf_from_flat(bytes,length);
            if(!om)result=BLE_HS_ENOMEM;
            else if(!current(epoch,connection)){os_mbuf_free_chain(om);result=BLE_HS_ENOTCONN;}
            else result=ble_gatts_notify_custom(connection,s_response_handle,om);
        }
    }
    // NOTIFY_TX is a synchronous enqueue-attempt callback in IDF v6.1,
    // not an over-the-air completion/credit. Only this mailbox token owns
    // the result; no GAP callback waits or starts another notification.
    portENTER_CRITICAL(&s_lock);
    if(token&&s_mail.token==token&&s_mail.state!=MAIL_CANCELED){
        if(result==0)response_queued_locked();
        s_mail.result=result;s_mail.state=MAIL_DONE;
    }
    portEXIT_CRITICAL(&s_lock);
    xSemaphoreGive(s_done);
}
static bool transmit(void *context,const uint8_t *packet,size_t length)
{
    control_runtime_t *r=context;
    while(esp_timer_get_time()<r->deadline&&current(r->epoch,r->connection)){
        xSemaphoreTake(s_done,0);
        portENTER_CRITICAL(&s_lock);
        uint64_t token=++s_mail.token;if(!token)token=++s_mail.token;
        memcpy(s_mail.bytes,packet,length);s_mail.length=(uint16_t)length;
        s_mail.epoch=r->epoch;s_mail.connection=r->connection;s_mail.state=MAIL_PENDING;
        portEXIT_CRITICAL(&s_lock);
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&s_notify_event);
        int result=BLE_HS_ETIMEOUT;bool done=false;
        while(esp_timer_get_time()<r->deadline&&current(r->epoch,r->connection)){
            (void)xSemaphoreTake(s_done,pdMS_TO_TICKS(20));
            portENTER_CRITICAL(&s_lock);
            done=s_mail.token==token&&s_mail.state==MAIL_DONE;
            if(done){result=s_mail.result;s_mail.state=MAIL_IDLE;}
            portEXIT_CRITICAL(&s_lock);
            if(done)break;
        }
        if(!done){
            portENTER_CRITICAL(&s_lock);
            if(s_mail.token==token)s_mail.state=MAIL_CANCELED;
            portEXIT_CRITICAL(&s_lock);return false;
        }
        if(result==0){
            int64_t now=esp_timer_get_time();
            if(!r->burst_packets)r->burst_started=now;
            if(++r->burst_packets>=CONTROL_TX_BURST_MAX||now-r->burst_started>=CONTROL_TX_BUSY_US){
                vTaskDelay(1);r->burst_packets=0;
            }
            return esp_timer_get_time()<r->deadline&&current(r->epoch,r->connection);
        }
        if(result!=BLE_HS_ENOMEM&&result!=BLE_HS_EAGAIN&&result!=BLE_HS_EBUSY)return false;
        vTaskDelay(1);r->burst_packets=0;
    }
    return false;
}
static esp_err_t encode_body(void *context,const char *bytes,size_t length)
{return ble_control_encoder_body(context,bytes,length)?ESP_OK:ESP_FAIL;}
static bool emit_text(control_runtime_t *r,unsigned code,const char *text)
{
    ble_control_encoder_t *e=&r->encoder;
    if(!ble_control_encoder_init(e,code,r->decoder.request_id,20,0,NULL,NULL)||
        !ble_control_encoder_body(e,text,strlen(text))||!ble_control_encoder_finish(e))return false;
    uint16_t total=(uint16_t)e->length;
    portENTER_CRITICAL(&s_lock);size_t limit=ble_control_packet_limit(s_mtu);portEXIT_CRITICAL(&s_lock);
    return ble_control_encoder_init(e,code,r->decoder.request_id,limit,total,transmit,r)&&
        ble_control_encoder_body(e,text,strlen(text))&&ble_control_encoder_finish(e);
}
static bool emit_response(control_runtime_t *r,control_api_response_t *response)
{
    ble_control_encoder_t *e=&r->encoder;
    control_api_sink_t sink={.context=e,.write=encode_body};
    unsigned code=response->code;
    bool measured=ble_control_encoder_init(e,code,r->decoder.request_id,20,0,NULL,NULL)&&
        control_api_emit(response,&sink)==ESP_OK&&ble_control_encoder_finish(e);
    if(!measured){
        bool oversized=e->length>=BLE_CONTROL_MESSAGE_MAX;
        return emit_text(r,oversized?413:500,oversized?
            "{\"code\":\"ble_response_too_large\"}":"{\"code\":\"ble_response_invalid\"}");
    }
    uint16_t total=(uint16_t)e->length;
    portENTER_CRITICAL(&s_lock);size_t limit=ble_control_packet_limit(s_mtu);portEXIT_CRITICAL(&s_lock);
    if(!ble_control_encoder_init(e,code,r->decoder.request_id,limit,total,transmit,r))return false;
    bool emitted=control_api_emit(response,&sink)==ESP_OK;
    return emitted&&ble_control_encoder_finish(e);
}
static bool execute(control_runtime_t *r)
{
    if(!current(r->epoch,r->connection))return false;
    r->burst_packets=0;
    control_api_request_t request={.path=r->decoder.path,
        .body=r->decoder.body_null?NULL:r->decoder.body,
        .body_length=r->decoder.body_length,.peer=peer(r->epoch),.control_authorized=true};
    control_api_response_t response={0};
    esp_err_t result=control_api_prepare(&request,&response);
    bool ok=current(r->epoch,r->connection)&&(result==ESP_OK?emit_response(r,&response):
        emit_text(r,result==ESP_ERR_NO_MEM?503:500,"{\"code\":\"control_api_unavailable\"}"));
    control_api_release(&response);return ok;
}
static void control_worker(void *argument)
{
    control_runtime_t *r=argument;r->last_received=esp_timer_get_time();
    for(;;){
        if(!current(r->epoch,r->connection))break;
        if(xQueueReceive(r->queue,&r->receive,pdMS_TO_TICKS(100))!=pdTRUE){
            int64_t now=esp_timer_get_time();
            if((r->decoder.active&&(now-r->last_received>=CONTROL_STALL_US||
                now-r->first_received>=CONTROL_RPC_US))||now-r->last_received>=CONTROL_IDLE_US){
                ESP_LOGW(TAG,"private control idle/partial timeout");
                close_link(r->epoch,r->connection,true);break;
            }
            continue;
        }
        if(!current(r->epoch,r->connection)||r->receive.epoch!=r->epoch||
            r->receive.connection!=r->connection)break;
        int64_t now=esp_timer_get_time();
        if(!r->decoder.active){r->first_received=now;r->deadline=now+CONTROL_RPC_US;}
        if(now-r->first_received>=CONTROL_RPC_US||
            (r->decoder.active&&now-r->last_received>=CONTROL_STALL_US)){
            close_link(r->epoch,r->connection,true);break;
        }
        r->last_received=now;
        ble_control_decode_result_t decoded=ble_control_decoder_feed(&r->decoder,
            r->receive.bytes,r->receive.length,r->receive.packet_limit);
        if(decoded==BLE_CONTROL_INVALID){
            ESP_LOGW(TAG,"invalid private control frame/envelope");
            close_link(r->epoch,r->connection,true);break;
        }
        if(decoded==BLE_CONTROL_COMPLETE){
            uint32_t completed_request=r->decoder.request_id;
            if(!execute(r)||!ble_control_decoder_next(&r->decoder)){
                close_link(r->epoch,r->connection,true);break;
            }
            portENTER_CRITICAL(&s_lock);
            if(r->request_barrier==completed_request)r->request_barrier=0;
            portEXIT_CRITICAL(&s_lock);
            r->last_received=esp_timer_get_time();
        }
    }
    control_api_disconnect(peer(r->epoch));
    // Stop new queue borrowers before deleting the queue. An ATT callback
    // that already borrowed it finishes its zero-wait copy before free.
    portENTER_CRITICAL(&s_lock);
    if(s_runtime==r)s_runtime=NULL;
    portEXIT_CRITICAL(&s_lock);
    for(;;){
        portENTER_CRITICAL(&s_lock);unsigned borrowers=r->borrowers;portEXIT_CRITICAL(&s_lock);
        if(!borrowers)break;
        vTaskDelay(1);
    }
    vQueueDelete(r->queue);free(r);
    (void)request_start();
    vTaskDelete(NULL);
}
static void start_deferred(void *unused,uint32_t unused_value)
{
    (void)unused;(void)unused_value;
    portENTER_CRITICAL(&s_lock);
    bool start=s_live&&s_subscribed&&!s_runtime;
    uint64_t epoch=s_epoch;uint16_t connection=s_connection;
    portEXIT_CRITICAL(&s_lock);
    if(!start){portENTER_CRITICAL(&s_lock);s_start_pending=false;portEXIT_CRITICAL(&s_lock);return;}
    control_runtime_t *r=calloc(1,sizeof(*r));
    if(r){r->queue=xQueueCreate(CONTROL_RX_DEPTH,sizeof(control_rx_t));r->epoch=epoch;r->connection=connection;}
    if(!r||!r->queue){
        if(r)free(r);
        portENTER_CRITICAL(&s_lock);s_start_pending=false;portEXIT_CRITICAL(&s_lock);
        close_link(epoch,connection,true);return;
    }
    portENTER_CRITICAL(&s_lock);
    bool install=current_locked(epoch,connection)&&!s_runtime;
    if(install){
        s_runtime=r;
        if(s_bootstrap_valid&&s_bootstrap.epoch==epoch){
            (void)xQueueSend(r->queue,&s_bootstrap,0);s_bootstrap_valid=false;
            if(s_bootstrap.bytes[3]&2)r->request_barrier=packet_id(s_bootstrap.bytes);
        }
    }
    s_start_pending=false;
    portEXIT_CRITICAL(&s_lock);
    if(!install){vQueueDelete(r->queue);free(r);(void)request_start();return;}
    if(xTaskCreate(control_worker,"ble_control",CONTROL_TASK_STACK,r,CONTROL_TASK_PRIORITY,NULL)!=pdPASS){
        close_link(epoch,connection,true);
        portENTER_CRITICAL(&s_lock);if(s_runtime==r)s_runtime=NULL;portEXIT_CRITICAL(&s_lock);
        for(;;){
            portENTER_CRITICAL(&s_lock);unsigned borrowers=r->borrowers;portEXIT_CRITICAL(&s_lock);
            if(!borrowers)break;
            vTaskDelay(1);
        }
        vQueueDelete(r->queue);free(r);return;
    }
    ESP_LOGI(TAG,"private worker lazy runtime=%u queue=%u stack=%u",
        (unsigned)sizeof(*r),(unsigned)(CONTROL_RX_DEPTH*sizeof(control_rx_t)),CONTROL_TASK_STACK);
}
static int access_request(uint16_t connection,uint16_t attribute,
                           struct ble_gatt_access_ctxt *ctxt,void *argument)
{
    (void)attribute;(void)argument;
    if(ctxt->op!=BLE_GATT_ACCESS_OP_WRITE_CHR)return BLE_ATT_ERR_READ_NOT_PERMITTED;
    control_rx_t packet;
    portENTER_CRITICAL(&s_lock);
    packet.epoch=s_epoch;packet.connection=connection;
    packet.packet_limit=(uint16_t)ble_control_packet_limit(s_mtu);
    bool ready=current_locked(packet.epoch,connection);
    portEXIT_CRITICAL(&s_lock);
    if(!ready)return BLE_ATT_ERR_INSUFFICIENT_RES;
    packet.length=OS_MBUF_PKTLEN(ctxt->om);
    if(packet.length<=12||packet.length>packet.packet_limit||
        os_mbuf_copydata(ctxt->om,0,packet.length,packet.bytes)!=0||
        packet.bytes[0]!=0x53||packet.bytes[1]!=1||packet.bytes[2]!=1||(packet.bytes[3]&~3u)){
        close_link(packet.epoch,connection,true);return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    control_runtime_t *r=NULL;bool bootstrap=false;
    portENTER_CRITICAL(&s_lock);
    if(current_locked(packet.epoch,connection)){
        if(s_runtime&&s_runtime->epoch==packet.epoch&&!s_runtime->request_barrier){r=s_runtime;r->borrowers++;}
        else if((!s_runtime||s_runtime->epoch!=packet.epoch)&&!s_bootstrap_valid){
            // Keep one bounded first frame for the new peer while the old
            // runtime finishes cancellation/cleanup. Only its matching epoch
            // may consume it after the old runtime is freed.
            s_bootstrap=packet;s_bootstrap_valid=true;bootstrap=true;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    bool queued=bootstrap;
    if(r){
        queued=xQueueSend(r->queue,&packet,0)==pdTRUE;
        portENTER_CRITICAL(&s_lock);
        if(queued&&(packet.bytes[3]&2))r->request_barrier=packet_id(packet.bytes);
        r->borrowers--;
        portEXIT_CRITICAL(&s_lock);
    }
    if(!queued||!request_start()){
        close_link(packet.epoch,connection,true);return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}
static int access_response(uint16_t connection,uint16_t attribute,
                            struct ble_gatt_access_ctxt *ctxt,void *argument)
{
    (void)connection;(void)attribute;(void)argument;
    return ctxt->op==BLE_GATT_ACCESS_OP_WRITE_CHR?BLE_ATT_ERR_WRITE_NOT_PERMITTED:BLE_ATT_ERR_READ_NOT_PERMITTED;
}
static const ble_uuid128_t s_service=BLE_UUID128_INIT(
    0xd0,0xd2,0xb7,0xb2,0x52,0x8b,0xc6,0x9a,0x0c,0x4f,0xf9,0x24,0xc0,0xa3,0xa0,0x7d);
static const ble_uuid128_t s_request=BLE_UUID128_INIT(
    0xd0,0xd2,0xb7,0xb2,0x52,0x8b,0xc6,0x9a,0x0c,0x4f,0xf9,0x24,0xc1,0xa3,0xa0,0x7d);
static const ble_uuid128_t s_response=BLE_UUID128_INIT(
    0xd0,0xd2,0xb7,0xb2,0x52,0x8b,0xc6,0x9a,0x0c,0x4f,0xf9,0x24,0xc2,0xa3,0xa0,0x7d);
static const struct ble_gatt_chr_def s_characteristics[]={
    {.uuid=&s_request.u,.access_cb=access_request,.flags=BLE_GATT_CHR_F_WRITE},
    {.uuid=&s_response.u,.access_cb=access_response,.flags=BLE_GATT_CHR_F_NOTIFY,.val_handle=&s_response_handle},
    {0}
};
static const struct ble_gatt_svc_def s_services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&s_service.u,.characteristics=s_characteristics},{0}
};
const struct ble_gatt_svc_def *ble_control_services(void){return s_services;}
esp_err_t ble_control_init(void)
{
    if(s_initialized)return ESP_OK;
    s_done=xSemaphoreCreateBinaryStatic(&s_done_storage);
    if(!s_done)return ESP_ERR_NO_MEM;
    ble_npl_event_init(&s_notify_event,notify_event,NULL);
    ble_npl_event_init(&s_terminate_event,terminate_event,NULL);
    s_initialized=true;return ESP_OK;
}
void ble_control_host_reset(void)
{
    portENTER_CRITICAL(&s_lock);uint64_t epoch=s_epoch;uint16_t connection=s_connection;portEXIT_CRITICAL(&s_lock);
    close_link(epoch,connection,false);
}
void ble_control_gap_event(const struct ble_gap_event *event)
{
    if(!s_initialized)return;
    if(event->type==BLE_GAP_EVENT_CONNECT&&event->connect.status==0){
        ble_control_host_reset();
        portENTER_CRITICAL(&s_lock);
        if(s_epoch!=UINT64_MAX){s_epoch++;s_live=true;}
        s_connection=event->connect.conn_handle;s_mtu=BLE_ATT_MTU_DFLT;
        s_subscribed=false;s_bootstrap_valid=false;
        portEXIT_CRITICAL(&s_lock);
    }else if(event->type==BLE_GAP_EVENT_DISCONNECT){
        portENTER_CRITICAL(&s_lock);uint64_t epoch=s_epoch;portEXIT_CRITICAL(&s_lock);
        close_link(epoch,event->disconnect.conn.conn_handle,false);
    }else if(event->type==BLE_GAP_EVENT_MTU){
        portENTER_CRITICAL(&s_lock);
        if(event->mtu.conn_handle==s_connection)s_mtu=event->mtu.value;
        portEXIT_CRITICAL(&s_lock);
    }else if(event->type==BLE_GAP_EVENT_NOTIFY_TX&&event->notify_tx.attr_handle==s_response_handle&&
              event->notify_tx.status==0&&!event->notify_tx.indication){
        portENTER_CRITICAL(&s_lock);
        if(event->notify_tx.conn_handle==s_mail.connection)response_queued_locked();
        portEXIT_CRITICAL(&s_lock);
    }else if(event->type==BLE_GAP_EVENT_SUBSCRIBE&&event->subscribe.attr_handle==s_response_handle){
        portENTER_CRITICAL(&s_lock);
        uint64_t epoch=s_epoch;bool was=s_subscribed;
        bool same=event->subscribe.conn_handle==s_connection;
        if(same&&s_live)s_subscribed=event->subscribe.cur_notify;
        portEXIT_CRITICAL(&s_lock);
        if(same&&was&&!event->subscribe.cur_notify)close_link(epoch,event->subscribe.conn_handle,false);
        else if(same&&event->subscribe.cur_notify&&!request_start())close_link(epoch,event->subscribe.conn_handle,true);
    }
}
