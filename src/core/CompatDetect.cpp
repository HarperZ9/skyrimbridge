//=============================================================================
//  CompatDetect.cpp — External plugin compatibility detection
//
//  At kPostLoad, scans for loaded DLLs that overlap with SkyrimBridge features.
//  When found, SkyrimBridge defers to the external plugin.
//
//  Detected plugins:
//    NativeEditorID Fix  → PG skips EditorID vtable hooks, proxies lookups
//    po3_Tweaks          → PG skips EditorID vtable hooks, proxies lookups
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "CompatDetect.h"

#include <SKSE/SKSE.h>
#include <Windows.h>

namespace SB
{

// ── Main detection entry point ────────────────────────────────────────────

void CompatDetect::Detect()
{
    DetectEditorIDPlugins();
    DetectSBProxy();

    // ── Summary ─────────────────────────────────────────────────────────
    if (m_detected.empty()) {
        SKSE::log::info("CompatDetect: no overlapping plugins found — "
            "SkyrimBridge will handle all features natively");
    } else {
        SKSE::log::info("CompatDetect: {} overlapping plugin(s) found — "
            "compatible mode active", m_detected.size());
    }
}

// ── EditorID plugin detection ─────────────────────────────────────────────

void CompatDetect::DetectEditorIDPlugins()
{
    // NativeEditorID Fix
    {
        const wchar_t* names[] = {
            L"NativeEditorIDFix.dll",
            L"NativeEditorIDFixNG.dll",
            L"NativeEditorIDFix",
            L"NativeEditorIDFixNG",
        };

        for (auto* name : names) {
            HMODULE mod = GetModuleHandleW(name);
            if (mod) {
                m_hasNativeEditorIDFix = true;

                auto fn = reinterpret_cast<GetFormEditorIDFn>(
                    GetProcAddress(mod, "GetFormEditorID"));
                if (fn)
                    m_externalGetEditorID = fn;

                m_detected.push_back({
                    "NativeEditorID Fix",
                    "NativeEditorIDFix.dll",
                    "EditorID Cache"
                });

                SKSE::log::info("CompatDetect: NativeEditorID Fix detected — "
                    "SkyrimBridge will defer EditorID caching to it (export={})",
                    fn ? "found" : "not found");
                break;
            }
        }
    }

    // po3_Tweaks
    {
        HMODULE mod = GetModuleHandleW(L"po3_Tweaks.dll");
        if (!mod)
            mod = GetModuleHandleW(L"po3_Tweaks");

        if (mod) {
            m_hasPo3EditorIDCache = true;

            if (!m_externalGetEditorID) {
                auto fn = reinterpret_cast<GetFormEditorIDFn>(
                    GetProcAddress(mod, "GetFormEditorID"));
                if (fn)
                    m_externalGetEditorID = fn;
            }

            m_detected.push_back({
                "powerofthree's Tweaks",
                "po3_Tweaks.dll",
                "EditorID Cache"
            });

            SKSE::log::info("CompatDetect: po3_Tweaks detected — "
                "SkyrimBridge will defer EditorID caching to it");
        }
    }
}

// ── SB Proxy detection ──────────────────────────────────────────────────

void CompatDetect::DetectSBProxy()
{
    HMODULE d3d11 = GetModuleHandleA("d3d11.dll");
    if (!d3d11) return;

    auto sbProxy = GetProcAddress(d3d11, "SB_GetProxyInterface");
    if (sbProxy) {
        m_hasSBProxy = true;
        SKSE::log::info("CompatDetect: SkyrimBridge d3d11 proxy detected");
    }
}

// ── Notification text ─────────────────────────────────────────────────────

std::string CompatDetect::GetNotificationText() const
{
    if (m_detected.empty())
        return {};

    std::string text = "SkyrimBridge: compatible with ";

    for (size_t i = 0; i < m_detected.size(); ++i) {
        if (i > 0)
            text += ", ";
        text += m_detected[i].name;
    }

    return text;
}

}  // namespace SB
