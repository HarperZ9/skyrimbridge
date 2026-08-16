# W2: SkyrimBridge Exported State ABI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give SkyrimBridge a versioned, exported C ABI that publishes its per-frame engine state to any in-process consumer, so state production stops depending on the shader host that consumes it.

**Architecture:** SkyrimBridge currently pushes state through `ENBSetParameter`, a symbol that exists only when ENBSeries is loaded. Under Effects 11 there is no ENB module, so the publish path goes dark. This adds `SB_GetBridgeInterface()`, returning a versioned struct that exposes the existing `SB::AllData` block by pointer. The ENB publish path is unchanged and keeps working; the new ABI is a second reader of the same data, not a replacement. It follows the `SB_GetProxyInterface` precedent already in this repository.

**Tech Stack:** C++23, CommonLibSSE-NG, MSVC x64, Python 3 stdlib validators.

## Global Constraints

- Repository licence is MIT and stays MIT. No GPL source enters this repository. Consumers link across a module boundary through `GetProcAddress`, never by source inclusion.
- The ABI is C-linkage and layout-stable. Every struct in it is standard-layout and trivially copyable. No `std::string`, no `std::vector`, no virtual functions, no exceptions crossing the boundary.
- Never renumber or reorder an existing field. Additive changes bump `kBridgeInterfaceVersion` and append only.
- The existing `ENBInterface` publish path must keep working unchanged under ENBSeries. Any task that touches it proves this.
- Tests are Python validators under `tests/`, run directly, matching the existing `validate_*.py` convention.

---

### Task 1: Define the public ABI header

**Files:**
- Create: `include/SkyrimBridgeAPI.h`
- Create: `tests/validate_bridge_abi_header.py`

**Interfaces:**
- Consumes: `SB::AllData` from `src/core/BridgeData.h`.
- Produces: `SB::Api::BridgeInterface`, `SB::Api::kBridgeInterfaceVersion`, and the exported symbol name `SB_GetBridgeInterface`. Task 2 implements the export. Task 3 tests it. W3's Community Shaders feature is the first external consumer.

- [ ] **Step 1: Write the failing test**

Create `tests/validate_bridge_abi_header.py`. It parses the header as text, so it runs without a build and catches ABI drift in review.

```python
#!/usr/bin/env python3
"""Contract checks for the public SkyrimBridge ABI header.

The header is consumed by out-of-tree plugins that resolve SB_GetBridgeInterface
with GetProcAddress. Field order is the ABI, so this asserts order, not just
presence. Standard library only.
"""

import pathlib
import re
import sys

HEADER = pathlib.Path(__file__).resolve().parents[1] / "include" / "SkyrimBridgeAPI.h"

EXPECTED_FIELDS = [
    "version",
    "allDataSize",
    "GetFrameIndex",
    "GetFrameData",
    "IsFrameValid",
]

FORBIDDEN = ["std::string", "std::vector", "virtual ", "throw "]


def main() -> int:
    failures = []

    if not HEADER.is_file():
        print(f"FAIL: header is absent: {HEADER}")
        return 1
    text = HEADER.read_text(encoding="utf-8")

    match = re.search(r"struct BridgeInterface\s*\{(.*?)\n\};", text, re.S)
    if not match:
        failures.append("struct BridgeInterface was not found")
    else:
        body = match.group(1)
        found = re.findall(r"(?:\*|\s)(\w+)\s*(?:\)\s*\(|;)", body)
        ordered = [f for f in found if f in EXPECTED_FIELDS]
        if ordered != EXPECTED_FIELDS:
            failures.append(
                f"field order is the ABI; expected {EXPECTED_FIELDS} got {ordered}")

    if not re.search(r"kBridgeInterfaceVersion\s*=\s*(\d+)U", text):
        failures.append("kBridgeInterfaceVersion is not defined as an unsigned literal")

    if 'extern "C" __declspec(dllexport) SB::Api::BridgeInterface* SB_GetBridgeInterface();' not in text:
        failures.append("the exported entry point declaration is missing or misspelled")

    for token in FORBIDDEN:
        if token in text:
            failures.append(f"non-ABI-safe token in a public header: {token!r}")

    for line in failures:
        print(f"FAIL: {line}")
    if not failures:
        print("validate_bridge_abi_header: all cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
python tests/validate_bridge_abi_header.py
```

Expected: FAIL with "header is absent: .../include/SkyrimBridgeAPI.h".

- [ ] **Step 3: Write the header**

Create `include/SkyrimBridgeAPI.h`:

```cpp
#pragma once
//=============================================================================
//  SkyrimBridgeAPI.h - public ABI for in-process consumers
//
//  Resolve at runtime, never by linking:
//
//    auto mod = GetModuleHandleW(L"SkyrimBridge.dll");
//    auto get = reinterpret_cast<SB::Api::BridgeInterface* (*)()>(
//        GetProcAddress(mod, "SB_GetBridgeInterface"));
//    auto* api = get ? get() : nullptr;
//
//  A consumer must check both version and allDataSize before dereferencing
//  GetFrameData, because a mismatched build has a different AllData layout.
//=============================================================================

#include <cstdint>

#include "core/BridgeData.h"

namespace SB::Api
{
    // Bump on any additive change. Never reorder or renumber existing fields.
    inline constexpr std::uint32_t kBridgeInterfaceVersion = 1U;

    struct BridgeInterface
    {
        // Always kBridgeInterfaceVersion for the build that returned this.
        std::uint32_t version;

        // sizeof(SB::AllData) for this build. A consumer compiled against a
        // different BridgeData.h will see a mismatch and must refuse the data.
        std::uint32_t allDataSize;

        // Monotonic, incremented once per published frame. A consumer polls
        // this to tell a fresh frame from a repeat without diffing the block.
        std::uint64_t (*GetFrameIndex)();

        // Points at the live state block. Valid only while IsFrameValid()
        // returns true and only on the thread that published it. Never freed
        // by the caller.
        const SB::AllData* (*GetFrameData)();

        // False before the first publish, and after teardown has begun.
        bool (*IsFrameValid)();
    };
}

extern "C" __declspec(dllexport) SB::Api::BridgeInterface* SB_GetBridgeInterface();
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
python tests/validate_bridge_abi_header.py
```

Expected: `validate_bridge_abi_header: all cases passed`.

- [ ] **Step 5: Verify by mutation, then revert**

Swap the declaration order of `GetFrameIndex` and `GetFrameData` in the header and re-run. Expected: FAIL with "field order is the ABI". Restore the original order and confirm the test passes.

- [ ] **Step 6: Commit**

```bash
git add include/SkyrimBridgeAPI.h tests/validate_bridge_abi_header.py
git commit -m "feat: define the public SkyrimBridge state ABI"
```

---

### Task 2: Implement and export the entry point

**Files:**
- Create: `src/core/BridgeApi.cpp`
- Create: `tests/validate_bridge_abi_implementation.py`
- Modify: `CMakeLists.txt`
- Modify: `src/core/main.cpp:326` and the shutdown path

**Interfaces:**
- Consumes: `SB::Api::BridgeInterface` and `kBridgeInterfaceVersion` from Task 1.
- Produces: the exported symbol `SB_GetBridgeInterface` in the built DLL. Task 3 asserts its presence.

- [ ] **Step 1: Write the failing test**

Create `tests/validate_bridge_abi_export.py`. It reads the PE export directory directly, so it needs no build tooling and no third-party module.

```python
#!/usr/bin/env python3
"""Assert SB_GetBridgeInterface is exported from the built DLL.

Parses the PE export directory with the standard library. A consumer resolves
this symbol with GetProcAddress, so a missing export is a silent runtime
failure rather than a link error, which is exactly what this catches.
"""

import pathlib
import struct
import sys

REQUIRED = [b"SB_GetBridgeInterface", b"SB_GetProxyInterface"]


def exported_names(path: pathlib.Path) -> list[bytes]:
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE image")
    optional_magic = struct.unpack_from("<H", data, pe + 24)[0]
    # 0x20B is PE32+, where the data directory starts 16 bytes later.
    dir_offset = pe + 24 + (112 if optional_magic == 0x20B else 96)
    export_rva, _ = struct.unpack_from("<II", data, dir_offset)
    if export_rva == 0:
        return []

    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    sections = []
    base = pe + 24 + opt_size
    for i in range(section_count):
        off = base + i * 40
        virt_addr, raw_size, raw_ptr = struct.unpack_from("<II4xI", data, off + 12)
        sections.append((virt_addr, raw_size, raw_ptr))

    def to_file_offset(rva: int) -> int:
        for virt_addr, raw_size, raw_ptr in sections:
            if virt_addr <= rva < virt_addr + raw_size:
                return raw_ptr + (rva - virt_addr)
        raise ValueError(f"unmapped RVA {rva:#x}")

    table = to_file_offset(export_rva)
    name_count = struct.unpack_from("<I", data, table + 24)[0]
    names_rva = struct.unpack_from("<I", data, table + 32)[0]
    names_off = to_file_offset(names_rva)

    out = []
    for i in range(name_count):
        rva = struct.unpack_from("<I", data, names_off + i * 4)[0]
        start = to_file_offset(rva)
        end = data.index(b"\0", start)
        out.append(data[start:end])
    return out


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: validate_bridge_abi_export.py <path-to-SkyrimBridge.dll>")
        return 2
    dll = pathlib.Path(sys.argv[1])
    if not dll.is_file():
        print(f"FAIL: DLL is absent: {dll}")
        return 1

    names = exported_names(dll)
    failures = [n.decode() for n in REQUIRED if n not in names]
    for missing in failures:
        print(f"FAIL: {missing} is not exported")
    if not failures:
        print(f"validate_bridge_abi_export: {len(names)} exports, all required present")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
```

Also create `tests/validate_bridge_abi_implementation.py` as a source-level
contract check for `src/core/BridgeApi.cpp` while the DLL is still awkward to
load outside the SKSE runtime. It must fail unless:

- `BridgeInterface g_interface` initializes all six v1 fields, including a
  non-null `&CopyFrameDataImpl` callback appended after `&IsFrameValidImpl`.
- `GetFrameDataImpl` returns a published snapshot buffer, never the mutating
  live `SB::GetMutableData()` block.
- `CopyFrameDataImpl` rejects null destinations and undersized buffers, copies
  exactly `sizeof(SB::AllData)`, and writes a frame index coherent with the
  copied snapshot when `frameIndex` is non-null.
- `MarkFramePublished(const SB::AllData& publishedData)` copies the sanitized
  frame into a synchronized published snapshot/double-buffer before advancing
  the public frame counter.
- `CMakeLists.txt` adds repository `include/` to the plugin target include
  paths and defines `SKYRIMBRIDGE_BUILDING_DLL` for the producer target.

- [ ] **Step 2: Run the test to verify it fails**

Build first, then run against the built DLL:

```bash
python tests/validate_bridge_abi_export.py build/Release/SkyrimBridge.dll
python tests/validate_bridge_abi_implementation.py
```

Expected: export validator fails with "SB_GetBridgeInterface is not exported".
`SB_GetProxyInterface` passes, which confirms the parser works rather than
reporting a false negative on both. The implementation validator fails because
`src/core/BridgeApi.cpp` is absent.

- [ ] **Step 3: Implement the entry point**

Create `src/core/BridgeApi.cpp`:

```cpp
#include "SkyrimBridgeAPI.h"

#include <atomic>
#include <cstring>
#include <mutex>

#include "core/BridgeData.h"

namespace SB::Api
{
    namespace
    {
        std::atomic<uint64_t> g_frameIndex{0};
        std::atomic<bool>     g_frameValid{false};

        std::mutex           g_snapshotMutex;
        SB::AllData          g_snapshots[2]{};
        std::atomic<uint32_t> g_publishedSlot{0};
        uint64_t             g_publishedFrameIndex = 0;

        uint64_t GetFrameIndexImpl()
        {
            return g_frameIndex.load(std::memory_order_acquire);
        }

        const SB::AllData* GetFrameDataImpl()
        {
            if (!g_frameValid.load(std::memory_order_acquire)) {
                return nullptr;
            }
            const uint32_t slot = g_publishedSlot.load(std::memory_order_acquire);
            return &g_snapshots[slot];
        }

        bool IsFrameValidImpl()
        {
            return g_frameValid.load(std::memory_order_acquire);
        }

        bool CopyFrameDataImpl(void* destination, uint32_t destinationSize, uint64_t* frameIndex)
        {
            if (!destination || destinationSize < sizeof(SB::AllData)) {
                return false;
            }

            std::lock_guard lock(g_snapshotMutex);
            if (!g_frameValid.load(std::memory_order_acquire)) {
                return false;
            }

            const uint32_t slot = g_publishedSlot.load(std::memory_order_acquire);
            std::memcpy(destination, &g_snapshots[slot], sizeof(SB::AllData));
            if (frameIndex) {
                *frameIndex = g_publishedFrameIndex;
            }
            return true;
        }

        BridgeInterface g_interface{
            kBridgeInterfaceVersion,
            static_cast<uint32_t>(sizeof(SB::AllData)),
            &GetFrameIndexImpl,
            &GetFrameDataImpl,
            &IsFrameValidImpl,
            &CopyFrameDataImpl,
        };
    }

    void MarkFramePublished(const SB::AllData& publishedData)
    {
        std::lock_guard lock(g_snapshotMutex);

        const uint32_t currentSlot = g_publishedSlot.load(std::memory_order_relaxed);
        const uint32_t nextSlot = 1U - currentSlot;
        g_snapshots[nextSlot] = publishedData;
        g_publishedSlot.store(nextSlot, std::memory_order_release);

        ++g_publishedFrameIndex;
        g_frameIndex.store(g_publishedFrameIndex, std::memory_order_release);
        g_frameValid.store(true, std::memory_order_release);
    }

    void MarkTeardown()
    {
        g_frameValid.store(false, std::memory_order_release);
    }
}

extern "C" SB_BRIDGE_API SB::Api::BridgeInterface* SB_GetBridgeInterface()
{
    return &SB::Api::g_interface;
}
```

This implementation must publish only the copied `publishedData` snapshot.
`GetFrameDataImpl` may expose the current snapshot pointer for short-lived
compatibility, but it must never return `&SB::GetMutableData()` or any other
mutating live producer block. `CopyFrameDataImpl` is the coherent consumer path:
it takes the same lock used by publication, copies one complete `SB::AllData`
snapshot, and returns the matching `g_publishedFrameIndex`.

Declare the two lifecycle helpers at the end of the `SB::Api` namespace in `include/SkyrimBridgeAPI.h`, inside a block marked internal so consumers do not call them:

```cpp
namespace SB::Api
{
    // Internal. Called by the publish path, not by consumers.
    void MarkFramePublished(const SB::AllData& publishedData);
    void MarkTeardown();
}
```

Add `src/core/BridgeApi.cpp` to the plugin target's source list in
`CMakeLists.txt`. Also add repository `include/` to that target's include
directories so `#include "SkyrimBridgeAPI.h"` resolves, and define
`SKYRIMBRIDGE_BUILDING_DLL=1` for the producer target so `SB_BRIDGE_API`
expands to `__declspec(dllexport)` only while building SkyrimBridge.

- [ ] **Step 4: Call the lifecycle hooks from the existing publish path**

The frame loop lives in `src/core/main.cpp`. It acquires the block at line 255 with `SB::AllData& data = SB::GetMutableData();`, sanitizes it at line 313 with `SanitizeAllData(data)`, and pushes to ENB at line 326 with `ENBInterface::PushAllData(data)`.

Add the publish mark immediately after line 326, so the counter only advances once the block is fully populated and sanitized:

```cpp
    ENBInterface::PushAllData(data);
    SB::Api::MarkFramePublished(data);
```

Add the teardown mark in the plugin's shutdown path, before any state the block points at is released:

```cpp
    SB::Api::MarkTeardown();
```

Include `SkyrimBridgeAPI.h` in `src/core/main.cpp`.

This is the only change to existing behaviour. It copies the sanitized frame
into the synchronized published snapshot after the ENB push has already
happened; it does not alter any value the ENB path reads, and it cannot reorder
or delay that push.

- [ ] **Step 5: Run the focused tests to verify they pass**

```bash
python tests/validate_bridge_abi_header.py
python tests/validate_bridge_abi_export.py build/Release/SkyrimBridge.dll
python tests/validate_bridge_abi_implementation.py
```

Expected: all three report all cases passed. The implementation validator must
prove the v1 `CopyFrameData` callback is non-null in the initializer and that
published copies come from the synchronized snapshot path rather than
`SB::GetMutableData()`.

- [ ] **Step 6: Verify the ENB path is unchanged**

Run the existing validator suite:

```bash
python tests/validate_command_protocol.py
```

Expected: unchanged from its pre-task result. If this regresses, the lifecycle hook was placed inside the ENB publish loop rather than after it.

- [ ] **Step 7: Commit**

```bash
git add include/SkyrimBridgeAPI.h src/core/BridgeApi.cpp src/core/main.cpp CMakeLists.txt tests/validate_bridge_abi_export.py tests/validate_bridge_abi_implementation.py
git commit -m "feat: export the versioned bridge state interface"
```

---

### Task 3: Document the consumer contract

**Files:**
- Create: `docs/BRIDGE-ABI.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: everything from Tasks 1 and 2.
- Produces: the document W3's feature implementer reads before writing the consumer side.

- [ ] **Step 1: Write the document**

Create `docs/BRIDGE-ABI.md` covering: the resolve sequence, the two checks a consumer must perform before dereferencing, thread and lifetime rules, the versioning policy, and a complete working consumer snippet.

```cpp
// Complete consumer example. Compiles against SkyrimBridgeAPI.h, links nothing.
SB::Api::BridgeInterface* AcquireBridge()
{
    HMODULE mod = GetModuleHandleW(L"SkyrimBridge.dll");
    if (!mod) {
        return nullptr;  // SkyrimBridge is not installed. This is not an error.
    }
    auto get = reinterpret_cast<SB::Api::BridgeInterface* (*)()>(
        GetProcAddress(mod, "SB_GetBridgeInterface"));
    if (!get) {
        return nullptr;  // Older SkyrimBridge without the ABI.
    }
    auto* api = get();
    if (!api || api->version != SB::Api::kBridgeInterfaceVersion) {
        return nullptr;  // Version we cannot read.
    }
    if (api->allDataSize != sizeof(SB::AllData)) {
        return nullptr;  // Layout mismatch. Never dereference past this.
    }
    return api;
}
```

State plainly that a consumer must treat an absent bridge as a supported configuration and fall back to its own data, so no downstream feature hard-depends on this mod.

- [ ] **Step 2: Add the README section**

Under the existing "For ENB preset authors" heading in `README.md`, add a sibling section for shader frameworks other than ENBSeries, naming the exported entry point and linking `docs/BRIDGE-ABI.md`. Keep it in user register with no local paths, per the shipping posture.

- [ ] **Step 3: Verify the snippet compiles**

Save the snippet to a scratch translation unit that includes only `SkyrimBridgeAPI.h` and `<windows.h>`, and compile it:

```bash
cl /std:c++23 /c /I include /I src scratch_consumer.cpp
```

Expected: compiles with no errors. A documented snippet that does not compile is worse than no snippet.

- [ ] **Step 4: Commit**

```bash
git add docs/BRIDGE-ABI.md README.md
git commit -m "docs: document the bridge state ABI consumer contract"
```

---

## Notes for the executor

Tasks are strictly ordered. Task 2 cannot be tested before Task 1's header exists, and Task 3 documents what Task 2 exports.

This workstream has value independent of Community Shaders. Under ENBSeries it decouples state production from the ENB SDK, so a future host needs a reader rather than a rewrite. Do not defer it pending any upstream decision.
