# Task 2 report — Implement and export the bridge state ABI

## Scope

- Worktree: `C:\dev\skyrimbridge-worktrees\bridge-state-abi`
- Branch: `feat/bridge-state-abi`
- Dispatch base: `7d585bc`
- Owned files changed:
  - `src/core/BridgeApi.cpp`
  - `include/SkyrimBridgeAPI.h` (internal producer lifecycle declarations only)
  - `src/core/main.cpp`
  - `CMakeLists.txt`
  - `tests/validate_bridge_abi_implementation.py`
  - `.superpowers/sdd/2026-08-15-w2-bridge-state-abi/task-2-report.md`

## Implementation

- Added `src/core/BridgeApi.cpp` implementing the static v1 producer interface
  returned by `SB_GetBridgeInterface`.
- Initialized all six v1 ABI fields in order:
  1. `version`
  2. `allDataSize`
  3. `GetFrameIndex`
  4. `GetFrameData`
  5. `IsFrameValid`
  6. `CopyFrameData`
- Published frame data through synchronized double-buffer snapshots:
  - `g_frameValid` starts false, so reads are invalid before the first publish.
  - `MarkFramePublished(const SB::AllData& publishedData)` copies the already
    sanitized frame into the alternate snapshot slot before advancing the public
    frame counter.
  - `GetFrameDataImpl` returns the current published snapshot, never
    `SB::GetMutableData()`.
  - `CopyFrameDataImpl` rejects null and undersized destinations, copies exactly
    `sizeof(SB::AllData)` while synchronized, and returns the matching
    `g_publishedFrameIndex` when requested.
  - `MarkTeardown()` release-stores validity false for shutdown.
- Hooked publication in `src/core/main.cpp` immediately after the existing
  `ENBInterface::PushAllData(data)` call, preserving the ENB push order.
- Hooked teardown at ENB `OnExit` before existing shutdown calls.
- Added repository `include/` to the SkyrimBridge target include paths, added
  `src/core/BridgeApi.cpp` to the plugin target sources, and defined
  `SKYRIMBRIDGE_BUILDING_DLL=1` for producer-side `SB_BRIDGE_API` export
  decoration.
- Did not modify shaders, artistic assets, SB_Retain, packaging/media/docs, or
  Kitsuune-derived/native replacement implementation files.

## TDD evidence

### RED

Command:

```powershell
python tests/validate_bridge_abi_implementation.py
```

Observed expected failure before implementation:

```text
FAIL: BridgeApi.cpp is absent: C:\dev\skyrimbridge-worktrees\bridge-state-abi\src\core\BridgeApi.cpp
FAIL: internal lifecycle declarations must be marked as producer-only
FAIL: header must declare internal MarkFramePublished helper
FAIL: header must declare internal MarkTeardown helper
FAIL: main.cpp must include SkyrimBridgeAPI.h
FAIL: main.cpp must publish the ABI snapshot only after sanitization and existing ENBInterface::PushAllData(data)
FAIL: ENB OnExit must call SB::Api::MarkTeardown()
FAIL: CMakeLists.txt must add src/core/BridgeApi.cpp to the plugin target sources
FAIL: CMakeLists.txt must add repository include/ to the plugin target include paths
FAIL: CMakeLists.txt must define SKYRIMBRIDGE_BUILDING_DLL=1 for the producer target
```

### GREEN

Command:

```powershell
python tests/validate_bridge_abi_implementation.py
```

Observed:

```text
validate_bridge_abi_implementation: source contract passed
```

### Mutation proof

Mutation: changed the `BridgeInterface g_interface` initializer’s final field
from `&CopyFrameDataImpl` to `nullptr`.

Command:

```powershell
python tests/validate_bridge_abi_implementation.py
```

Observed expected failure:

```text
FAIL: BridgeInterface g_interface must initialize all six v1 fields in ABI order; expected ['kBridgeInterfaceVersion', 'static_cast<uint32_t>(sizeof(SB::AllData))', '&GetFrameIndexImpl', '&GetFrameDataImpl', '&IsFrameValidImpl', '&CopyFrameDataImpl'] got ['kBridgeInterfaceVersion', 'static_cast<uint32_t>(sizeof(SB::AllData))', '&GetFrameIndexImpl', '&GetFrameDataImpl', '&IsFrameValidImpl', 'nullptr']
```

The mutation was reverted and the validator returned to green:

```text
validate_bridge_abi_implementation: source contract passed
```

## Build and export evidence

Configure command used for the focused PE build:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" -DSKYRIMBRIDGE_NATIVE_REPLACEMENTS=OFF
```

Notes:

- The first configure/build invocations outlived the 60-second tool timeout but
  continued in the background and produced the build tree/artifact.
- A fresh incremental build command exited 0:

```powershell
cmake --build build --config Release --target SkyrimBridge -- /m
```

Observed:

```text
MSBuild version 18.0.5+e22287bf1 for .NET Framework

  BridgeApi.cpp
     Creating library C:/dev/skyrimbridge-worktrees/bridge-state-abi/build/Release/SkyrimBridge.lib and object C:/dev/skyrimbridge-worktrees/bridge-state-abi/build/Release/SkyrimBridge.exp
  SkyrimBridge.vcxproj -> C:\dev\skyrimbridge-worktrees\bridge-state-abi\build\Release\SkyrimBridge.dll
    Copying config files to build directory
```

Built artifact:

```text
C:\dev\skyrimbridge-worktrees\bridge-state-abi\build\Release\SkyrimBridge.dll
Length: 1768960
LastWriteTime: 2026-08-16 13:53:01
```

PE export validation command:

```powershell
python tests/validate_bridge_abi_implementation.py --dll build/Release/SkyrimBridge.dll
```

Observed:

```text
validate_bridge_abi_implementation: source contract and PE export passed
```

## Focused verification

Baseline before Task 2 changes:

```powershell
python tests/validate_bridge_abi_header.py
python tests/validate_command_protocol.py
```

Observed:

```text
validate_bridge_abi_header: all cases passed
```

```text
15 passed, 0 failed
```

Post-implementation focused verification:

```powershell
python tests/validate_bridge_abi_header.py
python tests/validate_bridge_abi_implementation.py
python tests/validate_bridge_abi_implementation.py --dll build/Release/SkyrimBridge.dll
python tests/validate_command_protocol.py
```

Observed:

```text
validate_bridge_abi_header: all cases passed
validate_bridge_abi_implementation: source contract passed
validate_bridge_abi_implementation: source contract and PE export passed
15 passed, 0 failed
```

## Validator note

The PE parser in `tests/validate_bridge_abi_implementation.py` maps section
RVAs using the section header's `VirtualSize`, `VirtualAddress`,
`SizeOfRawData`, and `PointerToRawData`. This corrected an intermediate
validator-only failure where the raw pointer was parsed from the relocation
field; no production change was made for that parser issue.
