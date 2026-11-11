#include "fdc_control.h"
#include <math.h>
#include <string.h>
static portMUX_TYPE fdc_lock=portMUX_INITIALIZER_UNLOCKED;
static fdc_request_t reservation;
static web_fdc_state_t state;
static uint32_t next_id=1;
bool fdc_request_valid(const fdc_request_t *r)
{
    if(!r || r->op>FDC_CALIBRATE)return false;
    if(r->op!=FDC_CALIBRATE) {
        for(unsigned i=0;i<6;i++)if(r->words[i])return false;
        return true;
    }
    for(unsigned i=0;i<4;i++)if(!r->words[i] || r->words[i]>=UINT32_C(0x04000000))return false;
    for(unsigned i=0;i<2;i++) {
        uint32_t bits=r->words[4+i];float sigma;
        if((bits&UINT32_C(0x80000000)) || (bits&UINT32_C(0x7f800000))==UINT32_C(0x7f800000))return false;
        memcpy(&sigma,&bits,sizeof sigma);
        uint32_t a=r->words[2*i],b=r->words[2*i+1],span=a>b?a-b:b-a;
        if(span<64u || !isfinite(sigma) || (float)span<20.0f*sigma)return false;
    }
    return true;
}
bool fdc_control_reserve(fdc_request_t *r,int64_t now)
{
    if(!fdc_request_valid(r) || !r->event.token || !r->event.sequence || !r->event.context_epoch ||
       r->event.client>=4 || r->event.c || now<0 || r->event.expires<=now || r->event.expires-now>1200000LL)return false;
    portENTER_CRITICAL(&fdc_lock);bool ok=!reservation.event.c && next_id;
    if(ok){r->event.c=next_id++;reservation=*r;}
    portEXIT_CRITICAL(&fdc_lock);return ok;
}
bool fdc_control_claim(const web_v2_event_t *e,fdc_request_t *out)
{
    if(!e || !out || !e->c)return false;
    portENTER_CRITICAL(&fdc_lock);const web_v2_event_t *r=&reservation.event;
    bool ok=r->c==e->c && r->token==e->token && r->sequence==e->sequence &&
        r->context_epoch==e->context_epoch && r->client==e->client && r->expires==e->expires;
    if(ok)*out=reservation;
    portEXIT_CRITICAL(&fdc_lock);return ok;
}
void fdc_control_release(uint32_t id)
{portENTER_CRITICAL(&fdc_lock);if(id && reservation.event.c==id)memset(&reservation,0,sizeof reservation);portEXIT_CRITICAL(&fdc_lock);}
void fdc_control_state_read(web_fdc_state_t *out)
{portENTER_CRITICAL(&fdc_lock);*out=state;portEXIT_CRITICAL(&fdc_lock);}
void fdc_control_state_publish(const web_fdc_state_t *in)
{portENTER_CRITICAL(&fdc_lock);state=*in;portEXIT_CRITICAL(&fdc_lock);}

bool fdc_control_cancel_reserved(fdc_request_t *out)
{
    portENTER_CRITICAL(&fdc_lock);bool found=reservation.event.c!=0;
    if(found){*out=reservation;memset(&reservation,0,sizeof reservation);}
    portEXIT_CRITICAL(&fdc_lock);return found;
}
