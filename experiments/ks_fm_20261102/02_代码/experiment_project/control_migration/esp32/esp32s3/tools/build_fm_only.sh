#!/bin/bash
# Build only. Programming either ESP32 or FPGA requires separate authorization.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${FM_ONLY_BUILD_DIR:-"$root/build-fm-only"}
if [[ $build_dir != /* ]]; then build_dir="$PWD/$build_dir"; fi
config="$build_dir/sdkconfig.fm-only"
if ! command -v idf.py >/dev/null; then
    echo "Activate ESP-IDF v6.1 first: source /home/ice/esp/esp-idf-v6.1/export.sh" >&2
    exit 1
fi
idf.py --version
mkdir -p "$build_dir"
python3 - "$root/config/sdkconfig.esp32s3" "$config" <<'PY'
from pathlib import Path
import re,sys
source,target=map(Path,sys.argv[1:])
if target.exists():
    config=target.read_text()
else:
    config=source.read_text()
    config=re.sub(r'^(?:CONFIG_FPGA_FM_ONLY=.*|# CONFIG_FPGA_FM_ONLY is not set)\n?', '',config,flags=re.M)
    config+='\nCONFIG_FPGA_FM_ONLY=y\n'
    target.write_text(config)
for required in ('CONFIG_FPGA_FM_ONLY=y','CONFIG_IDF_TARGET="esp32s3"',
                 'CONFIG_ECT_WIFI_WEB=y','CONFIG_ECT_WIFI_SSID="FPGA_Project"'):
    if required not in config.splitlines():
        raise SystemExit(f'{target}: required setting missing: {required}')
PY
IDF_PY_BUILD_JOBS="${FM_ONLY_BUILD_JOBS:-2}" idf.py -C "$root" -B "$build_dir" -D "SDKCONFIG=$config" \
    -D "SDKCONFIG_DEFAULTS=$root/sdkconfig.defaults;$root/config/sdkconfig.fm-only.defaults" \
    -D "IDF_TARGET=esp32s3" build size
