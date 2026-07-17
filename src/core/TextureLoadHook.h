#pragma once
//=============================================================================
//  TextureLoadHook.h — in-flight foreign-texture substitution
//
//  The runtime half of native non-.dds asset integration. Vtable detours on
//  BSResource::LooseFileLocation (CommonLib VTABLE, AE ID 188191): when the
//  engine requests a textures\*.dds that does NOT exist but a .png/.tga/.bmp
//  sibling does, the sibling is transcoded once into
//  Data/SKSE/Plugins/SkyrimBridge/texcache/ and the ORIGINAL engine
//  implementation is re-invoked on the cache path. The engine serves its own
//  LooseFileStream, so sync and async I/O both stay engine-native; this
//  module never constructs engine objects.
//
//  Bounds: acts only when the original call failed (never interferes with an
//  existing .dds), only on paths under textures\, never on its own cache
//  paths, and any internal failure returns the original error unchanged.
//  Config: [Native] TextureLoadHook (default OFF), formats from
//  [TextureConvert]. Game-bound validation.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <filesystem>

namespace SB
{
    class TextureLoadHook
    {
    public:
        static TextureLoadHook& Get();

        // Install the vtable detours (call once, kPostLoad). Reads
        // [TextureConvert] from SkyrimBridge.ini in configDir.
        void Install(const std::filesystem::path& configDir);
        bool Installed() const { return m_installed; }

    private:
        bool m_installed = false;
    };
}
