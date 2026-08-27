# Task 4 report: strict distributable build and public package

Date: 2026-08-27. Branch `feat/bridge-state-abi`, closeout tip `0ac5df8`.

## Build

Isolated public build, native replacements OFF by default (no `-D` override
supplied, so the packaged binary proves the default, not an explicit flag):

    cmake -S . -B build-public-off -G "Visual Studio 18 2026" -A x64
    cmake --build build-public-off --config Release --target SkyrimBridge d3d11_proxy

## Validation battery (all exit 0)

    python tests/validate_bridge_abi_header.py            -> all cases passed
    python tests/validate_bridge_abi_implementation.py --dll build-public-off/Release/SkyrimBridge.dll
                                                          -> source contract and PE export passed
    python tests/validate_release_package.py --build-dir build-public-off
                                                          -> PASS: deterministic public release package
                                                             (27 payload files + manifest, distributable build)
    git diff --check                                      -> clean

Archive inspection: 28 entries (27 payloads + MANIFEST.json). No Sky.ini, no
WeatherRouting.example.ini, no native-suite marker in any payload. ABI consumer
kit ships complete: Docs/BRIDGE-ABI.md, SDK/SkyrimBridgeAPI.h,
SDK/core/BridgeData.h.

## Package

    python scripts/package.py --build-dir build-public-off --dist-dir dist-public-off --quiet

- File: SkyrimBridge-3.0.0.zip
- Size: 931,797 bytes
- SHA-256: 611a51e8450fd22d791e28cfe1ab586df5cc6f2b2697df9a549b5b3dd011899d
- Reproduction: a second independent packager run produced byte-identical
  output (same SHA-256), and the release test's own two-run comparison passed.
- Superseded artifact: the pre-review package at 931,803 bytes
  (sha256 9c1e6923...c51a) is retained in the campaign evidence directory; it
  predates the review fixes below and must not ship.

## Independent range review

Exact-range review of 56197e3..b9f46fb (read-only agent): **approve-with-notes**.
Category results all clean (ABI invariants, CMake/packager boundary, SDK
shipping, copy scrub, secrets/paths/GPL, snapshot race safety). Findings and
dispositions:

| sev | finding | disposition |
|---|---|---|
| 2 | compiled `engineFixes` default stayed true; deleting the shipped INI re-enabled engine writes in a public build | FIXED `ba2b6f9`: default now follows `SKYRIMBRIDGE_NATIVE_REPLACEMENTS`, static_assert enforces it |
| 3 | five fix commits touched files outside the brief's ownership list | accepted: review verified ABI header/field order untouched and each edit closes a real contract defect |
| 3 | `NATIVE_SUITE_MARKERS` duplicated in packager and test | FIXED `0ac5df8`: test asserts tuple equality with the packager |
| 3 | packager byte-scanned only the plugin, not the proxy | FIXED `0ac5df8`: proxy goes through the same scan |
| 3 | `detectDead`/`drunk` flags advertised by SDK but never set true | RESOLVED `477e6ed` (2026-08-27): re-documented as reserved. The vendored CommonLibSSE-NG surface has no detect-dead archetype and no intoxication actor value, so both fields publish a literal 0.0f with a reserved comment; the scalar-flag validator pins the new wording |
| 3 | teardown latches only on the ENBSeries exit path, not standalone | RESOLVED `fa0406c` (2026-08-27): `HookedWndProc` handles WM_DESTROY and calls a new `NotifyStandaloneWindowDestroyed()`, gated by `s_enbDrivesUpdates`, which invokes the idempotent `MarkTeardown()`; loader-lock-safe because WM_DESTROY precedes DLL unload. ABI implementation validator pins the guard-then-latch shape |

The two fix commits (`ba2b6f9`, `0ac5df8`) land after the reviewed range and
were not independently re-reviewed; both are small, and the rebuilt binary
re-passed the full battery and the deterministic-package contract.

## Limitations

- No in-game validation; everything here is static, PE-level, and packager
  contract testing on the pinned Windows toolchain.
- The two sev3 notes above were the pre-upload backlog; both RESOLVED on
  2026-08-27 (`477e6ed`, `fa0406c`), merged to main by fast-forward at
  `fa0406c`. The rebuilt deterministic package is SkyrimBridge-3.0.0.zip,
  932037 bytes, sha256
  91d1a68ae81ed892f4e49570860961cb2dcd5c14e5d2ae518befabcfe56b7180,
  reproduced byte-identically across two independent runs. Nothing uploads
  to Nexus from this campaign; upload remains a human gate.
