// Copyright (c) 2026 ICECREAM1230
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
#pragma once
#include "dds_document.h"
#include "control_source.h"

typedef enum { DDS_LIST=1,DDS_LOAD,DDS_SAVE,DDS_READ,DDS_WRITE,DDS_DEFAULT,DDS_PROGRAM,DDS_IMPORT } dds_operation_t;
typedef enum { DDS_IDLE,DDS_WAIT_OWNER,DDS_WORKING,DDS_READY,DDS_CLOSING,DDS_DONE,DDS_FAILED } dds_phase_t;
typedef struct {
    uint32_t id,context;
    dds_operation_t operation;
    dds_phase_t phase;
    bool mounted;
    unsigned warnings,next_offset;
    char path[DDS_PATH_MAX],error[DDS_ERROR_MAX];
} dds_job_status_t;

// Initialization is before voice_alloc starts. Mount failure never formats.
void dds_presets_init(void);
bool dds_presets_boot_patch(patch_t *patch);
// Non-blocking single-flight acceptance. Input is copied, no IO here.
// argument is list offset, or bank*128+program for DDS_PROGRAM.
bool dds_presets_request(dds_operation_t op,const char *path,const char *text,
                         size_t len,unsigned argument,uint32_t *id,char *error);
// Accepted import takes the already received heap text and NULLs *text.
// Rejection leaves ownership with the caller; no second document allocation.
bool dds_presets_request_owned(const char *path,char **text,size_t len,
                               uint32_t *id,char *error);
// Create-only User import; ownership rules are identical to WRITE above.
bool dds_presets_import_owned(const char *path,char **text,size_t len,
                              uint32_t *id,char *error);
// External writes retain an immutable transport fence through worker/owner
// completion. The no-source entry points above are for local console/MIDI
// and read-only list/read, plus explicit host compatibility fixtures.
bool dds_presets_request_source(control_source_ref_t source,dds_operation_t op,
                                const char *path,const char *text,size_t len,
                                unsigned argument,uint32_t *id,char *error);
bool dds_presets_request_owned_source(control_source_ref_t source,const char *path,
                                      char **text,size_t len,uint32_t *id,char *error);
bool dds_presets_import_owned_source(control_source_ref_t source,const char *path,
                                     char **text,size_t len,uint32_t *id,char *error);
void dds_presets_status(dds_job_status_t *status);
// One immutable result view may be held at a time (single HTTP owner).
// A new job can retire the cache while this view remains valid. Release once.
typedef struct {const char *text;size_t len;void *private_result;} dds_result_view_t;
bool dds_presets_result_acquire(uint32_t id,dds_result_view_t *view);
void dds_presets_result_release(dds_result_view_t *view);
// Compatibility copy helper; HTTP uses the view, avoiding a full extra copy.
char *dds_presets_result(uint32_t id,size_t *len);
// Only the voice owner calls these; IO worker never changes patch/SPI.
uint32_t dds_presets_waiting(void);
void dds_presets_owner_context(uint32_t context);
void dds_presets_owner_begin(uint32_t id,const patch_t *snapshot,uint32_t context,bool dds);
bool dds_presets_ready(uint32_t *id,patch_t *patch,uint32_t *context);
void dds_presets_owner_closing(uint32_t id);
void dds_presets_owner_complete(uint32_t id,uint32_t context,const char *error);
// An internal/local job has no transport fence. An external job must still
// match both its source generation and its original 1200 ms transaction.
bool dds_presets_source_valid(uint32_t id,int64_t now);
void dds_presets_owner_cancel(uint32_t id,uint32_t context,const char *error);
// Shared USB console reader feeds ':dds ...\n', existing one-key commands remain.
bool dds_presets_console_byte(uint8_t byte);
