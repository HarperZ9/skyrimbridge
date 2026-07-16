#pragma once
//=============================================================================
//  WeatherParameterComputer.h — Per-parameter weather-reactive ENB adjustments
//
//  Phase 2: Extends SkyrimBridge to compute weather-interpolated shader
//  parameters in C++, replacing enbParmLink's expression evaluator.
//
//  Architecture:
//    1. Weather Classification — maps TESWeather FormIDs to categories
//    2. Parameter Registry — stores per-category values for each shader param
//    3. Interpolation Engine — lerps between categories based on transition %
//    4. ENB Push — writes computed values via ENBSetShaderParameter()
//
//  Configuration: WeatherParams.ini (user-editable, hot-reloadable)
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <string>
#include <unordered_map>
#include <vector>
#include <array>
#include <functional>
#include <mutex>
#include <filesystem>
#include <chrono>

namespace SB
{
    //=========================================================================
    //  Weather categories — abstract groupings of TESWeather records
    //=========================================================================

    enum class WeatherCategory : uint8_t
    {
        Clear       = 0,   // Pleasant, sunny
        Cloudy      = 1,   // Overcast, no precipitation
        Foggy       = 2,   // Dense fog, low visibility
        Rain        = 3,   // Light to heavy rain
        ThunderRain = 4,   // Rain + lightning
        Snow        = 5,   // Light to heavy snow
        Blizzard    = 6,   // Heavy snow + wind
        Ash         = 7,   // Solstheim ash storms
        Special     = 8,   // Sovngarde, Soul Cairn, Blackreach, Apocrypha
        COUNT       = 9
    };

    static constexpr int kWeatherCategoryCount = static_cast<int>(WeatherCategory::COUNT);

    // String names for INI serialization
    inline const char* WeatherCategoryName(WeatherCategory cat)
    {
        switch (cat) {
        case WeatherCategory::Clear:       return "Clear";
        case WeatherCategory::Cloudy:      return "Cloudy";
        case WeatherCategory::Foggy:       return "Foggy";
        case WeatherCategory::Rain:        return "Rain";
        case WeatherCategory::ThunderRain: return "ThunderRain";
        case WeatherCategory::Snow:        return "Snow";
        case WeatherCategory::Blizzard:    return "Blizzard";
        case WeatherCategory::Ash:         return "Ash";
        case WeatherCategory::Special:     return "Special";
        default:                           return "Unknown";
        }
    }


    //=========================================================================
    //  WeatherParameterDef — one controllable shader parameter
    //=========================================================================

    struct WeatherParameterDef
    {
        // ENB target identifiers
        std::string shaderFile;    // e.g. "enbbloom.fx"
        std::string paramGroup;    // e.g. "ExternalParameters"
        std::string paramName;     // e.g. "WeatherBloom"

        // Per-category values
        std::array<float, kWeatherCategoryCount> values;

        // Interpolation settings
        float transitionSpeed = 1.0f;   // Multiplier on weather transition %
        float minValue        = 0.0f;   // Clamp floor
        float maxValue        = 10.0f;  // Clamp ceiling
        bool  useSmooth       = true;   // Smoothstep vs linear lerp

        // Current computed value (updated per frame)
        float currentValue    = 0.0f;
        float targetValue     = 0.0f;

        // SkyrimBridge parameter name (for direct SB push, optional)
        std::string sbParamName;  // e.g. "SB_WP_Bloom" — if non-empty, also push as SB param
    };


    //=========================================================================
    //  WeatherClassifier — maps FormIDs to WeatherCategory
    //=========================================================================

    class WeatherClassifier
    {
    public:
        // Register a FormID → category mapping
        void Register(uint32_t formID, WeatherCategory cat);

        // Classify a weather by FormID. Falls back to flag-based classification
        // if the FormID isn't in the manual table.
        WeatherCategory Classify(const RE::TESWeather* weather) const;

        // Load classification overrides from INI
        void LoadFromINI(const std::filesystem::path& iniPath);

        // Get all registered overrides (for debug/ImGui display)
        const std::unordered_map<uint32_t, WeatherCategory>& GetOverrides() const
        { return m_overrides; }

    private:
        std::unordered_map<uint32_t, WeatherCategory> m_overrides;

        // Flag-based fallback classification
        static WeatherCategory ClassifyByFlags(const RE::TESWeather* weather);
    };


    //=========================================================================
    //  WeatherParameterComputer — the main engine
    //=========================================================================

    class WeatherParameterComputer
    {
    public:
        static WeatherParameterComputer& Get()
        {
            static WeatherParameterComputer inst;
            return inst;
        }

        // ─── Lifecycle ──────────────────────────────────────────────────

        // Initialize: load INI, build parameter table, register classifications
        void Initialize(const std::filesystem::path& configDir);

        // Per-frame update: classify weather, interpolate params, push to ENB
        void Update(float deltaTime);

        // Shutdown: flush any pending state
        void Shutdown();

        // ─── Hot Reload ─────────────────────────────────────────────────

        // Check if config files have been modified, reload if so
        void CheckHotReload();

        // Force reload all configuration
        void ForceReload();

        // ─── Parameter Access ───────────────────────────────────────────

        // Get current interpolated value for a named parameter
        float GetValue(const std::string& paramName) const;

        // Get all parameters (for ImGui display)
        const std::vector<WeatherParameterDef>& GetParameters() const
        { return m_params; }

        // ─── Weather State ──────────────────────────────────────────────

        WeatherCategory GetCurrentCategory()  const { return m_currentCat; }
        WeatherCategory GetPreviousCategory() const { return m_prevCat; }
        float           GetTransitionPct()    const { return m_transitionPct; }
        uint32_t        GetCurrentWeatherID() const { return m_currentWeatherID; }
        uint32_t        GetPrevWeatherID()    const { return m_prevWeatherID; }

        // ─── Classifier Access ──────────────────────────────────────────

        WeatherClassifier&       GetClassifier()       { return m_classifier; }
        const WeatherClassifier& GetClassifier() const { return m_classifier; }

    private:
        WeatherParameterComputer() = default;

        // ─── Config Loading ─────────────────────────────────────────────

        void LoadParameterINI(const std::filesystem::path& path);
        void LoadWeatherClassINI(const std::filesystem::path& path);
        void PushAllToENB();

        // ─── Interpolation ──────────────────────────────────────────────

        static float SmoothLerp(float a, float b, float t);
        float InterpolateParam(const WeatherParameterDef& param, float transitionPct) const;

        // ─── State ──────────────────────────────────────────────────────

        WeatherClassifier                m_classifier;
        std::vector<WeatherParameterDef> m_params;
        std::unordered_map<std::string, size_t> m_paramIndex; // name → index

        WeatherCategory m_currentCat       = WeatherCategory::Clear;
        WeatherCategory m_prevCat          = WeatherCategory::Clear;
        float           m_transitionPct    = 0.0f;
        uint32_t        m_currentWeatherID = 0;
        uint32_t        m_prevWeatherID    = 0;

        // Hot reload
        std::filesystem::path                m_configDir;
        std::filesystem::file_time_type      m_lastParamMod;
        std::filesystem::file_time_type      m_lastClassMod;
        std::chrono::steady_clock::time_point m_lastReloadCheck;

        mutable std::mutex m_mutex;
    };

}  // namespace SB
