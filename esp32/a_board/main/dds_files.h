// Copyright (c) 2026 ICECREAM1230
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
#pragma once
#include "dds_document.h"

// Worker-only POSIX IO. Returned read text belongs to caller.
bool dds_files_read(const char *root,const char *path,char **text,size_t *len,char *error);
bool dds_files_save(const char *root,const char *path,const char *text,size_t len,char *error);
// Single file-worker transaction: never replace final or recovery documents.
bool dds_files_import(const char *root,const char *path,const char *text,size_t len,char *error);
unsigned dds_files_recover(const char *root);
bool dds_files_layout(const char *root,char *error);
// List one page as newline-separated relative paths, builtins are separate.
bool dds_files_list(const char *root,unsigned offset,char *text,size_t size,
                    unsigned *next,char *error);
