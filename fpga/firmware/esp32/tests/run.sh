#!/bin/bash
set -euo pipefail
pkg=$(cd "$(dirname "$0")/.." && pwd)
cd "$pkg"
export NODE_BINARY=${NODE_BINARY:-node}
"$NODE_BINARY" -e 'if(Number(process.versions.node.split(".")[0])<22)throw Error("Node.js >=22 required")'
python3 -B tools/check_fm_contract.py
bash tests/host/run.sh
"$NODE_BINARY" --test tests/web/tests/*.test.mjs
