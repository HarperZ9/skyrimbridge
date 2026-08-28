#include "ENBGuiIntegration.h"
#include "ENBInterface.h"
#include "BridgeData.h"

#include <SKSE/SKSE.h>
#include <bit>
#include <cmath>

// Raw Windows API, same pattern as ENBInterface.cpp.
extern "C" {
    __declspec(dllimport) void* __stdcall GetModuleHandleW(const wchar_t*);
    __declspec(dllimport) void* __stdcall GetProcAddress(void*, const char*);
}

namespace ENBGuiIntegration
{
    //─────────────────────────────────────────────────────────────────────
    //  AntTweakBar surface. ENB 0.504's d3d11.dll exports the ATB 1.16
    //  API; the editor windows are ATB bars in ENB's own context, so a
    //  bar we create is drawn/hidden by ENB with the editor toggle.
    //  Type ids are the ATB 1.16 TwType values; wrong ids fail per-var
    //  and are counted + logged, never fatal.
    //─────────────────────────────────────────────────────────────────────

    struct TwBar;   // opaque

    static constexpr int TW_TYPE_BOOL32  = 4;
    static constexpr int TW_TYPE_INT32   = 10;
    static constexpr int TW_TYPE_UINT32  = 11;
    static constexpr int TW_TYPE_FLOAT   = 12;
    static constexpr int TW_TYPE_COLOR3F = 15;

    using _TwNewBar       = TwBar* (*)(const char* barName);
    using _TwDeleteBar    = int (*)(TwBar* bar);
    using _TwAddVarRO     = int (*)(TwBar* bar, const char* name, int type, const void* var, const char* def);
    using _TwAddSeparator = int (*)(TwBar* bar, const char* name, const char* def);
    using _TwDefine       = int (*)(const char* def);
    using _TwGetLastError = const char* (*)();

    static _TwNewBar       TwNewBar       = nullptr;
    static _TwDeleteBar    TwDeleteBar    = nullptr;
    static _TwAddVarRO     TwAddVarRO     = nullptr;
    static _TwAddSeparator TwAddSeparator = nullptr;
    static _TwDefine       TwDefine       = nullptr;
    static _TwGetLastError TwGetLastError = nullptr;

    static bool   g_resolved     = false;
    static TwBar* g_bar          = nullptr;
    static bool   g_gaveUp       = false;
    static int    g_barAttempts  = 0;
    static unsigned int g_updateTick = 0;

    //─────────────────────────────────────────────────────────────────────
    //  Snapshot the bar reads. TwAddVarRO stores the pointer and reads it
    //  at draw time, so refreshing these fields every frame keeps the
    //  display live while the editor is open.
    //─────────────────────────────────────────────────────────────────────

    struct GuiSnapshot
    {
        // Status / wire
        unsigned int frameCount;
        float        fps;
        float        deltaMs;
        unsigned int pushCount;
        int          dirtyParams;
        int          paramTotal;
        int          wireOk;         // BOOL32
        int          enbBinary;
        int          sdkVersion;
        // Celestial
        float gameHour, dayProgress, sunElevDeg;
        float sunNdcX, sunNdcY;
        int   sunOnScreen;
        float masserPhase, secundaPhase;
        // Weather
        float windSpeed, windDirDeg;
        int   precipType;
        float precipIntensity, wetness, cloudCover, transitionPct, lightningFlash;
        int   isRainy, isSnowy;
        // Atmosphere & fog
        float ambientColor[3];
        float ambientIntensity, sunlightScale;
        float fogNearDist, fogFarDist, fogPower;
        // Player
        float health, stamina, magicka, moveSpeed;
        int   inCombat, weaponDrawn, underwater;
        float submersion;
        // Camera
        float fovDeg, nearClip, farClip, aspect;
        // Interior
        int   isInterior;
        float interiorAmbient[3];
        float intFogNear, intFogFar;
        // Effects / image space
        int   nightEye, detectLife;
        float slowTime;
        float isSaturation, isBrightness, isContrast;
        int   hasIMOD;
        // Nearby lights
        int   lightCount;
        float nearestLightDist, lightFlux;
        // UI state
        int   inMenu, inDialogue, isLoading, gamePaused;
    };

    static GuiSnapshot g_snap{};

    struct VarDef
    {
        const char* name;
        int         type;
        const void* ptr;
        const char* def;
    };

    static const VarDef kVars[] = {
        { "FrameCount",   TW_TYPE_UINT32, &g_snap.frameCount,     "group=Status label='Frame count'" },
        { "FPS",          TW_TYPE_FLOAT,  &g_snap.fps,            "group=Status label='FPS' precision=1" },
        { "DeltaMs",      TW_TYPE_FLOAT,  &g_snap.deltaMs,        "group=Status label='Frame time (ms)' precision=2" },
        { "PushCount",    TW_TYPE_UINT32, &g_snap.pushCount,      "group=Status label='Pushes'" },
        { "DirtyParams",  TW_TYPE_INT32,  &g_snap.dirtyParams,    "group=Status label='Dirty params'" },
        { "ParamTotal",   TW_TYPE_INT32,  &g_snap.paramTotal,     "group=Status label='Param total'" },
        { "WireOk",       TW_TYPE_BOOL32, &g_snap.wireOk,         "group=Status label='Wire OK'" },
        { "EnbBinary",    TW_TYPE_INT32,  &g_snap.enbBinary,      "group=Status label='ENB binary'" },
        { "SdkVersion",   TW_TYPE_INT32,  &g_snap.sdkVersion,     "group=Status label='ENB SDK'" },

        { "GameHour",     TW_TYPE_FLOAT,  &g_snap.gameHour,       "group=Celestial label='Game hour' precision=2" },
        { "DayProgress",  TW_TYPE_FLOAT,  &g_snap.dayProgress,    "group=Celestial label='Day progress' precision=3" },
        { "SunElev",      TW_TYPE_FLOAT,  &g_snap.sunElevDeg,     "group=Celestial label='Sun elevation (deg)' precision=1" },
        { "SunNdcX",      TW_TYPE_FLOAT,  &g_snap.sunNdcX,        "group=Celestial label='Sun NDC X' precision=3" },
        { "SunNdcY",      TW_TYPE_FLOAT,  &g_snap.sunNdcY,        "group=Celestial label='Sun NDC Y' precision=3" },
        { "SunOnScreen",  TW_TYPE_BOOL32, &g_snap.sunOnScreen,    "group=Celestial label='Sun on screen'" },
        { "MasserPhase",  TW_TYPE_FLOAT,  &g_snap.masserPhase,    "group=Celestial label='Masser phase' precision=2" },
        { "SecundaPhase", TW_TYPE_FLOAT,  &g_snap.secundaPhase,   "group=Celestial label='Secunda phase' precision=2" },

        { "WindSpeed",    TW_TYPE_FLOAT,  &g_snap.windSpeed,      "group=Weather label='Wind speed' precision=2" },
        { "WindDir",      TW_TYPE_FLOAT,  &g_snap.windDirDeg,     "group=Weather label='Wind dir (deg)' precision=0" },
        { "PrecipType",   TW_TYPE_INT32,  &g_snap.precipType,     "group=Weather label='Precip (0/1/2)'" },
        { "PrecipInt",    TW_TYPE_FLOAT,  &g_snap.precipIntensity,"group=Weather label='Precip intensity' precision=2" },
        { "Wetness",      TW_TYPE_FLOAT,  &g_snap.wetness,        "group=Weather label='Surface wetness' precision=2" },
        { "CloudCover",   TW_TYPE_FLOAT,  &g_snap.cloudCover,     "group=Weather label='Cloud cover' precision=2" },
        { "Transition",   TW_TYPE_FLOAT,  &g_snap.transitionPct,  "group=Weather label='Transition' precision=2" },
        { "Lightning",    TW_TYPE_FLOAT,  &g_snap.lightningFlash, "group=Weather label='Flash intensity' precision=2" },
        { "IsRainy",      TW_TYPE_BOOL32, &g_snap.isRainy,        "group=Weather label='Rainy'" },
        { "IsSnowy",      TW_TYPE_BOOL32, &g_snap.isSnowy,        "group=Weather label='Snowy'" },

        { "AmbColor",     TW_TYPE_COLOR3F,&g_snap.ambientColor,   "group=Atmosphere label='Ambient color'" },
        { "AmbIntensity", TW_TYPE_FLOAT,  &g_snap.ambientIntensity,"group=Atmosphere label='Ambient intensity' precision=2" },
        { "SunScale",     TW_TYPE_FLOAT,  &g_snap.sunlightScale,  "group=Atmosphere label='Sunlight scale' precision=2" },
        { "FogNear",      TW_TYPE_FLOAT,  &g_snap.fogNearDist,    "group=Atmosphere label='Fog near' precision=0" },
        { "FogFar",       TW_TYPE_FLOAT,  &g_snap.fogFarDist,     "group=Atmosphere label='Fog far' precision=0" },
        { "FogPower",     TW_TYPE_FLOAT,  &g_snap.fogPower,       "group=Atmosphere label='Fog power' precision=2" },

        { "Health",       TW_TYPE_FLOAT,  &g_snap.health,         "group=Player label='Health %' precision=1" },
        { "Stamina",      TW_TYPE_FLOAT,  &g_snap.stamina,        "group=Player label='Stamina %' precision=1" },
        { "Magicka",      TW_TYPE_FLOAT,  &g_snap.magicka,        "group=Player label='Magicka %' precision=1" },
        { "MoveSpeed",    TW_TYPE_FLOAT,  &g_snap.moveSpeed,      "group=Player label='Speed (u/s)' precision=0" },
        { "InCombat",     TW_TYPE_BOOL32, &g_snap.inCombat,       "group=Player label='In combat'" },
        { "WeaponDrawn",  TW_TYPE_BOOL32, &g_snap.weaponDrawn,    "group=Player label='Weapon drawn'" },
        { "Underwater",   TW_TYPE_BOOL32, &g_snap.underwater,     "group=Player label='Underwater'" },
        { "Submersion",   TW_TYPE_FLOAT,  &g_snap.submersion,     "group=Player label='Submersion' precision=2" },

        { "FOV",          TW_TYPE_FLOAT,  &g_snap.fovDeg,         "group=Camera label='FOV (deg)' precision=1" },
        { "NearClip",     TW_TYPE_FLOAT,  &g_snap.nearClip,       "group=Camera label='Near clip' precision=1" },
        { "FarClip",      TW_TYPE_FLOAT,  &g_snap.farClip,        "group=Camera label='Far clip' precision=0" },
        { "Aspect",       TW_TYPE_FLOAT,  &g_snap.aspect,         "group=Camera label='Aspect' precision=3" },

        { "IsInterior",   TW_TYPE_BOOL32, &g_snap.isInterior,     "group=Interior label='Interior'" },
        { "IntAmbient",   TW_TYPE_COLOR3F,&g_snap.interiorAmbient,"group=Interior label='Ambient color'" },
        { "IntFogNear",   TW_TYPE_FLOAT,  &g_snap.intFogNear,     "group=Interior label='Fog near' precision=0" },
        { "IntFogFar",    TW_TYPE_FLOAT,  &g_snap.intFogFar,      "group=Interior label='Fog far' precision=0" },

        { "NightEye",     TW_TYPE_BOOL32, &g_snap.nightEye,       "group=Effects label='Night eye'" },
        { "DetectLife",   TW_TYPE_BOOL32, &g_snap.detectLife,     "group=Effects label='Detect life'" },
        { "SlowTime",     TW_TYPE_FLOAT,  &g_snap.slowTime,       "group=Effects label='Slow-time factor' precision=2" },
        { "ISSat",        TW_TYPE_FLOAT,  &g_snap.isSaturation,   "group=Effects label='IS saturation' precision=2" },
        { "ISBright",     TW_TYPE_FLOAT,  &g_snap.isBrightness,   "group=Effects label='IS brightness' precision=2" },
        { "ISContrast",   TW_TYPE_FLOAT,  &g_snap.isContrast,     "group=Effects label='IS contrast' precision=2" },
        { "HasIMOD",      TW_TYPE_BOOL32, &g_snap.hasIMOD,        "group=Effects label='IMOD active'" },

        { "LightCount",   TW_TYPE_INT32,  &g_snap.lightCount,     "group=Lights label='Nearby lights'" },
        { "LightDist",    TW_TYPE_FLOAT,  &g_snap.nearestLightDist,"group=Lights label='Nearest dist' precision=0" },
        { "LightFlux",    TW_TYPE_FLOAT,  &g_snap.lightFlux,      "group=Lights label='Total flux' precision=1" },

        { "InMenu",       TW_TYPE_BOOL32, &g_snap.inMenu,         "group=UI label='In menu'" },
        { "InDialogue",   TW_TYPE_BOOL32, &g_snap.inDialogue,     "group=UI label='In dialogue'" },
        { "Loading",      TW_TYPE_BOOL32, &g_snap.isLoading,      "group=UI label='Loading'" },
        { "Paused",       TW_TYPE_BOOL32, &g_snap.gamePaused,     "group=UI label='Game paused'" },
    };

    bool Init()
    {
        void* enbModule = GetModuleHandleW(L"d3d11.dll");
        if (!enbModule)
            enbModule = GetModuleHandleW(L"d3d11_enb.dll");
        if (!enbModule)
            return false;

        TwNewBar       = reinterpret_cast<_TwNewBar>(GetProcAddress(enbModule, "TwNewBar"));
        TwDeleteBar    = reinterpret_cast<_TwDeleteBar>(GetProcAddress(enbModule, "TwDeleteBar"));
        TwAddVarRO     = reinterpret_cast<_TwAddVarRO>(GetProcAddress(enbModule, "TwAddVarRO"));
        TwAddSeparator = reinterpret_cast<_TwAddSeparator>(GetProcAddress(enbModule, "TwAddSeparator"));
        TwDefine       = reinterpret_cast<_TwDefine>(GetProcAddress(enbModule, "TwDefine"));
        TwGetLastError = reinterpret_cast<_TwGetLastError>(GetProcAddress(enbModule, "TwGetLastError"));

        g_resolved = TwNewBar && TwDeleteBar && TwAddVarRO && TwDefine;
        if (!g_resolved) {
            SKSE::log::info("SkyrimBridge GUI: ATB exports not found in ENB module — no editor bar");
            return false;
        }

        if (ENBInterface::GetSDKVersion)
            g_snap.sdkVersion = static_cast<int>(ENBInterface::GetSDKVersion());
        if (ENBInterface::GetVersion)
            g_snap.enbBinary = static_cast<int>(ENBInterface::GetVersion());

        SKSE::log::info("SkyrimBridge GUI: ATB exports resolved — bar will be created on first frame");
        return true;
    }

    static const char* LastAtbError()
    {
        const char* err = TwGetLastError ? TwGetLastError() : nullptr;
        return err ? err : "(no error text)";
    }

    static void TryCreateBar()
    {
        ++g_barAttempts;
        g_bar = TwNewBar("SkyrimBridge");
        if (!g_bar) {
            if (g_barAttempts == 1)
                SKSE::log::warn("SkyrimBridge GUI: TwNewBar failed: {}", LastAtbError());
            if (g_barAttempts >= 10) {
                g_gaveUp = true;
                SKSE::log::warn("SkyrimBridge GUI: giving up after {} TwNewBar attempts", g_barAttempts);
            }
            return;
        }

        int failed = 0;
        for (const auto& v : kVars) {
            if (!TwAddVarRO(g_bar, v.name, v.type, v.ptr, v.def))
                ++failed;
        }
        if (failed > 0)
            SKSE::log::warn("SkyrimBridge GUI: {}/{} variables failed to register, last ATB error: {}",
                failed, std::size(kVars), LastAtbError());

        TwDefine(" SkyrimBridge label='SkyrimBridge 3.0.0' position='24 96' size='330 560' valueswidth=96 refresh=0.1 ");
        TwDefine(" SkyrimBridge/Status opened=true ");
        TwDefine(" SkyrimBridge/Celestial opened=true ");
        TwDefine(" SkyrimBridge/Weather opened=false ");
        TwDefine(" SkyrimBridge/Atmosphere opened=false ");
        TwDefine(" SkyrimBridge/Player opened=false ");
        TwDefine(" SkyrimBridge/Camera opened=false ");
        TwDefine(" SkyrimBridge/Interior opened=false ");
        TwDefine(" SkyrimBridge/Effects opened=false ");
        TwDefine(" SkyrimBridge/Lights opened=false ");
        TwDefine(" SkyrimBridge/UI opened=false ");

        SKSE::log::info("SkyrimBridge GUI: bar created ({} vars, {} failed) — visible with the ENB editor",
            std::size(kVars) - failed, failed);
    }

    static void RefreshSnapshot(const SB::AllData& d)
    {
        constexpr float kRadToDeg = 57.2957795f;

        const auto& stats = ENBInterface::GetPushStats();
        g_snap.pushCount   = static_cast<unsigned int>(stats.pushCount);
        g_snap.dirtyParams = stats.dirtyParams;
        g_snap.paramTotal  = stats.totalParams;
        g_snap.wireOk      = (stats.setParamSuccess > 0) ? 1 : 0;

        g_snap.frameCount = static_cast<unsigned int>(d.render.FrameInfo.x);
        g_snap.deltaMs    = d.render.FrameInfo.y * 1000.0f;
        g_snap.fps        = (d.render.FrameInfo.y > 1e-6f) ? 1.0f / d.render.FrameInfo.y : 0.0f;

        g_snap.gameHour     = d.celestial.TimeData.x;
        g_snap.dayProgress  = d.celestial.TimeData.w;
        g_snap.sunElevDeg   = d.celestial.SunDirection.w * kRadToDeg;
        g_snap.sunNdcX      = d.derived.SunNDC.x;
        g_snap.sunNdcY      = d.derived.SunNDC.y;
        g_snap.sunOnScreen  = (d.derived.SunNDC.z > 0.5f) ? 1 : 0;
        g_snap.masserPhase  = d.celestial.MasserDirection.w;
        g_snap.secundaPhase = d.celestial.SecundaDirection.w;

        g_snap.windSpeed       = d.weather.Wind.x;
        g_snap.windDirDeg      = d.weather.Wind.y * kRadToDeg;
        g_snap.precipType      = static_cast<int>(d.weather.Precipitation.x + 0.5f);
        g_snap.precipIntensity = d.weather.Precipitation.y;
        g_snap.wetness         = d.weather.PrecipSurface.x;
        g_snap.cloudCover      = d.weather.CloudCover.x;
        g_snap.transitionPct   = d.weather.Transition.x;
        g_snap.lightningFlash  = d.weather.Lightning.z;
        g_snap.isRainy         = (d.weather.Flags.z > 0.5f) ? 1 : 0;
        g_snap.isSnowy         = (d.weather.Flags.w > 0.5f) ? 1 : 0;

        g_snap.ambientColor[0]  = d.atmosphere.Ambient.x;
        g_snap.ambientColor[1]  = d.atmosphere.Ambient.y;
        g_snap.ambientColor[2]  = d.atmosphere.Ambient.z;
        g_snap.ambientIntensity = d.atmosphere.Ambient.w;
        g_snap.sunlightScale    = d.atmosphere.SunlightColor.w;
        g_snap.fogNearDist      = d.fog.NearColor.w;
        g_snap.fogFarDist       = d.fog.FarColor.w;
        g_snap.fogPower         = d.fog.Density.x;

        g_snap.health      = d.player.Vitals.x * 100.0f;
        g_snap.stamina     = d.player.Vitals.y * 100.0f;
        g_snap.magicka     = d.player.Vitals.z * 100.0f;
        g_snap.moveSpeed   = d.player.Movement.x;
        g_snap.inCombat    = (std::bit_cast<std::uint32_t>(d.player.Combat.x) & 0x1u) ? 1 : 0;
        g_snap.weaponDrawn = (d.equipment.Flags.x > 0.5f) ? 1 : 0;
        g_snap.underwater  = (d.player.Water.x > 0.5f) ? 1 : 0;
        g_snap.submersion  = d.player.Water.z;

        g_snap.fovDeg   = d.camera.Params.x * kRadToDeg;
        g_snap.nearClip = d.camera.Params.y;
        g_snap.farClip  = d.camera.Params.z;
        g_snap.aspect   = d.camera.Params.w;

        g_snap.isInterior         = (d.interior.IsInterior.x > 0.5f) ? 1 : 0;
        g_snap.interiorAmbient[0] = d.interior.AmbientColor.x;
        g_snap.interiorAmbient[1] = d.interior.AmbientColor.y;
        g_snap.interiorAmbient[2] = d.interior.AmbientColor.z;
        g_snap.intFogNear         = d.interior.InteriorFogDist.x;
        g_snap.intFogFar          = d.interior.InteriorFogDist.y;

        g_snap.nightEye     = (d.effects.VisionEffects.x > 0.5f) ? 1 : 0;
        g_snap.detectLife   = (d.effects.VisionEffects.y > 0.5f) ? 1 : 0;
        g_snap.slowTime     = d.effects.TimeEffects.x;
        g_snap.isSaturation = d.imageSpace.Cinematic.x;
        g_snap.isBrightness = d.imageSpace.Cinematic.y;
        g_snap.isContrast   = d.imageSpace.Cinematic.z;
        g_snap.hasIMOD      = (d.imageSpace.IMOD.x > 0.5f) ? 1 : 0;

        g_snap.lightCount       = static_cast<int>(d.lights.Summary.x + 0.5f);
        g_snap.nearestLightDist = d.lights.Summary.y;
        g_snap.lightFlux        = d.lights.Summary.z;

        g_snap.inMenu     = (d.uiState.Menus.x > 0.5f) ? 1 : 0;
        g_snap.inDialogue = (d.uiState.Menus.y > 0.5f) ? 1 : 0;
        g_snap.isLoading  = (d.uiState.HUD.w > 0.5f) ? 1 : 0;
        g_snap.gamePaused = (d.render.StencilInfo.w > 0.5f) ? 1 : 0;
    }

    void Update(const SB::AllData& a_data)
    {
        if (!g_resolved)
            return;

        ++g_updateTick;
        if (!g_bar) {
            if (g_gaveUp)
                return;
            // First attempt immediately, then every 120 frames.
            if (g_barAttempts > 0 && (g_updateTick % 120) != 0)
                return;
            TryCreateBar();
            if (!g_bar)
                return;
        }

        RefreshSnapshot(a_data);
    }

    void OnPreReset()
    {
        if (g_bar && TwDeleteBar) {
            TwDeleteBar(g_bar);
            g_bar = nullptr;
        }
    }

    void OnPostReset()
    {
        // Bar was destroyed in OnPreReset; allow recreation attempts again.
        g_barAttempts = 0;
        g_gaveUp = false;
    }

    void Shutdown()
    {
        OnPreReset();
        g_resolved = false;
    }
}
