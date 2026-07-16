#include "GPUProfiler.h"
#include <SKSE/SKSE.h>
#include <numeric>

namespace SB
{

// =============================================================================
//  Initialize — create query pools for triple-buffered timing
// =============================================================================

bool GPUProfiler::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    if (m_initialized) return true;
    if (!dev || !ctx) return false;

    m_device  = dev;
    m_context = ctx;

    for (uint32_t f = 0; f < kBufferFrames; ++f) {
        if (!CreateQueries(m_frames[f])) {
            SKSE::log::error("GPUProfiler: failed to create queries for frame buffer {}", f);
            Shutdown();
            return false;
        }
    }

    m_initialized = true;
    m_enabled     = false;
    m_writeIdx    = 0;
    m_frameCount  = 0;

    SKSE::log::info("GPUProfiler: initialized ({} frame buffers, {} max passes)",
        kBufferFrames, kMaxPasses);
    return true;
}


bool GPUProfiler::CreateQueries(FrameData& fd)
{
    // Disjoint query (one per frame)
    D3D11_QUERY_DESC disjDesc{};
    disjDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    HRESULT hr = m_device->CreateQuery(&disjDesc, &fd.disjoint);
    if (FAILED(hr)) return false;

    // Timestamp queries (begin/end per pass)
    D3D11_QUERY_DESC tsDesc{};
    tsDesc.Query = D3D11_QUERY_TIMESTAMP;

    for (uint32_t i = 0; i < kMaxPasses; ++i) {
        hr = m_device->CreateQuery(&tsDesc, &fd.passes[i].begin);
        if (FAILED(hr)) return false;
        hr = m_device->CreateQuery(&tsDesc, &fd.passes[i].end);
        if (FAILED(hr)) return false;
    }

    fd.passCount  = 0;
    fd.submitted  = false;
    return true;
}


// =============================================================================
//  Shutdown
// =============================================================================

void GPUProfiler::Shutdown()
{
    for (uint32_t f = 0; f < kBufferFrames; ++f) {
        auto& fd = m_frames[f];
        if (fd.disjoint) { fd.disjoint->Release(); fd.disjoint = nullptr; }
        for (uint32_t i = 0; i < kMaxPasses; ++i) {
            if (fd.passes[i].begin) { fd.passes[i].begin->Release(); fd.passes[i].begin = nullptr; }
            if (fd.passes[i].end)   { fd.passes[i].end->Release();   fd.passes[i].end   = nullptr; }
        }
        fd.passCount = 0;
        fd.submitted = false;
    }

    m_results.clear();
    m_initialized = false;
    m_enabled     = false;
}


// =============================================================================
//  BeginFrame — collect results from oldest frame, start new disjoint
// =============================================================================

void GPUProfiler::BeginFrame()
{
    if (!m_initialized || !m_enabled) return;

    // Collect results from the oldest submitted frame
    uint32_t readIdx = (m_writeIdx + 1) % kBufferFrames;
    if (m_frames[readIdx].submitted) {
        CollectResults(m_frames[readIdx]);
    }

    // Reset current frame
    auto& cur = m_frames[m_writeIdx];
    cur.passCount = 0;
    cur.submitted = false;
    for (uint32_t i = 0; i < kMaxPasses; ++i)
        cur.passes[i].used = false;

    // Begin disjoint query for this frame
    m_context->Begin(cur.disjoint);
}


// =============================================================================
//  EndFrame — end disjoint, advance write index
// =============================================================================

void GPUProfiler::EndFrame()
{
    if (!m_initialized || !m_enabled) return;

    auto& cur = m_frames[m_writeIdx];
    m_context->End(cur.disjoint);
    cur.submitted = true;

    m_writeIdx = (m_writeIdx + 1) % kBufferFrames;
    ++m_frameCount;
}


// =============================================================================
//  BeginPass / EndPass — issue timestamp queries around a pass
// =============================================================================

uint32_t GPUProfiler::BeginPass(const char* name)
{
    if (!m_initialized || !m_enabled) return UINT32_MAX;

    auto& cur = m_frames[m_writeIdx];
    if (cur.passCount >= kMaxPasses) return UINT32_MAX;

    uint32_t id = cur.passCount++;
    cur.passes[id].name = name;
    cur.passes[id].used = true;
    m_context->End(cur.passes[id].begin);  // Timestamp queries use End(), not Begin()

    return id;
}

void GPUProfiler::EndPass(uint32_t passId)
{
    if (!m_initialized || !m_enabled) return;
    if (passId == UINT32_MAX) return;

    auto& cur = m_frames[m_writeIdx];
    if (passId >= cur.passCount) return;

    m_context->End(cur.passes[passId].end);
}


// =============================================================================
//  CollectResults — read back timestamp data from a completed frame
// =============================================================================

void GPUProfiler::CollectResults(FrameData& fd)
{
    m_results.clear();

    if (!fd.submitted || fd.passCount == 0) {
        fd.submitted = false;
        return;
    }

    // Get disjoint data (frequency + whether timestamps are valid)
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjData{};
    HRESULT hr = m_context->GetData(fd.disjoint, &disjData, sizeof(disjData),
                                     D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (hr != S_OK || disjData.Disjoint) {
        // Data not ready or timestamps unreliable — report invalid
        for (uint32_t i = 0; i < fd.passCount; ++i) {
            if (fd.passes[i].used) {
                m_results.push_back({ fd.passes[i].name, 0.0f, false });
            }
        }
        fd.submitted = false;
        return;
    }

    double msPerTick = 1000.0 / static_cast<double>(disjData.Frequency);

    for (uint32_t i = 0; i < fd.passCount; ++i) {
        auto& pass = fd.passes[i];
        if (!pass.used) continue;

        UINT64 tsBegin = 0, tsEnd = 0;
        HRESULT hrB = m_context->GetData(pass.begin, &tsBegin, sizeof(tsBegin),
                                          D3D11_ASYNC_GETDATA_DONOTFLUSH);
        HRESULT hrE = m_context->GetData(pass.end, &tsEnd, sizeof(tsEnd),
                                          D3D11_ASYNC_GETDATA_DONOTFLUSH);

        GPUTimingResult result;
        result.name  = pass.name;
        result.valid = (hrB == S_OK && hrE == S_OK);
        if (result.valid) {
            result.gpuMs = static_cast<float>(static_cast<double>(tsEnd - tsBegin) * msPerTick);
            // Sanity clamp (negative or absurd values mean something went wrong)
            if (result.gpuMs < 0.0f || result.gpuMs > 500.0f)
                result.gpuMs = 0.0f;
        }
        m_results.push_back(std::move(result));
    }

    fd.submitted = false;
}


// =============================================================================
//  GetTotalGpuMs
// =============================================================================

float GPUProfiler::GetTotalGpuMs() const
{
    float total = 0.0f;
    for (auto& r : m_results)
        if (r.valid) total += r.gpuMs;
    return total;
}

} // namespace SB
