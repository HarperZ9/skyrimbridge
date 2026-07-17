#pragma once
//=============================================================================
//  ENBInterface.h — Runtime resolution of the ENBSeries SDK
//
//  Based on the public ENB SDK by Boris Vorontsov (enbdev.com).
//  ENB for Skyrim SE/AE ships as d3d11.dll and exports C functions that
//  plugins resolve at runtime to read/write shader parameters and register
//  per-frame callbacks. Type values below match the SDK 1002 header.
//=============================================================================

#include "BridgeData.h"
#include <cstdint>
#include <cstring>

namespace ENBInterface
{
    // ── ENB SDK type definitions ─────────────────────────────────────────

    // Matches ENBCallbackType in the SDK header.
    enum class CallbackType : int
    {
        EndFrame = 1,    // end of frame, before present
        BeginFrame = 2,  // after present
        PreSave = 3,     // before config save
        PostLoad = 4,    // after parameters created and loaded
        // v1001:
        OnInit = 5,      // mod fully initialized
        OnExit = 6,      // game/mod closing
        PreReset = 7,    // display objects being destroyed
        PostReset = 8    // display objects re-created
    };

    // Matches ENBParameterType in the SDK header.
    enum class ENBParameterType : long
    {
        ENBParam_NONE       = 0,
        ENBParam_FLOAT      = 1,
        ENBParam_INT        = 2,
        ENBParam_HEX        = 3,
        ENBParam_BOOL       = 4,
        ENBParam_COLOR3     = 5,
        ENBParam_COLOR4     = 6,
        ENBParam_VECTOR3    = 7,
        ENBParam_FORCEDWORD = 0x7fffffff
    };

    // Matches ENBParameter in the SDK header:
    // Data at 0x00, Size at 0x10, Type at 0x14.
    struct ENBParameter
    {
        unsigned char    Data[16];
        unsigned long    Size;
        ENBParameterType Type;

        ENBParameter()
            : Size(0), Type(ENBParameterType::ENBParam_NONE)
        {
            for (int k = 0; k < 16; k++) Data[k] = 0;
        }
    };

    // Matches ENBStateType in the SDK header (v1001).
    enum class ENBStateType : long
    {
        IsEditorActive     = 1,  // mod editor window open
        IsEffectsWndActive = 2   // shader effects window open
    };

    // Matches ENBRenderInfo in the SDK header (v1001). Members are
    // ENB-wrapped D3D11 objects; treat as opaque unless you know better.
    struct ENBRenderInfo
    {
        void*         d3d11device;         // ID3D11Device
        void*         d3d11devicecontext;  // ID3D11DeviceContext
        void*         dxgiswapchain;       // IDXGISwapChain
        unsigned long ScreenSizeX;
        unsigned long ScreenSizeY;
    };

    using ENBCallbackFunction = void(__stdcall*)(int a_callbackType);

    using _ENBGetSDKVersion       = long(__stdcall*)();
    using _ENBGetVersion          = long(__stdcall*)();
    using _ENBSetCallbackFunction = void(__stdcall*)(ENBCallbackFunction a_func);
    using _ENBGetParameter        = int(__stdcall*)(const char*, const char*, const char*, ENBParameter*);
    using _ENBSetParameter        = int(__stdcall*)(const char*, const char*, const char*, ENBParameter*);
    // v1001:
    using _ENBGetState            = long(__stdcall*)(ENBStateType);
    using _ENBGetRenderInfo       = ENBRenderInfo*(__stdcall*)();
    using _ENBGetGameIdentifier   = long(__stdcall*)();
    // Undocumented: DirtyHack(3) reloads the ENB config from disk. Confirmed
    // exported by the ENB d3d11.dll; resolved by name at runtime. Version-
    // fragile, so calls are guarded on the pointer being non-null.
    using _ENBDirtyHack           = void(__stdcall*)(long);

    // ── Resolved function pointers ───────────────────────────────────────
    // Populated by Init(); valid for the process lifetime afterwards.
    inline _ENBGetSDKVersion       GetSDKVersion       = nullptr;
    inline _ENBGetVersion          GetVersion          = nullptr;
    inline _ENBSetCallbackFunction SetCallbackFunction = nullptr;
    inline _ENBGetParameter        GetParameter        = nullptr;
    inline _ENBSetParameter        SetParameter        = nullptr;
    inline _ENBGetState            GetState            = nullptr;
    inline _ENBGetRenderInfo       GetRenderInfo       = nullptr;
    inline _ENBGetGameIdentifier   GetGameIdentifier   = nullptr;
    inline _ENBDirtyHack           DirtyHack           = nullptr;

    // Ask ENB to reload its config from disk (DirtyHack selector 3).
    // No-op when the export is unavailable. Returns whether it fired.
    bool ReloadConfig();

    // ── Lifecycle ────────────────────────────────────────────────────────

    // Resolves the SDK exports from the loaded d3d11.dll. Call after ENB
    // has loaded (kPostPostLoad or later). Returns true when the required
    // functions (SetCallbackFunction, SetParameter) resolved.
    bool Init();

    // True after a successful Init().
    bool IsLoaded();

    // True when the ENB build supports editor state queries.
    bool IsGUISupported();

    // Editor window state (false when GetState is unavailable).
    bool IsEditorOpen();
    bool IsEffectsWindowOpen();

    // ── Push statistics ──────────────────────────────────────────────────

    struct PushStats
    {
        int dirtyParams      = 0;  // float4s changed this frame
        int totalParams      = 0;  // kParamCount
        int setParamCalls    = 0;  // SetParameter calls this frame
        int setParamSuccess  = 0;  // successes (first push only)
        int setParamFail     = 0;  // failures (first push only)
        std::size_t pushCount = 0; // frames pushed so far
        bool firstPushDone   = false;
    };

    const PushStats& GetPushStats();

    // ── SkyrimBridge data push ───────────────────────────────────────────
    // Pushes every parameter in kParamTable to every shader in
    // kTargetShaders, with per-frame dirty tracking.
    void PushAllData(const SB::AllData& a_data);
}
