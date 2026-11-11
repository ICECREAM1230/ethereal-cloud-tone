// Copyright (c) 2026 ICECREAM1230
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
#include "dds_presets.h"
#include "dds_files.h"
#include "event_bus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef ESP_PLATFORM
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "freertos/semphr.h"
#endif

#define TAG "dds_presets"
#ifndef DDS_STORAGE_ROOT
#define DDS_STORAGE_ROOT "/dds"
#endif
// Xtensa -Og recovery + six nested inline tables needs at least 8208 B,
// before RTOS/libc frames. Reserve 16 KiB rather than only that lower bound.
#define DDS_WORKER_STACK_BYTES (16u * 1024u)
// Directory paths already use next_offset pagination; do not reserve a full
// TOML document while Wi-Fi/BLE and the audio owner share internal RAM.
#define DDS_LIST_PAGE_BYTES 2048u
#include "dds_factory.inc.h"

static struct {
    dds_job_status_t status;
    patch_t patch;
    unsigned argument;
    char *input,*output;
    size_t input_len,output_len;
    control_source_ref_t source;
    uint32_t source_ticket;
    int64_t source_expires;
    bool source_cancelled;
} s_job;
static QueueHandle_t s_work;
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_next_id;
static uint32_t s_owner_context=1;
static bool s_mounted,s_boot_valid;
static patch_t s_boot_patch;
static const char *s_root=DDS_STORAGE_ROOT;
#ifdef ESP_PLATFORM
static SemaphoreHandle_t s_init_ready;
#endif
static const char *const s_factory_paths[]={"Patches/Factory/Pads/Supersaw.toml","Patches/Factory/Pads/Galaxy.toml"};
static const char *const s_factory_texts[]={dds_factory_supersaw,dds_factory_galaxy};

static bool active(dds_phase_t phase)
{return phase>=DDS_WAIT_OWNER && phase<=DDS_CLOSING;}
static bool fail(char *error,const char *reason)
{snprintf(error,DDS_ERROR_MAX,"%s",reason);return false;}
static bool read_document(const char *path,char **text,size_t *len,char *error)
{
    // Shipped Factory is immutable even if an old on-disk copy differs.
    for(unsigned i=0;i<2;i++)if(!strcasecmp(path,s_factory_paths[i])) {
        *len=strlen(s_factory_texts[i]);*text=malloc(*len+1);
        if(!*text)return fail(error,"out of memory");
        memcpy(*text,s_factory_texts[i],*len+1);return true;
    }
    if(!s_mounted)return fail(error,"storage unavailable (no automatic format)");
    return dds_files_read(s_root,path,text,len,error);
}
static bool load_patch(const char *path,patch_t *patch,unsigned *warnings,char *error)
{
    char *text;size_t n;if(!read_document(path,&text,&n,error))return false;
    bool ok=dds_patch_parse(text,n,patch,warnings,error);free(text);return ok;
}
bool dds_presets_source_valid(uint32_t id,int64_t now)
{
    portENTER_CRITICAL(&s_lock);
    bool matches=s_job.status.id==id && !s_job.source_cancelled;
    control_source_ref_t source=s_job.source;int64_t expires=s_job.source_expires;
    portEXIT_CRITICAL(&s_lock);
    return matches && (!source.epoch || (now>=0 && now<expires && control_source_valid(source)));
}
static void worker_complete(uint32_t id,const char *error,bool ready)
{
    control_source_ref_t source={0};uint32_t ticket=0;char *input=NULL;
    if(!dds_presets_source_valid(id,esp_timer_get_time())){error="control source revoked or expired";ready=false;}
    portENTER_CRITICAL(&s_lock);
    if(s_job.status.id==id) {
        source=s_job.source;ticket=s_job.source_ticket;
        if(source.epoch){input=s_job.input;s_job.input=NULL;s_job.input_len=0;}
        s_job.status.phase=error?DDS_FAILED:ready?DDS_READY:DDS_DONE;
        if(error)snprintf(s_job.status.error,sizeof(s_job.status.error),"%s",error);
    }
    portEXIT_CRITICAL(&s_lock);
    if(!ready)control_source_complete(source,0,ticket,CONTROL_SOURCE_OUTPUT,!error);
    free(input);
    ESP_LOGI(TAG,"job %u %s%s%s",(unsigned)id,error?"failed":ready?"prepared":"stored/read",error?": ":"",error?error:"");
}
// Only this worker owns file IO and prepared result buffers. A job cannot be
// replaced until terminal, so payload pointers stay valid throughout work.
static void work(uint32_t id)
{
    char error[DDS_ERROR_MAX]="";bool ok=false,ready=false;
    if(!dds_presets_source_valid(id,esp_timer_get_time())){worker_complete(id,"control source revoked or expired",false);return;}
    dds_operation_t op=s_job.status.operation;
    char path[DDS_PATH_MAX];strcpy(path,s_job.status.path);
    unsigned warnings=0,next_offset=0;
    if(op==DDS_PROGRAM) {
        char *banks;size_t len;
        if(read_document("banks.toml",&banks,&len,error)) {
            ok=dds_bank_path(banks,len,s_job.argument>>7,s_job.argument&127,path,error);
            free(banks);
        }
        if(!ok){worker_complete(id,error,false);return;}
    }
    if(op==DDS_LOAD || op==DDS_PROGRAM) {
        ok=load_patch(path,&s_job.patch,&warnings,error);ready=ok;
    } else if(op==DDS_SAVE) {
        int n=dds_patch_text(&s_job.patch,NULL,0);
        char *text=n>0 && n<=DDS_TEXT_MAX?malloc((size_t)n+1):NULL;
        if(n<=0 || n>DDS_TEXT_MAX)fail(error,"cannot serialize DDS snapshot");
        else if(!text)fail(error,"out of memory");
        else if(dds_patch_text(&s_job.patch,text,(size_t)n+1)!=n)fail(error,"cannot serialize DDS snapshot");
        else if(!s_mounted)fail(error,"storage unavailable (no automatic format)");
        else if(!dds_presets_source_valid(id,esp_timer_get_time()))fail(error,"control source revoked or expired");
        else ok=dds_files_save(s_root,s_job.status.path,text,(size_t)n,error);
        free(text);
    } else if(op==DDS_WRITE || op==DDS_IMPORT) {
        if(!s_mounted)fail(error,"storage unavailable (no automatic format)");
        else if(!dds_presets_source_valid(id,esp_timer_get_time()))fail(error,"control source revoked or expired");
        else if(op==DDS_IMPORT)ok=dds_files_import(s_root,s_job.status.path,s_job.input,s_job.input_len,error);
        else ok=dds_files_save(s_root,s_job.status.path,s_job.input,s_job.input_len,error);
    } else if(op==DDS_DEFAULT) {
        patch_t check;unsigned warnings=0;
        if(!s_mounted)fail(error,"storage unavailable (no automatic format)");
        else if(!*s_job.status.path || load_patch(s_job.status.path,&check,&warnings,error)) {
            char text[DDS_PATH_MAX+80];int n=snprintf(text,sizeof(text),"format = 1\nstartup_patch = \"%s\"\n",s_job.status.path);
            if(!dds_presets_source_valid(id,esp_timer_get_time()))fail(error,"control source revoked or expired");
            else ok=dds_files_save(s_root,"system.toml",text,(size_t)n,error);
        }
    } else if(op==DDS_READ) {
        ok=read_document(s_job.status.path,&s_job.output,&s_job.output_len,error);
    } else if(op==DDS_LIST) {
        s_job.output=malloc(DDS_LIST_PAGE_BYTES);
        if(!s_job.output)fail(error,"out of memory");
        else {
            size_t n=0;
            if(!s_job.argument)for(unsigned i=0;i<2;i++)n+=(size_t)snprintf(s_job.output+n,DDS_LIST_PAGE_BYTES-n,"%s\n",s_factory_paths[i]);
            if(s_mounted)ok=dds_files_list(s_root,s_job.argument,s_job.output+n,DDS_LIST_PAGE_BYTES-n,&next_offset,error);
            else {s_job.output[n]=0;ok=true;}
            s_job.output_len=strlen(s_job.output);
        }
    }
    if(op==DDS_WRITE || op==DDS_IMPORT) {
        // The worker no longer needs an upload after validation/persistence.
        // Release it before exposing terminal status, rather than retaining
        // a potentially 16 KiB body until the next accepted request.
        portENTER_CRITICAL(&s_lock);char *consumed=s_job.input;s_job.input=NULL;s_job.input_len=0;portEXIT_CRITICAL(&s_lock);
        free(consumed);
    }
    portENTER_CRITICAL(&s_lock);
    strcpy(s_job.status.path,path);s_job.status.warnings=warnings;s_job.status.next_offset=next_offset;
    portEXIT_CRITICAL(&s_lock);
    if(ok && op==DDS_LIST)ESP_LOGI(TAG,"preset list (next_offset=%u):\n%s",next_offset,s_job.output);
    worker_complete(id,ok?NULL:*error?error:"operation failed",ready);
}
static void init_storage(void)
{
#ifdef ESP_PLATFORM
    static wl_handle_t wl=WL_INVALID_HANDLE;
    const esp_vfs_fat_mount_config_t cfg={.format_if_mount_failed=false,.max_files=4,.allocation_unit_size=512};
    esp_err_t err=esp_vfs_fat_spiflash_mount_rw_wl(s_root,"dds_storage",&cfg,&wl);
    s_mounted=err==ESP_OK;
    if(!s_mounted)ESP_LOGW(TAG,"dds_storage mount: %s; no format; Factory/init remain available",esp_err_to_name(err));
#endif
    char error[DDS_ERROR_MAX];
    if(s_mounted) {
        unsigned repaired=dds_files_recover(s_root);
        if(repaired)ESP_LOGW(TAG,"recovered %u interrupted document operation(s)",repaired);
        if(!dds_files_layout(s_root,error))ESP_LOGW(TAG,"%s",error);
        char *system;size_t len;
        if(read_document("system.toml",&system,&len,error)) {
            char startup[DDS_PATH_MAX];
            if(dds_system_path(system,len,startup,error) && *startup)s_boot_valid=load_patch(startup,&s_boot_patch,NULL,error);
            free(system);
        }
        if(!s_boot_valid)ESP_LOGI(TAG,"using local init patch (startup missing/invalid)");
    }
}
static void worker_task(void *arg)
{
    (void)arg;
#ifdef ESP_PLATFORM
    // The main boot task has only 3584 B. Mount/recovery/TOML parsing run on
    // this worker's dedicated stack; boot waits before starting voice_alloc.
    init_storage();xSemaphoreGive(s_init_ready);
#endif
    uint32_t id;
    while(1)if(xQueueReceive(s_work,&id,portMAX_DELAY)==pdTRUE)work(id);
}
void dds_presets_init(void)
{
#ifndef ESP_PLATFORM
    init_storage(); // host RTOS fixture does not execute scheduled tasks
#else
    s_init_ready=xSemaphoreCreateBinary();
    if(!s_init_ready){ESP_LOGE(TAG,"startup semaphore unavailable");return;}
#endif
    s_work=xQueueCreate(1,sizeof(uint32_t));
    if(!s_work || xTaskCreate(worker_task,"dds_files",DDS_WORKER_STACK_BYTES,NULL,2,NULL)!=pdPASS) {
        s_work=NULL;ESP_LOGE(TAG,"storage worker unavailable");
        return;
    }
#ifdef ESP_PLATFORM
    xSemaphoreTake(s_init_ready,portMAX_DELAY);
#endif
}
bool dds_presets_boot_patch(patch_t *patch)
{if(!s_boot_valid)return false;*patch=s_boot_patch;return true;}
static bool request_job(dds_operation_t op,const char *path,const char *text,char **owned,size_t len,unsigned argument,uint32_t *id,char *error,control_source_ref_t source)
{
    if(!s_work)return fail(error,"storage worker unavailable");
    if(!path)path="";
    if(!strcasecmp(path,"system.toml"))path="system.toml";
    if(!strcasecmp(path,"banks.toml"))path="banks.toml";
    bool settings=!strcmp(path,"system.toml") || !strcmp(path,"banks.toml");
    if(op<DDS_LIST || op>DDS_IMPORT || strlen(path)>=DDS_PATH_MAX || len>DDS_TEXT_MAX ||
       (op==DDS_LOAD && !dds_path_valid(path,false)) || (op==DDS_SAVE && !dds_path_valid(path,true)) ||
       (op==DDS_READ && !settings && !dds_path_valid(path,false)) ||
       (op==DDS_WRITE && (!len || !text || (!settings && !dds_path_valid(path,true)))) ||
       (op==DDS_IMPORT && (!len || !text || !dds_path_valid(path,true))) ||
       (op==DDS_DEFAULT && *path && !dds_path_valid(path,false)) ||
       (op==DDS_PROGRAM && argument>2097151))return fail(error,"invalid DDS request");
    char *input=NULL;
    if(op==DDS_WRITE || op==DDS_IMPORT) {
        input=owned?*owned:malloc(len+1);
        if(!input)return fail(error,"out of memory");
        if(!owned){memcpy(input,text,len);input[len]=0;}
    }
    portENTER_CRITICAL(&s_lock);
    if(active(s_job.status.phase)){portEXIT_CRITICAL(&s_lock);if(!owned)free(input);return fail(error,"preset job busy; retry after completion");}
    int64_t now=esp_timer_get_time();uint32_t ticket=0;
    if(source.epoch && !control_source_reserve_job(source,now,now+CONTROL_SOURCE_LEASE_US,&ticket)) {
        portEXIT_CRITICAL(&s_lock);if(!owned)free(input);return fail(error,"control source busy or revoked");
    }
    char *old_input=s_job.input,*old_output=s_job.output;
    memset(&s_job,0,sizeof(s_job));
    if(++s_next_id==0)s_next_id=1;
    s_job.status.id=*id=s_next_id;s_job.status.phase=DDS_WAIT_OWNER;s_job.status.operation=op;
    s_job.status.context=s_owner_context;
    s_job.status.mounted=s_mounted;strcpy(s_job.status.path,path);
    s_job.argument=argument;s_job.input=input;s_job.input_len=len;
    s_job.source=source;s_job.source_ticket=ticket;s_job.source_expires=source.epoch?now+CONTROL_SOURCE_LEASE_US:0;
    if(owned)*owned=NULL;
    portEXIT_CRITICAL(&s_lock);
    free(old_input);free(old_output);
    evt_t evt={.kind=EVT_DDS_PRESET,.source=source,.dds_id=*id};event_bus_publish(&evt);
    return true;
}
bool dds_presets_request(dds_operation_t op,const char *path,const char *text,size_t len,unsigned argument,uint32_t *id,char *error)
{return request_job(op,path,text,NULL,len,argument,id,error,(control_source_ref_t){0});}
bool dds_presets_request_owned(dds_operation_t op,const char *path,char **text,size_t len,unsigned argument,uint32_t *id,char *error)
{
    if(!text || !*text || (op!=DDS_WRITE && op!=DDS_IMPORT))return fail(error,"invalid owned DDS request");
    return request_job(op,path,*text,text,len,argument,id,error,(control_source_ref_t){0});
}
bool dds_presets_request_source(control_source_ref_t source,dds_operation_t op,const char *path,
                                const char *text,size_t len,unsigned argument,uint32_t *id,char *error)
{
    if(!source.epoch || !control_source_valid(source))return fail(error,"control source revoked");
    return request_job(op,path,text,NULL,len,argument,id,error,source);
}
bool dds_presets_request_owned_source(control_source_ref_t source,dds_operation_t op,const char *path,
                                      char **text,size_t len,unsigned argument,uint32_t *id,char *error)
{
    if(!text || !*text || (op!=DDS_WRITE && op!=DDS_IMPORT) || !source.epoch || !control_source_valid(source))
        return fail(error,"control source revoked or invalid owned DDS request");
    return request_job(op,path,*text,text,len,argument,id,error,source);
}
void dds_presets_status(dds_job_status_t *status)
{portENTER_CRITICAL(&s_lock);*status=s_job.status;status->mounted=s_mounted;portEXIT_CRITICAL(&s_lock);}
char *dds_presets_result(uint32_t id,size_t *len)
{
    size_t size=0;portENTER_CRITICAL(&s_lock);
    bool available=s_job.status.id==id && s_job.status.phase==DDS_DONE && s_job.output;
    if(available)size=s_job.output_len;
    portEXIT_CRITICAL(&s_lock);
    if(!available)return NULL;
    char *copy=malloc(size+1);if(!copy)return NULL;
    portENTER_CRITICAL(&s_lock);
    available=s_job.status.id==id && s_job.status.phase==DDS_DONE && s_job.output && s_job.output_len==size;
    if(available)memcpy(copy,s_job.output,size+1);
    portEXIT_CRITICAL(&s_lock);
    if(!available){free(copy);return NULL;}
    *len=size;return copy;
}
uint32_t dds_presets_waiting(void)
{
    portENTER_CRITICAL(&s_lock);uint32_t id=s_job.status.phase==DDS_WAIT_OWNER?s_job.status.id:0;portEXIT_CRITICAL(&s_lock);return id;
}
void dds_presets_owner_context(uint32_t context)
{portENTER_CRITICAL(&s_lock);s_owner_context=context;portEXIT_CRITICAL(&s_lock);}
void dds_presets_owner_begin(uint32_t id,const patch_t *snapshot,uint32_t context,bool dds)
{
    if(!dds_presets_source_valid(id,esp_timer_get_time())) {
        dds_presets_owner_cancel(id,context,"control source revoked or expired");return;
    }
    bool send=false;portENTER_CRITICAL(&s_lock);
    if(id==s_job.status.id && s_job.status.phase==DDS_WAIT_OWNER) {
        bool music=s_job.status.operation==DDS_LOAD || s_job.status.operation==DDS_SAVE || s_job.status.operation==DDS_PROGRAM;
        if(music && (!dds || s_job.status.context!=context)){s_job.status.phase=DDS_FAILED;strcpy(s_job.status.error,"DDS unavailable or request context expired");}
        else {s_job.patch=*snapshot;s_job.status.context=context;s_job.status.phase=DDS_WORKING;send=true;}
    }
    portEXIT_CRITICAL(&s_lock);
    if(!send) {
        portENTER_CRITICAL(&s_lock);
        bool failed=s_job.status.id==id && s_job.status.phase==DDS_FAILED;
        control_source_ref_t source=s_job.source;uint32_t ticket=s_job.source_ticket;
        portEXIT_CRITICAL(&s_lock);
        if(failed)control_source_complete(source,0,ticket,CONTROL_SOURCE_OUTPUT,false);
    } else if(xQueueSend(s_work,&id,0)!=pdTRUE)worker_complete(id,"worker queue unavailable",false);
}
bool dds_presets_ready(uint32_t *id,patch_t *patch,uint32_t *context)
{
    portENTER_CRITICAL(&s_lock);bool ready=s_job.status.phase==DDS_READY;
    if(ready){*id=s_job.status.id;*patch=s_job.patch;*context=s_job.status.context;}
    portEXIT_CRITICAL(&s_lock);return ready;
}
void dds_presets_owner_closing(uint32_t id)
{portENTER_CRITICAL(&s_lock);if(s_job.status.id==id && s_job.status.phase==DDS_READY)s_job.status.phase=DDS_CLOSING;portEXIT_CRITICAL(&s_lock);}
void dds_presets_owner_complete(uint32_t id,uint32_t context,const char *error)
{
    control_source_ref_t source={0};uint32_t ticket=0;
    portENTER_CRITICAL(&s_lock);
    if(s_job.status.id==id && (s_job.status.phase==DDS_READY || s_job.status.phase==DDS_CLOSING)) {
        source=s_job.source;ticket=s_job.source_ticket;
        s_job.status.phase=error?DDS_FAILED:DDS_DONE;s_job.status.context=context;
        if(error)snprintf(s_job.status.error,sizeof(s_job.status.error),"%s",error);
    }
    portEXIT_CRITICAL(&s_lock);
    control_source_complete(source,0,ticket,CONTROL_SOURCE_OUTPUT,!error);
    ESP_LOGI(TAG,"job %u %s",(unsigned)id,error?error:"owner adopted DDS patch (not a DAC acknowledgement)");
}
void dds_presets_owner_cancel(uint32_t id,uint32_t context,const char *error)
{
    control_source_ref_t source={0};uint32_t ticket=0;char *input=NULL;
    portENTER_CRITICAL(&s_lock);
    if(s_job.status.id==id && active(s_job.status.phase)) {
        source=s_job.source;ticket=s_job.source_ticket;s_job.source_cancelled=true;s_job.status.context=context;
        snprintf(s_job.status.error,sizeof(s_job.status.error),"%s",error);
        // WORKING retains its payload and active slot until the worker exits.
        if(s_job.status.phase!=DDS_WORKING) {
            s_job.status.phase=DDS_FAILED;input=s_job.input;s_job.input=NULL;s_job.input_len=0;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    control_source_complete(source,0,ticket,CONTROL_SOURCE_OUTPUT,false);free(input);
}
bool dds_presets_console_byte(uint8_t byte)
{
    static char line[DDS_PATH_MAX+24];static unsigned used;static bool input,overflow;
    if(!input){if(byte!=':')return false;input=true;used=0;overflow=false;return true;}
    if(byte=='\r' || byte=='\n') {
        input=false;line[used]=0;
        if(overflow){ESP_LOGW(TAG,"console command too long");return true;}
        dds_operation_t op;char *path="";
        if(!strcmp(line,"dds list"))op=DDS_LIST;
        else if(!strncmp(line,"dds load ",9)){op=DDS_LOAD;path=line+9;}
        else if(!strncmp(line,"dds save ",9)){op=DDS_SAVE;path=line+9;}
        else if(!strncmp(line,"dds default ",12)){op=DDS_DEFAULT;path=line+12;}
        else {ESP_LOGI(TAG,":dds list | :dds load <path> | :dds save Patches/User/<category>/<name>.toml | :dds default <path>");return true;}
        char error[DDS_ERROR_MAX];uint32_t id;
        if(dds_presets_request(op,path,NULL,0,0,&id,error))ESP_LOGI(TAG,"accepted DDS job %u",(unsigned)id);
        else ESP_LOGW(TAG,"%s",error);
        return true;
    }
    if(byte==8 || byte==127){if(used)used--;return true;}
    if(used+1<sizeof(line))line[used++]=(char)byte;else overflow=true;
    return true;
}
