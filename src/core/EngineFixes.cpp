//=============================================================================
//  EngineFixes.cpp — binary-level engine patches (Address Library)
//=============================================================================

#include "EngineFixes.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace SB
{
    EngineFixes& EngineFixes::Get()
    {
        static EngineFixes instance;
        return instance;
    }

    std::uint32_t EngineFixes::Install()
    {
        if (m_installed) return 0;
        m_installed = true;

        std::uint32_t count = 0;
        if (PatchSpinLockThreshold()) ++count;

        SKSE::log::info("EngineFixes: {} patch(es) applied", count);
        return count;
    }

    bool EngineFixes::PatchSpinLockThreshold()
    {
        // BSSpinLock::Lock — AE RELOCATION_ID 68233.
        // The function contains `cmp r8d, 0x2710` (spin count vs 10000); the
        // 4-byte immediate 0x00002710 sits at +0x3C from the function start.
        // Reducing to 1000 (0x3E8) cuts busy-waiting ~90% while preserving the
        // spin -> yield -> wait semantics.
        REL::Relocation<std::uintptr_t> lockFunc{ REL::ID(68233) };
        auto addr = lockFunc.address();
        if (addr == 0) {
            SKSE::log::warn("EngineFixes: BSSpinLock::Lock address resolution failed");
            return false;
        }

        // Validate before writing: the live (decrypted) bytes at +0x3C must be
        // 10000. If not, the layout differs from the researched build; skip.
        std::uint32_t current = *reinterpret_cast<const std::uint32_t*>(addr + 0x3C);
        if (current != 10000) {
            SKSE::log::warn("EngineFixes: BSSpinLock threshold at {:#x}+0x3C = {} "
                "(expected 10000), skipping", addr, current);
            return false;
        }

        REL::safe_write<std::uint32_t>(addr + 0x3C, 1000);
        m_spinLockPatched = true;
        SKSE::log::info("EngineFixes: BSSpinLock threshold reduced 10000 -> 1000 at {:#x}+0x3C", addr);
        return true;
    }
}
