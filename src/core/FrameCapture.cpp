#include "FrameCapture.h"
#include <SKSE/SKSE.h>
#include <fstream>
#include <cstdio>
#include <filesystem>

namespace SB
{

// =============================================================================
//  StartCapture / StopCapture
// =============================================================================

void FrameCapture::StartCapture(uint32_t numFrames)
{
    m_frames.clear();
    m_frames.reserve(numFrames);
    m_targetFrames = numFrames;
    m_capturing    = true;
    m_inFrame      = false;
    m_current      = {};

    SKSE::log::info("FrameCapture: started ({} frames)", numFrames);
}

void FrameCapture::StopCapture()
{
    if (!m_capturing) return;
    m_capturing = false;
    m_inFrame   = false;

    SKSE::log::info("FrameCapture: stopped after {} frames", m_frames.size());

    // Auto-dump to log
    DumpToLog();

    // Auto-dump to CSV beside the log
    DumpToCSV("Data/SKSE/plugins/Playground/FrameCapture.csv");
}


// =============================================================================
//  BeginFrame / EndFrame
// =============================================================================

void FrameCapture::BeginFrame(uint32_t frameIndex, float deltaTime)
{
    if (!m_capturing) return;

    m_current = {};
    m_current.frameIndex = frameIndex;
    m_current.deltaTime  = deltaTime;
    m_inFrame = true;
}

void FrameCapture::EndFrame()
{
    if (!m_capturing || !m_inFrame) return;

    m_inFrame = false;
    m_frames.push_back(std::move(m_current));
    m_current = {};

    // Check if we've hit our target
    if (m_frames.size() >= m_targetFrames) {
        SKSE::log::info("FrameCapture: reached target {} frames", m_targetFrames);
        m_capturing = false;
        DumpToLog();
        DumpToCSV("Data/SKSE/plugins/Playground/FrameCapture.csv");
    }
}


// =============================================================================
//  Log pass / phase / scene RT / proxy stats
// =============================================================================

void FrameCapture::LogPass(const CapturedPass& pass)
{
    if (!m_capturing || !m_inFrame) return;
    m_current.passes.push_back(pass);
    if (pass.gpuMs > 0.0f)
        m_current.totalGpuMs += pass.gpuMs;
}

void FrameCapture::LogPhase(const CapturedPhase& phase)
{
    if (!m_capturing || !m_inFrame) return;
    m_current.phases.push_back(phase);
}

void FrameCapture::SetSceneRT(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv)
{
    if (!m_capturing || !m_inFrame) return;

    m_current.sceneRTVAddr = reinterpret_cast<uintptr_t>(rtv);
    m_current.sceneDSVAddr = reinterpret_cast<uintptr_t>(dsv);

    if (rtv) {
        ID3D11Resource* res = nullptr;
        rtv->GetResource(&res);
        if (res) {
            ID3D11Texture2D* tex = nullptr;
            if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D),
                                               reinterpret_cast<void**>(&tex)))) {
                D3D11_TEXTURE2D_DESC desc{};
                tex->GetDesc(&desc);
                m_current.sceneRTFormat = static_cast<uint32_t>(desc.Format);
                m_current.sceneRTWidth  = desc.Width;
                m_current.sceneRTHeight = desc.Height;
                tex->Release();
            }
            res->Release();
        }
    }
}

void FrameCapture::SetProxyStats(uint32_t drawCalls, uint32_t rtSwitches, uint8_t phase)
{
    if (!m_capturing || !m_inFrame) return;
    m_current.drawCalls    = drawCalls;
    m_current.rtSwitches   = rtSwitches;
    m_current.currentPhase = phase;
}


// =============================================================================
//  DumpToLog — structured summary to SKSE log
// =============================================================================

void FrameCapture::DumpToLog() const
{
    if (m_frames.empty()) return;

    SKSE::log::info("=== FrameCapture Dump: {} frames ===", m_frames.size());

    // Summary statistics
    float minGpu = 999.0f, maxGpu = 0.0f, sumGpu = 0.0f;
    uint32_t minDraw = UINT32_MAX, maxDraw = 0;
    uint32_t framesWithPasses = 0;
    uint32_t framesWithPhases = 0;

    for (auto& f : m_frames) {
        if (f.totalGpuMs < minGpu) minGpu = f.totalGpuMs;
        if (f.totalGpuMs > maxGpu) maxGpu = f.totalGpuMs;
        sumGpu += f.totalGpuMs;
        if (f.drawCalls < minDraw) minDraw = f.drawCalls;
        if (f.drawCalls > maxDraw) maxDraw = f.drawCalls;
        if (!f.passes.empty()) ++framesWithPasses;
        if (!f.phases.empty()) ++framesWithPhases;
    }

    float avgGpu = sumGpu / static_cast<float>(m_frames.size());

    SKSE::log::info("  GPU ms: min={:.3f} avg={:.3f} max={:.3f}", minGpu, avgGpu, maxGpu);
    SKSE::log::info("  Draw calls: min={} max={}", minDraw, maxDraw);
    SKSE::log::info("  Frames with passes: {}/{}", framesWithPasses, m_frames.size());
    SKSE::log::info("  Frames with phase transitions: {}/{}", framesWithPhases, m_frames.size());

    // Per-pass aggregate stats
    struct PassAggregate {
        float sumMs = 0.0f;
        float maxMs = 0.0f;
        uint32_t count = 0;
        uint32_t nullDepthCount = 0;
        uint32_t nullOutputCount = 0;
    };
    std::unordered_map<std::string, PassAggregate> passStats;

    for (auto& f : m_frames) {
        for (auto& p : f.passes) {
            auto& agg = passStats[p.name];
            agg.sumMs += p.gpuMs;
            if (p.gpuMs > agg.maxMs) agg.maxMs = p.gpuMs;
            agg.count++;
            if (p.depthNull) agg.nullDepthCount++;
            if (p.outputNull) agg.nullOutputCount++;
        }
    }

    SKSE::log::info("  --- Per-Pass Summary ---");
    for (auto& [name, agg] : passStats) {
        float avgMs = agg.count > 0 ? agg.sumMs / agg.count : 0.0f;
        SKSE::log::info("    {}: avg={:.3f}ms max={:.3f}ms runs={} nullDepth={} nullOutput={}",
            name, avgMs, agg.maxMs, agg.count, agg.nullDepthCount, agg.nullOutputCount);
    }

    // Phase transition summary
    std::unordered_map<uint16_t, uint32_t> transitionCounts;
    for (auto& f : m_frames) {
        for (auto& ph : f.phases) {
            uint16_t key = (static_cast<uint16_t>(ph.oldPhase) << 8) | ph.newPhase;
            transitionCounts[key]++;
        }
    }
    SKSE::log::info("  --- Phase Transitions ---");
    for (auto& [key, count] : transitionCounts) {
        uint8_t old_ = static_cast<uint8_t>(key >> 8);
        uint8_t new_ = static_cast<uint8_t>(key & 0xFF);
        SKSE::log::info("    Phase {}->{}: {} times", old_, new_, count);
    }

    // Dump first 5 frames in detail and last 5 frames
    auto dumpFrame = [](const CapturedFrame& f, uint32_t idx) {
        SKSE::log::info("  [Frame {}] dt={:.4f} gpuMs={:.3f} draws={} sceneRT=0x{:X} fmt={} {}x{}",
            f.frameIndex, f.deltaTime, f.totalGpuMs, f.drawCalls,
            f.sceneRTVAddr, f.sceneRTFormat, f.sceneRTWidth, f.sceneRTHeight);

        for (auto& p : f.passes) {
            SKSE::log::info("    Pass '{}': gpu={:.3f}ms stage={} rtv=0x{:X}(fmt={}) depth=0x{:X}({}) output=0x{:X}({})",
                p.name, p.gpuMs, p.stage,
                p.rtvAddr, p.rtvFormat,
                p.depthSRVAddr, p.depthNull ? "NULL" : "ok",
                p.outputSRV, p.outputNull ? "NULL" : "ok");
        }

        for (auto& ph : f.phases) {
            SKSE::log::info("    Phase {}->{} mapped={} dispatched={} draws={}",
                ph.oldPhase, ph.newPhase, ph.mappedStage, ph.dispatched, ph.drawCallsSoFar);
        }
    };

    uint32_t n = static_cast<uint32_t>(m_frames.size());
    uint32_t detailCount = (std::min)(n, 5u);

    SKSE::log::info("  --- First {} Frames Detail ---", detailCount);
    for (uint32_t i = 0; i < detailCount; ++i)
        dumpFrame(m_frames[i], i);

    if (n > 10) {
        SKSE::log::info("  --- Last 5 Frames Detail ---");
        for (uint32_t i = n - 5; i < n; ++i)
            dumpFrame(m_frames[i], i);
    }

    SKSE::log::info("=== End FrameCapture Dump ===");
}


// =============================================================================
//  DumpToCSV — machine-readable output for analysis
// =============================================================================

void FrameCapture::DumpToCSV(const char* path) const
{
    if (m_frames.empty()) return;

    // Ensure directory exists
    {
        std::filesystem::path p(path);
        auto dir = p.parent_path();
        if (!dir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
        }
    }

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        SKSE::log::warn("FrameCapture: failed to open CSV '{}'", path);
        return;
    }

    // Header
    out << "frame,deltaTime,totalGpuMs,drawCalls,rtSwitches,sceneRTFormat,sceneRTWidth,sceneRTHeight,"
           "passName,passGpuMs,passStage,passEnabled,passExecuted,"
           "rtvFormat,rtvWidth,rtvHeight,depthNull,outputNull\n";

    for (auto& f : m_frames) {
        if (f.passes.empty()) {
            // Frame with no passes — still record the frame-level data
            out << f.frameIndex << "," << f.deltaTime << "," << f.totalGpuMs << ","
                << f.drawCalls << "," << f.rtSwitches << ","
                << f.sceneRTFormat << "," << f.sceneRTWidth << "," << f.sceneRTHeight << ","
                << ",,,,,,,\n";
        } else {
            for (auto& p : f.passes) {
                out << f.frameIndex << "," << f.deltaTime << "," << f.totalGpuMs << ","
                    << f.drawCalls << "," << f.rtSwitches << ","
                    << f.sceneRTFormat << "," << f.sceneRTWidth << "," << f.sceneRTHeight << ","
                    << p.name << "," << p.gpuMs << "," << static_cast<int>(p.stage) << ","
                    << (p.enabled ? 1 : 0) << "," << (p.executed ? 1 : 0) << ","
                    << p.rtvFormat << "," << p.rtvWidth << "," << p.rtvHeight << ","
                    << (p.depthNull ? 1 : 0) << "," << (p.outputNull ? 1 : 0) << "\n";
            }
        }
    }

    out.close();
    SKSE::log::info("FrameCapture: wrote {} frames to '{}'", m_frames.size(), path);
}

} // namespace SB
