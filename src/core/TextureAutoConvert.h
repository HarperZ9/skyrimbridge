#pragma once
//=============================================================================
//  TextureAutoConvert.h — startup transcode of foreign-format textures
//
//  The bootstrap half of native non-.dds asset integration: scan the game's
//  texture tree for .png/.tga/.bmp files that have no .dds sibling and
//  transcode them with TextureCodec, so the engine's own loader finds a DDS
//  at the path it expects. Runs on a background thread at kDataLoaded; the
//  in-flight load hook (TextureLoadHook) covers files that appear after the
//  scan. Idempotent: an existing .dds sibling is never overwritten unless
//  Refresh is set and the source is newer.
//
//  Config (SkyrimBridge.ini):
//    [Native]         TextureAutoConvert = false   ; opt-in
//    [TextureConvert] Format = BC3 | BC1 | RGBA8   ; default BC3
//                     Mipmaps = true
//                     Refresh = false              ; re-convert when source newer
//                     Root = Data/Textures         ; scan root
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <atomic>
#include <cstdint>
#include <filesystem>

namespace SB
{
    class TextureAutoConvert
    {
    public:
        static TextureAutoConvert& Get();

        // Read config and start the background scan (no-op if already run).
        void Initialize(const std::filesystem::path& configDir);

        struct ScanResult
        {
            std::uint32_t candidates = 0;   // foreign files seen
            std::uint32_t converted = 0;    // transcodes performed
            std::uint32_t skipped = 0;      // .dds sibling already present
            std::uint32_t failed = 0;       // decode/write failures (logged)
        };

        // Synchronous scan (console-callable). dryRun counts without writing.
        ScanResult RunScan(bool dryRun = false);

        bool Busy() const { return m_busy.load(std::memory_order_acquire); }

    private:
        std::filesystem::path m_root = "Data/Textures";
        int                   m_format = 2;       // 0=RGBA8 1=BC1 2=BC3
        bool                  m_mipmaps = true;
        bool                  m_refresh = false;
        std::atomic<bool>     m_busy{ false };
    };
}
