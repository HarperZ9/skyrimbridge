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
//  Prefer CopyFrameData for cross-thread or long-lived reads.
//
//  This is a C++ header exposing a C-linkage symbol for GetProcAddress
//  consumers; it is not a pure-C header because the payload type is SB::AllData.
//=============================================================================

#include <stdint.h>

#include "core/BridgeData.h"

#if defined(SKYRIMBRIDGE_BUILDING_DLL)
#  define SB_BRIDGE_API __declspec(dllexport)
#else
#  define SB_BRIDGE_API
#endif

namespace SB::Api
{
    // Bump on any additive change. Never reorder or renumber existing fields.
    inline constexpr uint32_t kBridgeInterfaceVersion = 1U;

    struct BridgeInterface
    {
        // Always kBridgeInterfaceVersion for the build that returned this.
        uint32_t version;

        // sizeof(SB::AllData) for this build. A consumer compiled against a
        // different BridgeData.h will see a mismatch and must refuse the data.
        uint32_t allDataSize;

        // Monotonic, incremented once per published frame. A consumer polls
        // this to tell a fresh frame from a repeat without diffing the block.
        uint64_t (*GetFrameIndex)();

        // Points at the live state block. Valid only while IsFrameValid()
        // returns true and only on the thread that published it. Never freed
        // by the caller.
        const SB::AllData* (*GetFrameData)();

        // False before the first publish, and after teardown has begun.
        bool (*IsFrameValid)();

        // Copies the latest published frame into destination when the caller's
        // buffer is at least allDataSize bytes. On success, frameIndex receives
        // the copied frame counter when non-null.
        bool (*CopyFrameData)(void* destination, uint32_t destinationSize, uint64_t* frameIndex);
    };

    static_assert(__is_standard_layout(SB::Float4),
        "SB::Float4 must stay standard-layout for the public ABI");
    static_assert(__is_trivially_copyable(SB::Float4),
        "SB::Float4 must stay trivially copyable for the public ABI");
    static_assert(__is_standard_layout(SB::AllData),
        "SB::AllData must stay standard-layout for the public ABI");
    static_assert(__is_trivially_copyable(SB::AllData),
        "SB::AllData must stay trivially copyable for the public ABI");
    static_assert(__is_standard_layout(BridgeInterface),
        "BridgeInterface must stay standard-layout for the public ABI");
    static_assert(__is_trivially_copyable(BridgeInterface),
        "BridgeInterface must stay trivially copyable for the public ABI");
}

extern "C" SB_BRIDGE_API SB::Api::BridgeInterface* SB_GetBridgeInterface();
