//=============================================================================
//  PapyrusBridge.cpp — Native Papyrus functions for SkyrimBridge
//
//  Exposes SkyrimBridge data to Papyrus scripts so mod authors can read
//  any tracked game state parameter from their .psc files.
//
//  Functions registered under script name "SkyrimBridge":
//    bool   SkyrimBridge.IsActive()
//    float  SkyrimBridge.GetFloat(string paramName, int component)
//    float  SkyrimBridge.GetGameHour()
//    float  SkyrimBridge.GetQualityScale()
//    int    SkyrimBridge.GetWeatherFormID()
//    bool   SkyrimBridge.IsInterior()
//    int    SkyrimBridge.GetParamCount()
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "PapyrusBridge.h"
#include "BridgeData.h"
#include "WeatherEditor.h"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <cstring>

namespace SB::PapyrusBridge
{
    // Cached pointer to the last AllData — updated each frame by main.cpp
    static AllData s_cachedData{};
    static bool    s_active = false;

    void UpdateCache(const AllData& a_data)
    {
        std::memcpy(&s_cachedData, &a_data, sizeof(AllData));
        s_active = true;
    }

    // ── Papyrus native functions ─────────────────────────────────────────

    static bool IsActive(RE::StaticFunctionTag*)
    {
        return s_active;
    }

    static float GetFloat(RE::StaticFunctionTag*,
                          RE::BSFixedString a_paramName,
                          int32_t a_component)
    {
        if (!s_active || a_component < 0 || a_component > 3)
            return 0.f;

        const char* name = a_paramName.c_str();

        // Search the parameter table for the named field
        for (std::size_t i = 0; i < kParamCount; i++) {
            if (std::strcmp(kParamTable[i].name, name) == 0) {
                const auto* f4 = reinterpret_cast<const Float4*>(
                    reinterpret_cast<const char*>(&s_cachedData) + kParamTable[i].offset);
                switch (a_component) {
                case 0: return f4->x;
                case 1: return f4->y;
                case 2: return f4->z;
                case 3: return f4->w;
                }
            }
        }
        return 0.f;
    }

    static float GetGameHour(RE::StaticFunctionTag*)
    {
        return s_cachedData.celestial.TimeData.x;
    }

    static float GetQualityScale(RE::StaticFunctionTag*)
    {
        return s_cachedData.perf.Budget.y;
    }

    static int32_t GetWeatherFormID(RE::StaticFunctionTag*)
    {
        return static_cast<int32_t>(s_cachedData.weather.Transition.z);
    }

    static bool IsInterior(RE::StaticFunctionTag*)
    {
        return s_cachedData.interior.IsInterior.x > 0.5f;
    }

    static int32_t GetParamCount(RE::StaticFunctionTag*)
    {
        return static_cast<int32_t>(kParamCount);
    }

    // ── Weather workshop natives ─────────────────────────────────────────
    // Console-callable (cgf "SkyrimBridge.<name>"): the control surface for
    // the live weather editor when no GUI is present.

    static void CaptureWeather(RE::StaticFunctionTag*)
    {
        WeatherEditor::Get().CaptureCurrentWeather();
    }

    static void ApplyWeather(RE::StaticFunctionTag*)
    {
        WeatherEditor::Get().MarkDirty();
        WeatherEditor::Get().ApplyToGame();
    }

    static void RevertWeather(RE::StaticFunctionTag*)
    {
        WeatherEditor::Get().RevertToOriginal();
    }

    static bool SaveWeatherPreset(RE::StaticFunctionTag*, RE::BSFixedString a_name)
    {
        return WeatherEditor::Get().SavePreset(a_name.c_str());
    }

    static bool LoadWeatherPreset(RE::StaticFunctionTag*, RE::BSFixedString a_name)
    {
        bool ok = WeatherEditor::Get().LoadPreset(a_name.c_str());
        if (ok)
            WeatherEditor::Get().ApplyToGame();
        return ok;
    }

    static void SetWeatherCompare(RE::StaticFunctionTag*, bool a_original)
    {
        WeatherEditor::Get().SetCompareMode(a_original);
    }

    static void ForceWeatherByID(RE::StaticFunctionTag*, int32_t a_formID)
    {
        auto* weather = RE::TESForm::LookupByID<RE::TESWeather>(
            static_cast<RE::FormID>(a_formID));
        if (weather)
            WeatherEditor::Get().ForceWeather(weather);
        else
            SKSE::log::warn("PapyrusBridge: no weather with form ID 0x{:08X}", a_formID);
    }

    static void ClearForcedWeather(RE::StaticFunctionTag*)
    {
        WeatherEditor::Get().ClearForcedWeather();
    }

    // ── Registration ─────────────────────────────────────────────────────

    bool Register()
    {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            SKSE::log::warn("PapyrusBridge: VM not available");
            return false;
        }

        vm->RegisterFunction("IsActive",         "SkyrimBridge", IsActive);
        vm->RegisterFunction("GetFloat",          "SkyrimBridge", GetFloat);
        vm->RegisterFunction("GetGameHour",       "SkyrimBridge", GetGameHour);
        vm->RegisterFunction("GetQualityScale",   "SkyrimBridge", GetQualityScale);
        vm->RegisterFunction("GetWeatherFormID",  "SkyrimBridge", GetWeatherFormID);
        vm->RegisterFunction("IsInterior",        "SkyrimBridge", IsInterior);
        vm->RegisterFunction("GetParamCount",     "SkyrimBridge", GetParamCount);

        vm->RegisterFunction("CaptureWeather",     "SkyrimBridge", CaptureWeather);
        vm->RegisterFunction("ApplyWeather",       "SkyrimBridge", ApplyWeather);
        vm->RegisterFunction("RevertWeather",      "SkyrimBridge", RevertWeather);
        vm->RegisterFunction("SaveWeatherPreset",  "SkyrimBridge", SaveWeatherPreset);
        vm->RegisterFunction("LoadWeatherPreset",  "SkyrimBridge", LoadWeatherPreset);
        vm->RegisterFunction("SetWeatherCompare",  "SkyrimBridge", SetWeatherCompare);
        vm->RegisterFunction("ForceWeatherByID",   "SkyrimBridge", ForceWeatherByID);
        vm->RegisterFunction("ClearForcedWeather", "SkyrimBridge", ClearForcedWeather);

        SKSE::log::info("PapyrusBridge: registered 15 native functions under 'SkyrimBridge'");
        return true;
    }
}
