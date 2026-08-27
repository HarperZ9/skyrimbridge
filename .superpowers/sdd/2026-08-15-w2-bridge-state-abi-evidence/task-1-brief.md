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

