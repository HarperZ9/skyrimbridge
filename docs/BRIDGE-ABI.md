# SkyrimBridge bridge state ABI

SkyrimBridge exports a small, versioned in-process ABI for consumers that want
the same per-frame `SB::AllData` state block that SkyrimBridge publishes to ENB.
Consumers resolve the ABI dynamically from `SkyrimBridge.dll`; they do not link
against SkyrimBridge, and they must treat the bridge as optional.

This header is C++ because the payload type is `SB::AllData`. The exported entry
point itself has C linkage:

```cpp
SB_GetBridgeInterface
```

## Consumer contract

Use this sequence before reading any frame data:

1. Call `GetModuleHandleW(L"SkyrimBridge.dll")`.
2. If the module is absent, keep running through your own data path.
3. Call `GetProcAddress(mod, "SB_GetBridgeInterface")`.
4. If the symbol is absent, keep running through your own data path.
5. Call the function and reject a null interface pointer.
6. Check `api->version == SB::Api::kBridgeInterfaceVersion`.
7. Check `api->allDataSize == sizeof(SB::AllData)`.
8. Prefer `api->CopyFrameData(...)` for reads. Treat `false` as a normal
   fallback condition.

Do not dereference or copy `SB::AllData` after a version or size mismatch. A
different version or a different `allDataSize` means your consumer was compiled
against a layout the loaded DLL did not return.

SkyrimBridge is an optional provider. Downstream mods and shader frameworks must
keep their own native, spatial, identity, or host-specific fallback rather than
hard-depending on this mod.

## Recommended read path

`CopyFrameData` is the coherent consumer path. It copies one complete published
snapshot into a caller-owned buffer and, when requested, returns the frame index
that matches that copied snapshot.

Use the frame index to detect repeated frames. Do not combine a separately read
`GetFrameIndex()` value with a raw `GetFrameData()` pointer and assume they are
coherent. If you need a matching frame counter and data block, use
`CopyFrameData`.

`CopyFrameData` returns `false` when:

- the destination pointer is null;
- the destination size is smaller than `api->allDataSize`;
- no frame has been published yet;
- teardown has begun;
- the frame becomes invalid before the copy can complete.

All of those states are supported runtime states. Fall back cleanly and try
again on a later frame if your feature can use delayed data.

## Raw pointer lifetime

`GetFrameData` is present for short-lived compatibility reads. It returns a
pointer to a caller-thread-local snapshot, not to the producer's mutating live
block.

The pointer is valid only:

- on the same thread that called `GetFrameData`;
- until that same thread calls `GetFrameData` again;
- until that thread exits;
- while the loaded SkyrimBridge DLL and returned interface remain valid.

Do not share that pointer across threads. Do not store it as long-lived state.
Do not treat it as paired with a separately read frame index. If data must cross
threads, survive past the current call, or be associated with a frame counter,
call `CopyFrameData` into memory you own.

## Validity and teardown

`IsFrameValid()` is false before the first published frame and after teardown has
begun. A true result is advisory, because teardown can race a later read. The
return value from `CopyFrameData` is the authoritative result for a copy attempt.

Treat every unsupported or unavailable state as a normal fallback:

| Condition | Consumer behavior |
|---|---|
| `SkyrimBridge.dll` is absent | Use your own data path. |
| `SB_GetBridgeInterface` is absent | Older SkyrimBridge build. Use your own data path. |
| The returned interface is null | Use your own data path. |
| `version` is unsupported | Do not read the block. Use your own data path. |
| `allDataSize` differs from `sizeof(SB::AllData)` | Do not read the block. Use your own data path. |
| Required function pointer is null | Treat the provider as unavailable. |
| `IsFrameValid()` is false | No current frame. Use fallback data for this frame. |
| `CopyFrameData()` returns false | Copy failed or the frame became invalid. Use fallback data for this frame. |
| Teardown has begun | Stop reading from the bridge and use your own data path. |

## Versioning policy

`SB::Api::kBridgeInterfaceVersion` is bumped on ABI additions. Existing fields
are never renumbered or reordered. New fields append to the end of
`SB::Api::BridgeInterface`.

A consumer that only supports the current header should require an exact version
match and an exact `allDataSize` match. If a future consumer intentionally
supports multiple interface versions, it should branch by version and verify the
expected size for each layout before reading.

## Complete consumer example

This translation unit includes only `<windows.h>` and `SkyrimBridgeAPI.h`, links
nothing from SkyrimBridge, and returns `false` on every unsupported or invalid
condition.

```cpp
#include <windows.h>

#include "SkyrimBridgeAPI.h"

SB::Api::BridgeInterface* AcquireBridge()
{
    HMODULE mod = GetModuleHandleW(L"SkyrimBridge.dll");
    if (!mod) {
        return nullptr;
    }

    FARPROC proc = GetProcAddress(mod, "SB_GetBridgeInterface");
    if (!proc) {
        return nullptr;
    }

    auto get = reinterpret_cast<SB::Api::BridgeInterface* (*)()>(proc);
    SB::Api::BridgeInterface* api = get();
    if (!api) {
        return nullptr;
    }

    if (api->version != SB::Api::kBridgeInterfaceVersion) {
        return nullptr;
    }

    if (api->allDataSize != sizeof(SB::AllData)) {
        return nullptr;
    }

    if (!api->GetFrameIndex || !api->GetFrameData ||
        !api->IsFrameValid || !api->CopyFrameData) {
        return nullptr;
    }

    return api;
}

bool TryReadSkyrimBridgeFrame(SB::AllData& frame, uint64_t* frameIndex)
{
    SB::Api::BridgeInterface* api = AcquireBridge();
    if (!api) {
        return false;
    }

    if (!api->IsFrameValid()) {
        return false;
    }

    uint64_t copiedFrameIndex = 0;
    if (!api->CopyFrameData(&frame,
            static_cast<uint32_t>(sizeof(frame)),
            &copiedFrameIndex)) {
        return false;
    }

    if (frameIndex) {
        *frameIndex = copiedFrameIndex;
    }

    return true;
}
```

## Boundaries

This ABI does not replace the ENB shader parameter contract. ENB preset authors
should continue to use `shaders/SkyrimBridge.fxh`, including the documented
`SB_Retain` usage in `docs/parameters.md`.

This ABI also does not change the project's credits, provenance, Kitsuune
interoperability rules, or public-release exclusions. The public build does not
ship the private native replacement-suite implementation. It may still detect
the original Kitsuune plugins for compatibility/deferral paths, and the
provenance and distribution posture in `CREDITS.md` remains authoritative.
