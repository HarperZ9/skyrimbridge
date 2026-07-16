#pragma once
//=============================================================================
//  FrameCapture — Multi-frame diagnostic capture for mid-frame pipeline
//
//  Records per-frame snapshots of:
//    - Pipeline pass execution (which passes ran, timing, stage)
//    - Render target bindings (format, size, address)
//    - SRV bindings (slot, resource address, format)
//    - Phase transitions (proxy phases, pipeline stages dispatched)
//    - Effect state (enabled, output SRV null/valid)
//    - GPU profiler results (per-pass GPU ms)
//    - Depth buffer state (format, SRV address, null check)
//    - Scene RT format and copy texture format
//
//  Capture is triggered via DebugGUI or hotkey (F10).  Captures N frames
//  (default 600 = ~10s at 60fps) then dumps to log and optionally CSV.
//
//  Usage:
//    FrameCapture::Get().StartCapture(600);
//    // Each frame:
//    FrameCapture::Get().BeginFrame(frameIdx);
//    FrameCapture::Get().LogPass("GTAO", gpuMs, depthSRV, ...);
//    FrameCapture::Get().LogPhase(oldPhase, newPhase, stage);
//    FrameCapture::Get().EndFrame();
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <string>
#include <vector>

namespace SB
{

// Per-pass snapshot within a frame
struct CapturedPass
{
    std::string name;
    float       gpuMs        = 0.0f;
    bool        executed     = false;
    bool        enabled      = false;
    uint8_t     stage        = 0;      // PipelineStage cast to uint8_t

    // RT info at time of execution
    uintptr_t   rtvAddr      = 0;      // game scene RTV pointer
    uint32_t    rtvFormat    = 0;      // DXGI_FORMAT
    uint32_t    rtvWidth     = 0;
    uint32_t    rtvHeight    = 0;

    // Depth info
    uintptr_t   depthSRVAddr = 0;
    uint32_t    depthFormat  = 0;
    bool        depthNull    = true;

    // Output SRV
    uintptr_t   outputSRV    = 0;
    bool        outputNull   = true;
};

// Per-phase transition within a frame
struct CapturedPhase
{
    uint8_t  oldPhase     = 0;
    uint8_t  newPhase     = 0;
    uint8_t  mappedStage  = 0xFF;     // PipelineStage or 0xFF if no mapping
    bool     dispatched   = false;
    uint32_t drawCallsSoFar = 0;
};

// Full frame snapshot
struct CapturedFrame
{
    uint32_t    frameIndex  = 0;
    float       deltaTime   = 0.0f;
    float       totalGpuMs  = 0.0f;

    // Scene RT info (from mid-frame dispatch)
    uint32_t    sceneRTFormat = 0;
    uint32_t    sceneRTWidth  = 0;
    uint32_t    sceneRTHeight = 0;
    uintptr_t   sceneRTVAddr  = 0;
    uintptr_t   sceneDSVAddr  = 0;

    // Proxy stats
    uint32_t    drawCalls     = 0;
    uint32_t    rtSwitches    = 0;
    uint8_t     currentPhase  = 0;

    std::vector<CapturedPass>  passes;
    std::vector<CapturedPhase> phases;
};


class FrameCapture
{
public:
    static FrameCapture& Get()
    {
        static FrameCapture instance;
        return instance;
    }

    // Start capturing N frames.  Old capture data is cleared.
    void StartCapture(uint32_t numFrames = 600);

    // Stop capture early
    void StopCapture();

    // Is a capture in progress?
    bool IsCapturing() const { return m_capturing; }

    // How many frames captured so far?
    uint32_t GetCapturedCount() const { return static_cast<uint32_t>(m_frames.size()); }
    uint32_t GetTargetCount()   const { return m_targetFrames; }

    // Frame lifecycle (call from RenderPipeline::ExecuteStage or present hook)
    void BeginFrame(uint32_t frameIndex, float deltaTime);
    void EndFrame();

    // Log a pass execution
    void LogPass(const CapturedPass& pass);

    // Log a phase transition
    void LogPhase(const CapturedPhase& phase);

    // Set scene RT info for current frame
    void SetSceneRT(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv);

    // Set proxy stats for current frame
    void SetProxyStats(uint32_t drawCalls, uint32_t rtSwitches, uint8_t phase);

    // Dump capture to SKSE log (called automatically at end, or manually)
    void DumpToLog() const;

    // Dump capture to CSV file
    void DumpToCSV(const char* path) const;

    // Get captured frames (for DebugGUI rendering)
    const std::vector<CapturedFrame>& GetFrames() const { return m_frames; }

    // Does the capture have data to show?
    bool HasData() const { return !m_frames.empty(); }

private:
    FrameCapture() = default;

    bool     m_capturing    = false;
    bool     m_inFrame      = false;
    uint32_t m_targetFrames = 0;

    std::vector<CapturedFrame> m_frames;
    CapturedFrame              m_current;
};

} // namespace SB
