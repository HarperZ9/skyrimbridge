//=============================================================================
//  EnbLightInventoryFix.cpp — native ENB Light Inventory Fix
//
//  Reproduces the reversed detour (see the protected ELIF behavioral spec):
//  for the inventory-3D preview node, temporarily zero each light's emittance
//  field (+0x11c), force a NiAVObject transform update, run the original pass,
//  then restore. Offsets/IDs are AE 1.6.x; installs are SEH-guarded.
//=============================================================================

#include "EnbLightInventoryFix.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <excpt.h>      // EXCEPTION_EXECUTE_HANDLER (SEH)
#include <utility>
#include <vector>

namespace SB
{
    // Evidenced struct offsets (AE 1.6.x — see ELIF spec §3).
    static constexpr std::ptrdiff_t kKind      = 0x34;    // this->kind, gated == 1
    static constexpr std::ptrdiff_t kCount     = 0x148;   // entry count
    static constexpr std::ptrdiff_t kArray     = 0x78;    // 0x20-stride node array
    static constexpr std::ptrdiff_t kStride    = 0x20;
    static constexpr std::ptrdiff_t kEmittance = 0x11c;   // light field zeroed/restored

    using FuncT = std::uint32_t (*)(void*, void*);
    static std::uintptr_t g_origA = 0;
    static std::uintptr_t g_origB = 0;

    // Recurse the previewed item's 3D; save+zero every light's emittance field.
    static void CollectAndZero(RE::NiAVObject* obj,
                               std::vector<std::pair<RE::NiAVObject*, std::uint32_t>>& out,
                               int depth)
    {
        if (!obj || depth > 24) return;
        if (auto* light = ::netimmerse_cast<RE::NiLight*>(obj)) {
            auto& field = *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(light) + kEmittance);
            out.emplace_back(obj, field);
            field = 0;
            return;
        }
        if (auto* node = obj->AsNode()) {
            for (auto& child : node->GetChildren())
                CollectAndZero(child.get(), out, depth + 1);
        }
    }

    // Object-using body — kept out of the __try frame (SEH cannot unwind the
    // std::vector). A fault here is caught by RunDetour and degraded to orig().
    static std::uint32_t DetourBody(void* a_this, void* a_arg, FuncT orig)
    {
        char* self = reinterpret_cast<char*>(a_this);
        if (*reinterpret_cast<std::uint32_t*>(self + kKind) != 1)
            return orig(a_this, a_arg);
        std::uint32_t count = *reinterpret_cast<std::uint32_t*>(self + kCount);
        if (count == 0)
            return orig(a_this, a_arg);

        void* nodePtr = *reinterpret_cast<void**>(self + kArray + static_cast<std::size_t>(count - 1) * kStride);
        auto* node = reinterpret_cast<RE::NiAVObject*>(nodePtr);
        if (!node)
            return orig(a_this, a_arg);

        std::vector<std::pair<RE::NiAVObject*, std::uint32_t>> saved;
        CollectAndZero(node, saved, 0);

        RE::NiUpdateData upd{};
        node->Update(upd);
        std::uint32_t ret = orig(a_this, a_arg);
        for (auto& [obj, val] : saved)
            *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(obj) + kEmittance) = val;
        node->Update(upd);
        return ret;
    }

    static std::uint32_t RunDetour(void* a_this, void* a_arg, FuncT orig)
    {
        if (!a_this) return orig(a_this, a_arg);
        __try {
            return DetourBody(a_this, a_arg, orig);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return orig(a_this, a_arg);   // any fault -> behave like the vanilla pass
        }
    }

    static std::uint32_t DetourA(void* a_this, void* a_arg)
    {
        return RunDetour(a_this, a_arg, reinterpret_cast<FuncT>(g_origA));
    }
    static std::uint32_t DetourB(void* a_this, void* a_arg)
    {
        return RunDetour(a_this, a_arg, reinterpret_cast<FuncT>(g_origB));
    }

    EnbLightInventoryFix& EnbLightInventoryFix::Get()
    {
        static EnbLightInventoryFix instance;
        return instance;
    }

    void EnbLightInventoryFix::Install()
    {
        if (m_installed) return;

        if (!REL::Module::IsAE()) {
            SKSE::log::info("EnbLightInventoryFix: AE-only (reversed IDs are AE 1.6.x); "
                "not installed on this runtime");
            return;
        }

        try {
            REL::Relocation<std::uintptr_t> targetA{ REL::ID(51769) };
            REL::Relocation<std::uintptr_t> targetB{ REL::ID(51770) };

            SKSE::AllocTrampoline(1u << 7);
            auto& tr = SKSE::GetTrampoline();
            g_origA = tr.write_branch<5>(targetA.address(), &DetourA);
            g_origB = tr.write_branch<5>(targetB.address(), &DetourB);

            m_installed = (g_origA != 0 && g_origB != 0);
            SKSE::log::info("EnbLightInventoryFix: installed inventory-3D light hooks ({})",
                m_installed ? "ok" : "partial");
        } catch (const std::exception& e) {
            SKSE::log::warn("EnbLightInventoryFix: install failed ({}); disabled", e.what());
        } catch (...) {
            SKSE::log::warn("EnbLightInventoryFix: install failed (unknown); disabled");
        }
    }
}
