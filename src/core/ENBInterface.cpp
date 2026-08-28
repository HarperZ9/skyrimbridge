#include "ENBInterface.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <cstring>

// We need to access Windows API but CommonLibSSE-NG wraps it.
// Use forward declarations with extern "C" to call the raw API.
extern "C" {
    __declspec(dllimport) void* __stdcall GetModuleHandleW(const wchar_t*);
    __declspec(dllimport) void* __stdcall GetProcAddress(void*, const char*);
}

namespace ENBInterface
{
    static bool g_loaded = false;
    static bool g_guiSupported = false;
    static void* g_enbModule = nullptr;   // saved for diagnostics
    static PushStats g_pushStats{};

    // Push state, namespace-scope so InvalidateDirtyCache() and
    // PushAvailabilityZero() can reach it alongside PushAllData().
    static bool        g_firstPush = true;
    static std::size_t g_pushCount = 0;
    static SB::AllData g_prevData{};
    static bool        g_hasPrevData = false;

    const PushStats& GetPushStats()
    {
        return g_pushStats;
    }

    bool Init()
    {
        // ENB for Skyrim SE ships as d3d11.dll (DX11 wrapper).
        // Some setups use a proxy chain where the actual ENB is enbseries.dll.
        void* enbModule = GetModuleHandleW(L"d3d11.dll");
        if (!enbModule) {
            enbModule = GetModuleHandleW(L"d3d11_enb.dll");
        }
        if (!enbModule) {
            SKSE::log::info("SkyrimBridge: d3d11.dll not found (ENB not installed?)");
            return false;
        }

        // Check if this d3d11.dll is actually ENB (it exports ENBGetSDKVersion)
        GetSDKVersion = reinterpret_cast<_ENBGetSDKVersion>(
            GetProcAddress(enbModule, "ENBGetSDKVersion"));

        if (!GetSDKVersion) {
            SKSE::log::info("SkyrimBridge: d3d11.dll is not ENBSeries (no SDK exports)");
            return false;
        }

        long sdkVer = GetSDKVersion();
        SKSE::log::info("SkyrimBridge: ENB SDK version {}", sdkVer);

        if (sdkVer < 1000) {
            SKSE::log::warn("SkyrimBridge: ENB SDK version {} too old (need >= 1000)", sdkVer);
            return false;
        }

        // Resolve core functions
        GetVersion = reinterpret_cast<_ENBGetVersion>(
            GetProcAddress(enbModule, "ENBGetVersion"));

        SetCallbackFunction = reinterpret_cast<_ENBSetCallbackFunction>(
            GetProcAddress(enbModule, "ENBSetCallbackFunction"));

        GetParameter = reinterpret_cast<_ENBGetParameter>(
            GetProcAddress(enbModule, "ENBGetParameter"));

        SetParameter = reinterpret_cast<_ENBSetParameter>(
            GetProcAddress(enbModule, "ENBSetParameter"));

        // Resolve v1001 SDK functions (exported by ENB v504+)
        GetState = reinterpret_cast<_ENBGetState>(
            GetProcAddress(enbModule, "ENBGetState"));

        GetRenderInfo = reinterpret_cast<_ENBGetRenderInfo>(
            GetProcAddress(enbModule, "ENBGetRenderInfo"));

        GetGameIdentifier = reinterpret_cast<_ENBGetGameIdentifier>(
            GetProcAddress(enbModule, "ENBGetGameIdentifier"));

        DirtyHack = reinterpret_cast<_ENBDirtyHack>(
            GetProcAddress(enbModule, "DirtyHack"));

        if (!SetCallbackFunction || !SetParameter) {
            SKSE::log::error("SkyrimBridge: Failed to resolve ENB SDK functions");
            return false;
        }

        if (GetVersion) {
            SKSE::log::info("SkyrimBridge: ENBSeries binary version {}", GetVersion());
        }

        // Check GUI support — ATB functions (TwNewBar etc.) are resolved separately
        // by ENBGuiIntegration. ENBGetState provides editor state detection.
        g_guiSupported = true;  // ATB is available if we got this far
        SKSE::log::info("SkyrimBridge: GUI support: yes (GetState={}, GetRenderInfo={}, GetGameIdentifier={})",
            GetState ? "yes" : "no",
            GetRenderInfo ? "yes" : "no",
            GetGameIdentifier ? "yes" : "no");

        g_enbModule = enbModule;
        g_loaded = true;
        return true;
    }

    bool IsGUISupported()
    {
        return g_guiSupported;
    }

    bool IsLoaded()
    {
        return g_loaded;
    }

    bool IsEditorOpen()
    {
        if (GetState)
            return GetState(ENBStateType::IsEditorActive) != 0;
        return false;  // can't detect — assume closed
    }

    bool IsEffectsWindowOpen()
    {
        if (GetState)
            return GetState(ENBStateType::IsEffectsWndActive) != 0;
        return false;
    }

    void PushAllData(const SB::AllData& a_data)
    {
        if (!SetParameter)
            return;

        const auto* rawData = reinterpret_cast<const char*>(&a_data);
        const auto* prevRaw = reinterpret_cast<const char*>(&g_prevData);

        // Reusable ENBParameter struct — all SB params are float4 (COLOR4, 16 bytes)
        ENBParameter param;
        param.Size = 16;
        param.Type = ENBParameterType::ENBParam_COLOR4;

        int dirtyCount = 0;
        int totalSuccess = 0;
        int totalFail = 0;

        for (std::size_t i = 0; i < SB::kParamCount; ++i) {
            const auto& entry = SB::kParamTable[i];

            // Skip unchanged parameters (after first frame)
            if (g_hasPrevData && std::memcmp(rawData + entry.offset, prevRaw + entry.offset, 16) == 0)
                continue;

            ++dirtyCount;
            std::memcpy(param.Data, rawData + entry.offset, 16);

            for (const auto* shader : SB::kTargetShaders) {
                int result = SetParameter(nullptr, shader, entry.name, &param);
                if (g_firstPush) {
                    if (result) ++totalSuccess; else ++totalFail;
                }
            }
        }

        if (g_firstPush) {
            SKSE::log::info("SkyrimBridge: first push — {}/{} succeeded ({} dirty params x {} shaders)",
                totalSuccess, dirtyCount * std::size(SB::kTargetShaders),
                dirtyCount, std::size(SB::kTargetShaders));
            if (totalFail > 0) {
                SKSE::log::warn("SkyrimBridge: first push — {} SetParameter calls failed", totalFail);
            }
            g_firstPush = false;
        }

        // Store current frame for next-frame comparison
        g_prevData = a_data;
        g_hasPrevData = true;
        ++g_pushCount;

        // Update push stats for debug GUI
        g_pushStats.dirtyParams     = dirtyCount;
        g_pushStats.totalParams     = static_cast<int>(SB::kParamCount);
        g_pushStats.setParamCalls   = dirtyCount * static_cast<int>(std::size(SB::kTargetShaders));
        g_pushStats.pushCount       = g_pushCount;
        g_pushStats.firstPushDone   = !g_firstPush || g_pushCount > 0;
        if (g_pushCount == 1) {
            g_pushStats.setParamSuccess = totalSuccess;
            g_pushStats.setParamFail    = totalFail;
        }

        // Periodic dirty-ratio log (sparse: only at milestones)
        if (g_pushCount == 300 || g_pushCount == 3000) {
            SKSE::log::info("SkyrimBridge: push #{} — {}/{} params dirty",
                g_pushCount, dirtyCount, SB::kParamCount);
        }
    }

    void InvalidateDirtyCache()
    {
        g_hasPrevData = false;
    }

    void PushAvailabilityZero()
    {
        if (!SetParameter)
            return;

        ENBParameter param;   // ctor zeroes Data
        param.Size = 16;
        param.Type = ENBParameterType::ENBParam_COLOR4;

        for (const auto* shader : SB::kTargetShaders) {
            SetParameter(nullptr, shader, "SB_Render_Frame", &param);
        }

        // The live heartbeat must come back on the very next frame even
        // though the cached copy still holds the pre-save value.
        InvalidateDirtyCache();
    }

    bool ReloadConfig()
    {
        if (!DirtyHack)
            return false;
        DirtyHack(3);   // selector 3 = reload config from disk
        return true;
    }
}
