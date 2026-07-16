#pragma once
//=============================================================================
//  EditorIDCache.h — NativeEditorID Fix integrated into SkyrimBridge
//
//  Hooks TESForm::SetFormEditorID (vtable 0x33) across all major form types
//  to cache editor IDs that the engine discards at runtime. Also populates
//  the engine's own editorID→form map so the console `help` command and
//  LookupByEditorID() work for all form types.
//
//  Replaces: NativeEditorID Fix (Kitsuune), po3's CacheEditorIDs tweak
//  Zero external dependencies — integrated natively into SkyrimBridge.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <string>
#include <unordered_map>
#include <mutex>

namespace SB
{

class EditorIDCache
{
public:
    static EditorIDCache& Get()
    {
        static EditorIDCache inst;
        return inst;
    }

    // Install vtable hooks on all major form types.
    // Must be called at SKSEPluginLoad or kPostLoad — before ESP/ESM loading.
    // If an external EditorID provider is detected, hooks are skipped
    // and lookups proxy through the external plugin's API instead.
    void Install();

    // Store a formID → editorID mapping (thread-safe)
    void Store(RE::FormID a_formID, const char* a_editorID);

    // Lookup an editor ID by formID (returns "" if not found)
    // If an external provider is active, queries it transparently.
    const std::string& Lookup(RE::FormID a_formID) const;
    const std::string& Lookup(const RE::TESForm* a_form) const;

    // Number of cached entries (0 if using external provider)
    size_t Size() const;

    // Is the cache installed and active?
    bool IsInstalled() const { return m_installed; }

    // Are we proxying through an external plugin?
    bool IsUsingExternalProvider() const { return m_externalProvider != nullptr; }

private:
    EditorIDCache() = default;

    // Install our own vtable hooks (native mode)
    void InstallNative();

    using GetFormEditorIDFn = const char*(*)(std::uint32_t);

    mutable std::mutex m_lock;
    std::unordered_map<RE::FormID, std::string> m_cache;
    mutable std::string m_proxyBuffer;  // temp storage for external lookups
    GetFormEditorIDFn m_externalProvider = nullptr;
    bool m_installed = false;
};

}  // namespace SB


//=============================================================================
//  DLL Export — compatible with NativeEditorID Fix and po3's Tweaks API
//
//  Other SKSE plugins can call this via:
//    auto func = GetProcAddress(GetModuleHandle(L"SkyrimBridge_v3"), "GetFormEditorID");
//    const char* editorID = func(formID);
//=============================================================================

extern "C" __declspec(dllexport) const char* GetFormEditorID(std::uint32_t a_formID);
