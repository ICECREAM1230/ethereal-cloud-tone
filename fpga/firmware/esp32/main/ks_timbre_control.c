#include "ks_timbre_control.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#ifdef ESP_PLATFORM
#include "nvs.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#endif
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static ks_timbre_request_t reservation;
static uint32_t next_id=1;
static ks_timbre_state_t state;
static ks_timbre_slot_t slots[KS_TIMBRE_SLOTS];
static ks_timbre_startup_t startup;
static QueueHandle_t queue;
static bool storage_ready,storage_busy;
static uint32_t completed_id;
static bool completed_ok;
#ifdef ESP_PLATFORM
static SemaphoreHandle_t initialized;
static nvs_handle_t handle;
#else
static bool host_failure;
#endif
bool ks_timbre_reserve(ks_timbre_request_t *r,int64_t now)
{
    if(!r||r->event.c||!r->event.token||!r->event.sequence||!r->event.context_epoch||
       r->event.client>=4||now<0||r->event.expires<=now||r->event.expires-now>1200000LL)return false;
    bool ok=false;portENTER_CRITICAL(&lock);
    /* A dispatched job cannot be replaced by lease expiry: the owner must
     * retire it explicitly, preserving an unknown native COMMIT fence. */
    if(!reservation.event.c && next_id) {r->event.c=next_id++;reservation=*r;ok=true;}
    portEXIT_CRITICAL(&lock);return ok;
}
bool ks_timbre_claim(const web_v2_event_t *e,ks_timbre_request_t *out)
{
    if(!e||!e->c||!out)return false;
    portENTER_CRITICAL(&lock);const web_v2_event_t *r=&reservation.event;
    bool ok=r->c==e->c&&r->token==e->token&&r->sequence==e->sequence&&r->context_epoch==e->context_epoch&&r->client==e->client&&r->expires==e->expires;
    if(ok)*out=reservation;
    portEXIT_CRITICAL(&lock);return ok;
}
bool ks_timbre_reserved(ks_timbre_request_t *out)
{portENTER_CRITICAL(&lock);bool ok=reservation.event.c!=0;if(ok)*out=reservation;portEXIT_CRITICAL(&lock);return ok;}
void ks_timbre_release(uint32_t id)
{portENTER_CRITICAL(&lock);if(id&&reservation.event.c==id)memset(&reservation,0,sizeof reservation);portEXIT_CRITICAL(&lock);}
void ks_timbre_state_read(ks_timbre_state_t *out)
{if(!out)return;portENTER_CRITICAL(&lock);*out=state;portEXIT_CRITICAL(&lock);}
void ks_timbre_state_publish(const ks_timbre_state_t *s)
{portENTER_CRITICAL(&lock);state=*s;portEXIT_CRITICAL(&lock);}
void ks_timbre_directory(ks_timbre_slot_t out[8],ks_timbre_startup_t *s)
{portENTER_CRITICAL(&lock);memcpy(out,slots,sizeof slots);*s=startup;portEXIT_CRITICAL(&lock);}
bool ks_timbre_slot_read(unsigned slot,ks_timbre_setting_t *out)
{if(slot>=8||!out)return false;portENTER_CRITICAL(&lock);bool ok=slots[slot].occupied;if(ok)*out=slots[slot].setting;portEXIT_CRITICAL(&lock);return ok;}
bool ks_timbre_setting_saved(const ks_timbre_setting_t *setting)
{
    if(!setting)return false;
    bool saved=false;portENTER_CRITICAL(&lock);
    for(unsigned i=0;i<8;i++)if(slots[i].occupied && slots[i].setting.decay_ms==setting->decay_ms &&
       !memcmp(&slots[i].setting.preset,&setting->preset,sizeof setting->preset)){saved=true;break;}
    portEXIT_CRITICAL(&lock);return saved;
}
static bool setting_valid(const ks_timbre_setting_t *s)
{return s&&ks_timbre_validate(&s->preset)&&ks_timbre_name_valid(s->name)&&s->decay_ms>=200&&s->decay_ms<=8000;}
/* Stored values are physical settings, never tuning/filter coefficients,
 * runtime generation IDs, or references to a factory implementation. */
typedef struct {uint32_t magic,version;ks_timbre_setting_t setting;uint32_t crc;} stored_setting_t;
typedef struct {uint32_t magic,version;ks_timbre_startup_t startup;uint32_t crc;} stored_startup_t;
static uint32_t bytes_crc(const void *data,size_t n)
{uint32_t c=~0u;const unsigned char *s=data;for(size_t i=0;i<n;i++){c^=s[i];for(unsigned b=0;b<8;b++)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
static bool store_source_valid(const ks_timbre_request_t *r)
{return !r->source.epoch || (esp_timer_get_time()<r->event.expires && control_source_valid(r->source));}
static bool persist(const ks_timbre_request_t *r)
{
    if(!store_source_valid(r))return false;
    if(r->op==KST_STARTUP) {
        stored_startup_t blob={.magic=0x4b535031,.version=1,.startup={.enabled=r->enabled,.has_slot=r->has_slot,.slot=r->slot,.instrument_id=r->instrument_id}};
        blob.crc=bytes_crc(&blob,offsetof(stored_startup_t,crc));
#ifdef ESP_PLATFORM
        stored_startup_t check;size_t n=sizeof check;
        // Once set_blob starts, finish the existing set/commit/readback unit.
        // A later source revoke changes the result, never fakes a rollback.
        if(!store_source_valid(r) || nvs_set_blob(handle,"startup",&blob,sizeof blob)!=ESP_OK||nvs_commit(handle)!=ESP_OK||
           nvs_get_blob(handle,"startup",&check,&n)!=ESP_OK||n!=sizeof check||memcmp(&check,&blob,n))return false;
#else
        if(host_failure)return false;
#endif
        portENTER_CRITICAL(&lock);startup=blob.startup;portEXIT_CRITICAL(&lock);return true;
    }
    if(r->slot>=8||!setting_valid(&r->setting))return false;
    stored_setting_t blob={.magic=0x4b535431,.version=1,.setting=r->setting};
    blob.crc=bytes_crc(&blob,offsetof(stored_setting_t,crc));
#ifdef ESP_PLATFORM
    char key[8];snprintf(key,sizeof key,"slot%u",(unsigned)r->slot);
    stored_setting_t check;size_t n=sizeof check;
    if(!store_source_valid(r) || nvs_set_blob(handle,key,&blob,sizeof blob)!=ESP_OK||nvs_commit(handle)!=ESP_OK||
       nvs_get_blob(handle,key,&check,&n)!=ESP_OK||n!=sizeof check||memcmp(&check,&blob,n))return false;
#else
    if(host_failure)return false;
#endif
    portENTER_CRITICAL(&lock);slots[r->slot]=(ks_timbre_slot_t){.occupied=true,.setting=blob.setting};portEXIT_CRITICAL(&lock);return true;
}
static void work(const ks_timbre_request_t *request)
{
    bool ok=persist(request) && store_source_valid(request);
    portENTER_CRITICAL(&lock);completed_id=request->event.c;completed_ok=ok;storage_busy=false;portEXIT_CRITICAL(&lock);
}
#ifdef ESP_PLATFORM
static void storage_task(void *unused)
{
    (void)unused;
    storage_ready=nvs_open("ks_timbres",NVS_READWRITE,&handle)==ESP_OK;
    if(storage_ready) {
        for(unsigned i=0;i<8;i++) {
            char key[8];snprintf(key,sizeof key,"slot%u",i);stored_setting_t blob;size_t n=sizeof blob;
            if(nvs_get_blob(handle,key,&blob,&n)==ESP_OK&&n==sizeof blob&&blob.magic==0x4b535431&&blob.version==1&&
               blob.crc==bytes_crc(&blob,offsetof(stored_setting_t,crc))&&memchr(blob.setting.name,0,sizeof blob.setting.name)&&setting_valid(&blob.setting))slots[i]=(ks_timbre_slot_t){.occupied=true,.setting=blob.setting};
        }
        stored_startup_t blob;size_t n=sizeof blob;
        if(nvs_get_blob(handle,"startup",&blob,&n)==ESP_OK&&n==sizeof blob&&blob.magic==0x4b535031&&blob.version==1&&
           blob.crc==bytes_crc(&blob,offsetof(stored_startup_t,crc))&&blob.startup.instrument_id<=2&&blob.startup.slot<8)startup=blob.startup;
    }
    xSemaphoreGive(initialized);
    static ks_timbre_request_t request;
    while(1)if(xQueueReceive(queue,&request,portMAX_DELAY)==pdTRUE)work(&request);
}
#endif
void ks_timbre_store_init(void)
{
    state=(ks_timbre_state_t){.phase=KST_UNAVAILABLE,.setting={.decay_ms=3000,.name="Legacy"}};
    ks_timbre_factory(0,&state.setting.preset);
    queue=xQueueCreate(1,sizeof(ks_timbre_request_t));if(!queue)return;
#ifdef ESP_PLATFORM
    initialized=xSemaphoreCreateBinary();if(!initialized)return;
    if(xTaskCreate(storage_task,"ks_presets",6144,NULL,2,NULL)!=pdPASS)return;
    xSemaphoreTake(initialized,portMAX_DELAY);
#else
    storage_ready=true;
#endif
}
bool ks_timbre_store_request(const ks_timbre_request_t *r)
{
    if(!r||!r->event.c||!queue||!storage_ready||!store_source_valid(r))return false;
    portENTER_CRITICAL(&lock);bool ok=!storage_busy;if(ok){storage_busy=true;completed_id=0;}portEXIT_CRITICAL(&lock);
    if(ok&&xQueueSend(queue,r,0)!=pdTRUE){portENTER_CRITICAL(&lock);storage_busy=false;portEXIT_CRITICAL(&lock);return false;}return ok;
}
bool ks_timbre_store_result(uint32_t id,bool *success)
{portENTER_CRITICAL(&lock);bool done=id&&completed_id==id;if(done)*success=completed_ok;portEXIT_CRITICAL(&lock);return done;}
bool ks_timbre_startup_setting(ks_timbre_setting_t *out)
{
    portENTER_CRITICAL(&lock);ks_timbre_startup_t s=startup;portEXIT_CRITICAL(&lock);
    if(!s.enabled)return false;
    if(s.has_slot)return ks_timbre_slot_read(s.slot,out);
    memset(out,0,sizeof *out);out->decay_ms=3000;snprintf(out->name,sizeof out->name,"%s",s.instrument_id==1?"Harp":s.instrument_id==2?"Guzheng":"Legacy");return ks_timbre_factory(s.instrument_id,&out->preset);
}
#ifndef ESP_PLATFORM
void ks_timbre_store_host_step(void)
{ks_timbre_request_t r;if(queue&&xQueueReceive(queue,&r,0)==pdTRUE)work(&r);}
void ks_timbre_store_host_fail(bool fail){host_failure=fail;}
#endif
