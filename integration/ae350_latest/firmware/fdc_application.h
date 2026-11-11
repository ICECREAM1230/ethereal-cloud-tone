/* SPDX-License-Identifier: MIT */
#ifndef FDC_APPLICATION_H
#define FDC_APPLICATION_H
#include <stdint.h>
#include "control_service.h"

enum {
    FDC_APP_BAD_VALUE=1u, FDC_APP_BUSY=2u, FDC_APP_NOT_READY=4u,
    FDC_APP_BAD_CONFIG=8u, FDC_APP_DRIVER_FAULT=16u
};
typedef struct {
    uint32_t accepted, rejected, last_error, applied_configs, restarts;
    uint8_t initialized, requested_enable, operation, staging_mask;
} fdc_application_stats_t;

/* Single owner: initialize after cs_init, call poll once after cs_poll and
 * physical-key handling. Configuration records only queue bounded work.
 * Runtime performance starts disabled and must be explicitly re-enabled
 * after leaving FM, replacing its voice, applying calibration or restarting.
 * The FPGA wall clock is milliseconds; it is extended to monotonic64 us,
 * retaining1ms resolution. Calls must occur at least once per32bit wrap. */
int fdc_application_init(cs_service_t *service,uint32_t now_ms);
int fdc_application_record(void *context,uint16_t address,uint32_t data);
void fdc_application_poll(uint32_t now_ms);
const fdc_application_stats_t *fdc_application_stats(void);
uint32_t fdc_application_status_word(void);
/* RAM-only diagnostics. Gain is the algorithm output, not a native ACK. */
void fdc_application_telemetry(uint32_t words[3]);
#endif
