#!/bin/bash
# Public DDS document API depth/quote boundaries with actual vendor entry count.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo"
scratch=${S3_SIM_TMPDIR:-'/media/ice/D;/FPGA_simulation/ae350_fpga_menu_lab_20261028/dds_depth_host'}
mkdir -p "$scratch"
out=$(mktemp -d "$scratch/dds-depth.XXXXXX")
alias_dir=''
cleanup() {
    if [[ -n "$alias_dir" && -L "$alias_dir" && "$(readlink "$alias_dir")" == "$out/doc-root" ]]; then
        rm "$alias_dir"
    fi
    rm -rf "$out"
}
trap cleanup EXIT
source_file=${S3_DDS_DEPTH_DOCUMENT:-main/dds_document.c}
flags=(-std=c17 -Og -g -Wall -Wextra -Werror
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Imain)
cc "${flags[@]}" tests/host/test_dds_depth.c "$source_file" main/dds_patch_units.c \
    main/patch.c main/vendor/tomlc17/tomlc17.c -Wl,--wrap=toml_parse -lm \
    -o "$out/dds_depth"
status=0
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$out/dds_depth" || status=$?

# Existing compatibility test retains all original assertions and bytes. Its
# fixed-length /tmp template is redirected through a short task-owned symlink;
# all actual documents stay beneath the project scratch directory above.
mkdir "$out/doc-root"
alias_dir=$(mktemp -d /tmp/d.XXXXXX)
rmdir "$alias_dir"
ln -s "$out/doc-root" "$alias_dir"
cat > "$out/mkdtemp-wrapper.c" <<'WRAPPER'
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
char *__real_mkdtemp(char *template);
char *__wrap_mkdtemp(char *template)
{
    const char *base = getenv("S3_DDS_DOC_SHORT_TMP");
    if (!base) abort();
    size_t capacity = strlen(template) + 1;
    int n = snprintf(template, capacity, "%s/XXXXXX", base);
    if (n < 0 || (size_t)n >= capacity) abort();
    return __real_mkdtemp(template);
}
WRAPPER
cc "${flags[@]}" -Wno-format-truncation tests/host/test_dds_documents.c \
    "$source_file" main/dds_patch_units.c main/dds_files.c main/patch.c \
    main/vendor/tomlc17/tomlc17.c "$out/mkdtemp-wrapper.c" \
    -Wl,--wrap=mkdtemp -lm -o "$out/dds_documents"
S3_DDS_DOC_SHORT_TMP="$alias_dir" ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1 "$out/dds_documents" || status=$?
exit "$status"
