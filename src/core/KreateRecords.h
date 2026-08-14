#pragma once
//=============================================================================
//  KreateRecords.h — KreatE record-override appliers (non-imagespace types)
//
//  Applies the four record types a KreatE profile ships alongside its
//  ImageSpaces: Weathers, LightingTemplates, VolumetricLighting, and
//  ShaderParticleGeometries. Each is a directory of FormID-keyed .ini
//  overrides; this unit parses them (SkyrimBridge flat-INI) and writes them
//  onto the live game forms through typed CommonLibSSE-NG data members.
//
//  ImageSpaces stay in KreateProfile (they carry a spelling/section quirk with
//  a dedicated tolerant parser). This unit covers the other four so the whole
//  KreatE profile applies with no external loader.
//
//  Reimplements the functionality of KreatE by Kitsuune (LonelyKitsuune),
//  derived by reverse-engineering their compiled binaries, NOT clean-room.
//  See CREDITS.md — permission-gated, not for public release.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "SBConfig.h"

#include <RE/Skyrim.h>
#include <filesystem>
#include <vector>

namespace SB::Kreate
{
    // One parsed record override: the target form plus its parsed body.
    // Applied against the live form (read-modify-write, so keys the file omits
    // keep the form's current values).
    struct Override
    {
        RE::FormID    formID = 0;
        bool          optional = true;   // skip silently when the form is absent
        Cfg::Document body;              // parsed sections/keys (ID + Optional stripped)
    };

    // Parse a record .ini body: pulls ID + Optional, keeps the rest in `body`.
    // Returns false when no ID key is present.
    bool ParseOverride(const std::string& text, Override& out);

    // Per-type appliers. Return true when the form resolved and was written.
    bool ApplyWeather(const Override& o);
    bool ApplyLightingTemplate(const Override& o);
    bool ApplyVolumetric(const Override& o);
    bool ApplyShaderParticle(const Override& o);

    // The four record sets loaded from one profile directory.
    struct RecordSet
    {
        std::vector<Override> weathers;
        std::vector<Override> lightingTemplates;
        std::vector<Override> volumetrics;
        std::vector<Override> shaderParticles;

        void Clear();
        int  Count() const;

        // Load all four subdirectories under a profile directory. Returns the
        // number of overrides parsed.
        int LoadFrom(const std::filesystem::path& profileDir);

        // Apply every loaded override to the live forms. Returns count applied.
        int ApplyAll() const;
    };
}
