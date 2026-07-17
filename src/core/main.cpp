//=============================================================================
//  main.cpp — SkyrimBridge standalone entry point
//
//  SkyrimBridge measures live engine state through SKSE/CommonLibSSE-NG and
//  publishes it three ways each frame:
//    1. ENB shader parameters (SB_* float4s, via the ENB SDK)
//    2. A shared-memory channel for external tools
//    3. Papyrus native functions for mod authors
//
//  The frame driver is ENB's BeginFrame callback. Without ENB present the
//  plugin logs that fact and stays idle: SkyrimBridge is an ENB bridge, and
//  every published value defaults to zero when the plugin is absent or idle,
//  as documented in docs/parameters.md.
//=============================================================================

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <ShlObj.h>

#include "BridgeData.h"
#include "ENBInterface.h"
#include "CompatDetect.h"
#include "PapyrusBridge.h"
#include "WriteBackProcessor.h"
#include "WeatherEditor.h"
#include "EditorIDCache.h"
#include "KreateProfile.h"
#include "SBConfig.h"
#include "WorldspaceWeatherlist.h"
#include "SkyLighting.h"
#include "EnbLightInventoryFix.h"
#include "EngineReflect.h"
#include "EngineFixes.h"
#include "TextureAutoConvert.h"
#include "TextureLoadHook.h"
#include "BridgeCommand.h"
#include "ScriptReport.h"

// GPU tier
#include "D3D11Hook.h"
#include "ComputeManager.h"
#include "SRVInjector.h"
#include "RenderPassManager.h"
#include "RenderPipeline.h"
#include "HiZPyramid.h"
#include "PhaseDispatcher.h"
#include "AtmosphereRenderer.h"
#include "VolumetricClouds.h"
#include "SceneData.h"
#include "BootDiagnostics.h"
#include "GPUProfiler.h"

#include <thread>
#include <fstream>

// Individual tracker headers (Update() declarations)
#include "../Trackers.h"

// v3 expansion trackers (not in the aggregator header)
#include "RegionTracker.h"
#include "AudioTracker.h"
#include "NPCDetectTracker.h"

// Phase 2: weather parameter computation
#include "../WeatherParameterComputer.h"
// Phase 3: shared-memory channel
#include "../SharedMemoryBridge.h"
// Phase 4: enbParmLink compatibility
#include "../ParmLinkCompat.h"

// ── Game readiness ───────────────────────────────────────────────────────────
// ENB callbacks can fire before game singletons exist. Atomic because
// kDataLoaded fires on the main thread while the ENB callback may fire on
// the render thread.
static std::atomic<bool> s_gameReady{false};
static uint32_t s_frameCount = 0;

// When ENB's BeginFrame callback drives the frame update, the present-hook
// path must not run it a second time.
static std::atomic<bool> s_enbDrivesUpdates{false};

// ── GPU tier configuration (config/GPU.ini next to the plugin) ──────────────
struct GPUConfig
{
    bool enableGPUTier = true;        // master switch for the D3D11 hook
    bool enableAtmosphere = true;     // physically-based sky LUTs + celestials
    bool enableVolumetricClouds = false;  // heavy shader compile; off by default
};
static GPUConfig s_gpuConfig;

static void LoadGPUConfig(const std::filesystem::path& configDir)
{
    std::ifstream in(configDir / "GPU.ini");
    if (!in) return;  // defaults stand
    std::string line;
    auto truthy = [](const std::string& v) {
        return v == "1" || v == "true" || v == "True" || v == "TRUE";
    };
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t\r") + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t\r") + 1);
        if (key == "EnableGPUTier")           s_gpuConfig.enableGPUTier = truthy(val);
        else if (key == "EnableAtmosphere")   s_gpuConfig.enableAtmosphere = truthy(val);
        else if (key == "EnableVolumetricClouds") s_gpuConfig.enableVolumetricClouds = truthy(val);
    }
}

// ── Native replacement suite (config/SkyrimBridge.ini [Native]) ─────────────
// Toggles for the components that replace the third-party ENB plugins. The
// weatherlist router and Sky model default on; the AE-only inventory-light
// fix defaults off (validate in-game before dropping the third-party DLL).
struct NativeConfig
{
    bool weatherRouting = true;
    bool sky = true;
    bool enbLightInventoryFix = false;
    bool engineFixes = true;    // recovered AE spin-lock patch; validated before write
    bool textureAutoConvert = false;   // startup foreign-texture transcode (opt-in)
    bool textureLoadHook = false;      // in-flight foreign-texture substitution (opt-in)
    bool commandSurface = false;       // external command channel (opt-in)
};
static NativeConfig s_native;

static void LoadNativeConfig(const std::filesystem::path& configDir)
{
    bool found = false;
    auto doc = SB::Cfg::ParseFile(configDir / "SkyrimBridge.ini", &found);
    if (!found) return;
    if (auto* s = doc.Find("Native")) {
        s_native.weatherRouting       = s->Bool("WeatherRouting", s_native.weatherRouting);
        s_native.sky                  = s->Bool("Sky", s_native.sky);
        s_native.enbLightInventoryFix = s->Bool("EnbLightInventoryFix", s_native.enbLightInventoryFix);
        s_native.engineFixes          = s->Bool("EngineFixes", s_native.engineFixes);
        s_native.textureAutoConvert   = s->Bool("TextureAutoConvert", s_native.textureAutoConvert);
        s_native.textureLoadHook      = s->Bool("TextureLoadHook", s_native.textureLoadHook);
        s_native.commandSurface       = s->Bool("CommandSurface", s_native.commandSurface);
    }
}

// ── NaN/Inf sanitization ─────────────────────────────────────────────────────
// Prevents corrupt floats from propagating to ENB shaders or shared memory.
static void SanitizeAllData(SB::AllData& data)
{
    auto* raw = reinterpret_cast<char*>(&data);
    for (std::size_t i = 0; i < SB::kParamCount; ++i) {
        auto& v = *reinterpret_cast<SB::Float4*>(raw + SB::kParamTable[i].offset);
        if (!std::isfinite(v.x)) v.x = 0.0f;
        if (!std::isfinite(v.y)) v.y = 0.0f;
        if (!std::isfinite(v.z)) v.z = 0.0f;
        if (!std::isfinite(v.w)) v.w = 0.0f;
    }
}

// ── Self-healing tracker health ──────────────────────────────────────────────
// Auto-disables a tracker after repeated exceptions, retries periodically.
struct TrackerHealth
{
    int  consecutiveErrors = 0;
    int  totalErrors       = 0;
    bool disabled          = false;
    uint32_t disabledAtFrame = 0;

    static constexpr int      kDisableThreshold = 5;
    static constexpr uint32_t kRetryInterval    = 300;  // ~5 s at 60 fps

    bool ShouldRun(uint32_t frame) const {
        if (!disabled) return true;
        return (frame - disabledAtFrame) >= kRetryInterval;
    }

    void OnSuccess(const char* label) {
        if (disabled) {
            disabled = false;
            SKSE::log::info("SkyrimBridge: {} recovered after {} total errors", label, totalErrors);
        }
        consecutiveErrors = 0;
    }

    void OnError(uint32_t frame, const char* label) {
        consecutiveErrors++;
        totalErrors++;
        if (consecutiveErrors >= kDisableThreshold && !disabled) {
            disabled = true;
            disabledAtFrame = frame;
            SKSE::log::warn("SkyrimBridge: {} auto-disabled after {} consecutive errors "
                "(retry in {} frames)", label, kDisableThreshold, kRetryInterval);
        }
    }
};

enum TrackerID : int
{
    kTrkCelestial, kTrkAtmosphere, kTrkFog, kTrkWeather,
    kTrkPlayer, kTrkCamera, kTrkInterior, kTrkShadow,
    kTrkEffects, kTrkRender,
    kTrkImageSpace, kTrkLights, kTrkActorValues, kTrkCrosshair,
    kTrkEquipment, kTrkQuest, kTrkUIState,
    kTrkRegion, kTrkAudio, kTrkNPCDetect,
    kTrkCount
};

static TrackerHealth s_trackerHealth[kTrkCount];

// ── Delta time ───────────────────────────────────────────────────────────────
static float GetDeltaTime()
{
    static std::chrono::steady_clock::time_point s_lastFrame;
    static bool s_hasLastFrame = false;

    auto now = std::chrono::steady_clock::now();
    float dt = 1.0f / 60.0f;

    if (s_hasLastFrame) {
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - s_lastFrame);
        dt = elapsed.count() / 1'000'000.0f;
        if (dt < 0.0001f) dt = 0.0001f;
        if (dt > 0.5f)    dt = 0.5f;
    }

    s_lastFrame = now;
    s_hasLastFrame = true;
    return dt;
}

// ── Frame update ─────────────────────────────────────────────────────────────
static void DoFrameUpdate()
{
    float dt = GetDeltaTime();

    // During loading screens singletons are valid but cell-dependent data
    // (lights, NPCs, regions, crosshair) may be transitional. Skip those.
    bool isLoading = false;
    if (auto* ui = RE::UI::GetSingleton())
        isLoading = ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);

    // Collect into the shared global so Phase 2-4 readers see this frame.
    SB::AllData& data = SB::GetMutableData();

    #define SB_SAFE_UPDATE(field, expr, id, label)                              \
        if (s_trackerHealth[id].ShouldRun(s_frameCount)) {                      \
            try {                                                               \
                field = expr;                                                   \
                s_trackerHealth[id].OnSuccess(label);                           \
            }                                                                   \
            catch (const std::exception& e) {                                   \
                s_trackerHealth[id].OnError(s_frameCount, label);               \
                SKSE::log::error("SkyrimBridge: {} threw: {}", label, e.what());\
            }                                                                   \
            catch (...) {                                                       \
                s_trackerHealth[id].OnError(s_frameCount, label);               \
                SKSE::log::error("SkyrimBridge: {} threw unknown", label);      \
            }                                                                   \
        }

    // Core trackers (domains 1-10)
    SB_SAFE_UPDATE(data.celestial,  SB::CelestialTracker::Update(),   kTrkCelestial,  "CelestialTracker");
    SB_SAFE_UPDATE(data.atmosphere, SB::AtmosphereTracker::Update(),  kTrkAtmosphere, "AtmosphereTracker");
    SB_SAFE_UPDATE(data.fog,        SB::FogTracker::Update(),         kTrkFog,        "FogTracker");
    SB_SAFE_UPDATE(data.weather,    SB::WeatherTracker::Update(dt),   kTrkWeather,    "WeatherTracker");
    SB_SAFE_UPDATE(data.player,     SB::PlayerTracker::Update(),      kTrkPlayer,     "PlayerTracker");
    SB_SAFE_UPDATE(data.camera,     SB::CameraTracker::Update(),      kTrkCamera,     "CameraTracker");
    SB_SAFE_UPDATE(data.interior,   SB::InteriorTracker::Update(),    kTrkInterior,   "InteriorTracker");
    SB_SAFE_UPDATE(data.shadow,     SB::ShadowTracker::Update(),      kTrkShadow,     "ShadowTracker");
    SB_SAFE_UPDATE(data.effects,    SB::EffectsTracker::Update(),     kTrkEffects,    "EffectsTracker");
    SB_SAFE_UPDATE(data.render,     SB::RenderTracker::Update(dt),    kTrkRender,     "RenderTracker");

    // v2 expansion trackers (domains 11-17)
    SB_SAFE_UPDATE(data.imageSpace, SB::ImageSpaceTracker::Update(),  kTrkImageSpace, "ImageSpaceTracker");
    SB_SAFE_UPDATE(data.uiState,    SB::UIStateTracker::Update(),     kTrkUIState,    "UIStateTracker");
    SB_SAFE_UPDATE(data.equipment,  SB::EquipmentTracker::Update(),   kTrkEquipment,  "EquipmentTracker");

    if (!isLoading) {
        SB_SAFE_UPDATE(data.lights,      SB::LightTracker::Update(),      kTrkLights,      "LightTracker");
        SB_SAFE_UPDATE(data.actorValues, SB::ActorValueTracker::Update(), kTrkActorValues, "ActorValueTracker");
        SB_SAFE_UPDATE(data.crosshair,   SB::CrosshairTracker::Update(),  kTrkCrosshair,   "CrosshairTracker");
        SB_SAFE_UPDATE(data.quest,       SB::QuestTracker::Update(),      kTrkQuest,       "QuestTracker");
        SB_SAFE_UPDATE(data.region,      SB::RegionTracker::Update(),     kTrkRegion,      "RegionTracker");
        SB_SAFE_UPDATE(data.npcDetect,   SB::NPCDetectTracker::Update(),  kTrkNPCDetect,   "NPCDetectTracker");
    }

    // v3 trackers (domains 19-22, minus GPU-bound perf/scene)
    SB_SAFE_UPDATE(data.audio,      SB::AudioTracker::Update(),       kTrkAudio,      "AudioTracker");

    #undef SB_SAFE_UPDATE

    // Runtime actuation: INI-driven write-back of game state (FOV, fog,
    // sky lights, time) from measured data, fixed values, or live ENB
    // parameters. Rules ship disabled; the user opts in per rule.
    try {
        SB::WriteBackProcessor::Get().Execute(data);
    } catch (...) {
        SKSE::log::error("SkyrimBridge: WriteBackProcessor threw");
    }

    SanitizeAllData(data);

    // GPU tier: rebuild scene matrices for compute consumers, refresh the
    // atmosphere LUTs when the sun has moved.
    SB::SceneMatrices::Get().Update(data);
    if (SB::AtmosphereRenderer::Get().IsInitialized()) {
        float sunZenithCos = data.celestial.SunDirection.y;
        float sunAzimuth = std::atan2(data.celestial.SunDirection.x,
                                      data.celestial.SunDirection.z);
        SB::AtmosphereRenderer::Get().UpdateLUTs(sunZenithCos, sunAzimuth);
    }

    // 1. ENB shader parameters (dirty-tracked push of the whole table)
    ENBInterface::PushAllData(data);

    // 2. Phase 2: weather parameter computation and push
    try {
        SB::WeatherParameterComputer::Get().Update(dt);
    } catch (...) {
        SKSE::log::error("SkyrimBridge: WeatherParameterComputer threw");
    }

    // 3. Phase 3: shared-memory channel for external tools
    try {
        if (SB::SharedMemoryBridge::Get().IsActive())
            SB::SharedMemoryBridge::Get().WriteFrame(data, dt, s_frameCount);
    } catch (...) {
        SKSE::log::error("SkyrimBridge: SharedMemoryBridge::WriteFrame threw");
    }

    // 3b. External command channel: dispatch one pending request per frame.
    if (SB::BridgeCommand::Get().IsActive())
        SB::BridgeCommand::Get().Poll();

    // 3c. Queued Papyrus-VM reports run here: the frame thread may take the
    // VM's locks briefly; a Papyrus native must not (see ScriptReport.h).
    SB::ScriptReport::Tick();

    // 4. Phase 4: enbParmLink-compatible expression evaluation
    try {
        SB::ParmLinkCompat::Get().Update(dt);
    } catch (...) {
        SKSE::log::error("SkyrimBridge: ParmLinkCompat threw");
    }

    // 5. Papyrus data cache for script consumers
    try {
        SB::PapyrusBridge::UpdateCache(data);
    } catch (...) {
        SKSE::log::error("SkyrimBridge: PapyrusBridge::UpdateCache threw");
    }

    // 6. Weather workshop: weather-change capture, preset auto-load and
    //    hot-reload, auto-apply of live edits.
    try {
        SB::WeatherEditor::Get().Update();
    } catch (...) {
        SKSE::log::error("SkyrimBridge: WeatherEditor threw");
    }

    // 7. Native plugin replacements: per-worldspace ENB weatherlist routing
    //    and the celestial lighting model.
    try {
        SB::WorldspaceWeatherlist::Get().Update();
    } catch (...) {
        SKSE::log::error("SkyrimBridge: WorldspaceWeatherlist threw");
    }
    try {
        SB::SkyLighting::Get().Update();
    } catch (...) {
        SKSE::log::error("SkyrimBridge: SkyLighting threw");
    }

    ++s_frameCount;
}

// ── SEH-safe wrapper ─────────────────────────────────────────────────────────
// Access violations from engine reads at the main menu are SEH exceptions,
// not C++ exceptions. Catch them here and disable the update loop after
// repeated faults; kNewGame/kPostLoadGame re-enables it.
static int  s_frameAVCount = 0;
static bool s_frameUpdateDisabled = false;

static void RunFrameUpdate()
{
    if (!s_gameReady.load(std::memory_order_acquire))
        return;
    if (s_frameUpdateDisabled)
        return;

    __try {
        DoFrameUpdate();
        s_frameAVCount = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        s_frameAVCount++;
        SKSE::log::error("SkyrimBridge: frame update access violation #{}", s_frameAVCount);
        if (s_frameAVCount >= 5) {
            s_frameUpdateDisabled = true;
            SKSE::log::error("SkyrimBridge: too many access violations, frame updates disabled "
                "until the game world loads");
        }
    }
}

// ── Present-hook entry (called by D3D11Hook each frame) ─────────────────────
// When ENB's callback already drives updates, this is a no-op so the frame
// is never measured twice.
void RunStandaloneFrameUpdate()
{
    if (s_enbDrivesUpdates.load(std::memory_order_relaxed))
        return;
    RunFrameUpdate();
}

// ── ENB callback ─────────────────────────────────────────────────────────────
// BeginFrame fires right after present, at the start of the next frame's CPU
// work: parameters pushed here are consumed by the frame being built, and the
// measurement work stays off the present critical path.
static void __stdcall OnENBCallback(int a_callbackType)
{
    switch (static_cast<ENBInterface::CallbackType>(a_callbackType)) {
    case ENBInterface::CallbackType::BeginFrame:
        RunFrameUpdate();
        break;

    case ENBInterface::CallbackType::OnExit:
        SB::ParmLinkCompat::Get().Shutdown();
        SB::SharedMemoryBridge::Get().Shutdown();
        SB::BridgeCommand::Get().Shutdown();
        SB::WeatherParameterComputer::Get().Shutdown();
        SKSE::log::info("SkyrimBridge: shut down on ENB exit");
        break;

    default:
        break;
    }
}

// ── SKSE messaging ───────────────────────────────────────────────────────────
static void OnMessage(SKSE::MessagingInterface::Message* a_msg)
{
    switch (a_msg->type) {
    case SKSE::MessagingInterface::kPostLoad:
    {
        // Editor-ID cache hooks must install before data loading begins so
        // the weather workshop can name forms (presets are keyed by EditorID).
        // This natively replaces the third-party NativeEditorID fix.
        SB::EditorIDCache::Get().Install();

        // Native ENB Light Inventory Fix installs its engine hooks here,
        // before any menu can render a 3D item preview. Opt-in + AE-only.
        LoadNativeConfig(std::filesystem::path("Data/SKSE/Plugins/SkyrimBridge"));
        if (s_native.enbLightInventoryFix)
            SB::EnbLightInventoryFix::Get().Install();

        // In-flight foreign-texture substitution installs before any texture
        // can load. Redirect-only detours on LooseFileLocation; opt-in.
        if (s_native.textureLoadHook)
            SB::TextureLoadHook::Get().Install(
                std::filesystem::path("Data/SKSE/Plugins/SkyrimBridge"));
        break;
    }

    case SKSE::MessagingInterface::kPostPostLoad:
        // ENB (d3d11.dll) is loaded by now if it is present at all.
        if (ENBInterface::Init()) {
            ENBInterface::SetCallbackFunction(OnENBCallback);
            s_enbDrivesUpdates.store(true, std::memory_order_relaxed);
            SKSE::log::info("SkyrimBridge: ENB callback registered (ENB drives updates)");
        } else {
            SKSE::log::info("SkyrimBridge: ENBSeries not detected; the present "
                "hook drives updates and parameters publish to shared memory");
        }
        break;

    case SKSE::MessagingInterface::kDataLoaded:
    {
        SKSE::log::info("SkyrimBridge: game data loaded, initializing");

        SB::CompatDetect::Get().Detect();

        if (SB::SharedMemoryBridge::Get().Initialize())
            SKSE::log::info("SkyrimBridge: shared-memory channel active");

        if (s_native.commandSurface && SB::BridgeCommand::Get().Initialize())
            SKSE::log::info("SkyrimBridge: external command channel active");

        auto configDir = std::filesystem::path("Data/SKSE/Plugins/SkyrimBridge");
        SB::WeatherParameterComputer::Get().Initialize(configDir);

        SB::WriteBackProcessor::Get().LoadConfig(configDir);
        SKSE::log::info("SkyrimBridge: write-back rules loaded ({} rules, {} enabled)",
            SB::WriteBackProcessor::Get().GetRuleCount(),
            SB::WriteBackProcessor::Get().GetEnabledRuleCount());

        std::error_code ec;
        auto gameDir = std::filesystem::current_path(ec);
        if (!ec)
            SB::ParmLinkCompat::Get().Initialize(gameDir);
        else
            SKSE::log::error("SkyrimBridge: current_path failed: {}", ec.message());

        SB::PapyrusBridge::Register();

        // Weather workshop: live weather-record editing with per-weather
        // preset auto-load and hot-reload.
        SB::WeatherEditor::Get().SetPresetDir(configDir / "WeatherPresets");
        SKSE::log::info("SkyrimBridge: weather workshop ready "
            "(presets in SkyrimBridge/WeatherPresets, hot-reload live)");

        // KreatE image-space overlay loader: applies the per-FormID image
        // space grading that ships in a KreatE profile. Root is configurable;
        // the default matches the Elder ENB layout.
        {
            std::filesystem::path kreateRoot("KreatE/Presets");
            std::ifstream kcfg(configDir / "KreateRoot.txt");
            if (kcfg) {
                std::string line;
                if (std::getline(kcfg, line) && !line.empty())
                    kreateRoot = std::filesystem::path(line);
            }
            SB::KreateProfile::Get().SetProfileRoot(kreateRoot);
            auto profiles = SB::KreateProfile::Get().ListProfiles();
            SKSE::log::info("SkyrimBridge: KreatE loader ready — {} profiles under '{}'",
                profiles.size(), kreateRoot.string());
        }

        // ── Native ENB plugin replacements ───────────────────────────────
        // Per-worldspace ENB weatherlist routing (enbseries/ lives next to the
        // game exe) and the celestial lighting model. Both read SkyrimBridge's
        // own flat-INI configs, not the third-party plugins' formats.
        if (s_native.weatherRouting) {
            std::error_code wec;
            auto root = std::filesystem::current_path(wec);
            if (!wec)
                SB::WorldspaceWeatherlist::Get().Initialize(root / "enbseries");
        }
        if (s_native.sky)
            SB::SkyLighting::Get().Initialize(configDir);

        // EngineReflect: register the record schemas (read/write/translate/verify
        // spine). Built on CommonLibSSE-NG RE:: layouts, the reversal substrate.
        SB::Reflect::RegisterBuiltins();

        // EngineFixes: recovered binary patches, applied to the live (decrypted)
        // engine via the Address Library. Validated before each write.
        if (s_native.engineFixes)
            SB::EngineFixes::Get().Install();

        // Foreign-texture transcode pass: background scan of the texture tree
        // for .png/.tga/.bmp without a .dds sibling. Opt-in.
        if (s_native.textureAutoConvert)
            SB::TextureAutoConvert::Get().Initialize(configDir);

        // ── GPU tier ─────────────────────────────────────────────────────
        LoadGPUConfig(configDir);
        if (s_gpuConfig.enableGPUTier) {
            SB::BootDiag::Init();
            if (D3D11Hook::Init()) {
                auto* dev = D3D11Hook::GetDevice();
                auto* ctx = D3D11Hook::GetContext();
                auto* sc  = D3D11Hook::GetSwapChain();

                if (dev && ctx && sc) {
                    SB::ComputeManager::Get().Initialize(dev, ctx);
                    SB::SRVInjector::Get().Initialize(ctx);
                    SB::RenderPassManager::Get().Initialize(dev, ctx);
                    SB::RenderPipeline::Get().Initialize(dev, ctx, sc);

                    if (SB::GPUProfiler::Get().Initialize(dev, ctx))
                        SKSE::log::info("SkyrimBridge: GPU profiler ready (F11)");

                    // Hi-Z depth pyramid: builds at PostGeometry:1 (proxy mode
                    // only; without depth access the pass simply never runs).
                    if (SB::HiZPyramid::Get().Initialize(dev, sc)) {
                        SB::SRVInjector::Get().RegisterSRV(
                            SB::HiZPyramid::kSRVSlot, SB::HiZPyramid::Get().GetSRV());
                        SB::RenderPipeline::Get().AddPass({
                            .name     = "HiZPyramid",
                            .stage    = SB::PipelineStage::PostGeometry,
                            .priority = 1,
                            .enabled  = true,
                            .execute  = [](SB::PassContext& pctx) {
                                auto& hiz = SB::HiZPyramid::Get();
                                if (hiz.IsInitialized() && hiz.IsEnabled())
                                    hiz.BuildPyramid(pctx.context);
                            },
                        });
                    }

                    // Mid-frame dispatch (fires only in proxy mode).
                    SB::PhaseDispatcher::Get().Initialize(
                        ctx, D3D11Hook::GetInvalidateCacheFn());

                    if (s_gpuConfig.enableAtmosphere) {
                        if (SB::AtmosphereRenderer::Get().Initialize(dev, ctx, sc))
                            SKSE::log::info("SkyrimBridge: atmosphere renderer active");
                    }

                    if (s_gpuConfig.enableVolumetricClouds) {
                        // Heavy shader compilation: run off the main thread so
                        // kDataLoaded is never blocked. Pass registration is
                        // mutex-guarded; noise generation stays lazy on the
                        // render thread.
                        std::thread([dev, ctx, sc]() {
                            SKSE::log::info("SkyrimBridge: compiling volumetric "
                                "cloud shaders in the background");
                            bool ok = SB::VolumetricClouds::Get().Initialize(dev, ctx, sc);
                            SKSE::log::info("SkyrimBridge: volumetric clouds {}",
                                ok ? "active" : "failed to initialize");
                        }).detach();
                    }

                    SKSE::log::info("SkyrimBridge: GPU tier initialized ({} mode)",
                        D3D11Hook::IsProxyActive() ? "proxy" : "legacy");
                }
            } else {
                SKSE::log::warn("SkyrimBridge: GPU tier unavailable (hook failed)");
            }
        }

        s_gameReady.store(true, std::memory_order_release);
        SKSE::log::info("SkyrimBridge: ready ({} parameters defined)", SB::kParamCount);
        break;
    }

    case SKSE::MessagingInterface::kNewGame:
    case SKSE::MessagingInterface::kPostLoadGame:
        // The game world exists now; recover from main-menu faults.
        if (s_frameUpdateDisabled) {
            s_frameUpdateDisabled = false;
            s_frameAVCount = 0;
            SKSE::log::info("SkyrimBridge: game world loaded, frame updates re-enabled");
        }
        break;

    default:
        break;
    }
}

// ── Logging ──────────────────────────────────────────────────────────────────
static void SetupLog()
{
    std::filesystem::path logPath;

    PWSTR documentsPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documentsPath))) {
        logPath = std::filesystem::path(documentsPath) /
            "My Games" / "Skyrim Special Edition" / "SKSE" / "SkyrimBridge.log";
        CoTaskMemFree(documentsPath);
    }

    if (logPath.empty())
        return;

    std::error_code ec;
    std::filesystem::create_directories(logPath.parent_path(), ec);

    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
    auto logger = std::make_shared<spdlog::logger>("SkyrimBridge", std::move(sink));
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(std::move(logger));
}

// ── Plugin entry ─────────────────────────────────────────────────────────────
SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    SetupLog();

    SKSE::log::info("SkyrimBridge loaded ({} parameters defined)", SB::kParamCount);

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE::log::critical("SkyrimBridge: no SKSE messaging interface");
        return false;
    }
    messaging->RegisterListener(OnMessage);

    return true;
}
