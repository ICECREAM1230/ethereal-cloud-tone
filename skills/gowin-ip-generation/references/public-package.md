# Public package and native bridge

The Python/shell adapters, `scripts/src/bridge.cpp`, and the matching locally built `scripts/bin/libgowin_ip_bridge.so` are included. This library is an adapter built from the supplied source; vendor IDE/plugin/Qt libraries, device databases and license files are external.

The archived adapter is pinned to Linux x86_64, Gowin V1.9.12.04, compatible Qt 5.15, and the original installation `/home/ice/Gowin/V1.9.12.04/IDE`. The Python runner, build helper, C++ source and compiled library intentionally retain this same installation assumption. `GOWIN_IDE` alone does not make this adapter portable: `gowin_ip.py` rejects an unverified installation and the bridge uses an absolute plugin path.

On a different installation this adapter remains unvalidated. An authorized adaptation must update the coordinated source/runner paths, rebuild against the matching installed Qt/Gowin ABI, and verify actual output in a new isolated directory before calling it supported. Do not alter an installed vendor library or create a compatibility symlink merely to bypass this boundary. This publication did not run the adapter, rebuild it, generate IP, synthesize, place-and-route, or program hardware.

The source package's test and generation counts in `package-manifest.json` and the usage notes describe historical source-machine evidence, not a test of this public package or another host. The manifest's source file digests remain provenance for the archived adapter; public-package digests are maintained separately in the collection's `_metadata/resources.json`.
