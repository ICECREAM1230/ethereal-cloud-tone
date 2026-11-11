#ifndef FDC_RANGES_H
#define FDC_RANGES_H
#include "fdc2214.h"
typedef struct {
    uint64_t valid_count, error_count;
    uint32_t minimum, maximum, last_raw;
    bool has_range, last_valid;
} fdc_channel_range_t;
typedef struct {
    uint64_t epoch, frames, transport_errors;
    fdc_channel_range_t channel[FDC_CHANNELS];
} fdc_ranges_t;
void fdc_ranges_clear(fdc_ranges_t *ranges);
bool fdc_ranges_accept(fdc_ranges_t *ranges, const fdc_frame_t *frame);
void fdc_ranges_io_error(fdc_ranges_t *ranges);
uint32_t fdc_range_span(const fdc_channel_range_t *channel);
#endif
