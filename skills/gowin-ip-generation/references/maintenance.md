# Script Maintenance

For installation/ABI limits and the distinction between archived and public-package validation, read [Public package](public-package.md) before executing this adapter.

The package contains:

- `scripts/gowin_ip.py`: main script.
- `scripts/bin/vendor-runner`: vendor process runner.
- `scripts/src/bridge.cpp`: bridge source.
- `scripts/bin/libgowin_ip_bridge.so`: built bridge library.

The runtime files are inside the Skill; no external test-workspace scripts or SDK files are required. The local Gowin installation, Python 3, and binutils are still required.

The script review fixed default-range checks that blocked dependent settings, false parameter-verification results, and file inputs that bypassed copying. Reserved output controls cannot be changed through parameters.

The bundled library was verified only on the original pinned host. Another installation requires coordinated adaptation and a fresh ABI/output check; a header-only rebuild is insufficient if the pinned paths differ. The maintenance example for an already matched installation is:

```bash
QT5_INCLUDE_DIR=/path/to/qt5/headers python3 "${SKILLS_ROOT}/gowin-ip-generation/scripts/build_bridge.py"
```

Use headers compatible with the local Qt 5.15 ABI. The build links to Gowin's Qt libraries. Do not rebuild during a request. Test generation after rebuilding. The package does not install development dependencies.

Optional maintenance checks (set `SKILL_VALIDATOR` to an existing skill-creator `quick_validate.py`; no validator is installed by this package):

```bash
python3 -m unittest discover -s "${SKILLS_ROOT}/gowin-ip-generation/scripts/tests" -v
python3 "${SKILL_VALIDATOR}" "${SKILLS_ROOT}/gowin-ip-generation"
```

Run generation tests in a new separate folder. Do not use test coefficients or test EDID data in a real project. Vendor IP output, large test projects, and sample design inputs are not included in the Skill.
