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
#include "ModelSpawn.h"
#include "CellReport.h"
#include "ScriptReport.h"
#include "CollisionMaterial.h"
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

    // cgf "SkyrimBridge.ConvertModelTree" "in.obj" "out.nif"  — same converter
    // in tree mode: procedural wind weights painted into the vertex colors
    // (127 trunk base rising to 255 at canopy extremities, the mapping real
    // animated trees carry), SLSF2_Tree_Anim on the shader, BSLeafAnimNode
    // root. Whether a STAT-placed reference sways is in-game validation.
    static bool ConvertModelTree(RE::StaticFunctionTag*, RE::BSFixedString a_in, RE::BSFixedString a_out)
    {
        bool ok = ModelCodec::ConvertToNIF(a_in.c_str(), a_out.c_str(), true);
        SKSE::log::info("ModelCodec: {} -> {} [tree] : {}", a_in.c_str(), a_out.c_str(), ok ? "ok" : "failed");
        return ok;
    }

    // cgf "SkyrimBridge.ConvertModelEx" "in.obj" "out.nif" false true
    // Full option surface: tree mode and/or convex-hull collision (the F18
    // recipe: bhkConvexVerticesShape + templated static rigid body; a
    // degenerate hull falls back to no collision). Walk-testing in-game is
    // the collision acceptance oracle.
    // cgf "SkyrimBridge.CellReport"  — one-command performance census of the
    // player's current cell: refs by type, every shadow-casting light with
    // plugin attribution and distance (nearest first), refs per winning
    // plugin. Read-only. Full text: SkyrimBridge/dumps/cellreport.txt.
    // Returns the shadow-casting light count (-1 = no cell).
    static std::int32_t CellReportNative(RE::StaticFunctionTag*)
    {
        auto text = CellReport::Run();
        if (text.empty()) {
            SKSE::log::warn("CellReport: no player cell (in-game only)");
            return -1;
        }
        auto pos = text.find("shadowLights=");
        return pos != std::string::npos ? std::atoi(text.c_str() + pos + 13) : 0;
    }

    // cgf "SkyrimBridge.ScriptReport"  — live Papyrus VM monitor: overstress
    // flag, function-message queue depth (the script-lag indicator), running/
    // latent/frozen stack counts, which script classes are executing right
    // now, and the per-class instance census. Read-only. Runs on the NEXT
    // frame (a Papyrus native must not take the VM's own locks); full text
    // lands in SkyrimBridge/dumps/scriptreport.txt plus a log summary.
    static bool ScriptReportNative(RE::StaticFunctionTag*)
    {
        ScriptReport::Request();
        SKSE::log::info("ScriptReport: queued; dumps/scriptreport.txt next frame");
        return true;
    }

    // cgf "SkyrimBridge.FormChain" 0x10A241  — the form's plugin override
    // chain, oldest first, winner last: "which mod won this record",
    // scriptable and for ANY form (the console UI tools only answer for a
    // selected reference).
    static RE::BSFixedString FormChain(RE::StaticFunctionTag*, std::int32_t a_formID)
    {
        auto text = Reflect::SourceChain(static_cast<RE::FormID>(a_formID));
        if (text.empty()) {
            SKSE::log::warn("FormChain: no form 0x{:08X}", static_cast<std::uint32_t>(a_formID));
            return "";
        }
        SKSE::log::info("FormChain: {}", text);
        return text.c_str();
    }

    // cgf "SkyrimBridge.TextureInfo" "textures/foo.dds"  — header-only
    // texture inspection: container, format, dimensions, mips. The first
    // question of every texture-pipeline debugging session.
    static RE::BSFixedString TextureInfo(RE::StaticFunctionTag*, RE::BSFixedString a_path)
    {
        auto text = TexCodec::Describe(a_path.c_str());
        if (text.empty()) {
            SKSE::log::warn("TextureInfo: unreadable or unknown format: {}", a_path.c_str());
            return "";
        }
        SKSE::log::info("TextureInfo: {}: {}", a_path.c_str(), text);
        return text.c_str();
    }

    // a_collisionPieces: 0/1 = single convex hull, >=2 = decomposed
    // bhkListShape (concave-approximating). a_material: SkyrimHavokMaterial
    // name (snow/stone/wood/ice/...; empty = wood default) driving footstep
    // and impact feel. Walk-testing is the oracle.
    static bool ConvertModelEx(RE::StaticFunctionTag*, RE::BSFixedString a_in, RE::BSFixedString a_out,
                               bool a_tree, bool a_collision, std::int32_t a_collisionPieces,
                               RE::BSFixedString a_material)
    {
        const int pieces = a_collisionPieces < 1 ? 1 : (a_collisionPieces > 32 ? 32 : a_collisionPieces);
        const std::uint32_t mat = a_material.empty() ? 0u : CollisionMaterial::ByName(a_material.c_str());
        bool ok = ModelCodec::ConvertToNIF(a_in.c_str(), a_out.c_str(), a_tree, a_collision, pieces, mat);
        SKSE::log::info("ModelCodec: {} -> {} [{}{}{}] : {}", a_in.c_str(), a_out.c_str(),
                        a_tree ? "tree" : "static",
                        a_collision ? (pieces >= 2 ? "+collision x" + std::to_string(pieces) : "+collision")
                                    : "",
                        (a_collision && !a_material.empty()) ? " " + std::string(a_material.c_str()) : "",
                        ok ? "ok" : "failed");
        return ok;
    }

    // cgf "SkyrimBridge.MaterialHash" "snow"  — resolve a SkyrimHavokMaterial
    // name to its hash (introspection; 0 = unknown name).
    static std::int32_t MaterialHash(RE::StaticFunctionTag*, RE::BSFixedString a_name)
    {
        return static_cast<std::int32_t>(CollisionMaterial::ByName(a_name.c_str()));
    }

    // cgf "SkyrimBridge.ConvertModelMeshCollision" "in.obj" "out.nif" "stone"
    // Exact concave (mesh) collision: emits a bhkCompressedMeshShape chain
    // with an EMPTY MOPP placeholder. FILE OUTPUT ONLY, and NOT game-ready:
    // open out.nif in NifSkope and run "Update MOPP Code" (Havok tool) to
    // generate the MOPP before using it in-game. Never spawned live.
    static bool ConvertModelMeshCollision(RE::StaticFunctionTag*, RE::BSFixedString a_in,
                                          RE::BSFixedString a_out, RE::BSFixedString a_material)
    {
        const std::uint32_t mat = a_material.empty() ? 0u : CollisionMaterial::ByName(a_material.c_str());
        bool ok = ModelCodec::ConvertToNIF(a_in.c_str(), a_out.c_str(), false, true, 1, mat, true);
        SKSE::log::info("ModelCodec: {} -> {} [meshCollision {}] : {} (finalize MOPP in NifSkope)",
                        a_in.c_str(), a_out.c_str(),
                        a_material.empty() ? "wood" : a_material.c_str(), ok ? "ok" : "failed");
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
        std::string err;
        return static_cast<std::int32_t>(ModelSpawn::SpawnAtPlayer(a_in.c_str(), err));
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

    // cgf "SkyrimBridge.ConvertTextureFoliage" "in.png" "out.dds" "BC3" 128
    // Alpha-coverage-preserving mipmaps for alpha-tested foliage: plain
    // box-filter mips dilute alpha every level, so leaves and grass thin out
    // and vanish at distance. Here each generated mip's alpha is rescaled so
    // the fraction of texels passing the alpha test stays at the top level's
    // coverage. threshold <= 0 uses 128 (the usual NiAlphaProperty cutoff).
    static bool ConvertTextureFoliage(RE::StaticFunctionTag*, RE::BSFixedString a_in,
                                      RE::BSFixedString a_out, RE::BSFixedString a_fmt,
                                      std::int32_t a_threshold)
    {
        auto fmt = TexCodec::DDSFormat::BC3;
        if (_stricmp(a_fmt.c_str(), "BC7") == 0)        fmt = TexCodec::DDSFormat::BC7;
        else if (_stricmp(a_fmt.c_str(), "RGBA8") == 0) fmt = TexCodec::DDSFormat::RGBA8;
        else if (_stricmp(a_fmt.c_str(), "BC3") != 0) {
            SKSE::log::warn("TextureCodec: foliage format '{}' (BC3/BC7/RGBA8; BC1 carries no alpha)",
                            a_fmt.c_str());
            return false;
        }
        int t = a_threshold <= 0 ? 128 : (a_threshold > 255 ? 255 : a_threshold);
        bool ok = TexCodec::Convert(a_in.c_str(), a_out.c_str(), fmt, true, t);
        SKSE::log::info("TextureCodec: {} -> {} [{} coverage@{}] : {}",
                        a_in.c_str(), a_out.c_str(), a_fmt.c_str(), t, ok ? "ok" : "failed");
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

        vm->RegisterFunction("ConvertTexture",         "SkyrimBridge", ConvertTexture);
        vm->RegisterFunction("ConvertTextureFmt",      "SkyrimBridge", ConvertTextureFmt);
        vm->RegisterFunction("ConvertTextureFoliage",  "SkyrimBridge", ConvertTextureFoliage);
        vm->RegisterFunction("TextureScanNow",         "SkyrimBridge", TextureScanNow);
        vm->RegisterFunction("ConvertModel",           "SkyrimBridge", ConvertModel);
        vm->RegisterFunction("ConvertModelTree",       "SkyrimBridge", ConvertModelTree);
        vm->RegisterFunction("ConvertModelEx",         "SkyrimBridge", ConvertModelEx);
        vm->RegisterFunction("SpawnModel",             "SkyrimBridge", SpawnModel);
        vm->RegisterFunction("CellReport",             "SkyrimBridge", CellReportNative);
        vm->RegisterFunction("ScriptReport",           "SkyrimBridge", ScriptReportNative);
        vm->RegisterFunction("FormChain",              "SkyrimBridge", FormChain);
        vm->RegisterFunction("TextureInfo",            "SkyrimBridge", TextureInfo);
        vm->RegisterFunction("MaterialHash",           "SkyrimBridge", MaterialHash);
        vm->RegisterFunction("ConvertModelMeshCollision", "SkyrimBridge", ConvertModelMeshCollision);

        SKSE::log::info("PapyrusBridge: registered 41 native functions under 'SkyrimBridge'");
        return true;
    }
}
