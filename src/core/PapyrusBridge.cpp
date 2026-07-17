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
#include "KreateProfile.h"
#include "EngineReflect.h"
#include "TextureCodec.h"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

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

    // ── KreatE profile natives (the menu surface) ────────────────────────
    // Console: cgf "SkyrimBridge.LoadKreateProfile" "Arrival of Autumn"

    static int32_t LoadKreateProfile(RE::StaticFunctionTag*, RE::BSFixedString a_name)
    {
        return KreateProfile::Get().LoadAndApply(a_name.c_str());
    }

    static int32_t KreateProfileCount(RE::StaticFunctionTag*)
    {
        return static_cast<int32_t>(KreateProfile::Get().ListProfiles().size());
    }

    static RE::BSFixedString KreateProfileNameAt(RE::StaticFunctionTag*, int32_t a_index)
    {
        auto names = KreateProfile::Get().ListProfiles();
        if (a_index < 0 || a_index >= static_cast<int32_t>(names.size()))
            return RE::BSFixedString("");
        return RE::BSFixedString(names[a_index].c_str());
    }

    static RE::BSFixedString ActiveKreateProfile(RE::StaticFunctionTag*)
    {
        return RE::BSFixedString(KreateProfile::Get().LoadedName().c_str());
    }

    // ── EngineReflect natives (generic record read/write/verify) ─────────
    // In-game loop:  SkyrimBridge.EngineReflectDump 0x<formid>  (writes an INI
    //   under SkyrimBridge/dumps), edit it, then EngineReflectApply 0x<formid>.
    //   EngineReflectVerify witnesses a lossless serialize round-trip.

    static const char* kReflectDumpDir = "Data/SKSE/Plugins/SkyrimBridge/dumps";

    static RE::BSFixedString EngineReflectDump(RE::StaticFunctionTag*, int32_t a_formID)
    {
        auto text = Reflect::Dump(static_cast<RE::FormID>(a_formID));
        if (text.empty()) {
            SKSE::log::warn("EngineReflect: 0x{:08X} has no schema or does not resolve", a_formID);
            return RE::BSFixedString("");
        }
        std::error_code ec;
        std::filesystem::create_directories(kReflectDumpDir, ec);
        char name[80];
        std::snprintf(name, sizeof name, "%s/%08X.ini", kReflectDumpDir, a_formID);
        std::ofstream out(name);
        if (out) out << text;
        SKSE::log::info("EngineReflect: dumped 0x{:08X} -> {}", a_formID, name);
        return RE::BSFixedString(name);
    }

    static int32_t EngineReflectApply(RE::StaticFunctionTag*, int32_t a_formID)
    {
        char name[80];
        std::snprintf(name, sizeof name, "%s/%08X.ini", kReflectDumpDir, a_formID);
        std::ifstream in(name);
        if (!in) {
            SKSE::log::warn("EngineReflect: no dump at {}", name);
            return 0;
        }
        std::stringstream b; b << in.rdbuf();
        int n = Reflect::Apply(static_cast<RE::FormID>(a_formID), b.str());
        SKSE::log::info("EngineReflect: applied {} fields to 0x{:08X}", n, a_formID);
        return n;
    }

    static int32_t EngineReflectVerify(RE::StaticFunctionTag*, int32_t a_formID)
    {
        auto r = Reflect::Verify(static_cast<RE::FormID>(a_formID));
        if (r.ok)
            SKSE::log::info("EngineReflect: 0x{:08X} verified lossless ({} fields)", a_formID, r.fields);
        else
            SKSE::log::warn("EngineReflect: 0x{:08X} verify FAILED: {}", a_formID, r.detail);
        return r.ok ? r.fields : 0;
    }

    // ── Texture codec native (non-.dds asset integration) ────────────────
    // cgf "SkyrimBridge.ConvertTexture" "in.tga" "out.dds"  (paths game-relative
    // or absolute). Decodes TGA/BMP and writes an uncompressed DDS the engine
    // accepts. PNG is not yet supported (needs an inflate stage).

    static bool ConvertTexture(RE::StaticFunctionTag*, RE::BSFixedString a_in, RE::BSFixedString a_out)
    {
        bool ok = TexCodec::ConvertToDDS(a_in.c_str(), a_out.c_str());
        SKSE::log::info("TextureCodec: {} -> {} : {}", a_in.c_str(), a_out.c_str(), ok ? "ok" : "failed");
        return ok;
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

        vm->RegisterFunction("LoadKreateProfile",   "SkyrimBridge", LoadKreateProfile);
        vm->RegisterFunction("KreateProfileCount",  "SkyrimBridge", KreateProfileCount);
        vm->RegisterFunction("KreateProfileNameAt", "SkyrimBridge", KreateProfileNameAt);
        vm->RegisterFunction("ActiveKreateProfile", "SkyrimBridge", ActiveKreateProfile);

        vm->RegisterFunction("EngineReflectDump",   "SkyrimBridge", EngineReflectDump);
        vm->RegisterFunction("EngineReflectApply",  "SkyrimBridge", EngineReflectApply);
        vm->RegisterFunction("EngineReflectVerify", "SkyrimBridge", EngineReflectVerify);

        vm->RegisterFunction("ConvertTexture",      "SkyrimBridge", ConvertTexture);

        SKSE::log::info("PapyrusBridge: registered 23 native functions under 'SkyrimBridge'");
        return true;
    }
}
