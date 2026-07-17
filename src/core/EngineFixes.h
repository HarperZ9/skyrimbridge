#pragma once
//=============================================================================
//  EngineFixes — binary-level engine patches applied via Address Library
//
//  Recovered from the operator's prior reverse-engineering research (Playground
//  corpus). P0 optimizations for Skyrim SE/AE, resolved through the Address
//  Library so they apply against the DRM-decrypted code in the live process
//  (the on-disk .text is SteamStub-encrypted; runtime patching is the only path
//  and is exactly what this does):
//
//    - BSSpinLock::Lock threshold reduction (10000 -> 1000), AE REL::ID(68233),
//      +0x3C. Cuts busy-waiting by ~90% in contended paths, preserving the
//      spin -> yield -> wait semantics. Validated before writing.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <cstdint>

namespace SB
{
    class EngineFixes
    {
    public:
        static EngineFixes& Get();

        // Install all engine patches. Call from the kDataLoaded handler.
        // Returns the number of patches successfully applied.
        std::uint32_t Install();

        bool IsSpinLockPatched() const { return m_spinLockPatched; }

    private:
        EngineFixes() = default;

        bool PatchSpinLockThreshold();

        bool m_installed = false;
        bool m_spinLockPatched = false;
    };
}
