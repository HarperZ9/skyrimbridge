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
#include "RegionWalker.h"
#include "TextureCodec.h"
#include "TextureAutoConvert.h"
#include "ModelCodec.h"
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

    static int32_t EngineReflectVerifyStrict(RE::StaticFunctionTag*, int32_t a_formID)
    {
        // Opt-in: writes every schema field back with its current value.
        auto r = Reflect::VerifyStrict(static_cast<RE::FormID>(a_formID));
        if (r.ok)
            SKSE::log::info("EngineReflect: 0x{:08X} strict-verified: write-back idempotent ({} fields)", a_formID, r.fields);
        else
            SKSE::log::warn("EngineReflect: 0x{:08X} strict verify FAILED: {}", a_formID, r.detail);
        return r.ok ? r.fields : 0;
    }

    // cgf "SkyrimBridge.EngineReflectList" 0    -> all schemas (log + file)
    // cgf "SkyrimBridge.EngineReflectList" 0x<id> -> that form's schema fields
    static int32_t EngineReflectList(RE::StaticFunctionTag*, int32_t a_formID)
    {
        std::string text;
        std::string label;
        if (a_formID == 0) {
            text = Reflect::ListSchemas();
            label = "schemas";
        } else {
            auto* form = RE::TESForm::LookupByID(static_cast<RE::FormID>(a_formID));
            if (!form) { SKSE::log::warn("EngineReflect: 0x{:08X} does not resolve", a_formID); return 0; }
            auto* s = Reflect::SchemaFor(form->GetFormType());
            if (!s) { SKSE::log::warn("EngineReflect: no schema for form type of 0x{:08X}", a_formID); return 0; }
            text = Reflect::DescribeSchema(*s);
            label = s->name;
        }
        std::error_code ec;
        std::filesystem::create_directories(kReflectDumpDir, ec);
        std::string path = std::string(kReflectDumpDir) + "/schema-" + label + ".txt";
        std::ofstream out(path);
        if (out) out << text;
        SKSE::log::info("EngineReflect: {} ->\n{}", path, text);
        int32_t lines = 0;
        for (char c : text) if (c == '\n') ++lines;
        return lines;
    }

    // ── Region walker natives (structured TESRegionData subrecords) ──────
    // cgf "SkyrimBridge.RegionDump" 0x<id>  -> dumps/<id>.region.ini
    // (weather list with chances, sounds, map name, land icon). Edit the
    // WeatherChance lines, then RegionApply. Writes are BOUNDED: chance edits
    // on existing entries only, never list surgery.

    static RE::BSFixedString RegionDump(RE::StaticFunctionTag*, int32_t a_formID)
    {
        auto text = RegionWalker::Dump(static_cast<RE::FormID>(a_formID));
        if (text.empty()) {
            SKSE::log::warn("RegionWalker: 0x{:08X} is not a region", a_formID);
            return RE::BSFixedString("");
        }
        std::error_code ec;
        std::filesystem::create_directories(kReflectDumpDir, ec);
        char name[96];
        std::snprintf(name, sizeof name, "%s/%08X.region.ini", kReflectDumpDir, a_formID);
        std::ofstream out(name);
        if (out) out << text;
        SKSE::log::info("RegionWalker: dumped 0x{:08X} -> {}", a_formID, name);
        return RE::BSFixedString(name);
    }

    static int32_t RegionSetWeatherChance(RE::StaticFunctionTag*, int32_t a_regionID,
                                          int32_t a_weatherID, int32_t a_chance)
    {
        int n = RegionWalker::SetWeatherChance(static_cast<RE::FormID>(a_regionID),
                                               static_cast<RE::FormID>(a_weatherID),
                                               static_cast<std::uint32_t>(std::max(a_chance, 0)));
        SKSE::log::info("RegionWalker: 0x{:08X} weather 0x{:08X} chance -> {} ({} entries)",
                        a_regionID, a_weatherID, a_chance, n);
        return n;
    }

    static int32_t RegionApply(RE::StaticFunctionTag*, int32_t a_formID)
    {
        char name[96];
        std::snprintf(name, sizeof name, "%s/%08X.region.ini", kReflectDumpDir, a_formID);
        std::ifstream in(name);
        if (!in) {
            SKSE::log::warn("RegionWalker: no dump at {}", name);
            return 0;
        }
        std::stringstream b; b << in.rdbuf();
        int n = RegionWalker::Apply(static_cast<RE::FormID>(a_formID), b.str());
        SKSE::log::info("RegionWalker: applied {} weather-chance edits to 0x{:08X}", n, a_formID);
        return n;
    }

    // ── Texture codec natives (non-.dds asset integration) ───────────────
    // cgf "SkyrimBridge.ConvertTexture" "in.png" "out.dds"  (paths game-relative
    // or absolute). Decodes PNG/TGA/BMP/DDS (incl. DXT1/DXT5) and writes by
    // output extension: .dds = mipmapped uncompressed RGBA8, .tga = 32-bit TGA
    // (the DDS -> editable lane).
    // cgf "SkyrimBridge.ConvertTextureFmt" "in.png" "out.dds" "BC3"  writes
    // block-compressed DDS ("BC1" opaque / "BC3" with alpha / "BC7" DX10
    // header, mode-6 baseline / "RGBA8").

    static bool ConvertTexture(RE::StaticFunctionTag*, RE::BSFixedString a_in, RE::BSFixedString a_out)
    {
        bool ok = TexCodec::Convert(a_in.c_str(), a_out.c_str());
        SKSE::log::info("TextureCodec: {} -> {} : {}", a_in.c_str(), a_out.c_str(), ok ? "ok" : "failed");
        return ok;
    }

    // cgf "SkyrimBridge.TextureScanNow" true   -> dry run (counts only, no writes)
    // cgf "SkyrimBridge.TextureScanNow" false  -> convert foreign textures now
    static int32_t TextureScanNow(RE::StaticFunctionTag*, bool a_dryRun)
    {
        auto r = TextureAutoConvert::Get().RunScan(a_dryRun);
        SKSE::log::info("TextureAutoConvert ({}): {} foreign, {} would-convert/converted, "
                        "{} have .dds, {} failed",
                        a_dryRun ? "dry run" : "live",
                        r.candidates, r.converted, r.skipped, r.failed);
        return static_cast<int32_t>(r.converted);
    }

    // cgf "SkyrimBridge.ConvertModel" "in.obj" "out.nif"  — foreign static mesh
    // (OBJ / glTF / GLB) -> Skyrim SE NIF (BSTriShape). Static single-shape only.
    static bool ConvertModel(RE::StaticFunctionTag*, RE::BSFixedString a_in, RE::BSFixedString a_out)
    {
        bool ok = ModelCodec::ConvertToNIF(a_in.c_str(), a_out.c_str());
        SKSE::log::info("ModelCodec: {} -> {} : {}", a_in.c_str(), a_out.c_str(), ok ? "ok" : "failed");
        return ok;
    }

    // cgf "SkyrimBridge.SpawnModel" "in.obj"  — the pragmatic runtime-model
    // path. Materializes the mesh under meshes\SkyrimBridge\spawn\ (foreign
    // formats convert; a .nif copies), creates a dynamic Static form whose
    // model points at it, and places one reference at the player, so the
    // ENGINE's own model loader constructs the NiObject graph. No synthetic
    // engine objects: the same architecture rule as the texture-load hook.
    // Returns the placed reference's FormID, 0 on failure. The dynamic form
    // and the reference persist in the save: test on a disposable save and
    // remove with the console (click the ref, "markfordelete").
    static std::int32_t SpawnModel(RE::StaticFunctionTag*, RE::BSFixedString a_in)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded()) {
            SKSE::log::warn("SpawnModel: needs a loaded player (in-game only)");
            return 0;
        }

        std::filesystem::path in(a_in.c_str());
        std::error_code ec;
        std::filesystem::create_directories("Data/meshes/SkyrimBridge/spawn", ec);
        const std::string stem = in.stem().string();
        const std::filesystem::path out =
            std::filesystem::path("Data/meshes/SkyrimBridge/spawn") / (stem + ".nif");

        bool ok;
        if (_stricmp(in.extension().string().c_str(), ".nif") == 0) {
            std::filesystem::copy_file(in, out, std::filesystem::copy_options::overwrite_existing, ec);
            ok = !ec;
        } else {
            ok = ModelCodec::ConvertToNIF(in, out);
        }
        if (!ok) {
            SKSE::log::warn("SpawnModel: could not materialize {} -> {}", in.string(), out.string());
            return 0;
        }

        RE::TESObjectSTAT* stat = nullptr;
        if (auto* factory = RE::IFormFactory::GetFormFactoryByType(RE::FormType::Static))
            if (auto* form = factory->Create())
                stat = form->As<RE::TESObjectSTAT>();
        if (!stat) {
            SKSE::log::warn("SpawnModel: Static form factory unavailable");
            return 0;
        }
        const std::string rel = "SkyrimBridge\\spawn\\" + stem + ".nif";
        stat->SetModel(rel.c_str());

        auto ref = player->PlaceObjectAtMe(stat, false);
        if (!ref) {
            SKSE::log::warn("SpawnModel: PlaceObjectAtMe failed for {}", rel);
            return 0;
        }
        SKSE::log::info("SpawnModel: {} -> meshes\\{} : placed 0x{:08X} (dynamic STAT 0x{:08X})",
                        in.string(), rel, ref->GetFormID(), stat->GetFormID());
        return static_cast<std::int32_t>(ref->GetFormID());
    }

    static bool ConvertTextureFmt(RE::StaticFunctionTag*, RE::BSFixedString a_in,
                                  RE::BSFixedString a_out, RE::BSFixedString a_fmt)
    {
        auto fmt = TexCodec::DDSFormat::RGBA8;
        if (_stricmp(a_fmt.c_str(), "BC1") == 0)      fmt = TexCodec::DDSFormat::BC1;
        else if (_stricmp(a_fmt.c_str(), "BC3") == 0) fmt = TexCodec::DDSFormat::BC3;
        else if (_stricmp(a_fmt.c_str(), "BC7") == 0) fmt = TexCodec::DDSFormat::BC7;
        else if (_stricmp(a_fmt.c_str(), "RGBA8") != 0) {
            SKSE::log::warn("TextureCodec: unknown format '{}' (BC1/BC3/BC7/RGBA8)", a_fmt.c_str());
            return false;
        }
        bool ok = TexCodec::Convert(a_in.c_str(), a_out.c_str(), fmt);
        SKSE::log::info("TextureCodec: {} -> {} [{}] : {}", a_in.c_str(), a_out.c_str(), a_fmt.c_str(), ok ? "ok" : "failed");
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

        vm->RegisterFunction("EngineReflectDump",         "SkyrimBridge", EngineReflectDump);
        vm->RegisterFunction("EngineReflectApply",        "SkyrimBridge", EngineReflectApply);
        vm->RegisterFunction("EngineReflectVerify",       "SkyrimBridge", EngineReflectVerify);
        vm->RegisterFunction("EngineReflectVerifyStrict", "SkyrimBridge", EngineReflectVerifyStrict);
        vm->RegisterFunction("EngineReflectList",         "SkyrimBridge", EngineReflectList);

        vm->RegisterFunction("RegionDump",             "SkyrimBridge", RegionDump);
        vm->RegisterFunction("RegionSetWeatherChance", "SkyrimBridge", RegionSetWeatherChance);
        vm->RegisterFunction("RegionApply",            "SkyrimBridge", RegionApply);

        vm->RegisterFunction("ConvertTexture",      "SkyrimBridge", ConvertTexture);
        vm->RegisterFunction("ConvertTextureFmt",   "SkyrimBridge", ConvertTextureFmt);
        vm->RegisterFunction("TextureScanNow",      "SkyrimBridge", TextureScanNow);
        vm->RegisterFunction("ConvertModel",        "SkyrimBridge", ConvertModel);
        vm->RegisterFunction("SpawnModel",          "SkyrimBridge", SpawnModel);

        SKSE::log::info("PapyrusBridge: registered 32 native functions under 'SkyrimBridge'");
        return true;
    }
}
