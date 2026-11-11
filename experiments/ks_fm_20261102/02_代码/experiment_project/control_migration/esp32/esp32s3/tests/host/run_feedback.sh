#!/bin/bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo"
out=$(mktemp -d /tmp/feedback-host.XXXXXX)
trap 'rm -rf "$out"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -Itests/host/stubs -Imain)
cc "${flags[@]}" tests/host/test_parameter_cc.c main/parameter_cc.c main/patch.c -lm -o "$out/parameter_cc"
"$out/parameter_cc"
cc "${flags[@]}" tests/host/test_midi_feedback_packet.c main/midi_feedback_packet.c -o "$out/midi_feedback_packet"
"$out/midi_feedback_packet"
cc "${flags[@]}" tests/host/test_midi_feedback_guard.c main/midi_feedback_guard.c main/patch.c \
    -o "$out/midi_feedback_guard"
"$out/midi_feedback_guard"
cc "${flags[@]}" tests/host/test_midi_feedback_transport.c main/midi_feedback_transport.c \
    main/midi_feedback_guard.c main/patch.c -o "$out/midi_feedback_transport"
"$out/midi_feedback_transport"
