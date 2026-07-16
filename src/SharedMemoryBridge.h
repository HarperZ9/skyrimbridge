#pragma once
//=============================================================================
//  SharedMemoryBridge.h — External app data access via named shared memory
//
//  Phase 3: Writes SkyrimBridge AllData to a memory-mapped file every frame.
//  External applications read game state without touching the game process.
//
//  Usage from external apps (C/C++):
//    HANDLE hMap = OpenFileMapping(FILE_MAP_READ, FALSE, L"SkyrimBridge_GameState");
//    void*  ptr  = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(SB_SharedData));
//    auto*  data = reinterpret_cast<const SB::SB_SharedData*>(ptr);
//    // data->header.version, data->allData.celestial, etc.
//
//  Consumers: OBS overlays, Corsair iCUE LED sync, stream deck plugins,
//  companion mobile apps, external analysis tools, replay systems,
//  and the SkyrimBridge ENB external plugin (.dllplugin).
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "SB_SharedLayout.h"
#include "WeatherParameterComputer.h"

#include <Windows.h>
#include <atomic>

namespace SB
{
    //=========================================================================
    //  SharedMemoryBridge — manages the shared memory region (writer side)
    //=========================================================================

    class SharedMemoryBridge
    {
    public:
        static SharedMemoryBridge& Get()
        {
            static SharedMemoryBridge inst;
            return inst;
        }

        // ─── Lifecycle ──────────────────────────────────────────────────

        bool Initialize();
        void Shutdown();
        bool IsActive() const { return m_active.load(std::memory_order_relaxed); }

        // ─── Per-Frame Update ───────────────────────────────────────────

        // Write current frame data to shared memory
        void WriteFrame(const AllData& data, float deltaTime, uint32_t frameCount);

        // ─── Statistics ─────────────────────────────────────────────────

        uint32_t GetFramesWritten()  const { return m_framesWritten; }
        uint32_t GetClientsConnected() const;

    private:
        SharedMemoryBridge() = default;
        ~SharedMemoryBridge() { Shutdown(); }

        HANDLE          m_hMapFile  = nullptr;
        HANDLE          m_hEvent    = nullptr;
        SB_SharedData*  m_pData     = nullptr;
        std::atomic<bool> m_active{false};
        uint32_t        m_framesWritten = 0;
    };

}  // namespace SB
