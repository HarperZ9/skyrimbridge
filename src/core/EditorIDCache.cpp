//=============================================================================
//  EditorIDCache.cpp — NativeEditorID Fix integrated into SkyrimBridge
//
//  Two modes of operation:
//
//  NATIVE MODE (no external EditorID plugin detected):
//    Hooks SetFormEditorID (vtable 0x33) on 85+ form types to cache editor
//    IDs the engine discards. Populates the engine's global editorID map.
//
//  PROXY MODE (NativeEditorID Fix or po3_Tweaks detected):
//    Skips all vtable hooks. Lookups are proxied through the external
//    plugin's exported GetFormEditorID(). Zero conflicts, zero overhead.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "EditorIDCache.h"
#include "CompatDetect.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace SB
{

//=============================================================================
//  Cache Operations
//=============================================================================

void EditorIDCache::Store(RE::FormID a_formID, const char* a_editorID)
{
    std::lock_guard lock(m_lock);
    m_cache.try_emplace(a_formID, a_editorID);
}

const std::string& EditorIDCache::Lookup(RE::FormID a_formID) const
{
    // Proxy mode: query the external plugin
    if (m_externalProvider) {
        const char* result = m_externalProvider(a_formID);
        if (result && result[0] != '\0') {
            std::lock_guard lock(m_lock);
            m_proxyBuffer = result;
            return m_proxyBuffer;
        }
        static const std::string empty;
        return empty;
    }

    // Native mode: query our own cache
    std::lock_guard lock(m_lock);
    auto it = m_cache.find(a_formID);
    if (it != m_cache.end())
        return it->second;

    static const std::string empty;
    return empty;
}

const std::string& EditorIDCache::Lookup(const RE::TESForm* a_form) const
{
    return a_form ? Lookup(a_form->GetFormID()) : Lookup(static_cast<RE::FormID>(0));
}

size_t EditorIDCache::Size() const
{
    std::lock_guard lock(m_lock);
    return m_cache.size();
}


//=============================================================================
//  SetFormEditorID Hook (native mode only)
//=============================================================================

struct SetFormEditorIDHook
{
    static bool thunk(RE::TESForm* a_this, const char* a_str)
    {
        if (a_str && a_str[0] != '\0' && !a_this->IsDynamicForm()) {
            EditorIDCache::Get().Store(a_this->GetFormID(), a_str);

            const auto& [map, lock] = RE::TESForm::GetAllFormsByEditorID();
            const RE::BSWriteLockGuard locker{ lock };
            if (map) {
                map->emplace(a_str, a_this);
            }
        }

        return func(a_this, a_str);
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

template <typename T>
static void HookFormType()
{
    REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
    SetFormEditorIDHook::func = vtbl.write_vfunc(0x33, SetFormEditorIDHook::thunk);
}


//=============================================================================
//  Install — detect external providers, then hook or defer
//=============================================================================

void EditorIDCache::Install()
{
    if (m_installed)
        return;

    // Run compatibility detection first
    auto& compat = CompatDetect::Get();
    compat.Detect();

    // If an external EditorID provider is loaded, proxy through it
    if (compat.HasExternalEditorIDProvider()) {
        m_externalProvider = compat.GetExternalEditorIDFunc();
        m_installed = true;

        if (m_externalProvider) {
            SKSE::log::info("EditorIDCache: external provider detected — "
                "proxying lookups (no vtable hooks installed)");
        } else {
            // Plugin is loaded but export not found — fall back to native
            SKSE::log::warn("EditorIDCache: external provider loaded but "
                "GetFormEditorID export not found — installing native hooks");
            m_externalProvider = nullptr;
            InstallNative();
        }
        return;
    }

    // No external provider — install our own hooks
    InstallNative();
}


//=============================================================================
//  InstallNative — hook all major form types (only when no external provider)
//=============================================================================

void EditorIDCache::InstallNative()
{
    // ── Objects ─────────────────────────────────────────────────────────
    HookFormType<RE::TESObjectARMO>();
    HookFormType<RE::TESObjectWEAP>();
    HookFormType<RE::TESObjectBOOK>();
    HookFormType<RE::TESObjectMISC>();
    HookFormType<RE::TESObjectSTAT>();
    HookFormType<RE::TESObjectACTI>();
    HookFormType<RE::TESObjectDOOR>();
    HookFormType<RE::TESObjectCONT>();
    HookFormType<RE::TESObjectLIGH>();
    HookFormType<RE::TESObjectTREE>();
    HookFormType<RE::TESObjectANIO>();
    HookFormType<RE::TESObjectARMA>();

    // ── Items ───────────────────────────────────────────────────────────
    HookFormType<RE::TESKey>();
    HookFormType<RE::TESSoulGem>();
    HookFormType<RE::TESAmmo>();
    HookFormType<RE::TESFlora>();
    HookFormType<RE::TESFurniture>();
    HookFormType<RE::TESGrass>();
    HookFormType<RE::AlchemyItem>();
    HookFormType<RE::IngredientItem>();
    HookFormType<RE::ScrollItem>();
    HookFormType<RE::EnchantmentItem>();
    HookFormType<RE::BGSApparatus>();

    // ── Actors / NPCs ───────────────────────────────────────────────────
    HookFormType<RE::TESNPC>();
    HookFormType<RE::TESLevCharacter>();
    HookFormType<RE::TESLevItem>();
    HookFormType<RE::TESLevSpell>();
    HookFormType<RE::TESClass>();
    HookFormType<RE::TESFaction>();
    HookFormType<RE::TESRace>();
    HookFormType<RE::TESCombatStyle>();

    // ── References ──────────────────────────────────────────────────────
    HookFormType<RE::TESObjectREFR>();

    // ── World / Environment ─────────────────────────────────────────────
    HookFormType<RE::TESWeather>();
    HookFormType<RE::TESClimate>();
    HookFormType<RE::TESRegion>();
    HookFormType<RE::TESWaterForm>();
    HookFormType<RE::TESImageSpace>();
    HookFormType<RE::TESImageSpaceModifier>();
    HookFormType<RE::TESLandTexture>();

    // ── Magic ───────────────────────────────────────────────────────────
    HookFormType<RE::SpellItem>();
    HookFormType<RE::TESShout>();
    HookFormType<RE::TESWordOfPower>();
    HookFormType<RE::EffectSetting>();

    // ── BGS Types ───────────────────────────────────────────────────────
    HookFormType<RE::BGSKeyword>();
    HookFormType<RE::BGSAction>();
    HookFormType<RE::BGSTextureSet>();
    HookFormType<RE::BGSHeadPart>();
    HookFormType<RE::BGSArtObject>();
    HookFormType<RE::BGSSoundDescriptorForm>();
    HookFormType<RE::BGSMusicType>();
    HookFormType<RE::BGSFootstep>();
    HookFormType<RE::BGSFootstepSet>();
    HookFormType<RE::BGSExplosion>();
    HookFormType<RE::BGSProjectile>();
    HookFormType<RE::BGSHazard>();
    HookFormType<RE::TESEffectShader>();
    HookFormType<RE::BGSDebris>();
    HookFormType<RE::BGSImpactData>();
    HookFormType<RE::BGSImpactDataSet>();
    HookFormType<RE::BGSEncounterZone>();
    HookFormType<RE::BGSLocation>();
    HookFormType<RE::BGSLocationRefType>();
    HookFormType<RE::BGSMessage>();
    HookFormType<RE::BGSLightingTemplate>();
    HookFormType<RE::BGSColorForm>();
    HookFormType<RE::BGSCameraShot>();
    HookFormType<RE::BGSCameraPath>();
    HookFormType<RE::BGSVoiceType>();
    HookFormType<RE::BGSMaterialType>();
    HookFormType<RE::BGSMovementType>();
    HookFormType<RE::BGSSoundOutput>();
    HookFormType<RE::BGSCollisionLayer>();
    HookFormType<RE::BGSEquipSlot>();
    HookFormType<RE::BGSOutfit>();
    HookFormType<RE::BGSRelationship>();
    HookFormType<RE::BGSAssociationType>();
    HookFormType<RE::BGSConstructibleObject>();
    HookFormType<RE::BGSListForm>();
    HookFormType<RE::BGSPerk>();
    HookFormType<RE::BGSReferenceEffect>();
    HookFormType<RE::BGSScene>();
    HookFormType<RE::BGSIdleMarker>();
    HookFormType<RE::BGSAddonNode>();
    HookFormType<RE::BGSDualCastData>();
    HookFormType<RE::BGSAcousticSpace>();
    HookFormType<RE::BGSMaterialObject>();
    HookFormType<RE::BGSLensFlare>();

    // ── Dialogue ────────────────────────────────────────────────────────
    HookFormType<RE::BGSDialogueBranch>();
    HookFormType<RE::TESTopic>();
    HookFormType<RE::TESTopicInfo>();

    // ── Quest / Story ───────────────────────────────────────────────────
    HookFormType<RE::TESQuest>();

    // ── Misc Engine Types ───────────────────────────────────────────────
    HookFormType<RE::TESIdleForm>();
    HookFormType<RE::TESPackage>();
    HookFormType<RE::TESSound>();
    HookFormType<RE::TESGlobal>();
    HookFormType<RE::TESLoadScreen>();
    HookFormType<RE::ActorValueInfo>();

    m_installed = true;

    SKSE::log::info("EditorIDCache: native mode — installed SetFormEditorID "
        "hooks on all form types");
}

}  // namespace SB


//=============================================================================
//  DLL Export — GetFormEditorID
//=============================================================================

extern "C" __declspec(dllexport) const char* GetFormEditorID(std::uint32_t a_formID)
{
    return SB::EditorIDCache::Get().Lookup(a_formID).c_str();
}
