#pragma once
//=============================================================================
//  WeatherEditor.h — Real-time weather record editor + preset system
//
//  Captures TESWeather, TESImageSpace, BGSVolumetricLighting forms into
//  editable snapshots, writes changes back to live game memory, and
//  persists presets to INI files per weather EditorID.
//
//  Greater scope than KreatE: full color/fog/lighting/imagespace/volumetric
//  editing from both ImGui and ENB ATB GUI. SkyrimBridge shaders adapt
//  automatically to edited game state — creating a correct baseline for
//  ENB preset development.
//
//  Author: Zain Dana Harper
//=============================================================================

#include <RE/Skyrim.h>
#include <filesystem>
#include <string>
#include <vector>

namespace SB
{
    // ── Color stored as normalized float [0,1] ──────────────────────────────
    struct ColorF
    {
        float r = 0, g = 0, b = 0, a = 1;

        void FromColor(const RE::Color& c)
        {
            r = c.red   / 255.0f;
            g = c.green / 255.0f;
            b = c.blue  / 255.0f;
            a = c.alpha / 255.0f;
        }
        void ToColor(RE::Color& c) const
        {
            c.red   = static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
            c.green = static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
            c.blue  = static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
            c.alpha = static_cast<uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        float* Ptr() { return &r; }
    };

    // ── Time of day indices (matches TESWeather::ColorTime) ─────────────────
    enum ToD : int { kSunrise = 0, kDay = 1, kSunset = 2, kNight = 3, kToDCount = 4 };

    // ── Weather color type indices (matches TESWeather::ColorTypes) ─────────
    enum WColor : int
    {
        kSkyUpper = 0, kFogNear = 1, kUnknown = 2, kAmbient = 3,
        kSunlight = 4, kSun = 5, kStars = 6, kSkyLower = 7,
        kHorizon = 8, kEffectLighting = 9, kCloudLODDiffuse = 10,
        kCloudLODAmbient = 11, kFogFar = 12, kSkyStatics = 13,
        kWaterMultiplier = 14, kSunGlare = 15, kMoonGlare = 16,
        kColorTypeCount = 17
    };

    // ── ImageSpace snapshot ─────────────────────────────────────────────────
    struct ImageSpaceSnapshot
    {
        bool valid = false;
        RE::FormID formID = 0;

        // HDR
        float eyeAdaptSpeed = 0;
        float bloomBlurRadius = 0;
        float bloomThreshold = 0;
        float bloomScale = 0;
        float receiveBloomThreshold = 0;
        float white = 0;
        float sunlightScale = 0;
        float skyScale = 0;
        float eyeAdaptStrength = 0;

        // Cinematic
        float saturation = 0;
        float brightness = 0;
        float contrast = 0;

        // Tint
        float tintAmount = 0;
        float tintR = 0, tintG = 0, tintB = 0;

        // DOF
        float dofStrength = 0;
        float dofDistance = 0;
        float dofRange = 0;

        void ReadFrom(const RE::TESImageSpace* is);
        void WriteTo(RE::TESImageSpace* is) const;
    };

    // ── Volumetric lighting snapshot ────────────────────────────────────────
    struct VolumetricSnapshot
    {
        bool valid = false;
        RE::FormID formID = 0;

        float intensity = 0;
        float customColorContrib = 0;
        float colorR = 0, colorG = 0, colorB = 0;
        float densityContrib = 0;
        float densitySize = 0;
        float densityWindSpeed = 0;
        float densityFallingSpeed = 0;
        float phaseContrib = 0;
        float phaseScattering = 0;
        float samplingRangeFactor = 0;

        void ReadFrom(const RE::BGSVolumetricLighting* vl);
        void WriteTo(RE::BGSVolumetricLighting* vl) const;
    };

    // ── Directional ambient snapshot (per ToD) ──────────────────────────────
    struct DirAmbientSnapshot
    {
        ColorF xMax, xMin;
        ColorF yMax, yMin;
        ColorF zMax, zMin;
        ColorF specular;
        float  fresnelPower = 0;

        void ReadFrom(const RE::BGSDirectionalAmbientLightingColors& da);
        void WriteTo(RE::BGSDirectionalAmbientLightingColors& da) const;
    };

    // ── Full weather snapshot ───────────────────────────────────────────────
    struct WeatherSnapshot
    {
        // Identity
        RE::FormID  formID = 0;
        std::string editorID;

        // 17 color types x 4 ToD
        ColorF colors[kColorTypeCount][kToDCount];

        // Fog distances and power
        float fogDayNear = 0, fogDayFar = 0;
        float fogNightNear = 0, fogNightFar = 0;
        float fogDayPower = 0, fogNightPower = 0;
        float fogDayMax = 0, fogNightMax = 0;

        // Weather data (normalized 0-1 from int8 originals)
        float windSpeed = 0;
        float windDirection = 0;
        float windDirRange = 0;
        float transDelta = 0;
        float sunGlare = 0;
        float sunDamage = 0;
        float precipBeginFadeIn = 0;
        float precipEndFadeOut = 0;
        float thunderBeginFadeIn = 0;
        float thunderEndFadeOut = 0;
        float thunderFrequency = 0;
        float visualEffectBegin = 0;
        float visualEffectEnd = 0;
        uint8_t flags = 0;
        ColorF lightningColor;

        // Cloud layers (32 max)
        struct CloudLayer
        {
            float speedX = 0, speedY = 0;    // normalized from int8
            float alpha[kToDCount] = {};
            bool  enabled = true;
        };
        CloudLayer clouds[32];
        uint32_t numCloudLayers = 0;

        // Directional ambient (per ToD)
        DirAmbientSnapshot dirAmbient[kToDCount];

        // Associated forms (per ToD)
        ImageSpaceSnapshot  imageSpaces[kToDCount];
        VolumetricSnapshot  volumetric[kToDCount];

        // Read all data from a TESWeather form
        void ReadFromWeather(RE::TESWeather* w);

        // Write all data back to a TESWeather form
        void WriteToWeather(RE::TESWeather* w) const;
    };

    // ── WeatherEditor singleton ─────────────────────────────────────────────
    class WeatherEditor
    {
    public:
        static WeatherEditor& Get();

        // Per-frame update — detects weather changes, auto-loads presets
        void Update();

        // Capture the current weather into editor
        void CaptureCurrentWeather();

        // Apply current snapshot back to game
        void ApplyToGame();

        // Revert to original (pre-edit) values
        void RevertToOriginal();

        // Preset management
        bool SavePreset(const std::string& name = "");
        bool LoadPreset(const std::string& name = "");
        bool DeletePreset(const std::string& name);
        std::vector<std::string> ListPresets() const;
        void SetPresetDir(const std::filesystem::path& dir) { m_presetDir = dir; }

        // Force a specific ToD for preview (negative = auto from game hour)
        void SetPreviewToD(int tod) { m_previewToD = tod; }
        int  GetPreviewToD() const { return m_previewToD; }
        int  GetCurrentToD() const;  // from game hour

        // Force weather transition
        void ForceWeather(RE::TESWeather* w);
        void ClearForcedWeather();

        // State
        bool IsActive() const { return m_active; }
        bool HasUnsavedChanges() const { return m_dirty; }
        bool IsAutoApply() const { return m_autoApply; }
        void SetAutoApply(bool v) { m_autoApply = v; }
        bool IsAutoCapture() const { return m_autoCapture; }
        void SetAutoCapture(bool v) { m_autoCapture = v; }

        // Data access
        WeatherSnapshot&       GetSnapshot()  { return m_current; }
        const WeatherSnapshot& GetOriginal()  const { return m_original; }
        RE::TESWeather*        GetTargetWeather() const { return m_targetWeather; }

        // Mark dirty (called by GUI when user edits a value)
        void MarkDirty() { m_dirty = true; }

        // A/B compare: temporarily apply original values
        bool IsCompareMode() const { return m_compareMode; }
        void SetCompareMode(bool v) { m_compareMode = v; }

    private:
        WeatherEditor() = default;

        // INI save/load helpers
        bool SaveSnapshotToINI(const WeatherSnapshot& snap, const std::filesystem::path& path);
        bool LoadSnapshotFromINI(WeatherSnapshot& snap, const std::filesystem::path& path);

        std::filesystem::path GetPresetPath(const std::string& name) const;

        bool             m_active = false;
        bool             m_dirty = false;
        bool             m_autoApply = true;
        bool             m_autoCapture = true;
        bool             m_compareMode = false;
        int              m_previewToD = -1;
        RE::TESWeather*  m_targetWeather = nullptr;
        RE::FormID       m_lastWeatherID = 0;

        WeatherSnapshot  m_original;
        WeatherSnapshot  m_current;

        std::filesystem::path m_presetDir;

        // Preset hot-reload state
        std::filesystem::file_time_type m_presetMtime{};
        uint32_t m_reloadCounter = 0;
    };

    // (GUI tab not part of the standalone: control via presets, hot-reload,
    //  and the Papyrus console surface.)

} // namespace SB
