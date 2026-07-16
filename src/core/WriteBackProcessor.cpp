#include "WriteBackProcessor.h"
#include "ENBInterface_v3.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <fstream>
#include <string>
#include <algorithm>
#include <cstring>
#include <cctype>

namespace SB
{
    // ── String helpers ──────────────────────────────────────────────────────────

    static std::string TrimWS(const std::string& s)
    {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return {};
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    static std::string ToLower(std::string s)
    {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // ── Singleton ───────────────────────────────────────────────────────────────

    WriteBackProcessor& WriteBackProcessor::Get()
    {
        static WriteBackProcessor instance;
        return instance;
    }

    int WriteBackProcessor::GetEnabledRuleCount() const
    {
        int count = 0;
        for (auto& r : m_rules)
            if (r.enabled) ++count;
        return count;
    }

    // ── Enum parsing ────────────────────────────────────────────────────────────

    WriteBackTarget WriteBackProcessor::ParseTarget(const std::string& s)
    {
        auto lower = ToLower(TrimWS(s));
        if (lower == "camerafov")          return WriteBackTarget::CameraFOV;
        if (lower == "camerafov1st")       return WriteBackTarget::CameraFOV1st;
        if (lower == "fogneardist")        return WriteBackTarget::FogNearDist;
        if (lower == "fogfardist")         return WriteBackTarget::FogFarDist;
        if (lower == "sunlightdiffuse_r")  return WriteBackTarget::SunlightDiffuse_R;
        if (lower == "sunlightdiffuse_g")  return WriteBackTarget::SunlightDiffuse_G;
        if (lower == "sunlightdiffuse_b")  return WriteBackTarget::SunlightDiffuse_B;
        if (lower == "ambientdiffuse_r")   return WriteBackTarget::AmbientDiffuse_R;
        if (lower == "ambientdiffuse_g")   return WriteBackTarget::AmbientDiffuse_G;
        if (lower == "ambientdiffuse_b")   return WriteBackTarget::AmbientDiffuse_B;
        if (lower == "actorvalue")         return WriteBackTarget::ActorValue;
        if (lower == "timescale")          return WriteBackTarget::TimeScale;
        if (lower == "gamehour")           return WriteBackTarget::GameHour;
        return WriteBackTarget::CameraFOV; // default fallback
    }

    WriteBackSource WriteBackProcessor::ParseSource(const std::string& s)
    {
        auto lower = ToLower(TrimWS(s));
        if (lower == "fixed")          return WriteBackSource::Fixed;
        if (lower == "alldatafield")   return WriteBackSource::AllDataField;
        if (lower == "enbparameter")   return WriteBackSource::ENBParameter;
        if (lower == "enbreadback")    return WriteBackSource::ENBParameter;  // legacy alias
        return WriteBackSource::Fixed;
    }

    // ── Field name resolution ───────────────────────────────────────────────────
    // Parses "SB_Computed_Luminance.x" → (byte offset into AllData, component 0-3)

    std::pair<std::size_t, int> WriteBackProcessor::ParseFieldName(const std::string& fieldName)
    {
        // Split at '.' to get param name and swizzle component
        auto dot = fieldName.rfind('.');
        std::string paramName;
        int component = 0;

        if (dot != std::string::npos && dot + 1 < fieldName.size()) {
            paramName = fieldName.substr(0, dot);
            char comp = static_cast<char>(std::tolower(static_cast<unsigned char>(fieldName[dot + 1])));
            switch (comp) {
            case 'x': case 'r': component = 0; break;
            case 'y': case 'g': component = 1; break;
            case 'z': case 'b': component = 2; break;
            case 'w': case 'a': component = 3; break;
            default: component = 0; break;
            }
        } else {
            paramName = fieldName;
        }

        // Look up in kParamTable
        for (std::size_t i = 0; i < kParamCount; ++i) {
            if (paramName == kParamTable[i].name)
                return { kParamTable[i].offset, component };
        }

        SKSE::log::warn("WriteBackProcessor: unknown field '{}'", fieldName);
        return { SIZE_MAX, 0 };
    }

    // ── Source resolution ───────────────────────────────────────────────────────

    float WriteBackProcessor::ResolveSource(const WriteBackRule& rule, const AllData& data)
    {
        switch (rule.source) {
        case WriteBackSource::Fixed:
            return rule.fixedValue;

        case WriteBackSource::AllDataField: {
            auto [offset, comp] = ParseFieldName(rule.sourceField);
            if (offset == SIZE_MAX) return 0.0f;

            auto* base = reinterpret_cast<const uint8_t*>(&data);
            auto& vec = *reinterpret_cast<const Float4*>(base + offset);
            return (&vec.x)[comp];
        }

        case WriteBackSource::ENBParameter: {
            // Live read of an ENB shader parameter through the SDK: presets
            // can drive engine state by authoring a parameter and naming it
            // here. Returns 0 when ENB is absent or the parameter is unknown.
            if (rule.sourceShader.empty() || rule.sourceField.empty())
                return 0.0f;
            return ENBInterface::GetFloat(
                rule.sourceShader.c_str(), nullptr, rule.sourceField.c_str());
        }

        default:
            return 0.0f;
        }
    }

    // ── Target application ──────────────────────────────────────────────────────

    void WriteBackProcessor::ApplyTarget(WriteBackTarget target, float value, int avId)
    {
        switch (target) {
        case WriteBackTarget::CameraFOV: {
            auto* cam = RE::PlayerCamera::GetSingleton();
            if (cam) cam->worldFOV = value;
            break;
        }
        case WriteBackTarget::CameraFOV1st: {
            auto* cam = RE::PlayerCamera::GetSingleton();
            if (cam) cam->firstPersonFOV = value;
            break;
        }
        case WriteBackTarget::FogNearDist: {
            auto* sky = RE::Sky::GetSingleton();
            if (sky && sky->currentWeather) {
                sky->currentWeather->fogData.dayNear = value;
                sky->currentWeather->fogData.nightNear = value;
            }
            break;
        }
        case WriteBackTarget::FogFarDist: {
            auto* sky = RE::Sky::GetSingleton();
            if (sky && sky->currentWeather) {
                sky->currentWeather->fogData.dayFar = value;
                sky->currentWeather->fogData.nightFar = value;
            }
            break;
        }
        case WriteBackTarget::SunlightDiffuse_R:
        case WriteBackTarget::SunlightDiffuse_G:
        case WriteBackTarget::SunlightDiffuse_B: {
            auto* sky = RE::Sky::GetSingleton();
            if (sky && sky->sun && sky->sun->light) {
                // NiDirectionalLight inherits NiLight but is only forward-declared
                auto* niLight = reinterpret_cast<RE::NiLight*>(sky->sun->light.get());
                auto& color = niLight->GetLightRuntimeData().diffuse;
                int comp = static_cast<int>(target) - static_cast<int>(WriteBackTarget::SunlightDiffuse_R);
                (&color.red)[comp] = value;
            }
            break;
        }
        case WriteBackTarget::AmbientDiffuse_R:
        case WriteBackTarget::AmbientDiffuse_G:
        case WriteBackTarget::AmbientDiffuse_B: {
            auto* sky = RE::Sky::GetSingleton();
            if (sky && sky->sun && sky->sun->light) {
                auto* niLight = reinterpret_cast<RE::NiLight*>(sky->sun->light.get());
                auto& color = niLight->GetLightRuntimeData().ambient;
                int comp = static_cast<int>(target) - static_cast<int>(WriteBackTarget::AmbientDiffuse_R);
                (&color.red)[comp] = value;
            }
            break;
        }
        case WriteBackTarget::ActorValue: {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                if (auto* avOwner = player->AsActorValueOwner()) {
                    auto av = static_cast<RE::ActorValue>(avId);
                    avOwner->SetActorValue(av, value);
                }
            }
            break;
        }
        case WriteBackTarget::TimeScale: {
            auto* calendar = RE::Calendar::GetSingleton();
            if (calendar && calendar->timeScale) {
                calendar->timeScale->value = value;
            }
            break;
        }
        case WriteBackTarget::GameHour: {
            auto* calendar = RE::Calendar::GetSingleton();
            if (calendar && calendar->gameHour) {
                calendar->gameHour->value = value;
            }
            break;
        }
        default:
            break;
        }
    }

    // ── Per-frame execution ─────────────────────────────────────────────────────

    void WriteBackProcessor::Execute(const AllData& data)
    {
        for (auto& rule : m_rules) {
            if (!rule.enabled) continue;

            // Resolve source value
            float raw = ResolveSource(rule, data);

            // Apply transform: scale, offset, clamp
            float transformed = raw * rule.transform.scale + rule.transform.offset;
            transformed = (std::max)(transformed, rule.transform.clampMin);
            transformed = (std::min)(transformed, rule.transform.clampMax);

            // Temporal smoothing via lerp
            if (!rule.initialized) {
                rule.currentValue = transformed;
                rule.initialized = true;
            } else {
                float alpha = rule.transform.lerpAlpha;
                rule.currentValue += (transformed - rule.currentValue) * alpha;
            }

            ApplyTarget(rule.target, rule.currentValue, rule.actorValueId);
        }
    }

    // ── INI loading ─────────────────────────────────────────────────────────────

    void WriteBackProcessor::LoadConfig(const std::filesystem::path& configDir)
    {
        auto path = configDir / "WriteBackConfig.ini";

        std::ifstream file(path);
        if (!file.is_open()) {
            SKSE::log::info("WriteBackProcessor: no config at {} — write-back disabled",
                path.string());
            return;
        }

        SKSE::log::info("WriteBackProcessor: loading config from {}", path.string());

        std::string currentSection;
        WriteBackRule currentRule;
        bool inRule = false;

        auto flushRule = [&]() {
            if (inRule) {
                m_rules.push_back(std::move(currentRule));
                currentRule = WriteBackRule{};
                inRule = false;
            }
        };

        std::string line;
        while (std::getline(file, line)) {
            auto trimmed = TrimWS(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
                continue;

            // Section header: [Rule_N]
            if (trimmed[0] == '[') {
                flushRule();
                auto close = trimmed.find(']');
                if (close != std::string::npos)
                    currentSection = trimmed.substr(1, close - 1);
                else
                    currentSection = trimmed.substr(1);

                // Only process Rule_* sections
                if (currentSection.rfind("Rule_", 0) == 0) {
                    inRule = true;
                    currentRule = WriteBackRule{};
                }
                continue;
            }

            if (!inRule) continue;

            // Parse key=value
            auto eq = trimmed.find('=');
            if (eq == std::string::npos) continue;

            auto key = ToLower(TrimWS(trimmed.substr(0, eq)));
            auto val = TrimWS(trimmed.substr(eq + 1));

            if (key == "name")          currentRule.name = val;
            else if (key == "enabled")  currentRule.enabled = (ToLower(val) == "true" || val == "1");
            else if (key == "target")   currentRule.target = ParseTarget(val);
            else if (key == "source")   currentRule.source = ParseSource(val);
            else if (key == "sourcefield") currentRule.sourceField = val;
            else if (key == "sourceshader") currentRule.sourceShader = val;
            else if (key == "sourceindex") {
                try { currentRule.sourceIndex = std::stoi(val); } catch (...) {}
            }
            else if (key == "fixedvalue") {
                try { currentRule.fixedValue = std::stof(val); } catch (...) {}
            }
            else if (key == "actorvalueid") {
                try { currentRule.actorValueId = std::stoi(val); } catch (...) {}
            }
            else if (key == "scale") {
                try { currentRule.transform.scale = std::stof(val); } catch (...) {}
            }
            else if (key == "offset") {
                try { currentRule.transform.offset = std::stof(val); } catch (...) {}
            }
            else if (key == "clampmin") {
                try { currentRule.transform.clampMin = std::stof(val); } catch (...) {}
            }
            else if (key == "clampmax") {
                try { currentRule.transform.clampMax = std::stof(val); } catch (...) {}
            }
            else if (key == "lerpalpha") {
                try { currentRule.transform.lerpAlpha = std::stof(val); } catch (...) {}
            }
        }

        flushRule();

        int enabledCount = GetEnabledRuleCount();
        SKSE::log::info("WriteBackProcessor: loaded {} rules ({} enabled)",
            m_rules.size(), enabledCount);

        for (int i = 0; i < static_cast<int>(m_rules.size()); ++i) {
            auto& r = m_rules[i];
            SKSE::log::info("  Rule[{}]: '{}' — {} ({})",
                i, r.name, r.enabled ? "ENABLED" : "disabled",
                static_cast<int>(r.target));
        }
    }
}
