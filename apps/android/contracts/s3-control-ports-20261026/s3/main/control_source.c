#include "control_source.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

#define CONTROL_SOURCE_OUTPUTS 8u
typedef struct {
    uint32_t token, sequence;
    int64_t expires;
    bool raw;
} performance_t;
typedef struct {uint32_t token,sequence;int64_t expires;} output_t;
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static control_source_ref_t s_source;
static uint32_t s_next_epoch=1;
static performance_t s_performance[CONTROL_SOURCE_CLIENTS];
/* The final output slot belongs exclusively to anonymous DDS jobs. */
static output_t s_outputs[CONTROL_SOURCE_OUTPUTS+1];

bool control_source_same(control_source_ref_t a,control_source_ref_t b)
{return a.kind==b.kind && a.epoch==b.epoch;}
static bool valid_locked(control_source_ref_t source)
{return source.epoch && source.kind!=CONTROL_SOURCE_NONE && control_source_same(source,s_source);}
static void clear_holds(void)
{memset(s_performance,0,sizeof(s_performance));memset(s_outputs,0,sizeof(s_outputs));}
static void expire_locked(int64_t now)
{
    for(unsigned i=0;i<CONTROL_SOURCE_CLIENTS;i++)
        if(s_performance[i].expires && now>=s_performance[i].expires)
            memset(&s_performance[i],0,sizeof(s_performance[i]));
    for(unsigned i=0;i<=CONTROL_SOURCE_OUTPUTS;i++)
        if(s_outputs[i].expires && now>=s_outputs[i].expires)
            memset(&s_outputs[i],0,sizeof(s_outputs[i]));
}
void control_source_init(void)
{
    portENTER_CRITICAL(&s_lock);
    s_source=(control_source_ref_t){0};s_next_epoch=1;clear_holds();
    portEXIT_CRITICAL(&s_lock);
}
bool control_source_claim(control_source_kind_t kind,int64_t now,control_source_ref_t *out)
{
    if((kind!=CONTROL_SOURCE_WIFI && kind!=CONTROL_SOURCE_BLE_CTRL) || now<0 || !out)return false;
    bool ok=false;
    portENTER_CRITICAL(&s_lock);
    expire_locked(now);
    if(s_source.kind==kind && s_source.epoch)ok=true;
    else {
        bool held=false;
        for(unsigned i=0;i<CONTROL_SOURCE_CLIENTS;i++)held|=s_performance[i].expires!=0;
        for(unsigned i=0;i<=CONTROL_SOURCE_OUTPUTS;i++)held|=s_outputs[i].expires!=0;
        if(!held && s_next_epoch) {
            s_source=(control_source_ref_t){.kind=kind,.epoch=s_next_epoch++};
            clear_holds();ok=true;
        }
    }
    if(ok)*out=s_source;
    portEXIT_CRITICAL(&s_lock);return ok;
}
bool control_source_valid(control_source_ref_t source)
{
    portENTER_CRITICAL(&s_lock);bool valid=valid_locked(source);
    portEXIT_CRITICAL(&s_lock);return valid;
}
void control_source_revoke(control_source_ref_t source)
{
    portENTER_CRITICAL(&s_lock);
    if(valid_locked(source)){s_source=(control_source_ref_t){0};clear_holds();}
    portEXIT_CRITICAL(&s_lock);
}
bool control_source_reserve(control_source_ref_t source,uint32_t token,uint32_t sequence,
                            int64_t now,int64_t expires,control_source_activity_t activity)
{
    if(!sequence || now<0 || expires<=now || expires-now>CONTROL_SOURCE_LEASE_US ||
       (unsigned)activity>CONTROL_SOURCE_RAW || (!token && activity!=CONTROL_SOURCE_OUTPUT))return false;
    bool ok=false;
    portENTER_CRITICAL(&s_lock);
    expire_locked(now);
    if(!valid_locked(source))goto done;
    if(activity==CONTROL_SOURCE_IDLE){ok=true;goto done;}
    if(activity==CONTROL_SOURCE_OUTPUT) {
        unsigned first=token?0:CONTROL_SOURCE_OUTPUTS;
        unsigned last=token?CONTROL_SOURCE_OUTPUTS:CONTROL_SOURCE_OUTPUTS+1;
        output_t *free_slot=NULL;
        for(unsigned i=first;i<last;i++) {
            if(s_outputs[i].expires && s_outputs[i].token==token && s_outputs[i].sequence==sequence) {
                ok=true;goto done;
            }
            if(!s_outputs[i].expires && !free_slot)free_slot=&s_outputs[i];
        }
        if(free_slot){*free_slot=(output_t){.token=token,.sequence=sequence,.expires=expires};ok=true;}
    } else {
        performance_t *slot=NULL,*free_slot=NULL;
        for(unsigned i=0;i<CONTROL_SOURCE_CLIENTS;i++) {
            if(s_performance[i].expires && s_performance[i].token==token)slot=&s_performance[i];
            if(!s_performance[i].expires && !free_slot)free_slot=&s_performance[i];
        }
        if(!slot)slot=free_slot;
        if(slot && (!slot->expires || sequence>=slot->sequence)) {
            bool raw=slot->expires && slot->raw;
            *slot=(performance_t){.token=token,.sequence=sequence,.expires=expires,
                .raw=raw || activity==CONTROL_SOURCE_RAW};ok=true;
        }
    }
done:
    portEXIT_CRITICAL(&s_lock);return ok;
}
void control_source_release(control_source_ref_t source,uint32_t token,uint32_t sequence)
{
    if(!token)return;
    portENTER_CRITICAL(&s_lock);
    if(valid_locked(source))for(unsigned i=0;i<CONTROL_SOURCE_CLIENTS;i++)
        if(s_performance[i].token==token && sequence>=s_performance[i].sequence)
            memset(&s_performance[i],0,sizeof(s_performance[i]));
    portEXIT_CRITICAL(&s_lock);
}
void control_source_complete(control_source_ref_t source,uint32_t token,uint32_t sequence,
                             control_source_activity_t activity,bool accepted)
{
    portENTER_CRITICAL(&s_lock);
    if(!valid_locked(source))goto done;
    if(activity==CONTROL_SOURCE_OUTPUT) {
        for(unsigned i=0;i<=CONTROL_SOURCE_OUTPUTS;i++)
            if(s_outputs[i].token==token && s_outputs[i].sequence==sequence)
                memset(&s_outputs[i],0,sizeof(s_outputs[i]));
    } else if(token && ((activity==CONTROL_SOURCE_IDLE && accepted) ||
                       ((activity==CONTROL_SOURCE_KEYS || activity==CONTROL_SOURCE_RAW) && !accepted))) {
        for(unsigned i=0;i<CONTROL_SOURCE_CLIENTS;i++)
            if(s_performance[i].token==token && sequence>=s_performance[i].sequence &&
               (activity!=CONTROL_SOURCE_IDLE || !s_performance[i].raw))
                memset(&s_performance[i],0,sizeof(s_performance[i]));
    }
done:
    portEXIT_CRITICAL(&s_lock);
}
