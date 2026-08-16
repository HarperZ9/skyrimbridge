# Task 1 report — Define the public ABI header

## Scope

- Worktree: `C:\dev\skyrimbridge-worktrees\bridge-state-abi`
- Branch: `feat/bridge-state-abi`
- Owned files changed:
  - `include/SkyrimBridgeAPI.h`
  - `tests/validate_bridge_abi_header.py`
  - `docs/superpowers/plans/2026-08-15-w2-bridge-state-abi.md`
  - `.superpowers/sdd/2026-08-15-w2-bridge-state-abi/task-1-report.md`

## Implementation

- Added public ABI version `SB::Api::kBridgeInterfaceVersion = 1U`.
- Added `SB::Api::BridgeInterface` with exact v1 field order:
  1. `version`
  2. `allDataSize`
  3. `GetFrameIndex`
  4. `GetFrameData`
  5. `IsFrameValid`
  6. `CopyFrameData`
- Appended `CopyFrameData(void* destination, uint32_t destinationSize, uint64_t* frameIndex)` after the plan's original fields.
- Preserved C linkage while hiding producer export decoration behind
  `SB_BRIDGE_API`:
  `extern "C" SB_BRIDGE_API SB::Api::BridgeInterface* SB_GetBridgeInterface();`
- Added `SKYRIMBRIDGE_BUILDING_DLL`/`SB_BRIDGE_API` so producer builds expand to
  `__declspec(dllexport)` and consumer/GetProcAddress inclusion expands empty.
- Documented that `SkyrimBridgeAPI.h` is a C++ header exposing a C-linkage
  symbol, not a pure-C header.
- Added compile-time ABI assertions for `SB::Float4`, `SB::AllData`, and `SB::Api::BridgeInterface` using compiler layout/copyability traits, without STL-bearing public signatures or exceptions.
- Added a focused Python validator that enforces:
  - exact field order,
  - exact field signatures,
  - version 1 unsigned literal,
  - producer-only export macro behavior,
  - export declaration spelling through `SB_BRIDGE_API`,
  - public C++/C-linkage/not-pure-C documentation,
  - required static assertions,
  - no STL includes or ABI-unsafe constructs in the public ABI header.
- Updated Task 2 in `docs/superpowers/plans/2026-08-15-w2-bridge-state-abi.md`
  so the future implementation:
  - initializes all six v1 interface fields, including non-null
    `&CopyFrameDataImpl`,
  - publishes from a synchronized `SB::AllData` snapshot/double-buffer instead
    of the mutating live `SB::GetMutableData()` block,
  - requires tests for non-null callbacks and coherent copies,
  - adds repository `include/` to the target include path,
  - defines `SKYRIMBRIDGE_BUILDING_DLL` for the producer target.

## TDD evidence

### RED

Command:

```powershell
python tests/validate_bridge_abi_header.py
```

Observed failure before the header existed:

```text
FAIL: header is absent: C:\dev\skyrimbridge-worktrees\bridge-state-abi\include\SkyrimBridgeAPI.h
```

### GREEN

Command:

```powershell
python tests/validate_bridge_abi_header.py
```

Observed pass after adding the header:

```text
validate_bridge_abi_header: all cases passed
```

### Mutation proof

Mutation 1: moved `CopyFrameData` before `IsFrameValid`.

Command:

```powershell
python tests/validate_bridge_abi_header.py
```

Observed expected failure:

```text
FAIL: field order is the ABI; expected ['version', 'allDataSize', 'GetFrameIndex', 'GetFrameData', 'IsFrameValid', 'CopyFrameData'] got ['version', 'allDataSize', 'GetFrameIndex', 'GetFrameData', 'CopyFrameData', 'IsFrameValid']
```

Mutation 2: swapped `GetFrameIndex` and `GetFrameData`.

Command:

```powershell
python tests/validate_bridge_abi_header.py
```

Observed expected failure:

```text
FAIL: field order is the ABI; expected ['version', 'allDataSize', 'GetFrameIndex', 'GetFrameData', 'IsFrameValid', 'CopyFrameData'] got ['version', 'allDataSize', 'GetFrameData', 'GetFrameIndex', 'IsFrameValid', 'CopyFrameData']
```

Restored the header after both mutations.

## Focused verification

Commands:

```powershell
python tests/validate_bridge_abi_header.py
python -m py_compile tests/validate_bridge_abi_header.py
```

Observed:

```text
validate_bridge_abi_header: all cases passed
```

Both commands exited 0.

## Review fix evidence

Review base: `eaa0e2e`.

### RED

After adding validator checks for producer-only export macro behavior and
C++/C-linkage documentation, the old header failed as expected:

```text
FAIL: SB_BRIDGE_API must dllexport only when SKYRIMBRIDGE_BUILDING_DLL is defined and expand empty for GetProcAddress consumers
FAIL: the exported entry point must use extern C plus SB_BRIDGE_API
FAIL: the consumer-visible declaration must not hard-code dllexport
FAIL: missing public header documentation phrase: 'C++ header'
FAIL: missing public header documentation phrase: 'C-linkage symbol'
FAIL: missing public header documentation phrase: 'not a pure-C header'
```

### GREEN

After adding `SB_BRIDGE_API`, `SKYRIMBRIDGE_BUILDING_DLL`, and the header
documentation, the focused validator passed:

```text
validate_bridge_abi_header: all cases passed
```

### Review mutation proof

Mutation: changed the consumer macro branch from empty to
`__declspec(dllimport)`.

Expected validator failure:

```text
FAIL: SB_BRIDGE_API must dllexport only when SKYRIMBRIDGE_BUILDING_DLL is defined and expand empty for GetProcAddress consumers
FAIL: consumer inclusion must not force dllimport; consumers use GetProcAddress
```

Mutation: changed the export declaration back to direct
`extern "C" __declspec(dllexport)`.

Expected validator failure:

```text
FAIL: dllexport must appear exactly once, inside the SB_BRIDGE_API producer macro
FAIL: the exported entry point must use extern C plus SB_BRIDGE_API
FAIL: the consumer-visible declaration must not hard-code dllexport
```

Both mutations were reverted before final verification.

### Final review-focused verification

Commands:

```powershell
python tests/validate_bridge_abi_header.py
python -c "import pathlib; compile(pathlib.Path('tests/validate_bridge_abi_header.py').read_text(encoding='utf-8'), 'tests/validate_bridge_abi_header.py', 'exec')"
git diff --check
```

Observed:

```text
validate_bridge_abi_header: all cases passed
```

All three commands exited 0. `git diff --check` produced no whitespace or patch
hygiene findings.

## Compile-check note

No C++ compiler was available on PATH in this environment during verification (`cl`, `clang++`, `clang-cl`, `g++`, `gcc`, and `c++` were absent). The header still contains compile-time static assertions for MSVC/clang-compatible C++ type-trait builtins so the ABI layout/copyability checks run when the project is compiled in the intended toolchain.

## Index note

The required read-only `index` orientation was attempted for `C:/dev` and then the isolated worktree; both calls timed out before returning a map. Work then continued from the task brief, full plan, ledger, and direct repository inspection.
