#pragma once
//=============================================================================
//  BootDiagnostics — First-frame trace for rapid issue isolation
//
//  Records every init step, every pipeline pass execution, every bail-out
//  reason, and every resource creation result for the first N frames.
//  Auto-dumps a structured report to the log after frame N.
//
//  Usage:
//    BootDiag::Init();                           // once at plugin load
//    BootDiag::LogInit("GTAORenderer", true);    // init result
//    BootDiag::LogInit("SSR", false, "CompileShaders failed");
//    BootDiag::BeginFrame(frameIdx);             // top of DoFrameUpdate
//    BootDiag::LogPass("GrassLighting", "bail: materialSRV null");
//    BootDiag::LogPass("GTAO", "dispatched 240x135");
//    BootDiag::EndFrame();                       // bottom of DoFrameUpdate
//    BootDiag::DumpReport();                     // manual trigger
//
//  The system automatically dumps after kTrackedFrames frames.
//  All events are stored in a flat array — zero allocations after init.
//=============================================================================

#include <cstdint>
#include <chrono>

namespace SB
{

class BootDiag
{
public:
    static constexpr uint32_t kTrackedFrames = 15;
    static constexpr uint32_t kMaxEvents     = 1024;

    enum class Category : uint8_t {
        Init,       // System initialization (success/fail)
        Frame,      // Frame begin/end markers
        Pass,       // Pipeline pass execution
        Resource,   // D3D11 resource creation
        Guard,      // Guard check (SRV null, format mismatch, etc.)
        Tracker,    // Tracker update result
        Error,      // Exception or crash caught
        Info        // General diagnostic info
    };

    struct Event {
        uint32_t    frame;
        float       timeMs;         // ms since Init()
        Category    category;
        bool        success;        // true = OK, false = fail/bail
        char        source[48];     // system name
        char        detail[128];    // what happened
    };

    static void Init();
    static bool IsActive() { return s_active; }

    // Log an initialization result
    static void LogInit(const char* system, bool success, const char* detail = nullptr);

    // Log a resource creation result (texture, UAV, SRV, CB, shader)
    static void LogResource(const char* system, const char* resource, bool success,
                            const char* detail = nullptr);

    // Frame markers
    static void BeginFrame(uint32_t frameIdx);
    static void EndFrame();

    // Pipeline pass execution trace
    static void LogPass(const char* passName, const char* detail);
    static void LogPassBail(const char* passName, const char* reason);

    // Tracker update result
    static void LogTracker(const char* name, bool success, const char* detail = nullptr);

    // Guard check (why something didn't run)
    static void LogGuard(const char* system, const char* check, bool passed);

    // General info or error
    static void LogInfo(const char* system, const char* detail);
    static void LogError(const char* system, const char* detail);

    // Dump accumulated report to SKSE::log
    static void DumpReport();

    // Query
    static uint32_t GetEventCount() { return s_eventCount; }
    static const Event* GetEvents() { return s_events; }
    static uint32_t GetCurrentFrame() { return s_currentFrame; }
    static bool HasDumped() { return s_dumped; }

private:
    static void AddEvent(Category cat, bool success, const char* source, const char* detail);
    static float ElapsedMs();

    static Event    s_events[kMaxEvents];
    static uint32_t s_eventCount;
    static uint32_t s_currentFrame;
    static bool     s_active;
    static bool     s_dumped;
    static std::chrono::steady_clock::time_point s_startTime;
};

} // namespace SB
