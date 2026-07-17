#pragma once
//=============================================================================
//  SkyLighting.h — SkyrimBridge's celestial lighting model
//
//  Native replacement for the third-party orbital/ambient lighting plugins
//  (the mature one supersedes the older shadow/VL one — a single component
//  replaces both). A real astronomical model: axial tilt + latitude + solstice
//  timing -> sun declination / hour-angle -> altitude / azimuth / incident
//  vector, plus moon-phase-weighted direct/volumetric source intensities and
//  dynamic directional-ambient.
//
//  Config is SkyrimBridge's OWN flat INI — `Sky.ini`, not the third-party
//  nested `.kfg`. Sections: [Sky] [Orbit] [Ambient] [Light] [Masser]
//  [Secunda] [NightSky] [Tweaks], with per-form overrides as [Orbit:FormID],
//  [Light:FormID], [Weather:FormID].
//
//  Live writes (dynamic ambient, sun repositioning) are config-gated and ship
//  OFF: the orbital math and parsing are exact and offline-testable; the
//  frame-level lighting output is game-bound and validated in-game.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace SB
{
    // ── Pure orbital model (no engine access — unit-testable) ─────────────
    namespace SkyModel
    {
        struct SunState
        {
            float altitude = 0;   // radians above horizon (can be negative)
            float azimuth = 0;    // radians, clockwise from north
            float dirX = 0, dirY = 0, dirZ = 0;  // unit incident vector (to the sun)
        };

        // day: 0..365 day-of-year; hour: 0..24; all angles in degrees in.
        SunState ComputeSun(float dayOfYear, float hour,
                            float axialTilt, float latitude,
                            float summerSolsticeDay);

        // Moon phase fraction 0 (new) .. 1 (full) -> exponential-ish weight.
        float PhaseWeight(float phase01);
    }

    // ── Config ────────────────────────────────────────────────────────────
    enum class LightSource { Sun, NightSky, Vanilla, Zenith, Custom };

    struct MoonConfig
    {
        float fullDirect = 1.0f, fullVolumetric = 1.0f;
        float newDirect = 0.1f, newVolumetric = 0.7f;
        std::array<float, 4> color{ 0.15f, 0.04f, 0.0f, 1.0f };
        float fadeStart = 20.0f, fadeEnd = 5.0f;
    };

    struct SkyConfig
    {
        bool  masterEnable = false;    // [Sky] Enable — master live-write switch

        // [Orbit]
        bool  orbitEnable = true;
        bool  adjustDayLength = true;
        bool  moveSun = false;         // reposition the sun NiNode (game-bound; off)
        float axialTilt = 23.4f, latitude = 35.0f, twilightAngle = 24.0f;
        float masserTilt = 2.0f, secundaTilt = 18.0f;
        float summerSolstice = 173.0f, winterSolstice = 355.5f;

        // [Ambient]
        bool  ambientEnable = true;
        float tiltScale = 0.8f;

        // [Light]
        LightSource daySource = LightSource::Sun;
        LightSource nightSource = LightSource::NightSky;
        float minAngle = 5.0f;

        // [Masser] [Secunda]
        MoonConfig masser{};
        MoonConfig secunda{ 0.5f, 0.7f, 0.1f, 0.7f, { 0.15f, 0.04f, 0.0f, 1.0f }, 20.0f, 5.0f };

        // [NightSky]
        float nightSkyDirect = 0.2f, nightSkyVolumetric = 0.0f, auroraVolumetric = 0.5f;
        std::array<float, 4> nightSkyColor{ 1, 1, 1, 0 };

        // [Tweaks]
        bool  syncWeatherColors = true;

        // Per-form overrides: raw key/value carried until resolved by FormID.
        struct Override
        {
            std::uint32_t formID = 0;
            std::string   base;       // "Orbit" | "Light" | "Weather"
            std::vector<std::pair<std::string, std::string>> keys;
        };
        std::vector<Override> overrides;

        static LightSource ParseSource(const std::string& v, LightSource def);
    };

    // ── Component ────────────────────────────────────────────────────────
    class SkyLighting
    {
    public:
        static SkyLighting& Get();

        // Load Sky.ini from the given config directory (Data/SKSE/Plugins/
        // SkyrimBridge). Absent file -> inactive.
        void Initialize(const std::filesystem::path& configDir);

        bool IsActive() const { return m_active; }
        const SkyConfig& Config() const { return m_cfg; }

        // Per-frame: recompute the celestial model and, when enabled, write the
        // dynamic directional-ambient (and reposition the sun if configured).
        void Update();

        // Parse Sky.ini text into a config. Pure and unit-testable.
        static SkyConfig ParseConfig(const std::string& text);

    private:
        SkyLighting() = default;

        SkyConfig m_cfg;
        bool m_active = false;
    };
}
