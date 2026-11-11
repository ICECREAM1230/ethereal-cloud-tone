#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
# LeakSanitizer cannot run under the desktop sandbox's ptrace supervisor.
# AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.
export ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
B_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
B_TEST_BUILD="${B_TEST_BUILD:-$B_ROOT/.build/host}"
python3 "$B_ROOT/tests/test_io_profile.py"
mkdir -p "$B_TEST_BUILD"
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$B_ROOT/components/harp_core/include" \
  "$B_ROOT/tests/test_harp.c" "$B_ROOT/components/harp_core/harp_core.c" \
  -o "$B_TEST_BUILD/test_harp"
"$B_TEST_BUILD/test_harp"
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$B_ROOT/components/b_control/include" "$B_ROOT/tests/test_pinmap.c" "$B_ROOT/components/b_control/b_pinmap.c" \
  -o "$B_TEST_BUILD/test_pinmap"
"$B_TEST_BUILD/test_pinmap"
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$B_ROOT/components/harp_core/include" \
  "$B_ROOT/tests/test_mapping.c" "$B_ROOT/components/harp_core/harp_core.c" \
  -o "$B_TEST_BUILD/test_mapping"
"$B_TEST_BUILD/test_mapping"
bash "$B_ROOT/tests/test_control.sh"
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$B_ROOT/components/harp_core/include" -I "$B_ROOT/components/b_control/include" \
  "$B_ROOT/tests/test_wire_bridge.c" "$B_ROOT/components/harp_core/harp_core.c" \
  "$B_ROOT/components/b_control/b_config.c" "$B_ROOT/components/b_control/b_protocol.c" \
  "$B_ROOT/components/b_control/b_engine.c" "$B_ROOT/components/b_control/b_pinmap.c" \
  -o "$B_TEST_BUILD/test_wire_bridge"
B_REPO_ROOT="$(cd -- "$B_ROOT/../.." && pwd)"
PYTHONPATH="$B_REPO_ROOT${PYTHONPATH:+:$PYTHONPATH}" python3 "$B_ROOT/tests/test_wire.py" "$B_TEST_BUILD/test_wire_bridge"

# Link the existing A decoder read-only; no A source is copied or changed.
B_A_ROOT="${A_BOARD_ROOT:-$B_ROOT/../a_board}"
if [[ ! -f "$B_A_ROOT/main/ble_midi_unframer.c" ]]; then
  echo 'A decoder not found; set A_BOARD_ROOT to run the compatibility check.' >&2
  exit 1
fi
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$B_ROOT/components/harp_core/include" -I "$B_A_ROOT/main" \
  "$B_ROOT/tests/test_a_unframer.c" "$B_ROOT/components/harp_core/harp_core.c" \
  "$B_A_ROOT/main/ble_midi_unframer.c" -o "$B_TEST_BUILD/test_a_unframer"
"$B_TEST_BUILD/test_a_unframer"
