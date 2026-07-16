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

    SanitizeAllData(data);

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
    case SKSE::MessagingInterface::kPostPostLoad:
        // ENB (d3d11.dll) is loaded by now if it is present at all.
        if (ENBInterface::Init()) {
            ENBInterface::SetCallbackFunction(OnENBCallback);
            SKSE::log::info("SkyrimBridge: ENB callback registered");
        } else {
            SKSE::log::warn("SkyrimBridge: ENBSeries not detected, plugin idle "
                "(parameters stay at their zero defaults)");
        }
        break;

    case SKSE::MessagingInterface::kDataLoaded:
    {
        SKSE::log::info("SkyrimBridge: game data loaded, initializing");

        SB::CompatDetect::Get().Detect();

        if (SB::SharedMemoryBridge::Get().Initialize())
            SKSE::log::info("SkyrimBridge: shared-memory channel active");

        auto configDir = std::filesystem::path("Data/SKSE/Plugins/SkyrimBridge");
        SB::WeatherParameterComputer::Get().Initialize(configDir);

        std::error_code ec;
        auto gameDir = std::filesystem::current_path(ec);
        if (!ec)
            SB::ParmLinkCompat::Get().Initialize(gameDir);
        else
            SKSE::log::error("SkyrimBridge: current_path failed: {}", ec.message());

        SB::PapyrusBridge::Register();

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
