//=============================================================================
//  SharedMemoryBridge.cpp — External app data access via named shared memory
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "SharedMemoryBridge.h"

#include <SKSE/SKSE.h>

namespace SB
{

bool SharedMemoryBridge::Initialize()
{
    if (m_active.load()) return true;

    // Create the shared memory region
    DWORD size = static_cast<DWORD>(sizeof(SB_SharedData));

    m_hMapFile = CreateFileMappingW(
        INVALID_HANDLE_VALUE,   // Use paging file
        nullptr,                // Default security
        PAGE_READWRITE,         // Read/write access
        0,                      // High-order DWORD of size
        size,                   // Low-order DWORD of size
        kSharedMemName          // Name of mapping object
    );

    if (!m_hMapFile) {
        SKSE::log::error("SharedMemoryBridge: CreateFileMapping failed (err={})", GetLastError());
        return false;
    }

    m_pData = reinterpret_cast<SB_SharedData*>(
        MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, size)
    );

    if (!m_pData) {
        SKSE::log::error("SharedMemoryBridge: MapViewOfFile failed (err={})", GetLastError());
        CloseHandle(m_hMapFile);
        m_hMapFile = nullptr;
        return false;
    }

    // Zero-initialize
    memset(m_pData, 0, size);

    // Write header
    m_pData->header.magic      = kSharedMemMagic;
    m_pData->header.version    = kSharedMemVersion;
    m_pData->header.structSize = sizeof(SB_SharedData);

    // Create event for signaling clients
    m_hEvent = CreateEventW(nullptr, FALSE, FALSE, kEventName);
    if (!m_hEvent) {
        SKSE::log::warn("SharedMemoryBridge: CreateEvent failed (non-fatal, err={})",
            GetLastError());
    }

    m_active.store(true, std::memory_order_release);
    SKSE::log::info("SharedMemoryBridge: initialized ({} bytes shared as '{}')",
        size, "SkyrimBridge_GameState");

    return true;
}


void SharedMemoryBridge::Shutdown()
{
    m_active.store(false, std::memory_order_release);

    if (m_pData) {
        // Write shutdown signal
        m_pData->header.magic = 0;
        UnmapViewOfFile(m_pData);
        m_pData = nullptr;
    }

    if (m_hMapFile) {
        CloseHandle(m_hMapFile);
        m_hMapFile = nullptr;
    }

    if (m_hEvent) {
        CloseHandle(m_hEvent);
        m_hEvent = nullptr;
    }

    SKSE::log::info("SharedMemoryBridge: shut down");
}


void SharedMemoryBridge::WriteFrame(const AllData& data, float deltaTime, uint32_t frameCount)
{
    if (!m_active.load(std::memory_order_relaxed) || !m_pData) return;

    // ─── Header ─────────────────────────────────────────────────────

    m_pData->header.frameCount = frameCount;
    m_pData->header.deltaTime  = deltaTime;
    m_pData->header.gameHour   = data.celestial.TimeData.x;

    // Weather info (null-safe — Sky may not be ready during early frames)
    auto* sky = RE::Sky::GetSingleton();
    if (sky && sky->currentWeather) {
        m_pData->header.weatherFormID   = sky->currentWeather->GetFormID();
        m_pData->header.weatherCategory = static_cast<uint8_t>(
            WeatherParameterComputer::Get().GetCurrentCategory());
        m_pData->header.transitionPct   = sky->currentWeatherPct;
    }

    m_pData->header.isInterior = (data.interior.IsInterior.x > 0.5f) ? 1 : 0;
    m_pData->header.isInMenu   = (data.uiState.Menus.x > 0.5f) ? 1 : 0;
    m_pData->header.isLoading  = (data.uiState.HUD.w > 0.5f) ? 1 : 0;

    // ─── AllData bulk copy ──────────────────────────────────────────

    memcpy(&m_pData->allData, &data, sizeof(AllData));

    // ─── Weather parameters from Phase 2 ────────────────────────────

    auto& wpc = WeatherParameterComputer::Get();
    m_pData->weatherParams.bloomIntensity  = wpc.GetValue("WeatherBloomIntensity");
    m_pData->weatherParams.bloomRadius     = wpc.GetValue("WeatherBloomRadius");
    m_pData->weatherParams.adaptSpeed      = wpc.GetValue("WeatherAdaptSpeed");
    m_pData->weatherParams.exposureBias    = wpc.GetValue("WeatherExposureBias");
    m_pData->weatherParams.saturation      = wpc.GetValue("WeatherSaturation");
    m_pData->weatherParams.contrast        = wpc.GetValue("WeatherContrast");
    m_pData->weatherParams.colorTemp       = wpc.GetValue("WeatherColorTempShift");
    m_pData->weatherParams.sharpen         = wpc.GetValue("WeatherSharpenStrength");
    m_pData->weatherParams.grain           = wpc.GetValue("WeatherGrainIntensity");
    m_pData->weatherParams.aoIntensity     = wpc.GetValue("WeatherAOIntensity");
    m_pData->weatherParams.ssrIntensity    = wpc.GetValue("WeatherSSRIntensity");
    m_pData->weatherParams.godRayIntensity = wpc.GetValue("WeatherGodRayIntensity");
    m_pData->weatherParams.dofStrength     = wpc.GetValue("WeatherDOFStrength");
    m_pData->weatherParams.lensDirt        = wpc.GetValue("WeatherLensDirtIntensity");
    m_pData->weatherParams.rainOnLens      = wpc.GetValue("WeatherRainOnLens");
    m_pData->weatherParams.frostOnLens     = wpc.GetValue("WeatherFrostOnLens");

    // ─── Signal waiting clients ─────────────────────────────────────

    if (m_hEvent)
        SetEvent(m_hEvent);

    m_framesWritten++;
}


uint32_t SharedMemoryBridge::GetClientsConnected() const
{
    // No direct way to count readers of a file mapping.
    // We can check if the event has waiters, but that's also approximate.
    // For now, just report that we're active.
    return m_active.load() ? 1 : 0;
}


}  // namespace SB
