#include "fdc_ranges.h"
#include <string.h>

static void increment(uint64_t *value) { if (*value!=UINT64_MAX) ++*value; }
void fdc_ranges_clear(fdc_ranges_t *r)
{
    uint64_t next=r->epoch==UINT64_MAX ? UINT64_MAX:r->epoch+1;
    memset(r,0,sizeof(*r));r->epoch=next;
}
void fdc_ranges_io_error(fdc_ranges_t *r)
{
    increment(&r->transport_errors);
    for (unsigned ch=0;ch<FDC_CHANNELS;ch++) {
        increment(&r->channel[ch].error_count);r->channel[ch].last_valid=false;
    }
}
bool fdc_ranges_accept(fdc_ranges_t *r, const fdc_frame_t *f)
{
    if (f->read_mask!=0x0F) {fdc_ranges_io_error(r);return false;}
    increment(&r->frames);
    for (unsigned ch=0;ch<FDC_CHANNELS;ch++) {
        fdc_channel_range_t *s=&r->channel[ch];
        s->last_raw=f->raw[ch];
        s->last_valid=(f->valid_mask&(1u<<ch)) && !f->flags[ch] && !f->status_errors &&
            !(f->ratio_error_mask&(1u<<ch)) && f->raw[ch]<UINT32_C(0x04000000);
        if (!s->last_valid) {increment(&s->error_count);continue;}
        if (!s->has_range) {s->minimum=s->maximum=f->raw[ch];s->has_range=true;}
        if (f->raw[ch]<s->minimum) s->minimum=f->raw[ch];
        if (f->raw[ch]>s->maximum) s->maximum=f->raw[ch];
        increment(&s->valid_count);
    }
    return true;
}
uint32_t fdc_range_span(const fdc_channel_range_t *s) {return s->has_range ? s->maximum-s->minimum:0;}
