#define _POSIX_C_SOURCE 200809L
#include "test_ble_control_platform.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "control_api.h"
#include "ble_control.h"
#include "ble_control_codec.h"

static atomic_int workers,queues,mbufs,prepare_calls,emit_calls,releases,notifies,attempts,terminated;
static atomic_int fail_notifications,msys_free=20,fail_queue,fail_task,fail_prepare;
static atomic_int inject_next,inject_count,inject_result;
static atomic_int worker_yields;
static atomic_bool freeze_clock;
static atomic_llong frozen_time;
static _Thread_local bool worker_context;
static int write_fragment(uint32_t id,unsigned at,unsigned total,unsigned flags,const char *text,size_t n);
static atomic_llong time_offset;
static atomic_ullong closed_epoch,cleanup_epoch;
static bool stopping,host_paused,timer_paused;
static pthread_t host_thread,timer_thread;
static pthread_mutex_t event_mutex=PTHREAD_MUTEX_INITIALIZER,host_execution=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t event_condition=PTHREAD_COND_INITIALIZER;
static struct ble_npl_event *events[32];static unsigned events_used;
typedef struct {void (*function)(void*,uint32_t);void *argument;uint32_t value;} pending_t;
static pending_t pending[16];static unsigned pending_used;
static pthread_mutex_t timer_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t timer_condition=PTHREAD_COND_INITIALIZER;
static pthread_mutex_t result_mutex=PTHREAD_MUTEX_INITIALIZER;
static char result[65536];static size_t result_length;static unsigned response_total;
static const char *response_text="{\"accepted\":1}";
static pthread_mutex_t prepare_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t prepare_condition=PTHREAD_COND_INITIALIZER;
static bool prepare_paused;
static pthread_mutex_t receive_mutex=PTHREAD_MUTEX_INITIALIZER,cleanup_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t receive_condition=PTHREAD_COND_INITIALIZER,cleanup_condition=PTHREAD_COND_INITIALIZER;
static bool receive_paused,cleanup_paused;
static atomic_int receive_waiting,cleanup_waiting;

static struct timespec after_ms(unsigned milliseconds)
{
    struct timespec t;clock_gettime(CLOCK_REALTIME,&t);
    t.tv_sec+=milliseconds/1000;t.tv_nsec+=(long)(milliseconds%1000)*1000000;
    if(t.tv_nsec>=1000000000){t.tv_sec++;t.tv_nsec-=1000000000;}
    return t;
}
int64_t esp_timer_get_time(void)
{
    if(atomic_load(&freeze_clock))return atomic_load(&frozen_time);
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000+atomic_load(&time_offset);
}
void vTaskDelay(unsigned ticks)
{
    if(worker_context&&ticks)atomic_fetch_add(&worker_yields,1);
    struct timespec t={.tv_sec=ticks/1000,.tv_nsec=(long)(ticks%1000)*1000000};nanosleep(&t,NULL);
}
struct fake_queue {pthread_mutex_t mutex;pthread_cond_t condition;unsigned count,head,used;size_t size;uint8_t *bytes;};
QueueHandle_t xQueueCreate(unsigned count,size_t size)
{
    if(atomic_exchange(&fail_queue,0))return NULL;
    struct fake_queue *q=calloc(1,sizeof(*q));assert(q);
    q->bytes=malloc(count*size);assert(q->bytes);q->count=count;q->size=size;
    pthread_mutex_init(&q->mutex,NULL);pthread_cond_init(&q->condition,NULL);atomic_fetch_add(&queues,1);return q;
}
int xQueueSend(QueueHandle_t q,const void *item,unsigned timeout)
{
    assert(timeout==0);pthread_mutex_lock(&q->mutex);bool ok=q->used<q->count;
    if(ok){memcpy(q->bytes+((q->head+q->used)%q->count)*q->size,item,q->size);q->used++;pthread_cond_signal(&q->condition);}
    pthread_mutex_unlock(&q->mutex);return ok;
}
int xQueueReceive(QueueHandle_t q,void *item,unsigned timeout)
{
    pthread_mutex_lock(&receive_mutex);
    if(receive_paused){
        atomic_fetch_add(&receive_waiting,1);
        while(receive_paused)pthread_cond_wait(&receive_condition,&receive_mutex);
        atomic_fetch_sub(&receive_waiting,1);
    }
    pthread_mutex_unlock(&receive_mutex);
    struct timespec deadline=after_ms(timeout);pthread_mutex_lock(&q->mutex);
    while(!q->used){if(pthread_cond_timedwait(&q->condition,&q->mutex,&deadline)==ETIMEDOUT)break;}
    bool ok=q->used!=0;
    if(ok){memcpy(item,q->bytes+q->head*q->size,q->size);q->used--;q->head=(q->head+1)%q->count;}
    pthread_mutex_unlock(&q->mutex);return ok;
}
void vQueueDelete(QueueHandle_t q)
{pthread_mutex_destroy(&q->mutex);pthread_cond_destroy(&q->condition);free(q->bytes);free(q);atomic_fetch_sub(&queues,1);}
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *s)
{pthread_mutex_init(&s->mutex,NULL);pthread_cond_init(&s->condition,NULL);s->available=false;return s;}
int xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout)
{
    struct timespec deadline=after_ms(timeout);pthread_mutex_lock(&s->mutex);
    while(!s->available&&timeout){if(pthread_cond_timedwait(&s->condition,&s->mutex,&deadline)==ETIMEDOUT)break;}
    bool ok=s->available;s->available=false;pthread_mutex_unlock(&s->mutex);return ok;
}
void xSemaphoreGive(SemaphoreHandle_t s)
{pthread_mutex_lock(&s->mutex);s->available=true;pthread_cond_broadcast(&s->condition);pthread_mutex_unlock(&s->mutex);}
typedef struct {void (*function)(void*);void *argument;} task_t;
static void *task_entry(void *context)
{task_t t=*(task_t*)context;free(context);worker_context=true;t.function(t.argument);assert(false);return NULL;}
int xTaskCreate(void (*function)(void*),const char *name,unsigned stack,void *argument,unsigned priority,TaskHandle_t *handle)
{
    (void)name;(void)handle;assert(stack<=8192&&priority<4);
    if(atomic_exchange(&fail_task,0))return 0;
    task_t *t=malloc(sizeof(*t));assert(t);*t=(task_t){function,argument};pthread_t thread;
    atomic_fetch_add(&workers,1);assert(!pthread_create(&thread,NULL,task_entry,t));pthread_detach(thread);return pdPASS;
}
_Noreturn void vTaskDelete(TaskHandle_t task)
{assert(task==NULL);atomic_fetch_sub(&workers,1);pthread_exit(NULL);}
int xTimerPendFunctionCall(void (*function)(void*,uint32_t),void *argument,uint32_t value,unsigned timeout)
{
    assert(timeout==0);pthread_mutex_lock(&timer_mutex);assert(pending_used<16);
    pending[pending_used++]=(pending_t){function,argument,value};pthread_cond_signal(&timer_condition);pthread_mutex_unlock(&timer_mutex);return pdPASS;
}
static void *timer_loop(void *unused)
{
    (void)unused;
    for(;;){
        pthread_mutex_lock(&timer_mutex);
        while(!stopping&&(!pending_used||timer_paused))pthread_cond_wait(&timer_condition,&timer_mutex);
        if(stopping){pthread_mutex_unlock(&timer_mutex);return NULL;}
        pending_t p=pending[0];memmove(pending,pending+1,--pending_used*sizeof(*pending));pthread_mutex_unlock(&timer_mutex);
        p.function(p.argument,p.value);
    }
}
void ble_npl_event_init(struct ble_npl_event *e,void (*function)(struct ble_npl_event*),void *argument)
{*e=(struct ble_npl_event){.function=function,.argument=argument};}
void *nimble_port_get_dflt_eventq(void){return events;}
void ble_npl_eventq_put(void *queue,struct ble_npl_event *e)
{
    assert(queue==events);pthread_mutex_lock(&event_mutex);
    if(!e->queued){assert(events_used<32);events[events_used++]=e;e->queued=true;pthread_cond_signal(&event_condition);}
    pthread_mutex_unlock(&event_mutex);
}
static void *host_loop(void *unused)
{
    (void)unused;
    for(;;){
        pthread_mutex_lock(&event_mutex);
        while(!stopping&&(!events_used||host_paused))pthread_cond_wait(&event_condition,&event_mutex);
        if(stopping){pthread_mutex_unlock(&event_mutex);return NULL;}
        struct ble_npl_event *e=events[0];memmove(events,events+1,--events_used*sizeof(*events));e->queued=false;
        pthread_mutex_unlock(&event_mutex);pthread_mutex_lock(&host_execution);e->function(e);pthread_mutex_unlock(&host_execution);
    }
}
int os_msys_num_free(void){return atomic_load(&msys_free);}
int os_mbuf_copydata(const struct os_mbuf *m,unsigned offset,unsigned length,void *output)
{if(offset+length>m->length)return -1;memcpy(output,m->bytes+offset,length);return 0;}
struct os_mbuf *ble_hs_mbuf_from_flat(const void *bytes,uint16_t length)
{struct os_mbuf *m=malloc(sizeof(*m));assert(m);m->length=length;m->heap=true;memcpy(m->bytes,bytes,length);atomic_fetch_add(&mbufs,1);return m;}
void os_mbuf_free_chain(struct os_mbuf *m)
{if(m){assert(m->heap);free(m);atomic_fetch_sub(&mbufs,1);}}
int ble_gatts_notify_custom(uint16_t connection,uint16_t attribute,struct os_mbuf *m)
{
    assert(pthread_equal(pthread_self(),host_thread)&&connection==7&&attribute==0x40);
    atomic_fetch_add(&attempts,1);int result_code=0;
    int failures=atomic_load(&fail_notifications);
    if(failures>0){atomic_fetch_sub(&fail_notifications,1);result_code=BLE_HS_ENOMEM;}
    else{
        uint8_t *p=m->bytes;unsigned at=(unsigned)p[8]*256+p[9],total=(unsigned)p[10]*256+p[11];
        pthread_mutex_lock(&result_mutex);
        assert(at==result_length&&m->length>=13&&m->length<=182);
        if(!at)response_total=total;else assert(response_total==total);
        memcpy(result+result_length,p+12,m->length-12);result_length+=m->length-12;result[result_length]=0;
        assert(((p[3]&2)!=0)==(result_length==total));
        pthread_mutex_unlock(&result_mutex);atomic_fetch_add(&notifies,1);
    }
    // Real v6.1 issues NOTIFY_TX synchronously, before returning.
    struct ble_gap_event tx={.type=BLE_GAP_EVENT_NOTIFY_TX,.notify_tx={.conn_handle=connection,.attr_handle=attribute,.status=result_code}};
    ble_control_gap_event(&tx);
    if(!result_code&&(m->bytes[3]&2)&&atomic_exchange(&inject_next,0)){
        // The peer starts the next RPC immediately after LAST reaches it,
        // before the previous worker's post-send delay/cleanup completes.
        const char *next="{\"path\":\"/api/v2/state\",\"body\":null}";
        pthread_mutex_lock(&result_mutex);result_length=0;response_total=0;pthread_mutex_unlock(&result_mutex);
        int rc=write_fragment(2,0,(unsigned)strlen(next),3,next,strlen(next));
        atomic_store(&inject_result,rc);atomic_fetch_add(&inject_count,1);
    }
    os_mbuf_free_chain(m);return result_code;
}
int ble_gap_terminate(uint16_t connection,unsigned reason)
{assert(connection==7&&reason==BLE_ERR_REM_USER_CONN_TERM);atomic_fetch_add(&terminated,1);return 0;}
void control_api_peer_invalidate(control_api_peer_t p)
{assert(p.transport==CONTROL_API_BLE_CONTROL);uint64_t old=atomic_load(&closed_epoch);while(old<p.connection_epoch&&!atomic_compare_exchange_weak(&closed_epoch,&old,p.connection_epoch)){};}
void control_api_disconnect(control_api_peer_t p)
{
    control_api_peer_invalidate(p);
    pthread_mutex_lock(&cleanup_mutex);
    if(cleanup_paused){
        atomic_fetch_add(&cleanup_waiting,1);
        while(cleanup_paused)pthread_cond_wait(&cleanup_condition,&cleanup_mutex);
        atomic_fetch_sub(&cleanup_waiting,1);
    }
    pthread_mutex_unlock(&cleanup_mutex);
    atomic_store(&cleanup_epoch,p.connection_epoch);
}
esp_err_t control_api_prepare(const control_api_request_t *request,control_api_response_t *response)
{
    assert(!pthread_equal(pthread_self(),host_thread));assert(request->control_authorized&&request->owned_body==NULL);
    assert(request->peer.transport==CONTROL_API_BLE_CONTROL&&request->peer.connection_epoch);
    assert(!strncmp(request->path,"/api/",5));atomic_fetch_add(&prepare_calls,1);
    if(atomic_exchange(&fail_prepare,0))return ESP_ERR_NO_MEM;
    pthread_mutex_lock(&prepare_mutex);
    while(prepare_paused)pthread_cond_wait(&prepare_condition,&prepare_mutex);
    pthread_mutex_unlock(&prepare_mutex);
    if(request->peer.connection_epoch<=atomic_load(&closed_epoch))return ESP_FAIL;
    response->code=202;response->private_data=strdup(response_text);assert(response->private_data);return ESP_OK;
}
esp_err_t control_api_emit(const control_api_response_t *response,const control_api_sink_t *sink)
{
    atomic_fetch_add(&emit_calls,1);const char *p=response->private_data;size_t n=strlen(p);
    while(n){size_t count=n<7?n:7;esp_err_t result_code=sink->write(sink->context,p,count);if(result_code!=ESP_OK)return result_code;p+=count;n-=count;}
    return ESP_OK;
}
void control_api_release(control_api_response_t *response)
{if(response->private_data){free(response->private_data);response->private_data=NULL;atomic_fetch_add(&releases,1);}}

static void gap(struct ble_gap_event *event)
{pthread_mutex_lock(&host_execution);ble_control_gap_event(event);pthread_mutex_unlock(&host_execution);}
static void connect_private(bool subscribe)
{
    struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT,.connect={.status=0,.conn_handle=7}};gap(&e);
    e=(struct ble_gap_event){.type=BLE_GAP_EVENT_MTU,.mtu={.conn_handle=7,.value=185}};gap(&e);
    if(subscribe){e=(struct ble_gap_event){.type=BLE_GAP_EVENT_SUBSCRIBE,.subscribe={.conn_handle=7,.attr_handle=0x40,.cur_notify=true}};gap(&e);}
}
static void disconnect_private(void)
{struct ble_gap_event e={.type=BLE_GAP_EVENT_DISCONNECT,.disconnect={.conn={.conn_handle=7}}};gap(&e);}
static void wait_for(atomic_int *value,int minimum)
{for(unsigned i=0;i<2000&&atomic_load(value)<minimum;i++)vTaskDelay(1);assert(atomic_load(value)>=minimum);}
static void wait_reclaimed(void)
{for(unsigned i=0;i<2000&&(atomic_load(&workers)||atomic_load(&queues));i++)vTaskDelay(1);assert(!atomic_load(&workers)&&!atomic_load(&queues)&&!atomic_load(&mbufs));}
static void clear_result(void)
{pthread_mutex_lock(&result_mutex);result_length=0;response_total=0;result[0]=0;pthread_mutex_unlock(&result_mutex);}
static bool response_complete(void)
{pthread_mutex_lock(&result_mutex);bool complete=response_total&&result_length==response_total;pthread_mutex_unlock(&result_mutex);return complete;}
static void wait_response(void)
{for(unsigned i=0;i<2000&&!response_complete();i++)vTaskDelay(1);assert(response_complete());vTaskDelay(5);}
static void pause_host(bool pause)
{pthread_mutex_lock(&event_mutex);host_paused=pause;pthread_cond_broadcast(&event_condition);pthread_mutex_unlock(&event_mutex);}
static int write_fragment(uint32_t id,unsigned at,unsigned total,unsigned flags,const char *text,size_t n)
{
    struct os_mbuf m={.length=12+n};uint8_t *p=m.bytes;
    p[0]=0x53;p[1]=1;p[2]=1;p[3]=(uint8_t)flags;
    p[4]=(uint8_t)(id>>24);p[5]=(uint8_t)(id>>16);p[6]=(uint8_t)(id>>8);p[7]=(uint8_t)id;
    p[8]=(uint8_t)(at>>8);p[9]=(uint8_t)at;p[10]=(uint8_t)(total>>8);p[11]=(uint8_t)total;
    memcpy(p+12,text,n);struct ble_gatt_access_ctxt ctxt={.op=BLE_GATT_ACCESS_OP_WRITE_CHR,.om=&m};
    bool host=pthread_equal(pthread_self(),host_thread);
    if(!host)pthread_mutex_lock(&host_execution);
    int rc=ble_control_services()[0].characteristics[0].access_cb(7,0,&ctxt,NULL);
    if(!host)pthread_mutex_unlock(&host_execution);
    return rc;
}
static void send_request(uint32_t id,const char *text)
{size_t n=strlen(text);for(size_t at=0;at<n;){size_t count=n-at;if(count>8)count=8;assert(!write_fragment(id,(unsigned)at,(unsigned)n,(at?0:1)|(at+count==n?2:0),text+at,count));at+=count;vTaskDelay(1);}}
static void check_services_and_lazy_midi(void)
{
    assert(ble_control_init()==ESP_OK&&ble_control_init()==ESP_OK);
    const struct ble_gatt_svc_def *s=ble_control_services();assert(s&&s[0].type==BLE_GATT_SVC_TYPE_PRIMARY&&!s[1].type);
    const ble_uuid128_t *uuid=(const ble_uuid128_t*)s[0].uuid;assert(!memcmp(uuid->value,ble_control_service_uuid_le,16));
    const struct ble_gatt_chr_def *c=s[0].characteristics;
    assert(c[0].flags==BLE_GATT_CHR_F_WRITE&&c[1].flags==BLE_GATT_CHR_F_NOTIFY&&!c[2].uuid);
    assert(((const ble_uuid128_t*)c[0].uuid)->value[12]==0xc1&&((const ble_uuid128_t*)c[1].uuid)->value[12]==0xc2);
    *c[1].val_handle=0x40;connect_private(false);vTaskDelay(20);
    assert(!atomic_load(&queues)&&!atomic_load(&workers));disconnect_private();
}
static void complete_once_and_backpressure(void)
{
    connect_private(true);wait_for(&workers,1);clear_result();
    const char *json="{\"path\":\"/api/v2/session\",\"body\":\"\"}";size_t n=strlen(json);
    int prepared=atomic_load(&prepare_calls),emitted=atomic_load(&emit_calls),sent=atomic_load(&notifies);
    assert(!write_fragment(1,0,(unsigned)n,1,json,8));vTaskDelay(10);
    assert(atomic_load(&prepare_calls)==prepared&&atomic_load(&notifies)==sent);
    atomic_store(&fail_notifications,2);
    for(size_t at=8;at<n;){size_t count=n-at;if(count>8)count=8;assert(!write_fragment(1,(unsigned)at,(unsigned)n,at+count==n?2:0,json+at,count));at+=count;vTaskDelay(1);}
    wait_response();assert(atomic_load(&prepare_calls)==prepared+1&&atomic_load(&emit_calls)==emitted+2);
    assert(!strcmp(result,"{\"code\":202,\"body\":\"{\\\"accepted\\\":1}\"}"));
    assert(atomic_load(&attempts)>=atomic_load(&notifies)+2);
    clear_result();atomic_store(&msys_free,7);prepared=atomic_load(&prepare_calls);sent=atomic_load(&notifies);
    send_request(2,"{\"path\":\"/api/state\",\"body\":null}");wait_for(&prepare_calls,prepared+1);vTaskDelay(30);
    assert(atomic_load(&notifies)==sent);atomic_store(&msys_free,20);wait_response();
    disconnect_private();wait_reclaimed();
}
static void partial_timeout_and_invalid_frame(void)
{
    connect_private(true);wait_for(&workers,1);int prepared=atomic_load(&prepare_calls);
    assert(!write_fragment(1,0,100,1,"{\"path\":",8));vTaskDelay(10);
    atomic_fetch_add(&time_offset,2000000);wait_reclaimed();assert(atomic_load(&prepare_calls)==prepared);
    assert(atomic_load(&closed_epoch));disconnect_private();
    connect_private(true);wait_for(&workers,1);
    assert(write_fragment(1,0,1,3,"x",1)==0); // legal frame, invalid JSON: callback never dispatches
    wait_reclaimed();assert(atomic_load(&prepare_calls)==prepared);disconnect_private();
    connect_private(true);wait_for(&workers,1);uint64_t before=atomic_load(&closed_epoch);
    assert(write_fragment(1,0,1,3,"",0)==BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
    assert(atomic_load(&closed_epoch)>before);wait_reclaimed();disconnect_private();
    connect_private(true);wait_for(&workers,1);before=atomic_load(&closed_epoch);
    pthread_mutex_lock(&host_execution);ble_control_host_reset();pthread_mutex_unlock(&host_execution);
    assert(atomic_load(&closed_epoch)>before);wait_reclaimed();
    assert(atomic_load(&prepare_calls)==prepared);disconnect_private();
}
static void next_request_immediately_after_last(void)
{
    connect_private(true);wait_for(&workers,1);clear_result();
    int prepared=atomic_load(&prepare_calls),injected=atomic_load(&inject_count);
    atomic_store(&inject_next,1);
    send_request(1,"{\"path\":\"/api/state\",\"body\":null}");
    wait_for(&inject_count,injected+1);assert(atomic_load(&inject_result)==0);
    wait_for(&prepare_calls,prepared+2);wait_response();
    disconnect_private();wait_reclaimed();
}
static void bounded_burst_pacing(void)
{
    char large[4097];memset(large,'x',4096);large[4096]=0;
    response_text=large;connect_private(true);wait_for(&workers,1);vTaskDelay(10);
    struct ble_gap_event e={.type=BLE_GAP_EVENT_MTU,.mtu={.conn_handle=7,.value=23}};gap(&e);
    atomic_store(&frozen_time,esp_timer_get_time());atomic_store(&freeze_clock,true);
    clear_result();atomic_store(&worker_yields,0);int sent=atomic_load(&notifies);
    send_request(1,"{\"path\":\"/api/state\",\"body\":null}");wait_response();
    unsigned packets=(unsigned)(atomic_load(&notifies)-sent),yields=(unsigned)atomic_load(&worker_yields);
    assert(packets==(4096+22+7)/8&&yields==packets/16);
    assert(yields*10<1000); // target CONFIG_FREERTOS_HZ=100, 10 ms/tick
    printf("ble_control_pacing: MTU23 4118B=%u packets, %u forced ticks=%ums software throttle\n",packets,yields,yields*10);
    atomic_store(&freeze_clock,false);response_text="{\"accepted\":1}";
    disconnect_private();wait_reclaimed();
}
static void framed_resource_and_encoding_errors(void)
{
    char *large=malloc(65515);assert(large);memset(large,'x',65514);large[65514]=0;
    response_text=large;connect_private(true);wait_for(&workers,1);clear_result();
    send_request(1,"{\"path\":\"/api/state\",\"body\":null}");wait_response();
    assert(strstr(result,"{\"code\":413,\"body\":\"")==result&&result_length<200);
    free(large);response_text="\xed\xa0\x80";clear_result();
    send_request(2,"{\"path\":\"/api/state\",\"body\":null}");wait_response();
    assert(strstr(result,"{\"code\":500,\"body\":\"")==result&&result_length<200);
    response_text="{\"accepted\":1}";atomic_store(&fail_prepare,1);clear_result();
    send_request(3,"{\"path\":\"/api/state\",\"body\":null}");wait_response();
    assert(strstr(result,"{\"code\":503,\"body\":\"")==result&&result_length<200);
    disconnect_private();wait_reclaimed();
}
static void stale_host_event_and_handle_reuse(void)
{
    connect_private(true);wait_for(&workers,1);clear_result();pause_host(true);
    int prepared=atomic_load(&prepare_calls),sent=atomic_load(&notifies);
    send_request(1,"{\"path\":\"/api/session\",\"body\":\"\"}");wait_for(&prepare_calls,prepared+1);vTaskDelay(5);
    disconnect_private();wait_reclaimed();connect_private(true);wait_for(&workers,1);
    pause_host(false);vTaskDelay(20);assert(atomic_load(&notifies)==sent);
    send_request(1,"{\"path\":\"/api/v2/state\",\"body\":null}");wait_response();
    disconnect_private();wait_reclaimed();
}
static void immediate_reconnect_before_old_worker_reclaimed(void)
{
    const char *caps="{\"path\":\"/api/v2/capabilities\",\"body\":null}";
    pthread_mutex_lock(&receive_mutex);receive_paused=true;pthread_mutex_unlock(&receive_mutex);
    connect_private(true);wait_for(&receive_waiting,1);clear_result();
    int prepared=atomic_load(&prepare_calls);
    disconnect_private();connect_private(true);
    // The old receive call is still blocked; acceptance cannot depend on
    // its eventual 100 ms timeout or on the previous runtime disappearing.
    assert(!write_fragment(1,0,(unsigned)strlen(caps),3,caps,strlen(caps)));
    assert(atomic_load(&prepare_calls)==prepared&&atomic_load(&workers)==1&&atomic_load(&queues)==1);
    pthread_mutex_lock(&receive_mutex);receive_paused=false;pthread_cond_broadcast(&receive_condition);pthread_mutex_unlock(&receive_mutex);
    wait_for(&prepare_calls,prepared+1);wait_response();disconnect_private();wait_reclaimed();

    pthread_mutex_lock(&cleanup_mutex);cleanup_paused=true;pthread_mutex_unlock(&cleanup_mutex);
    connect_private(true);wait_for(&workers,1);disconnect_private();wait_for(&cleanup_waiting,1);clear_result();
    prepared=atomic_load(&prepare_calls);
    connect_private(true);
    assert(!write_fragment(1,0,(unsigned)strlen(caps),3,caps,strlen(caps)));
    // Cancel the first replacement peer before handover. Its buffered RPC
    // must disappear; only the next epoch's first RPC may execute.
    disconnect_private();connect_private(true);
    assert(!write_fragment(1,0,(unsigned)strlen(caps),3,caps,strlen(caps)));
    assert(atomic_load(&prepare_calls)==prepared&&atomic_load(&workers)==1&&atomic_load(&queues)==1);
    pthread_mutex_lock(&cleanup_mutex);cleanup_paused=false;pthread_cond_broadcast(&cleanup_condition);pthread_mutex_unlock(&cleanup_mutex);
    wait_for(&prepare_calls,prepared+1);wait_response();vTaskDelay(10);
    assert(atomic_load(&prepare_calls)==prepared+1&&atomic_load(&workers)==1&&atomic_load(&queues)==1);
    disconnect_private();wait_reclaimed();
}
static void cccd_fence_during_prepare(void)
{
    connect_private(true);wait_for(&workers,1);clear_result();int prepared=atomic_load(&prepare_calls),sent=atomic_load(&notifies);
    pthread_mutex_lock(&prepare_mutex);prepare_paused=true;pthread_mutex_unlock(&prepare_mutex);
    send_request(1,"{\"path\":\"/api/session\",\"body\":\"\"}");wait_for(&prepare_calls,prepared+1);
    uint64_t before=atomic_load(&closed_epoch);
    struct ble_gap_event e={.type=BLE_GAP_EVENT_SUBSCRIBE,.subscribe={.conn_handle=7,.attr_handle=0x40,.cur_notify=false}};gap(&e);
    assert(atomic_load(&closed_epoch)>before); // immediate, even while prepare is blocked
    pthread_mutex_lock(&prepare_mutex);prepare_paused=false;pthread_cond_broadcast(&prepare_condition);pthread_mutex_unlock(&prepare_mutex);
    wait_reclaimed();assert(atomic_load(&notifies)==sent);disconnect_private();
}
static void transmit_timeout_and_idle_reclaim(void)
{
    connect_private(true);wait_for(&workers,1);clear_result();pause_host(true);
    int prepared=atomic_load(&prepare_calls),sent=atomic_load(&notifies);
    send_request(1,"{\"path\":\"/api/state\",\"body\":null}");wait_for(&prepare_calls,prepared+1);vTaskDelay(10);
    atomic_fetch_add(&time_offset,11000000);wait_reclaimed();pause_host(false);vTaskDelay(20);
    assert(atomic_load(&notifies)==sent);disconnect_private();
    connect_private(true);wait_for(&workers,1);vTaskDelay(10);
    atomic_fetch_add(&time_offset,31000000);wait_reclaimed();disconnect_private();
}
static void startup_overload_and_allocation_failure(void)
{
    pthread_mutex_lock(&timer_mutex);timer_paused=true;pthread_mutex_unlock(&timer_mutex);
    connect_private(true);assert(!write_fragment(1,0,100,1,"{\"path\":",8));
    assert(write_fragment(1,8,100,0,"\"/api/st",8)==BLE_ATT_ERR_INSUFFICIENT_RES);
    pthread_mutex_lock(&timer_mutex);timer_paused=false;pthread_cond_broadcast(&timer_condition);pthread_mutex_unlock(&timer_mutex);
    vTaskDelay(20);assert(!atomic_load(&workers)&&!atomic_load(&queues));disconnect_private();
    atomic_store(&fail_queue,1);connect_private(true);vTaskDelay(20);wait_reclaimed();disconnect_private();
    atomic_store(&fail_task,1);connect_private(true);vTaskDelay(20);wait_reclaimed();disconnect_private();
}
int main(void)
{
    assert(!pthread_create(&host_thread,NULL,host_loop,NULL));assert(!pthread_create(&timer_thread,NULL,timer_loop,NULL));
    check_services_and_lazy_midi();complete_once_and_backpressure();next_request_immediately_after_last();bounded_burst_pacing();
    framed_resource_and_encoding_errors();partial_timeout_and_invalid_frame();
    stale_host_event_and_handle_reuse();immediate_reconnect_before_old_worker_reclaimed();
    cccd_fence_during_prepare();transmit_timeout_and_idle_reclaim();
    startup_overload_and_allocation_failure();
    pthread_mutex_lock(&event_mutex);pthread_mutex_lock(&timer_mutex);stopping=true;
    pthread_cond_broadcast(&event_condition);pthread_cond_broadcast(&timer_condition);
    pthread_mutex_unlock(&timer_mutex);pthread_mutex_unlock(&event_mutex);
    pthread_join(host_thread,NULL);pthread_join(timer_thread,NULL);
    assert(!atomic_load(&workers)&&!atomic_load(&queues)&&!atomic_load(&mbufs));
    printf("ble_control_gatt: lazy allocation/partial/once/notify backpressure/epoch/CCCD/timeouts/reclaim passed; prepare=%d emits=%d releases=%d\n",
        atomic_load(&prepare_calls),atomic_load(&emit_calls),atomic_load(&releases));
    return 0;
}
