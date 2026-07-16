//=============================================================================
//  WeatherParameterComputer.cpp — Per-parameter weather-reactive ENB adjustments
//
//  Replaces enbParmLink's expression evaluator with compiled C++ computation.
//  Reads weather state from RE::Sky, classifies into categories, interpolates
//  per-parameter values, and pushes results to ENB shaders.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "WeatherParameterComputer.h"
#include "ENBInterface_v3.h"  // v3 convenience wrappers (includes base ENBInterface)
#include "BridgeData.h"      // AllData for SB param push

#include <RE/Skyrim.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace SB
{

//=============================================================================
//  WeatherClassifier
//=============================================================================

void WeatherClassifier::Register(uint32_t formID, WeatherCategory cat)
{
    m_overrides[formID] = cat;
}

WeatherCategory WeatherClassifier::Classify(const RE::TESWeather* weather) const
{
    if (!weather) return WeatherCategory::Clear;

    // Check manual override table first
    uint32_t formID = weather->GetFormID();
    auto it = m_overrides.find(formID);
    if (it != m_overrides.end())
        return it->second;

    // Fallback: classify by weather flags
    return ClassifyByFlags(weather);
}

WeatherCategory WeatherClassifier::ClassifyByFlags(const RE::TESWeather* weather)
{
    if (!weather) return WeatherCategory::Clear;

    using F = RE::TESWeather::WeatherDataFlag;
    auto flags = weather->data.flags;

    // Check for blizzard (snow + high wind + low visibility)
    bool isSnow = flags.any(F::kSnow);
    bool isRain = flags.any(F::kRainy);
    bool isCloudy = flags.any(F::kCloudy);
    bool isPleasant = flags.any(F::kPleasant);

    // Check wind speed from weather data
    float windSpeed = weather->data.windSpeed / 255.0f;

    // Check precipitation intensity
    float precipAmt = 0.0f;
    if (weather->precipitationData) {
        // TESWeather stores precipitation begin/end fade distances
        precipAmt = 1.0f;  // If precipitationData exists, there's precipitation
    }

    // Lightning check (thunder storms have > 0 lightning frequency)
    bool hasLightning = weather->data.thunderLightningFrequency > 0;

    // Ash check (Solstheim weathers typically have specific formIDs,
    // but we can also check for the ash keyword on the weather record)
    // For flag-based detection, we check if it's in known DLC2 ranges
    uint32_t fid = weather->GetFormID();
    bool isDLC2 = (fid >> 24) == 0x04;  // DLC2 plugin index (approximate)

    // Classification priority: most specific → least specific
    if (isSnow && windSpeed > 0.7f)        return WeatherCategory::Blizzard;
    if (isSnow)                             return WeatherCategory::Snow;
    if (isDLC2 && !isPleasant && !isRain)   return WeatherCategory::Ash;
    if (isRain && hasLightning)             return WeatherCategory::ThunderRain;
    if (isRain)                             return WeatherCategory::Rain;

    // Fog detection: low fog distances indicate fog weather
    float fogNearDay = weather->fogData.dayNear;
    float fogFarDay  = weather->fogData.dayFar;
    if (fogFarDay < 3000.0f && fogNearDay < 500.0f)
        return WeatherCategory::Foggy;

    if (isCloudy)                           return WeatherCategory::Cloudy;

    return WeatherCategory::Clear;
}

void WeatherClassifier::LoadFromINI(const std::filesystem::path& iniPath)
{
    std::ifstream file(iniPath);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line))
    {
        // Skip comments and empty lines
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        // Format: 0x000ABCDE = CategoryName
        // or:     000ABCDE = CategoryName
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string formStr = line.substr(0, eq);
        std::string catStr  = line.substr(eq + 1);

        // Trim whitespace
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(formStr);
        trim(catStr);

        // Parse FormID (hex)
        uint32_t formID = 0;
        try {
            formID = std::stoul(formStr, nullptr, 16);
        } catch (...) { continue; }

        // Parse category name
        WeatherCategory cat = WeatherCategory::Clear;
        if      (catStr == "Clear")       cat = WeatherCategory::Clear;
        else if (catStr == "Cloudy")      cat = WeatherCategory::Cloudy;
        else if (catStr == "Foggy")       cat = WeatherCategory::Foggy;
        else if (catStr == "Rain")        cat = WeatherCategory::Rain;
        else if (catStr == "ThunderRain") cat = WeatherCategory::ThunderRain;
        else if (catStr == "Snow")        cat = WeatherCategory::Snow;
        else if (catStr == "Blizzard")    cat = WeatherCategory::Blizzard;
        else if (catStr == "Ash")         cat = WeatherCategory::Ash;
        else if (catStr == "Special")     cat = WeatherCategory::Special;
        else continue;

        Register(formID, cat);
    }
}


//=============================================================================
//  WeatherParameterComputer
//=============================================================================

void WeatherParameterComputer::Initialize(const std::filesystem::path& configDir)
{
    std::lock_guard lock(m_mutex);
    m_configDir = configDir;

    // Load weather classification overrides
    auto classPath = configDir / "WeatherClasses.ini";
    if (std::filesystem::exists(classPath)) {
        m_classifier.LoadFromINI(classPath);
        m_lastClassMod = std::filesystem::last_write_time(classPath);
    }

    // Load parameter definitions
    auto paramPath = configDir / "WeatherParams.ini";
    if (std::filesystem::exists(paramPath)) {
        LoadParameterINI(paramPath);
        m_lastParamMod = std::filesystem::last_write_time(paramPath);
    }

    m_lastReloadCheck = std::chrono::steady_clock::now();

    SKSE::log::info("WeatherParameterComputer: initialized with {} params, {} classification overrides",
        m_params.size(), m_classifier.GetOverrides().size());
}


void WeatherParameterComputer::LoadParameterINI(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open()) return;

    m_params.clear();
    m_paramIndex.clear();

    // INI Format:
    //
    // [enbbloom.fx:ExternalParameters:WeatherBloom]
    // Clear       = 0.80
    // Cloudy      = 0.90
    // Foggy       = 0.60
    // Rain        = 0.70
    // ThunderRain = 0.75
    // Snow        = 0.85
    // Blizzard    = 0.65
    // Ash         = 0.50
    // Special     = 1.00
    // TransitionSpeed = 1.0
    // MinValue = 0.0
    // MaxValue = 5.0
    // SmoothLerp = true
    // SBParam = SB_WP_Bloom
    //
    // [enbeffect.fx:ExternalParameters:WeatherSaturation]
    // ...

    WeatherParameterDef* current = nullptr;
    std::string line;

    while (std::getline(file, line))
    {
        // Trim
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        // Section header: [shader:group:name]
        if (line.front() == '[' && line.back() == ']')
        {
            std::string section = line.substr(1, line.size() - 2);

            // Parse shader:group:name
            auto c1 = section.find(':');
            auto c2 = section.find(':', c1 + 1);
            if (c1 == std::string::npos || c2 == std::string::npos) continue;

            WeatherParameterDef def;
            def.shaderFile = section.substr(0, c1);
            def.paramGroup = section.substr(c1 + 1, c2 - c1 - 1);
            def.paramName  = section.substr(c2 + 1);
            def.values.fill(0.0f);

            m_params.push_back(std::move(def));
            m_paramIndex[m_params.back().paramName] = m_params.size() - 1;
            current = &m_params.back();
            continue;
        }

        if (!current) continue;

        // Key = Value
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        key.erase(key.find_last_not_of(" \t") + 1);
        val.erase(0, val.find_first_not_of(" \t"));

        // Weather category values
        auto tryCategory = [&](const char* name, WeatherCategory cat) -> bool {
            if (key == name) {
                current->values[static_cast<int>(cat)] = std::stof(val);
                return true;
            }
            return false;
        };

        if (tryCategory("Clear",       WeatherCategory::Clear))       continue;
        if (tryCategory("Cloudy",      WeatherCategory::Cloudy))      continue;
        if (tryCategory("Foggy",       WeatherCategory::Foggy))       continue;
        if (tryCategory("Rain",        WeatherCategory::Rain))        continue;
        if (tryCategory("ThunderRain", WeatherCategory::ThunderRain)) continue;
        if (tryCategory("Snow",        WeatherCategory::Snow))        continue;
        if (tryCategory("Blizzard",    WeatherCategory::Blizzard))    continue;
        if (tryCategory("Ash",         WeatherCategory::Ash))         continue;
        if (tryCategory("Special",     WeatherCategory::Special))     continue;

        // Meta parameters
        if (key == "TransitionSpeed") { current->transitionSpeed = std::stof(val); continue; }
        if (key == "MinValue")        { current->minValue = std::stof(val); continue; }
        if (key == "MaxValue")        { current->maxValue = std::stof(val); continue; }
        if (key == "SmoothLerp")      { current->useSmooth = (val == "true" || val == "1"); continue; }
        if (key == "SBParam")         { current->sbParamName = val; continue; }
    }
}


void WeatherParameterComputer::Update(float deltaTime)
{
    std::lock_guard lock(m_mutex);

    // ─── Read weather state from game ───────────────────────────────

    auto* sky = RE::Sky::GetSingleton();
    if (!sky) return;

    // Store previous weather state
    m_prevWeatherID = m_currentWeatherID;
    m_prevCat       = m_currentCat;

    // Current weather
    if (sky->currentWeather) {
        m_currentWeatherID = sky->currentWeather->GetFormID();
        m_currentCat       = m_classifier.Classify(sky->currentWeather);
    }

    // Transition percentage from Sky
    m_transitionPct = sky->currentWeatherPct;

    // Previous weather (during transition)
    WeatherCategory prevTransCat = m_currentCat;
    if (sky->lastWeather) {
        prevTransCat = m_classifier.Classify(sky->lastWeather);
    }

    // ─── Interpolate all parameters ─────────────────────────────────

    for (auto& param : m_params)
    {
        // Get target values from both weather categories
        float curVal  = param.values[static_cast<int>(m_currentCat)];
        float prevVal = param.values[static_cast<int>(prevTransCat)];

        // Interpolate based on transition percentage
        float t = std::clamp(m_transitionPct * param.transitionSpeed, 0.0f, 1.0f);

        if (param.useSmooth)
            param.targetValue = SmoothLerp(prevVal, curVal, t);
        else
            param.targetValue = prevVal + (curVal - prevVal) * t;

        // Temporal smoothing: don't snap instantly
        float smoothSpeed = std::min(deltaTime * 2.0f, 1.0f);
        param.currentValue += (param.targetValue - param.currentValue) * smoothSpeed;

        // Clamp to valid range
        param.currentValue = std::clamp(param.currentValue, param.minValue, param.maxValue);
    }

    // ─── Push to ENB ────────────────────────────────────────────────

    PushAllToENB();

    // ─── Check hot reload periodically ──────────────────────────────

    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - m_lastReloadCheck).count() > 2) {
        m_lastReloadCheck = now;
        CheckHotReload();
    }
}


void WeatherParameterComputer::PushAllToENB()
{
    for (const auto& param : m_params)
    {
        // Push to ENB shader parameter
        // ENBSetShaderParameter format: (shaderFile, group, name, value)
        // This uses the same API that ParmLink's enb.setFloat() calls internally
        ENBInterface::SetFloat(
            param.shaderFile.c_str(),
            param.paramGroup.c_str(),
            param.paramName.c_str(),
            param.currentValue
        );

        // Optionally also push as an SB_ parameter (for SkyrimBridge-aware shaders)
        if (!param.sbParamName.empty()) {
            // Pack into a float4 for consistency (value in .x, category info in .yzw)
            Float4 packed;
            packed.x = param.currentValue;
            packed.y = static_cast<float>(m_currentCat);
            packed.z = m_transitionPct;
            packed.w = param.targetValue;
            ENBInterface::SetFloat4(param.sbParamName.c_str(), packed);
        }
    }

    // Also push weather state summary as a dedicated SB parameter
    // .w = 1.0 signals to shaders that weather params are actively being pushed.
    // SB_GetWP() in SkyrimBridge.fxh checks this sentinel instead of testing
    // individual params != 0.0 (which fails when a param legitimately equals 0).
    Float4 weatherState;
    weatherState.x = static_cast<float>(m_currentCat);
    weatherState.y = static_cast<float>(m_prevCat);
    weatherState.z = m_transitionPct;
    weatherState.w = 1.0f;  // active sentinel — shaders check this
    ENBInterface::SetFloat4("SB_WP_State", weatherState);
}


void WeatherParameterComputer::Shutdown()
{
    std::lock_guard lock(m_mutex);
    m_params.clear();
    m_paramIndex.clear();
}


void WeatherParameterComputer::CheckHotReload()
{
    // Check without lock (we're already locked in Update())
    auto classPath = m_configDir / "WeatherClasses.ini";
    auto paramPath = m_configDir / "WeatherParams.ini";

    bool needReload = false;

    if (std::filesystem::exists(classPath)) {
        auto mod = std::filesystem::last_write_time(classPath);
        if (mod != m_lastClassMod) {
            m_lastClassMod = mod;
            needReload = true;
        }
    }

    if (std::filesystem::exists(paramPath)) {
        auto mod = std::filesystem::last_write_time(paramPath);
        if (mod != m_lastParamMod) {
            m_lastParamMod = mod;
            needReload = true;
        }
    }

    if (needReload) {
        SKSE::log::info("WeatherParameterComputer: config changed, hot-reloading...");
        m_classifier = WeatherClassifier{};
        if (std::filesystem::exists(classPath))
            m_classifier.LoadFromINI(classPath);
        if (std::filesystem::exists(paramPath))
            LoadParameterINI(paramPath);
        SKSE::log::info("WeatherParameterComputer: reloaded {} params", m_params.size());
    }
}


void WeatherParameterComputer::ForceReload()
{
    std::lock_guard lock(m_mutex);
    m_classifier = WeatherClassifier{};
    auto classPath = m_configDir / "WeatherClasses.ini";
    auto paramPath = m_configDir / "WeatherParams.ini";
    if (std::filesystem::exists(classPath))
        m_classifier.LoadFromINI(classPath);
    if (std::filesystem::exists(paramPath))
        LoadParameterINI(paramPath);
}


float WeatherParameterComputer::GetValue(const std::string& paramName) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_paramIndex.find(paramName);
    if (it == m_paramIndex.end()) return 0.0f;
    return m_params[it->second].currentValue;
}


float WeatherParameterComputer::SmoothLerp(float a, float b, float t)
{
    // Hermite smoothstep: smoother transitions at boundaries
    t = t * t * (3.0f - 2.0f * t);
    return a + (b - a) * t;
}

float WeatherParameterComputer::InterpolateParam(
    const WeatherParameterDef& param, float transitionPct) const
{
    float curVal  = param.values[static_cast<int>(m_currentCat)];
    float prevVal = param.values[static_cast<int>(m_prevCat)];
    float t = std::clamp(transitionPct * param.transitionSpeed, 0.0f, 1.0f);

    if (param.useSmooth)
        return SmoothLerp(prevVal, curVal, t);
    return prevVal + (curVal - prevVal) * t;
}


}  // namespace SB
