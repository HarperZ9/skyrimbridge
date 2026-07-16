#pragma once
//=============================================================================
//  GPUProfiler — D3D11 timestamp query-based per-pass GPU timing
//
//  Wraps D3D11_QUERY_TIMESTAMP and TIMESTAMP_DISJOINT to measure how long
//  each render/compute pass takes on the GPU.  Results are available the
//  next frame (GPU→CPU readback latency).
//
//  Usage:
//    profiler.BeginFrame(ctx);
//    uint32_t id = profiler.BeginPass("GTAO");
//    ...dispatch work...
//    profiler.EndPass(id);
//    profiler.EndFrame();
//
//    // Next frame+:
//    for (auto& r : profiler.GetResults()) { /* r.name, r.gpuMs */ }
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace SB
{

struct GPUTimingResult
{
    std::string name;
    float       gpuMs    = 0.0f;   // GPU time in milliseconds
    bool        valid    = false;   // true if query data was available
};

class GPUProfiler
{
public:
    static GPUProfiler& Get()
    {
        static GPUProfiler instance;
        return instance;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled()  const { return m_enabled; }

    // Frame lifecycle
    void BeginFrame();
    void EndFrame();

    // Pass timing (returns a pass ID, or UINT32_MAX if disabled/full)
    uint32_t BeginPass(const char* name);
    void     EndPass(uint32_t passId);

    // Results from the previous frame (available after BeginFrame collects)
    const std::vector<GPUTimingResult>& GetResults() const { return m_results; }

    // Total GPU time across all measured passes
    float GetTotalGpuMs() const;

private:
    GPUProfiler() = default;

    static constexpr uint32_t kMaxPasses    = 64;
    static constexpr uint32_t kBufferFrames = 3;  // triple-buffer for latency

    struct TimestampPair
    {
        ID3D11Query* begin = nullptr;
        ID3D11Query* end   = nullptr;
        std::string  name;
        bool         used  = false;
    };

    struct FrameData
    {
        ID3D11Query*    disjoint = nullptr;
        TimestampPair   passes[kMaxPasses];
        uint32_t        passCount = 0;
        bool            submitted = false;
    };

    bool CreateQueries(FrameData& fd);
    void CollectResults(FrameData& fd);

    bool m_initialized = false;
    bool m_enabled     = false;

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    FrameData m_frames[kBufferFrames];
    uint32_t  m_writeIdx  = 0;  // current frame being written
    uint32_t  m_frameCount = 0;

    std::vector<GPUTimingResult> m_results;
};

} // namespace SB
