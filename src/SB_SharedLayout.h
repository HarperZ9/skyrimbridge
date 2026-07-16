#pragma once
//=============================================================================
//  SB_SharedLayout.h — Shared memory struct layout for SkyrimBridge
//
//  This header defines the memory layout shared between:
//    1. SkyrimBridge.dll    (SKSE plugin — writer)
//    2. external readers    (tools, overlays  — reader)
//
//  NO dependencies on SKSE, CommonLibSSE, or Windows.h.
//  Include from both DLL projects safely.
//
//  Author: Zain Dana Harper
//=============================================================================

#include "core/BridgeData.h"   // Float4, AllData, kParamTable, etc.
#include <cstdint>

namespace SB
{
    //=========================================================================
    //  Shared memory layout — must match between writer and reader
    //=========================================================================

    #pragma pack(push, 1)

    struct SharedMemoryHeader
    {
        uint32_t magic;           // 'SB01' = 0x53423031
        uint32_t version;         // Protocol version (increment on layout change)
        uint32_t structSize;      // sizeof(SB_SharedData) for validation
        uint32_t frameCount;      // Frame counter (monotonic)
        float    deltaTime;       // Seconds since last frame
        float    gameHour;        // Current game hour [0,24)
        uint32_t weatherFormID;   // Current weather TESWeather FormID
        uint8_t  weatherCategory; // WeatherCategory enum value
        uint8_t  isInterior;      // 1 if player is indoors
        uint8_t  isInMenu;        // 1 if any menu is open
        uint8_t  isLoading;       // 1 if loading screen active
        float    transitionPct;   // Weather transition progress [0,1]
        uint32_t padding[7];      // Alignment to 64 bytes
    };
    static_assert(sizeof(SharedMemoryHeader) == 64, "Header must be 64 bytes");

    struct SB_SharedData
    {
        SharedMemoryHeader header;
        AllData            allData;

        // Weather parameter computer output (Phase 2 integration)
        struct {
            float bloomIntensity;
            float bloomRadius;
            float adaptSpeed;
            float exposureBias;
            float saturation;
            float contrast;
            float colorTemp;
            float sharpen;
            float grain;
            float aoIntensity;
            float ssrIntensity;
            float godRayIntensity;
            float dofStrength;
            float lensDirt;
            float rainOnLens;
            float frostOnLens;
            float _pad[16];  // Room for expansion
        } weatherParams;
    };

    #pragma pack(pop)

    // Constants
    static constexpr uint32_t kSharedMemMagic    = 0x53423031;  // 'SB01'
    static constexpr uint32_t kSharedMemVersion   = 1;
    static constexpr const wchar_t* kSharedMemName = L"SkyrimBridge_GameState";
    static constexpr const wchar_t* kEventName     = L"SkyrimBridge_DataReady";

}  // namespace SB
