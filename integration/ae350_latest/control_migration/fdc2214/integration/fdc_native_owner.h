/* SPDX-License-Identifier: MIT */
#ifndef FDC_NATIVE_OWNER_H
#define FDC_NATIVE_OWNER_H
#include "control_service.h"
#include "../firmware/fdc_ae350.h"

#if ENABLE_AE350_FDC
/* A broker into the existing service and allocator. It neither writes APB
 * command registers nor allocates a separate voice pool. Call serially from
 * the same cooperative loop as cs_poll. Runtime FDC starts suspended. */
typedef struct {
    cs_service_t *service;
    vm_sensor_handle_t handle;
    fdc_ae350_control_t sending;
    uint32_t epoch, applied, cancelled;
    uint8_t have_handle, pending, stopping, suspended, epoch_restart;
} fdc_native_owner_t;
void fdc_native_owner_init(fdc_native_owner_t *owner, cs_service_t *service);
/* Use as the driver's publish callback, with a small application wrapper if
 * its other IO callbacks need another context. 1 means final native ACK was
 * consumed by the real service; 0 means retry identical control; -2 means
 * cancelled after actual stop/identity replacement, and -1 is a local FDC
 * failure. The driver never counts -2 as applied or writes its CTRL COMMIT.
 * This broker does not set the global service fatal flag. */
int fdc_native_publish(void *context, const fdc_ae350_control_t *control);
/* Repeatedly call while cs_poll continues. Completion requires real KILL ACK
 * or proof that the old identity has already been replaced. */
int fdc_native_stop(fdc_native_owner_t *owner);
int fdc_native_resume(fdc_native_owner_t *owner);
#endif
#endif
