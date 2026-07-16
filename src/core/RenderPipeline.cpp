#include "RenderPipeline.h"
#include "BootDiagnostics.h"
#include "GPUProfiler.h"
#include "FrameCapture.h"
#include <SKSE/SKSE.h>
#include <dxgi.h>
#include <algorithm>

namespace SB
{

// ─── Stage name lookup ───────────────────────────────────────────────────

const char* PipelineStageName(PipelineStage stage)
{
    switch (stage) {
    case PipelineStage::PostDepthPrepass: return "PostDepthPrepass";
    case PipelineStage::PostGeometry:     return "PostGeometry";
    case PipelineStage::PostSky:          return "PostSky";
    case PipelineStage::PreUI:            return "PreUI";
    case PipelineStage::PrePresent:       return "PrePresent";
    default:                              return "Unknown";
    }
}


// ─── ManagedRT ───────────────────────────────────────────────────────────

void ManagedRT::Release()
{
    if (uav)     { uav->Release();     uav     = nullptr; }
    if (srv)     { srv->Release();     srv     = nullptr; }
    if (rtv)     { rtv->Release();     rtv     = nullptr; }
    if (texture) { texture->Release(); texture = nullptr; }
    width  = 0;
    height = 0;
}


// ─── RenderPipeline ──────────────────────────────────────────────────────

bool RenderPipeline::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                 IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    m_device    = dev;
    m_context   = ctx;
    m_swapChain = sc;

    // Query backbuffer dimensions
    DXGI_SWAP_CHAIN_DESC desc{};
    if (SUCCEEDED(sc->GetDesc(&desc))) {
        m_screenW = desc.BufferDesc.Width;
        m_screenH = desc.BufferDesc.Height;
    }

    m_initialized = true;
    SKSE::log::info("SkyrimBridge: RenderPipeline initialized ({}x{})", m_screenW, m_screenH);
    return true;
}

void RenderPipeline::Shutdown()
{
    if (!m_initialized) return;

    // Release all managed render targets
    for (auto& [name, rt] : m_rtPool)
        rt.Release();
    m_rtPool.clear();

    m_passes.clear();
    m_nextHandle = 1;
    m_sorted     = false;
    m_frameIndex = 0;

    m_device    = nullptr;
    m_context   = nullptr;
    m_swapChain = nullptr;
    m_initialized = false;

    SKSE::log::info("SkyrimBridge: RenderPipeline shut down");
}


// ── Pass management ──────────────────────────────────────────────────────

PassHandle RenderPipeline::AddPass(const PassDef& def)
{
    std::lock_guard lock(m_passMutex);
    PassHandle h = m_nextHandle++;
    m_passes.push_back({ def, h, true });
    m_sorted = false;

    SKSE::log::info("SkyrimBridge: RenderPipeline added pass '{}' (stage={}, priority={}, handle={})",
        def.name, PipelineStageName(def.stage), def.priority, h);

    return h;
}

void RenderPipeline::SetPassEnabled(PassHandle handle, bool enabled)
{
    std::lock_guard lock(m_passMutex);
    for (auto& entry : m_passes) {
        if (entry.handle == handle && entry.alive) {
            entry.def.enabled = enabled;
            return;
        }
    }
}

void RenderPipeline::RemovePass(PassHandle handle)
{
    std::lock_guard lock(m_passMutex);
    for (auto& entry : m_passes) {
        if (entry.handle == handle) {
            entry.alive = false;
            return;
        }
    }
}

void RenderPipeline::SortPasses()
{
    // Remove dead entries
    m_passes.erase(
        std::remove_if(m_passes.begin(), m_passes.end(),
            [](const PassEntry& e) { return !e.alive; }),
        m_passes.end());

    // Stable sort by stage first, then priority within stage
    std::stable_sort(m_passes.begin(), m_passes.end(),
        [](const PassEntry& a, const PassEntry& b) {
            if (a.def.stage != b.def.stage)
                return static_cast<uint8_t>(a.def.stage) < static_cast<uint8_t>(b.def.stage);
            return a.def.priority < b.def.priority;
        });

    m_sorted = true;
}


// ── Managed RT pool ──────────────────────────────────────────────────────

ManagedRT& RenderPipeline::GetOrCreateRT(const std::string& name,
                                           DXGI_FORMAT format,
                                           float scale,
                                           bool needUAV)
{
    uint32_t w = static_cast<uint32_t>(m_screenW * scale);
    uint32_t h = static_cast<uint32_t>(m_screenH * scale);
    if (w == 0) w = 1;
    if (h == 0) h = 1;

    // Check if an existing RT matches
    auto it = m_rtPool.find(name);
    if (it != m_rtPool.end()) {
        auto& rt = it->second;
        if (rt.format == format && rt.width == w && rt.height == h)
            return rt;
        // Size/format changed — release old and recreate
        rt.Release();
    }

    // Create new managed RT
    ManagedRT rt;
    rt.name   = name;
    rt.format = format;
    rt.width  = w;
    rt.height = h;

    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width            = w;
    texDesc.Height           = h;
    texDesc.MipLevels        = 1;
    texDesc.ArraySize        = 1;
    texDesc.Format           = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage            = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (needUAV)
        texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, &rt.texture);
    if (FAILED(hr)) {
        SKSE::log::error("SkyrimBridge: RenderPipeline failed to create RT '{}' ({}x{}, fmt={})",
            name, w, h, static_cast<int>(format));
        // Insert a blank entry so we don't retry every frame
        m_rtPool[name] = rt;
        return m_rtPool[name];
    }

    // RTV
    hr = m_device->CreateRenderTargetView(rt.texture, nullptr, &rt.rtv);
    if (FAILED(hr)) {
        SKSE::log::error("SkyrimBridge: RenderPipeline failed to create RTV for '{}'", name);
    }

    // SRV
    hr = m_device->CreateShaderResourceView(rt.texture, nullptr, &rt.srv);
    if (FAILED(hr)) {
        SKSE::log::error("SkyrimBridge: RenderPipeline failed to create SRV for '{}'", name);
    }

    // UAV (optional)
    if (needUAV) {
        hr = m_device->CreateUnorderedAccessView(rt.texture, nullptr, &rt.uav);
        if (FAILED(hr)) {
            SKSE::log::error("SkyrimBridge: RenderPipeline failed to create UAV for '{}'", name);
        }
    }

    SKSE::log::info("SkyrimBridge: RenderPipeline created RT '{}' ({}x{}, fmt={}, uav={})",
        name, w, h, static_cast<int>(format), needUAV);

    m_rtPool[name] = std::move(rt);
    return m_rtPool[name];
}

ManagedRT* RenderPipeline::FindRT(const std::string& name)
{
    auto it = m_rtPool.find(name);
    if (it != m_rtPool.end())
        return &it->second;
    return nullptr;
}


// ── Execution ────────────────────────────────────────────────────────────

void RenderPipeline::ExecuteStage(PipelineStage stage, float deltaTime,
                                    IDXGISwapChain* sc,
                                    ID3D11RenderTargetView* gameRTV,
                                    ID3D11DepthStencilView* gameDSV)
{
    if (!m_initialized) return;

    std::lock_guard lock(m_passMutex);

    if (!m_sorted)
        SortPasses();

    // Allow per-call swapchain override (e.g., PrePresent gets the real SC)
    IDXGISwapChain* activeSwapChain = sc ? sc : m_swapChain;

    // Build pass context
    PassContext ctx;
    ctx.device       = m_device;
    ctx.context      = m_context;
    ctx.swapChain    = activeSwapChain;
    ctx.screenW      = m_screenW;
    ctx.screenH      = m_screenH;
    ctx.frameIndex   = m_frameIndex;
    ctx.deltaTime    = deltaTime;
    ctx.gameSceneRTV = gameRTV;
    ctx.gameSceneDSV = gameDSV;

    // Log scene RT info to frame capture
    auto& capture = FrameCapture::Get();
    if (capture.IsCapturing()) {
        capture.SetSceneRT(gameRTV, gameDSV);
    }

    auto& profiler = GPUProfiler::Get();

    // Execute all enabled passes in this stage
    for (auto& entry : m_passes) {
        if (!entry.alive || !entry.def.enabled)
            continue;
        if (entry.def.stage != stage)
            continue;
        if (!entry.def.execute)
            continue;

        // GPU profiling: bracket pass with timestamp queries
        uint32_t profileId = profiler.BeginPass(entry.def.name);

        try {
            BootDiag::LogPass(entry.def.name, "executing");
            entry.def.execute(ctx);
            BootDiag::LogPass(entry.def.name, "completed");
        } catch (const std::exception& e) {
            BootDiag::LogError(entry.def.name, e.what());
            SKSE::log::error("SkyrimBridge: RenderPipeline pass '{}' threw: {}",
                entry.def.name, e.what());
        } catch (...) {
            BootDiag::LogError(entry.def.name, "unknown exception");
            SKSE::log::error("SkyrimBridge: RenderPipeline pass '{}' threw unknown exception",
                entry.def.name);
        }

        profiler.EndPass(profileId);

        // Frame capture: log pass execution
        if (capture.IsCapturing()) {
            CapturedPass cp;
            cp.name     = entry.def.name;
            cp.executed = true;
            cp.enabled  = entry.def.enabled;
            cp.stage    = static_cast<uint8_t>(entry.def.stage);

            // Record RT info
            if (gameRTV) {
                cp.rtvAddr = reinterpret_cast<uintptr_t>(gameRTV);
                ID3D11Resource* res = nullptr;
                gameRTV->GetResource(&res);
                if (res) {
                    ID3D11Texture2D* tex = nullptr;
                    if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D),
                                                       reinterpret_cast<void**>(&tex)))) {
                        D3D11_TEXTURE2D_DESC desc{};
                        tex->GetDesc(&desc);
                        cp.rtvFormat = static_cast<uint32_t>(desc.Format);
                        cp.rtvWidth  = desc.Width;
                        cp.rtvHeight = desc.Height;
                        tex->Release();
                    }
                    res->Release();
                }
            }

            // Get GPU timing from profiler results (will be previous frame's data)
            for (auto& r : profiler.GetResults()) {
                if (r.name == entry.def.name && r.valid) {
                    cp.gpuMs = r.gpuMs;
                    break;
                }
            }

            capture.LogPass(cp);
        }
    }

    // Increment frame counter once per PrePresent (last stage in frame)
    if (stage == PipelineStage::PrePresent) {
        ++m_frameIndex;

        // ── Heartbeat: periodic health summary (~30s at 60fps) ──────
        if (m_frameIndex % 1800 == 0) {
            auto& results = profiler.GetResults();

            // Count passes per stage
            uint32_t registered[5] = {}, executed[5] = {}, errored[5] = {};
            for (auto& e : m_passes) {
                if (!e.alive) continue;
                uint8_t s = static_cast<uint8_t>(e.def.stage);
                if (s >= 5) continue;
                registered[s]++;
                if (e.def.enabled) executed[s]++;
            }

            // Aggregate GPU time
            float totalGpuMs = 0;
            std::string hotPass;
            float hotMs = 0;
            for (auto& r : results) {
                if (r.valid) {
                    totalGpuMs += r.gpuMs;
                    if (r.gpuMs > hotMs) { hotMs = r.gpuMs; hotPass = r.name; }
                }
            }

            SKSE::log::info("Pipeline[f{}]: PostGeo={}/{} PreUI={}/{} PrePresent={}/{} | GPU={:.1f}ms (hot: {} {:.1f}ms)",
                m_frameIndex,
                executed[1], registered[1],  // PostGeometry
                executed[3], registered[3],  // PreUI
                executed[4], registered[4],  // PrePresent
                totalGpuMs,
                hotPass.empty() ? "none" : hotPass.c_str(), hotMs);
        }
    }
}


// ── Queries ──────────────────────────────────────────────────────────────

uint32_t RenderPipeline::GetPassCount() const
{
    uint32_t count = 0;
    for (auto& e : m_passes)
        if (e.alive) ++count;
    return count;
}

uint32_t RenderPipeline::GetPassCount(PipelineStage stage) const
{
    uint32_t count = 0;
    for (auto& e : m_passes)
        if (e.alive && e.def.stage == stage) ++count;
    return count;
}

} // namespace SB
