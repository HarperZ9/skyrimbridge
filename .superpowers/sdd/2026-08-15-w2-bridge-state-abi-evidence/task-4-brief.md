### Task 4: Strict distributable build and public package

**Base:** `b3f5ea0`

**Ownership:**
- Modify: `CMakeLists.txt`
- Modify: `scripts/package.py`
- Modify: `tests/validate_release_package.py`
- Modify as needed: `scripts/run_validation.py`
- Modify public copy only where required: `README.md`, `docs/BRIDGE-ABI.md`, `docs/VALIDATION-PROTOCOL.md`, `project-docs/NEXUS-PAGE.md`, `CREDITS.md`, `THIRD_PARTY_NOTICES.md`
- Do not edit Kitsuune-derived implementation sources, private/protected material, `SB_Retain`, shader data contracts, Nexus media bytes, or ABI field order/version.

**Required build/package boundary:**
1. `SKYRIMBRIDGE_NATIVE_REPLACEMENTS` defaults `OFF`. Private builds must opt in explicitly with `ON`; public/distributable builds must contain none of those source objects.
2. `scripts/package.py` is a public packager. It must refuse a plugin carrying any permission-gated native-suite marker, not package it with a warning or alternate payload. Never ship `Sky.ini` or `WeatherRouting.example.ini` in the public archive.
3. The public archive ships the independently authored ABI consumer kit:
   - `Docs/BRIDGE-ABI.md`
   - `SDK/SkyrimBridgeAPI.h`
   - `SDK/core/BridgeData.h`
   This preserves the header's current include contract and lets an extracted consumer build with one SDK include root.
4. The exact deterministic manifest/ZIP/sidecar contract remains. Two independent package runs from identical public binaries must be byte-identical.
5. `tests/validate_release_package.py` is unconditionally strict for a public archive. Remove the warning-only/private-success path and conditional expected payload. A marker or native-suite config is always a failing public-package test.
6. Public copy advertises only shipped independent facilities. Move native replacement-suite feature text into an explicitly private/excluded provenance note; keep Kitsuune/LonelyKitsuune attribution, state the historical implementation was binary-reversed and not clean-room, and explain that public SkyrimBridge interoperates with/defers to original plugins without copying their implementation.
7. Generated promotional art remains repository media only, labeled non-gameplay/AI media in its README; do not package it as runtime evidence or upload it.

**TDD/focused verification:**
1. Extend the release-package test first so old behavior fails for missing ABI SDK/docs and warning-only native-suite handling.
2. Implement the packager/build/copy changes.
3. Configure an isolated public build directory explicitly with native replacements OFF; do not reuse a private/stale Release DLL.
4. Build only `SkyrimBridge` and `d3d11_proxy` in Release.
5. Run only:
   - `python tests/validate_bridge_abi_header.py`
   - `python tests/validate_bridge_abi_implementation.py --dll <public-build>/Release/SkyrimBridge.dll`
   - `python tests/validate_release_package.py` against that public build (adapt its CLI/build-dir handling if needed)
   - `git diff --check`
6. Inspect the final ZIP names/manifest and scan binary/archive payloads for all native-suite markers plus secret-like material. Do not run broad portable/live tests.

**Output:**
- Deterministic `SkyrimBridge-3.0.0.zip` and `.sha256` under an isolated public dist directory.
- Record exact byte size, SHA-256, payload count, configure/build/test commands, and limitations.

**Commit:** `release: harden the SkyrimBridge public package`

**Review:** independent exact-range review is required before any Nexus page creation or file upload.
