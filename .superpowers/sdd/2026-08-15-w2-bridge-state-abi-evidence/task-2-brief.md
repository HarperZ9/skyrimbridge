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

