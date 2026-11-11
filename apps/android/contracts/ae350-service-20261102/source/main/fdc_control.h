#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "event_bus.h"
#define FDC_RAW_MAGIC_REG UINT16_C(0x0730)
#define FDC_RAW_DATA_REG UINT16_C(0x0731)
#define FDC_RAW_MAGIC UINT32_C(0x46445231)
#define FDC_STATUS_REG UINT16_C(0x071f)
#define FDC_STATUS_TAG UINT32_C(0xfdc10000)
#define FDC_FRESH_US INT64_C(500000)
#define FDC_HTTP_BODY_MAX 128u
enum { FDC_DISABLE=0,FDC_ENABLE=1,FDC_RESTART=2,FDC_CALIBRATE=3 };
enum { FDC_SENT=0,FDC_CANCELLED=1,FDC_UNKNOWN=2,FDC_FAILED=3 };
typedef struct {web_v2_event_t event;uint32_t op,words[6];} fdc_request_t;
typedef struct {
    bool supported,valid,fresh,sample_valid,sample_fresh;
    bool enabled,muted,pitch_stale,volume_stale;
    uint32_t revision,sequence,raw0,raw1,gain_q16,quality0,quality1;
    int64_t observed_us;
} engine_fdc_raw_state_t;
typedef struct {
    engine_fdc_raw_state_t raw;
    bool valid,available,fresh,pending,disable_pending;
    uint32_t word,request_id,completed_request_id,result;
    int64_t observed_us;
} engine_fdc_state_t;
typedef struct {uint32_t token,sequence,context_epoch,request_id,op,code;} fdc_result_t;
typedef struct {engine_fdc_state_t link;bool pending;fdc_result_t request,result;} web_fdc_state_t;
bool fdc_request_valid(const fdc_request_t *request);
bool fdc_control_reserve(fdc_request_t *request,int64_t now);
bool fdc_control_claim(const web_v2_event_t *event,fdc_request_t *request);
void fdc_control_release(uint32_t id);
bool fdc_control_cancel_reserved(fdc_request_t *request);
void fdc_control_state_read(web_fdc_state_t *state);
void fdc_control_state_publish(const web_fdc_state_t *state);
void engine_link_fdc_state(engine_fdc_state_t *state);
bool engine_link_fdc_request(const fdc_request_t *request);
bool engine_link_fdc_request_source(control_source_ref_t source,const fdc_request_t *request);
/* RAM-only. Global STOP is separate from ordinary source-local MIDI flush. */
void engine_link_fdc_cancel(uint32_t request_id,bool disable);
void engine_link_fdc_stop(void);
