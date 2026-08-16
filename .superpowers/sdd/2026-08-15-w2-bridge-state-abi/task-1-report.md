# Task 1 report — Define the public ABI header

## Scope

- Worktree: `C:\dev\skyrimbridge-worktrees\bridge-state-abi`
- Branch: `feat/bridge-state-abi`
- Owned files changed:
  - `include/SkyrimBridgeAPI.h`
  - `tests/validate_bridge_abi_header.py`
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
- Preserved C linkage/export spelling:
  `extern "C" __declspec(dllexport) SB::Api::BridgeInterface* SB_GetBridgeInterface();`
- Added compile-time ABI assertions for `SB::Float4`, `SB::AllData`, and `SB::Api::BridgeInterface` using compiler layout/copyability traits, without STL-bearing public signatures or exceptions.
- Added a focused Python validator that enforces:
  - exact field order,
  - exact field signatures,
  - version 1 unsigned literal,
  - export declaration spelling,
  - required static assertions,
  - no STL includes or ABI-unsafe constructs in the public ABI header.

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

## Compile-check note

No C++ compiler was available on PATH in this environment during verification (`cl`, `clang++`, `clang-cl`, `g++`, `gcc`, and `c++` were absent). The header still contains compile-time static assertions for MSVC/clang-compatible C++ type-trait builtins so the ABI layout/copyability checks run when the project is compiled in the intended toolchain.

## Index note

The required read-only `index` orientation was attempted for `C:/dev` and then the isolated worktree; both calls timed out before returning a map. Work then continued from the task brief, full plan, ledger, and direct repository inspection.
