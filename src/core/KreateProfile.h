#pragma once
//=============================================================================
//  KreateProfile.h — native KreatE profile loader (all five record types)
//
//  Applies every record override that ships inside a KreatE profile directly
//  through CommonLibSSE-NG, so the operator's presets work with no external
//  loader (no KreatE.dll, no Kitsuune plugins). Reimplements the functionality
//  of KreatE by Kitsuune (LonelyKitsuune), derived by reverse-engineering their
//  compiled binaries, NOT clean-room. See CREDITS.md — permission-gated, not
//  for public release. Record types applied:
//
//    <profile>/ImageSpaces/*.ini              -> RE::TESImageSpace
//    <profile>/Weathers/*.ini                 -> RE::TESWeather
//    <profile>/LightingTemplates/*.ini        -> RE::BGSLightingTemplate
//    <profile>/VolumetricLighting/*.ini       -> RE::BGSVolumetricLighting
//    <profile>/ShaderParticleGeometries/*.ini -> RE::BGSShaderParticleGeometryData
//
//  ImageSpaces are handled here (their files carry a spelling/section quirk
//  with a dedicated tolerant parser); the other four live in KreateRecords.
//  The profile's celestial-lighting sidecars (the old .cfg/.kfg) are ignored:
//  SkyrimBridge's own Sky model supersedes them.
//=============================================================================

#include "WeatherEditor.h"   // ImageSpaceSnapshot
#include "KreateRecords.h"   // Kreate::RecordSet

#include <filesystem>
#include <string>
#include <vector>

namespace SB
{
    class KreateProfile
    {
    public:
        static KreateProfile& Get();

        // Root that holds profile subdirectories (KreatE/Presets).
        void SetProfileRoot(const std::filesystem::path& root) { m_root = root; }

        // Profile subdirectory names available under the root.
        std::vector<std::string> ListProfiles() const;

        // Load a profile's overlays from disk into memory (does not apply).
        // Returns the number of image-space overrides parsed.
        int LoadProfile(const std::string& name);

        // Apply the loaded overrides to the live game forms. Returns how many
        // forms were found and written.
        int ApplyLoaded();

        // Convenience: load then apply.
        int LoadAndApply(const std::string& name)
        {
            LoadProfile(name);
            return ApplyLoaded();
        }

        const std::string& LoadedName() const { return m_loadedName; }
        int LoadedCount() const { return static_cast<int>(m_overrides.size()) + m_records.Count(); }
        int LoadedImageSpaceCount() const { return static_cast<int>(m_overrides.size()); }

        // Parse a single KreatE image-space .ini body into a snapshot.
        // Pure and free of engine/filesystem access, so it is unit-testable.
        // Returns false when no ID key is present.
        static bool ParseImageSpaceIni(const std::string& text,
                                       ImageSpaceSnapshot& out);

    private:
        KreateProfile() = default;

        std::filesystem::path m_root;
        std::string m_loadedName;
        std::vector<ImageSpaceSnapshot> m_overrides;   // ImageSpaces
        Kreate::RecordSet m_records;                    // the other four types
    };
}
