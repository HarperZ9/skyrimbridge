//=============================================================================
//  CompatDetect.cpp — External plugin compatibility detection
//
//  At kPostLoad, scans for loaded DLLs that overlap with SkyrimBridge features.
//  When found, SkyrimBridge defers to the external plugin.
//
//  Detected plugins:
//    NativeEditorID Fix  → PG skips EditorID vtable hooks, proxies lookups
//    po3_Tweaks          → PG skips EditorID vtable hooks, proxies lookups
//    KreatE              → SB skips native KreatE profile/record application
//    ELIF                → SB skips its inventory-3D ENB light hooks
//    EVLaS / AELAS       → SB skips its celestial lighting model
//    ENBWorldspaceWeatherlists → SB skips its weatherlist routing
//
//  The Kitsuune-authored plugins above are the originals for features
//  SkyrimBridge also implements. When one is present it wins. See CREDITS.md.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "CompatDetect.h"

#include <SKSE/SKSE.h>
#include <Windows.h>

#include <cstddef>
#include <iterator>

namespace SB
{

// ── Main detection entry point ────────────────────────────────────────────

//  Detect() is safe to call more than once and is meant to be. SKSE plugins
//  are all loaded by kPostLoad, but KiLoader-hosted plugins (AELAS, ENB
//  Worldspace Weatherlists) can appear later, so callers re-probe at
//  kDataLoaded. Each plugin is recorded and logged only the first time it is
//  seen, so a repeat call can add a late arrival but never duplicates one.

void CompatDetect::Detect()
{
    const std::size_t before = m_detected.size();

    DetectEditorIDPlugins();
    DetectKitsuunePlugins();
    DetectSBProxy();

    // ── Summary ─────────────────────────────────────────────────────────
    if (m_detected.empty()) {
        if (!m_summaryLogged) {
            SKSE::log::info("CompatDetect: no overlapping plugins found — "
                "SkyrimBridge will handle all features natively");
        }
    } else if (m_detected.size() != before || !m_summaryLogged) {
        SKSE::log::info("CompatDetect: {} overlapping plugin(s) found — "
            "compatible mode active", m_detected.size());
    }

    m_summaryLogged = true;
}

// ── EditorID plugin detection ─────────────────────────────────────────────

void CompatDetect::DetectEditorIDPlugins()
{
    // NativeEditorID Fix
    if (!m_hasNativeEditorIDFix) {
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
    if (!m_hasPo3EditorIDCache) {
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

// ── Module probe ──────────────────────────────────────────────────────────

void* CompatDetect::ProbeModule(const wchar_t* const* names, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        if (HMODULE mod = GetModuleHandleW(names[i]))
            return mod;
    }
    return nullptr;
}

// ── Kitsuune plugin detection ─────────────────────────────────────────────
//
//  Each of these owns a feature that SkyrimBridge also implements natively.
//  The original is authoritative: when it is loaded we stand down, so a user
//  can run both without a conflict and without being asked to choose. Our
//  replacements exist for load orders that do not have them, not to displace
//  them. See CREDITS.md for the attribution and permission record.
//
//  ELIF, EVLaS and KreatE are SKSE plugins. AELAS and ENBWorldspaceWeatherlists
//  load through KiLoader, so KiLoader itself is probed as a coarse signal too.

void CompatDetect::DetectKitsuunePlugins()
{
    struct Probe
    {
        const wchar_t* const* names;
        std::size_t           count;
        bool*                 flag;
        const char*           displayName;
        const char*           dllName;
        const char*           feature;
    };

    static const wchar_t* kKreatE[]   = { L"KreatE.dll", L"KreatE" };
    static const wchar_t* kELIF[]     = { L"ELIF.dll", L"ELIF" };
    static const wchar_t* kEVLaS[]    = { L"EVLaS.dll", L"EVLaS" };
    static const wchar_t* kAELAS[]    = { L"AELAS.dll", L"AELAS" };
    static const wchar_t* kWWL[]      = { L"ENBWorldspaceWeatherlists.dll",
                                          L"ENBWorldspaceWeatherlists" };
    static const wchar_t* kKiLoader[] = { L"KiLoader.dll",
                                          L"KiLoaderSatelliteSKSE.dll",
                                          L"KiLoader" };

    const Probe probes[] = {
        { kKreatE,   std::size(kKreatE),   &m_hasKreatE,
          "KreatE", "KreatE.dll", "KreatE profile loading" },
        { kELIF,     std::size(kELIF),     &m_hasELIF,
          "ENB Light Inventory Fix (ELIF)", "ELIF.dll", "inventory-3D ENB light fix" },
        { kEVLaS,    std::size(kEVLaS),    &m_hasEVLaS,
          "EVLaS", "EVLaS.dll", "celestial lighting" },
        { kAELAS,    std::size(kAELAS),    &m_hasAELAS,
          "AELAS", "AELAS.dll", "celestial lighting" },
        { kWWL,      std::size(kWWL),      &m_hasWorldspaceWeatherlists,
          "ENB Worldspace Weatherlists", "ENBWorldspaceWeatherlists.dll",
          "per-worldspace weatherlist routing" },
    };

    for (const auto& p : probes) {
        if (*p.flag)            // already found on an earlier probe
            continue;
        if (!ProbeModule(p.names, p.count))
            continue;

        *p.flag = true;
        m_detected.push_back({ p.displayName, p.dllName, p.feature });

        SKSE::log::info("CompatDetect: {} detected — SkyrimBridge defers {} to it",
            p.displayName, p.feature);
    }

    // KiLoader itself is not an overlapping feature, only a host. Record it so
    // the log explains why a KiLoader-hosted plugin might appear or not.
    if (!m_hasKiLoader && ProbeModule(kKiLoader, std::size(kKiLoader))) {
        m_hasKiLoader = true;
        SKSE::log::info("CompatDetect: KiLoader present (host for AELAS / "
            "ENB Worldspace Weatherlists)");
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
