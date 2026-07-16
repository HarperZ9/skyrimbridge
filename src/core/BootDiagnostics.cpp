#include "BootDiagnostics.h"
#include <SKSE/SKSE.h>
#include <cstring>
#include <cstdio>

namespace SB
{

// Static storage
BootDiag::Event BootDiag::s_events[kMaxEvents] = {};
uint32_t BootDiag::s_eventCount   = 0;
uint32_t BootDiag::s_currentFrame = 0;
bool     BootDiag::s_active       = false;
bool     BootDiag::s_dumped       = false;
std::chrono::steady_clock::time_point BootDiag::s_startTime;


void BootDiag::Init()
{
    s_startTime  = std::chrono::steady_clock::now();
    s_eventCount = 0;
    s_currentFrame = 0;
    s_active     = true;
    s_dumped     = false;
    SKSE::log::info("BootDiag: === BOOT DIAGNOSTICS ACTIVE (tracking {} frames, {} max events) ===",
        kTrackedFrames, kMaxEvents);
}

float BootDiag::ElapsedMs()
{
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<float, std::milli>(now - s_startTime).count();
}

void BootDiag::AddEvent(Category cat, bool success, const char* source, const char* detail)
{
    if (!s_active || s_eventCount >= kMaxEvents) return;

    Event& e = s_events[s_eventCount++];
    e.frame    = s_currentFrame;
    e.timeMs   = ElapsedMs();
    e.category = cat;
    e.success  = success;

    if (source) {
        strncpy(e.source, source, sizeof(e.source) - 1);
        e.source[sizeof(e.source) - 1] = '\0';
    } else {
        e.source[0] = '\0';
    }

    if (detail) {
        strncpy(e.detail, detail, sizeof(e.detail) - 1);
        e.detail[sizeof(e.detail) - 1] = '\0';
    } else {
        e.detail[0] = '\0';
    }
}


// ── Public API ────────────────────────────────────────────────────────────

void BootDiag::LogInit(const char* system, bool success, const char* detail)
{
    AddEvent(Category::Init, success, system, detail);
    // Also log immediately so it appears even if we crash before dump
    if (detail && detail[0])
        SKSE::log::info("BootDiag: INIT {} — {} ({})", system, success ? "OK" : "FAIL", detail);
    else
        SKSE::log::info("BootDiag: INIT {} — {}", system, success ? "OK" : "FAIL");
}

void BootDiag::LogResource(const char* system, const char* resource, bool success,
                            const char* detail)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "resource '%s' %s%s%s",
        resource, success ? "OK" : "FAIL",
        (detail && detail[0]) ? " — " : "",
        (detail && detail[0]) ? detail : "");
    AddEvent(Category::Resource, success, system, buf);

    if (!success)
        SKSE::log::warn("BootDiag: RESOURCE {} {} — FAIL {}", system, resource,
            detail ? detail : "");
}

void BootDiag::BeginFrame(uint32_t frameIdx)
{
    if (!s_active) return;

    s_currentFrame = frameIdx;

    if (frameIdx < kTrackedFrames) {
        char buf[64];
        snprintf(buf, sizeof(buf), "=== FRAME %u BEGIN ===", frameIdx);
        AddEvent(Category::Frame, true, "FrameUpdate", buf);
    }

    // Auto-dump after tracked frames
    if (frameIdx == kTrackedFrames && !s_dumped) {
        DumpReport();
    }
}

void BootDiag::EndFrame()
{
    if (!s_active || s_currentFrame >= kTrackedFrames) return;

    char buf[64];
    snprintf(buf, sizeof(buf), "=== FRAME %u END ===", s_currentFrame);
    AddEvent(Category::Frame, true, "FrameUpdate", buf);
}

void BootDiag::LogPass(const char* passName, const char* detail)
{
    if (!s_active || s_currentFrame >= kTrackedFrames) return;
    AddEvent(Category::Pass, true, passName, detail);
}

void BootDiag::LogPassBail(const char* passName, const char* reason)
{
    if (!s_active || s_currentFrame >= kTrackedFrames) return;
    AddEvent(Category::Pass, false, passName, reason);
    // Log immediately for visibility
    SKSE::log::info("BootDiag: PASS {} BAIL — {}", passName, reason);
}

void BootDiag::LogTracker(const char* name, bool success, const char* detail)
{
    if (!s_active || s_currentFrame >= kTrackedFrames) return;
    AddEvent(Category::Tracker, success, name, detail);
}

void BootDiag::LogGuard(const char* system, const char* check, bool passed)
{
    if (!s_active || s_currentFrame >= kTrackedFrames) return;
    char buf[128];
    snprintf(buf, sizeof(buf), "guard '%s' %s", check, passed ? "PASSED" : "FAILED");
    AddEvent(Category::Guard, passed, system, buf);
}

void BootDiag::LogInfo(const char* system, const char* detail)
{
    AddEvent(Category::Info, true, system, detail);
}

void BootDiag::LogError(const char* system, const char* detail)
{
    AddEvent(Category::Error, false, system, detail);
    SKSE::log::error("BootDiag: ERROR {} — {}", system, detail);
}


// ── Dump ─────────────────────────────────────────────────────────────────

static const char* CategoryName(BootDiag::Category cat)
{
    switch (cat) {
    case BootDiag::Category::Init:     return "INIT    ";
    case BootDiag::Category::Frame:    return "FRAME   ";
    case BootDiag::Category::Pass:     return "PASS    ";
    case BootDiag::Category::Resource: return "RESOURCE";
    case BootDiag::Category::Guard:    return "GUARD   ";
    case BootDiag::Category::Tracker:  return "TRACKER ";
    case BootDiag::Category::Error:    return "ERROR   ";
    case BootDiag::Category::Info:     return "INFO    ";
    default: return "???     ";
    }
}

void BootDiag::DumpReport()
{
    if (s_dumped) return;
    s_dumped = true;

    SKSE::log::info("╔══════════════════════════════════════════════════════════════════╗");
    SKSE::log::info("║              BOOT DIAGNOSTICS REPORT ({} events)              ║",
        s_eventCount);
    SKSE::log::info("╚══════════════════════════════════════════════════════════════════╝");

    // ── Init summary ──────────────────────────────────────────────────
    uint32_t initOK = 0, initFail = 0;
    SKSE::log::info("");
    SKSE::log::info("─── INITIALIZATION RESULTS ───");
    for (uint32_t i = 0; i < s_eventCount; ++i) {
        auto& e = s_events[i];
        if (e.category == Category::Init) {
            SKSE::log::info("  {} {} {}{}",
                e.success ? "[OK]  " : "[FAIL]",
                e.source,
                e.detail[0] ? " — " : "",
                e.detail);
            if (e.success) initOK++; else initFail++;
        }
    }
    SKSE::log::info("  Total: {} OK, {} FAILED", initOK, initFail);

    // ── Resource failures ──────────────────────────────────────────────
    bool anyResourceFail = false;
    for (uint32_t i = 0; i < s_eventCount; ++i) {
        if (s_events[i].category == Category::Resource && !s_events[i].success) {
            if (!anyResourceFail) {
                SKSE::log::info("");
                SKSE::log::info("─── RESOURCE CREATION FAILURES ───");
                anyResourceFail = true;
            }
            SKSE::log::info("  [FAIL] {} — {}", s_events[i].source, s_events[i].detail);
        }
    }
    if (!anyResourceFail) {
        SKSE::log::info("");
        SKSE::log::info("─── RESOURCE CREATION FAILURES ───  (none)");
    }

    // ── Errors ──────────────────────────────────────────────────────────
    bool anyError = false;
    for (uint32_t i = 0; i < s_eventCount; ++i) {
        if (s_events[i].category == Category::Error) {
            if (!anyError) {
                SKSE::log::info("");
                SKSE::log::info("─── ERRORS ───");
                anyError = true;
            }
            SKSE::log::info("  F{:03} {:8.1f}ms {} — {}",
                s_events[i].frame, s_events[i].timeMs,
                s_events[i].source, s_events[i].detail);
        }
    }

    // ── Per-frame trace ──────────────────────────────────────────────
    SKSE::log::info("");
    SKSE::log::info("─── PER-FRAME TRACE (first {} frames) ───", kTrackedFrames);

    uint32_t lastFrame = UINT32_MAX;
    for (uint32_t i = 0; i < s_eventCount; ++i) {
        auto& e = s_events[i];
        // Skip init events (already shown above) unless they have frame context
        if (e.category == Category::Init && e.frame == 0)
            continue;

        if (e.frame != lastFrame && e.frame < kTrackedFrames) {
            SKSE::log::info("");
            SKSE::log::info("  ──── Frame {} ────", e.frame);
            lastFrame = e.frame;
        }

        if (e.frame < kTrackedFrames) {
            SKSE::log::info("    {:8.1f}ms {} {} {} {}",
                e.timeMs,
                CategoryName(e.category),
                e.success ? "OK  " : "FAIL",
                e.source,
                e.detail);
        }
    }

    // ── Pass execution summary ──────────────────────────────────────
    SKSE::log::info("");
    SKSE::log::info("─── PIPELINE PASS SUMMARY (frames 0-{}) ───", kTrackedFrames - 1);

    // Count executions and bails per pass
    struct PassStats { uint32_t ran = 0; uint32_t bailed = 0; char name[48] = {}; };
    PassStats stats[64];
    uint32_t statCount = 0;

    for (uint32_t i = 0; i < s_eventCount; ++i) {
        auto& e = s_events[i];
        if (e.category != Category::Pass) continue;

        // Find or create stats entry
        uint32_t idx = UINT32_MAX;
        for (uint32_t j = 0; j < statCount; ++j) {
            if (strcmp(stats[j].name, e.source) == 0) { idx = j; break; }
        }
        if (idx == UINT32_MAX && statCount < 64) {
            idx = statCount++;
            strncpy(stats[idx].name, e.source, 47);
        }
        if (idx < 64) {
            if (e.success) stats[idx].ran++;
            else stats[idx].bailed++;
        }
    }

    for (uint32_t i = 0; i < statCount; ++i) {
        SKSE::log::info("  {:30} ran {:3}x, bailed {:3}x",
            stats[i].name, stats[i].ran, stats[i].bailed);
    }

    SKSE::log::info("");
    SKSE::log::info("═══════════════════════════ END BOOT DIAGNOSTICS ═══════════════════════════");

    // Stop tracking to avoid any overhead
    s_active = false;
}

} // namespace SB
