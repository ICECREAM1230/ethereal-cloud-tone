// Copyright (c) 2026 ICECREAM1230
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
#pragma once
#include "dds_document.h"

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
// WRITE/IMPORT only. On acceptance transfer *text and set it to NULL; rejected
// requests leave the caller's allocation untouched. Buffer has len+1 bytes.
bool dds_presets_request_owned(dds_operation_t op,const char *path,char **text,
                               size_t len,unsigned argument,uint32_t *id,char *error);
void dds_presets_status(dds_job_status_t *status);
// A consistent heap copy of a finished export/list. Caller frees it.
char *dds_presets_result(uint32_t id,size_t *len);
// Only the voice owner calls these; IO worker never changes patch/SPI.
uint32_t dds_presets_waiting(void);
void dds_presets_owner_context(uint32_t context);
void dds_presets_owner_begin(uint32_t id,const patch_t *snapshot,uint32_t context,bool dds);
bool dds_presets_ready(uint32_t *id,patch_t *patch,uint32_t *context);
void dds_presets_owner_closing(uint32_t id);
void dds_presets_owner_complete(uint32_t id,uint32_t context,const char *error);
// Shared USB console reader feeds ':dds ...\n', existing one-key commands remain.
bool dds_presets_console_byte(uint8_t byte);
