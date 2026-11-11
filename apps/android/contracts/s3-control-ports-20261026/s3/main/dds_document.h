// Copyright (c) 2026 ICECREAM1230
// DDS TOML documents; derived from Ethereal Cloud Tone storage_format/patch_file.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "patch.h"

#define DDS_PATH_MAX 192
#define DDS_TEXT_MAX 16384
#define DDS_ERROR_MAX 160
#define DDS_FORMAT 1

// Snapshot filter.cutoff carries signed owner bus offset; resonance is
// the effective base+CC71 value. Never pass a live shared patch to file IO.
bool dds_patch_parse(const char *text, size_t len, patch_t *patch,
                     unsigned *warnings, char *error);
int dds_patch_text(const patch_t *patch, char *text, size_t size);
bool dds_name_valid(const char *name);
bool dds_path_valid(const char *path, bool user_only);
bool dds_document_validate(const char *path, const char *text, size_t len,
                           char *error);
bool dds_system_path(const char *text, size_t len, char *path, char *error);
bool dds_bank_path(const char *text, size_t len, unsigned bank, unsigned program,
                   char *path, char *error);
static inline void dds_bank_select(uint16_t banks[16],uint8_t channel,uint8_t cc,uint8_t value)
{
    if(channel>=16 || value>127)return;
    if(cc==0)banks[channel]=(uint16_t)((banks[channel]&127)|((uint16_t)value<<7));
    else if(cc==32)banks[channel]=(uint16_t)((banks[channel]&0x3F80)|value);
}
