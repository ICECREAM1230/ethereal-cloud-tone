#!/bin/bash
# CPU-only protocol/owner regression; never opens or resets hardware.
set -euo pipefail
ulimit -c 0
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
out=$(mktemp -d /tmp/s3-local-engine.XXXXXX)
trap 'rm -rf "$out"' EXIT
common=(-std=c11 -Wall -Wextra -Werror -Itests/host/stubs -Imain)
if [[ ${SANITIZE:-0} == 1 ]]; then
    common+=(-g -fsanitize=undefined -fno-omit-frame-pointer)
fi
cc "${common[@]}" tests/host/fake_queue.c tests/host/fake_runtime.c \
    tests/host/test_local_engine.c tests/host/fake_performance_link.c \
    main/event_bus.c main/control_source.c -o "$out/engine"
"$out/engine"
cc "${common[@]}" -DREAL_RAW_MIDI tests/host/fake_queue.c tests/host/fake_runtime.c \
    tests/host/test_local_transport.c tests/host/fake_performance_link.c main/raw_midi.c \
    main/event_bus.c main/control_source.c -o "$out/transport"
"$out/transport"
cc "${common[@]}" tests/host/fake_queue.c tests/host/fake_runtime.c \
    tests/host/test_local_owner.c main/event_bus.c main/patch.c tests/host/fake_dds_presets.c \
    main/web_control.c main/control_source.c main/ks_params.c main/ks_voice.c main/fm_voice.c tests/host/fake_fm_engine.c \
    tests/host/fake_ks_engine.c main/web_raw_queue.c main/midi_templates.c main/parameter_cc.c \
    -lm -o "$out/owner"
"$out/owner"
# Complete remote acquisition with the actual three-engine service. The wire
# exposes no additive ACK; any access to the removed hardware is a failure.
cc "${common[@]}" tests/host/fake_queue.c tests/host/fake_runtime.c \
    tests/host/test_three_engine.c main/raw_midi.c main/midi_template_link.c \
    main/event_bus.c main/control_source.c -o "$out/remote"
for initial in 0 1 2; do
    for target in 0 1 2; do
        "$out/remote" "$initial" "$target" normal
    done
done
for overhead in 60 80 100; do
    for initial in 0 1 2; do
        for target in 0 1 2; do
            "$out/remote" "$initial" "$target" normal "$overhead"
        done
    done
done
"$out/remote" 0 1 hold-mode
"$out/remote" 1 0 hold-mode
"$out/remote" 2 1 hold-mode
for mode in 0 1 2; do "$out/remote" "$mode" "$mode" hold-template; done
"$out/remote" 0 1 priority

# Stable idle health must leave the native template/RAW handshake time to run.
# The measured-style slow transport still preserves every urgent operation.
for mode in 0 1 2; do "$out/remote" 1 "$mode" idle-budget 140; done
"$out/remote" 1 1 idle-urgent 140
