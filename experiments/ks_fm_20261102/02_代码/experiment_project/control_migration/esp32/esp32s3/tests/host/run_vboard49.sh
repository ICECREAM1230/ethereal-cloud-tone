#!/bin/bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo"
out=${1:?Provide a task-specific output directory outside the repository}
mkdir -p "$out"
cc -std=c17 -Wall -Wextra -Werror -Wno-format-truncation -DVBOARD49_FIELDS \
    -Itests/host/stubs -Imain tests/host/test_vboard49_expression.c \
    main/midi_templates.c main/ks_params.c main/patch.c main/dds_document.c \
    main/dds_patch_units.c main/vendor/tomlc17/tomlc17.c -lm -o "$out/templates"
"$out/templates"
