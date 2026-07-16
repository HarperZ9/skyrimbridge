//=============================================================================
//  WrappedSwapChain.cpp — IDXGISwapChain through IDXGISwapChain4 wrapper
//
//  Hooks Present/Present1 for pre-present callbacks, forwards everything else
//  to the real swap chain. Higher interfaces (1-4) are QI'd at construction
//  and may be nullptr on older systems.
//=============================================================================

#include "WrappedSwapChain.h"
#include "WrappedContext.h"
#include "RenderPhaseDetector.h"
#include "MaterialPipeline.h"
#include "CBDirtyTracker.h"
#include "OcclusionCuller.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"
#include <vector>

namespace SB::Proxy
{

// ── Global callback lists + frame counter ───────────────────────────────────
inline std::vector<PrePresentCallback> g_prePresentCallbacks;
inline std::vector<OnResizeCallback>   g_resizeCallbacks;
inline uint32_t g_frameCount = 0;

// ── Shared depth capture state ──────────────────────────────────────────────
// The game clears its depth buffer before Present, so we must COPY it to a
// persistent texture during CaptureDepthBuffer (called at Present entry).
// The SRV points to our copy, not the live (cleared) depth buffer.
static ID3D11DepthStencilView*   s_mainDSV        = nullptr;  // locked main depth DSV
static ID3D11Texture2D*          s_depthCopyTex   = nullptr;
static ID3D11ShaderResourceView* s_cachedDepthSRV = nullptr;
static uint32_t                  s_copyCount       = 0;       // copies per frame (for diagnostics)

// Map depth/typeless formats to their typeless version (for CopyResource-compatible textures)
static DXGI_FORMAT DepthToTypelessFormat(DXGI_FORMAT fmt)
{
    switch (fmt) {
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_TYPELESS:      return DXGI_FORMAT_R32_TYPELESS;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        case DXGI_FORMAT_R24G8_TYPELESS:    return DXGI_FORMAT_R24G8_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32G8X24_TYPELESS;
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_R16_TYPELESS:      return DXGI_FORMAT_R16_TYPELESS;
        default:                            return DXGI_FORMAT_UNKNOWN;
    }
}

// Map typeless formats to SRV-compatible typed formats
static DXGI_FORMAT TypelessToSRVFormat(DXGI_FORMAT fmt)
{
    switch (fmt) {
        case DXGI_FORMAT_R32_TYPELESS:      return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R24G8_TYPELESS:    return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DXGI_FORMAT_R16_TYPELESS:      return DXGI_FORMAT_R16_UNORM;
        default:                            return DXGI_FORMAT_UNKNOWN;
    }
}

// Called from Present — ensures ProxyInterface has the depth SRV from
// PreClearDepthCopy.  The actual depth copy happens in ClearDepthStencilView
// (before the game clears the depth buffer).
static void CaptureDepthBuffer(ID3D11DeviceContext* /*ctx*/)
{
    auto* pi = PG_GetProxyInterface();
    if (!pi) return;

    // PreClearDepthCopy already set gameDepthSRV during the frame.
    // Just ensure it's still valid.
    if (!s_cachedDepthSRV) {
        pi->gameDepthDSV = nullptr;
        pi->gameDepthSRV = nullptr;
    }
    // gameDepthSRV/gameDepthDSV were set by PreClearDepthCopy during the frame.
}

// Frame-limited diagnostic counter
static uint32_t s_depthDiagFrame = 0;

// Called from ClearDepthStencilView — identifies the main depth DSV and
// creates an SRV DIRECTLY on the live depth texture (no copy needed).
void PreClearDepthCopy(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv)
{
    if (!dsv || !ctx || s_mainDSV) return; // already identified

    ID3D11Resource* res = nullptr;
    dsv->GetResource(&res);
    if (!res) return;

    ID3D11Texture2D* depthTex = nullptr;
    HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D),
                                     reinterpret_cast<void**>(&depthTex));
    res->Release();
    if (FAILED(hr) || !depthTex) return;

    D3D11_TEXTURE2D_DESC desc;
    depthTex->GetDesc(&desc);

    if (desc.Width < 512 || desc.Height < 512) { depthTex->Release(); return; }

    // Check if the texture supports SRV binding
    bool hasSRVBind = (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0;
    Log("[DepthCapture] Found depth %ux%u fmt=%d bind=0x%X hasSRV=%d samples=%u",
        desc.Width, desc.Height, static_cast<int>(desc.Format), desc.BindFlags,
        hasSRVBind ? 1 : 0, desc.SampleDesc.Count);

    DXGI_FORMAT typelessFmt = DepthToTypelessFormat(desc.Format);
    DXGI_FORMAT srvFmt      = TypelessToSRVFormat(typelessFmt);

    if (hasSRVBind && srvFmt != DXGI_FORMAT_UNKNOWN) {
        // APPROACH A: Create SRV directly on the live depth texture (no copy)
        ID3D11Device* d = nullptr;
        ctx->GetDevice(&d);
        if (d) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                    = srvFmt;
            srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels       = 1;
            HRESULT srvHr = d->CreateShaderResourceView(depthTex, &srvDesc, &s_cachedDepthSRV);
            if (SUCCEEDED(srvHr) && s_cachedDepthSRV) {
                s_mainDSV = dsv;
                Log("[DepthCapture] DIRECT SRV on live depth tex=%p SRV=%p (fmt=%d -> srvFmt=%d)",
                    depthTex, s_cachedDepthSRV, static_cast<int>(desc.Format), static_cast<int>(srvFmt));

                auto* pi = PG_GetProxyInterface();
                if (pi) {
                    pi->gameDepthDSV = dsv;
                    pi->gameDepthSRV = s_cachedDepthSRV;
                }
            } else {
                Log("[DepthCapture] FAILED CreateSRV on live depth: hr=0x%08X", static_cast<unsigned>(srvHr));
            }
            d->Release();
        }
    } else {
        Log("[DepthCapture] Depth texture lacks SRV bind flag or unknown format");
    }

    depthTex->Release();
}

// No longer needed — SRV points to live depth, no copy required.
void OnDepthUnbind(ID3D11DeviceContext* /*ctx*/, ID3D11DepthStencilView* /*newDSV*/)
{
}

// Called from Present — diagnostics
void DepthCaptureFrameEnd()
{
    if (s_depthDiagFrame < 10) {
        Log("[DepthCapture] Frame %u: mainDSV=%p SRV=%p (direct, no copy)",
            s_depthDiagFrame, s_mainDSV, s_cachedDepthSRV);
    }
    ++s_depthDiagFrame;
}

// Forward-declare pre-UI cleanup (defined after pre-UI capture section below)
static void ReleasePreUICache();

// Release cached depth SRV — called during DLL_PROCESS_DETACH
void ReleaseDepthCache()
{
    if (s_cachedDepthSRV) { s_cachedDepthSRV->Release(); s_cachedDepthSRV = nullptr; }
    if (s_depthCopyTex)   { s_depthCopyTex->Release();   s_depthCopyTex   = nullptr; }
    s_mainDSV = nullptr;
    ReleasePreUICache();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Pre-UI Scene Capture — Snapshot backbuffer before Scaleform draws HUD
//
//  When RenderPhaseDetector transitions to UI phase, we copy the backbuffer.
//  Post-processing passes (bloom, DoF, color grade) then operate on this
//  pre-UI copy so they don't affect HUD/menu elements.
// ═══════════════════════════════════════════════════════════════════════════
static ID3D11Texture2D*          s_preUISceneTex = nullptr;
static ID3D11ShaderResourceView* s_preUISceneSRV = nullptr;
static bool                      s_preUISceneValid = false;
static bool                      s_preUICallbackRegistered = false;

// Phase change callback — fires when RenderPhaseDetector transitions phases
static void OnPhaseChange(RenderPhase oldPhase, RenderPhase newPhase)
{
    // Capture backbuffer when transitioning TO UI phase from any non-UI phase
    if (newPhase == RenderPhase::UI && oldPhase != RenderPhase::UI) {
        auto* wc = WrappedContext::s_instance;
        if (!wc) return;

        auto* ctx = wc->GetReal();
        auto* pi = PG_GetProxyInterface();
        if (!ctx || !pi || !pi->swapChain) return;

        // Get backbuffer
        ID3D11Texture2D* backTex = nullptr;
        HRESULT hr = pi->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                                reinterpret_cast<void**>(&backTex));
        if (FAILED(hr) || !backTex) return;

        D3D11_TEXTURE2D_DESC bbDesc;
        backTex->GetDesc(&bbDesc);

        // Create pre-UI texture on first use or if size changed
        if (!s_preUISceneTex) {
            D3D11_TEXTURE2D_DESC copyDesc = bbDesc;
            copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            copyDesc.Usage = D3D11_USAGE_DEFAULT;
            copyDesc.CPUAccessFlags = 0;
            copyDesc.MiscFlags = 0;

            ID3D11Device* dev = nullptr;
            ctx->GetDevice(&dev);
            if (dev) {
                hr = dev->CreateTexture2D(&copyDesc, nullptr, &s_preUISceneTex);
                if (SUCCEEDED(hr) && s_preUISceneTex) {
                    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                    srvDesc.Format = bbDesc.Format;
                    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Texture2D.MipLevels = 1;
                    dev->CreateShaderResourceView(s_preUISceneTex, &srvDesc, &s_preUISceneSRV);
                    Log("[PreUICapture] Created pre-UI scene texture %ux%u fmt=%d",
                        bbDesc.Width, bbDesc.Height, (int)bbDesc.Format);
                }
                dev->Release();
            }
        }

        // Copy current backbuffer (scene without UI) to our pre-UI texture
        if (s_preUISceneTex) {
            ctx->CopyResource(s_preUISceneTex, backTex);
            s_preUISceneValid = true;
        }

        backTex->Release();
    }
}

// Initialize pre-UI capture system (called once from first Present)
static void InitPreUICapture()
{
    if (s_preUICallbackRegistered) return;
    RenderPhaseDetector::Get().RegisterPhaseChangeCallback(OnPhaseChange);
    s_preUICallbackRegistered = true;
    Log("[PreUICapture] Phase change callback registered");
}

// Reset per-frame validity + sync to ProxyInterface (called from Present)
static void SyncPreUIState()
{
    auto* pi = PG_GetProxyInterface();
    if (pi) {
        pi->preUISceneSRV   = s_preUISceneSRV;
        pi->preUISceneTex   = s_preUISceneTex;
        pi->preUISceneValid = s_preUISceneValid;
    }
    // Reset for next frame
    s_preUISceneValid = false;
}

// Release pre-UI resources (call during shutdown)
static void ReleasePreUICache()
{
    if (s_preUISceneSRV) { s_preUISceneSRV->Release(); s_preUISceneSRV = nullptr; }
    if (s_preUISceneTex) { s_preUISceneTex->Release(); s_preUISceneTex = nullptr; }
    s_preUISceneValid = false;
}

// ── Construction / Destruction ──────────────────────────────────────────────

WrappedSwapChain::WrappedSwapChain(IDXGISwapChain* real)
    : m_real(real)
{
    Log("WrappedSwapChain: wrapping IDXGISwapChain %p", real);

    // QI for higher swap chain interfaces — failures are expected on older systems
    if (SUCCEEDED(m_real->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&m_real1))))
        Log("WrappedSwapChain: IDXGISwapChain1 available");

    if (SUCCEEDED(m_real->QueryInterface(__uuidof(IDXGISwapChain2), reinterpret_cast<void**>(&m_real2))))
        Log("WrappedSwapChain: IDXGISwapChain2 available");

    if (SUCCEEDED(m_real->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&m_real3))))
        Log("WrappedSwapChain: IDXGISwapChain3 available");

    if (SUCCEEDED(m_real->QueryInterface(__uuidof(IDXGISwapChain4), reinterpret_cast<void**>(&m_real4))))
        Log("WrappedSwapChain: IDXGISwapChain4 available");
}

WrappedSwapChain::~WrappedSwapChain()
{
    Log("WrappedSwapChain: destroying wrapper (real=%p)", m_real);

    if (m_real4) { m_real4->Release(); m_real4 = nullptr; }
    if (m_real3) { m_real3->Release(); m_real3 = nullptr; }
    if (m_real2) { m_real2->Release(); m_real2 = nullptr; }
    if (m_real1) { m_real1->Release(); m_real1 = nullptr; }
    // m_real is NOT released here — the caller owns the original reference
}

// ── IUnknown ────────────────────────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedSwapChain::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;

    if (riid == __uuidof(IUnknown)         ||
        riid == __uuidof(IDXGIObject)      ||
        riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGISwapChain))
    {
        *ppv = static_cast<IDXGISwapChain*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(IDXGISwapChain1))
    {
        if (!m_real1) { *ppv = nullptr; return E_NOINTERFACE; }
        *ppv = static_cast<IDXGISwapChain1*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(IDXGISwapChain2))
    {
        if (!m_real2) { *ppv = nullptr; return E_NOINTERFACE; }
        *ppv = static_cast<IDXGISwapChain2*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(IDXGISwapChain3))
    {
        if (!m_real3) { *ppv = nullptr; return E_NOINTERFACE; }
        *ppv = static_cast<IDXGISwapChain3*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(IDXGISwapChain4))
    {
        if (!m_real4) { *ppv = nullptr; return E_NOINTERFACE; }
        *ppv = static_cast<IDXGISwapChain4*>(this);
        AddRef();
        return S_OK;
    }

    // Unknown interface — forward to real object
    return m_real->QueryInterface(riid, ppv);
}

ULONG STDMETHODCALLTYPE WrappedSwapChain::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

ULONG STDMETHODCALLTYPE WrappedSwapChain::Release()
{
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0)
    {
        Log("WrappedSwapChain: ref count hit 0, deleting");
        delete this;
    }
    return count;
}

// ── IDXGIObject ─────────────────────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedSwapChain::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData)
{
    return m_real->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown)
{
    return m_real->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData)
{
    return m_real->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetParent(REFIID riid, void** ppParent)
{
    return m_real->GetParent(riid, ppParent);
}

// ── IDXGIDeviceSubObject ────────────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetDevice(REFIID riid, void** ppDevice)
{
    if (!ppDevice) return E_POINTER;

    // Return the wrapped device to maintain COM identity chain
    if (m_wrappedDevice && (riid == __uuidof(ID3D11Device) || riid == __uuidof(IUnknown)))
    {
        m_wrappedDevice->AddRef();
        *ppDevice = m_wrappedDevice;
        return S_OK;
    }

    // For other interfaces (IDXGIDevice etc.), forward to real
    return m_real->GetDevice(riid, ppDevice);
}

// ── IDXGISwapChain ──────────────────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedSwapChain::Present(UINT SyncInterval, UINT Flags)
{
    ++g_frameCount;

    // Log first few frames to confirm hook is active
    if (g_frameCount <= 5)
        Log("WrappedSwapChain::Present frame %u (sync=%u flags=0x%X)", g_frameCount, SyncInterval, Flags);

    // Fire pre-present callbacks (always — SKSE plugin uses these)
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    m_real->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev));
    if (dev)
    {
        dev->GetImmediateContext(&ctx);
        dev->Release();
    }

    for (auto& cb : g_prePresentCallbacks)
    {
        if (cb) cb(ctx, m_real);
    }

    // In safe mode: skip all proxy subsystems, just update frame count for SKSE
    if (PG_IsSafeMode())
    {
        auto* pi = PG_GetProxyInterface();
        if (pi) pi->frameCount = g_frameCount;
        if (ctx) ctx->Release();
        return m_real->Present(SyncInterval, Flags);
    }

    // Pre-UI capture: register phase change callback on first Present
    InitPreUICapture();

    // MaterialPipeline: lazy init on first Present, then clear for next frame
    auto& matPipe = MaterialPipeline::Get();
    if (!matPipe.IsInitialized() && dev)
        matPipe.Initialize(dev, m_real);
    if (ctx) matPipe.OnPresent(ctx);

    // Sync material pipeline stats to ProxyInterface for SKSE plugin access
    {
        auto* pi = PG_GetProxyInterface();
        if (pi) {
            pi->materialPipelineActive  = matPipe.IsEnabled();
            pi->materialPatchedCount    = matPipe.GetPatchedCount();
            pi->materialCandidateCount  = matPipe.GetCandidateCount();
            pi->materialClassifiedCount = matPipe.GetClassifiedCount();
            pi->gBufferAlbedo           = matPipe.GetAlbedoSRV();
            pi->gBufferNormals          = matPipe.GetNormalSRV();
            pi->gBufferMaterial         = matPipe.GetMaterialSRV();
        }
    }

    // Capture main depth buffer before phase detector resets it
    CaptureDepthBuffer(ctx);
    DepthCaptureFrameEnd();

    // Sync pre-UI scene state to ProxyInterface + reset for next frame
    SyncPreUIState();

    // Occlusion culler: kick off depth readback for next frame
    OcclusionCuller::Get().OnPresent(ctx);

    // Sync optimization stats to ProxyInterface + reset per-frame counters
    {
        auto* pi = PG_GetProxyInterface();
        auto* wc = WrappedContext::s_instance;
        auto& cb = CBDirtyTracker::Get();
        auto& oc = OcclusionCuller::Get();
        if (pi && wc) {
            pi->optStats.cbMapsIntercepted  = cb.GetMapCalls();
            pi->optStats.cbUpdatesSkipped   = cb.GetSkippedUpdates();
            pi->optStats.cbUpdatesCommitted = cb.GetCommittedUpdates();
            pi->optStats.cbTrackedBuffers   = cb.GetTrackedBuffers();
            pi->optStats.srvCallsRedundant  = wc->GetRedundantSRVCalls();
            pi->optStats.srvCallsTotal      = wc->GetTotalSRVCalls();
            pi->optStats.blendCallsRedundant = wc->GetRedundantBlendCalls();
            pi->optStats.blendCallsTotal    = wc->GetTotalBlendCalls();
            pi->optStats.dsCallsRedundant   = wc->GetRedundantDSCalls();
            pi->optStats.dsCallsTotal       = wc->GetTotalDSCalls();
            pi->optStats.rsCallsRedundant   = wc->GetRedundantRSCalls();
            pi->optStats.rsCallsTotal       = wc->GetTotalRSCalls();
            pi->optStats.occDrawsTested     = oc.GetDrawsTested();
            pi->optStats.occDrawsCulled     = oc.GetDrawsCulled();
            pi->drawCallsThisFrame     = wc->GetDrawCalls();
            pi->rtSwitchesThisFrame    = wc->GetRTSwitches();
            pi->shaderChangesThisFrame = wc->GetShaderChanges();
            pi->frameCount             = g_frameCount;
        }
        cb.OnFrameEnd();
        if (wc) wc->ResetFrameStats();
    }

    if (ctx) ctx->Release();

    RenderPhaseDetector::Get().OnPresent();
    return m_real->Present(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface)
{
    return m_real->GetBuffer(Buffer, riid, ppSurface);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget)
{
    return m_real->SetFullscreenState(Fullscreen, pTarget);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget)
{
    return m_real->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc)
{
    return m_real->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height,
                                                          DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    Log("WrappedSwapChain::ResizeBuffers %ux%u fmt=%d flags=0x%X buffers=%u",
        Width, Height, static_cast<int>(NewFormat), SwapChainFlags, BufferCount);
    HRESULT hr = m_real->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
    if (SUCCEEDED(hr)) {
        if (!PG_IsSafeMode() && Width > 0 && Height > 0) {
            ID3D11Device* realDev = nullptr;
            m_real->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&realDev));
            if (realDev) {
                MaterialPipeline::Get().OnResize(realDev, Width, Height);
                realDev->Release();
            }
        }
        for (auto& cb : g_resizeCallbacks)
            if (cb) cb(Width, Height, NewFormat);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters)
{
    return m_real->ResizeTarget(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetContainingOutput(IDXGIOutput** ppOutput)
{
    return m_real->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats)
{
    return m_real->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetLastPresentCount(UINT* pLastPresentCount)
{
    return m_real->GetLastPresentCount(pLastPresentCount);
}

// ── IDXGISwapChain1 ─────────────────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc)
{
    if (!m_real1) return E_NOINTERFACE;
    return m_real1->GetDesc1(pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc)
{
    if (!m_real1) return E_NOINTERFACE;
    return m_real1->GetFullscreenDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetHwnd(HWND* pHwnd)
{
    if (!m_real1) return E_NOINTERFACE;
    return m_real1->GetHwnd(pHwnd);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetCoreWindow(REFIID refiid, void** ppUnk)
{
    if (!m_real1) return E_NOINTERFACE;
    return m_real1->GetCoreWindow(refiid, ppUnk);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::Present1(UINT SyncInterval, UINT PresentFlags,
                                                     const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    if (!m_real1) return E_NOINTERFACE;

    ++g_frameCount;

    if (g_frameCount <= 5)
        Log("WrappedSwapChain::Present1 frame %u (sync=%u flags=0x%X)", g_frameCount, SyncInterval, PresentFlags);

    // Fire pre-present callbacks (always — SKSE plugin uses these)
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    m_real->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev));
    if (dev)
    {
        dev->GetImmediateContext(&ctx);
        dev->Release();
    }

    for (auto& cb : g_prePresentCallbacks)
    {
        if (cb) cb(ctx, m_real);
    }

    // In safe mode: skip all proxy subsystems, just update frame count for SKSE
    if (PG_IsSafeMode())
    {
        auto* pi = PG_GetProxyInterface();
        if (pi) pi->frameCount = g_frameCount;
        if (ctx) ctx->Release();
        return m_real1->Present1(SyncInterval, PresentFlags, pPresentParameters);
    }

    // Pre-UI capture: register phase change callback on first Present
    InitPreUICapture();

    // MaterialPipeline: lazy init on first Present, then clear for next frame
    auto& matPipe = MaterialPipeline::Get();
    if (!matPipe.IsInitialized() && dev)
        matPipe.Initialize(dev, m_real);
    if (ctx) matPipe.OnPresent(ctx);

    // Sync material pipeline stats to ProxyInterface for SKSE plugin access
    {
        auto* pi = PG_GetProxyInterface();
        if (pi) {
            pi->materialPipelineActive  = matPipe.IsEnabled();
            pi->materialPatchedCount    = matPipe.GetPatchedCount();
            pi->materialCandidateCount  = matPipe.GetCandidateCount();
            pi->materialClassifiedCount = matPipe.GetClassifiedCount();
            pi->gBufferAlbedo           = matPipe.GetAlbedoSRV();
            pi->gBufferNormals          = matPipe.GetNormalSRV();
            pi->gBufferMaterial         = matPipe.GetMaterialSRV();
        }
    }

    // Capture main depth buffer before phase detector resets it
    CaptureDepthBuffer(ctx);
    DepthCaptureFrameEnd();

    // Sync pre-UI scene state to ProxyInterface + reset for next frame
    SyncPreUIState();

    // Occlusion culler: kick off depth readback for next frame
    OcclusionCuller::Get().OnPresent(ctx);

    // Sync optimization stats to ProxyInterface + reset per-frame counters
    {
        auto* pi = PG_GetProxyInterface();
        auto* wc = WrappedContext::s_instance;
        auto& cbt = CBDirtyTracker::Get();
        auto& oc  = OcclusionCuller::Get();
        if (pi && wc) {
            pi->optStats.cbMapsIntercepted  = cbt.GetMapCalls();
            pi->optStats.cbUpdatesSkipped   = cbt.GetSkippedUpdates();
            pi->optStats.cbUpdatesCommitted = cbt.GetCommittedUpdates();
            pi->optStats.cbTrackedBuffers   = cbt.GetTrackedBuffers();
            pi->optStats.srvCallsRedundant  = wc->GetRedundantSRVCalls();
            pi->optStats.srvCallsTotal      = wc->GetTotalSRVCalls();
            pi->optStats.blendCallsRedundant = wc->GetRedundantBlendCalls();
            pi->optStats.blendCallsTotal    = wc->GetTotalBlendCalls();
            pi->optStats.dsCallsRedundant   = wc->GetRedundantDSCalls();
            pi->optStats.dsCallsTotal       = wc->GetTotalDSCalls();
            pi->optStats.rsCallsRedundant   = wc->GetRedundantRSCalls();
            pi->optStats.rsCallsTotal       = wc->GetTotalRSCalls();
            pi->optStats.occDrawsTested     = oc.GetDrawsTested();
            pi->optStats.occDrawsCulled     = oc.GetDrawsCulled();
            pi->drawCallsThisFrame     = wc->GetDrawCalls();
            pi->rtSwitchesThisFrame    = wc->GetRTSwitches();
            pi->shaderChangesThisFrame = wc->GetShaderChanges();
            pi->frameCount             = g_frameCount;
        }
        cbt.OnFrameEnd();
        if (wc) wc->ResetFrameStats();
    }

    if (ctx) ctx->Release();

    RenderPhaseDetector::Get().OnPresent();
    return m_real1->Present1(SyncInterval, PresentFlags, pPresentParameters);
}

BOOL STDMETHODCALLTYPE WrappedSwapChain::IsTemporaryMonoSupported()
{
    if (!m_real1) return FALSE;
    return m_real1->IsTemporaryMonoSupported();
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput)
{
    if (!m_real1) return E_NOINTERFACE;
    return m_real1->GetRestrictToOutput(ppRestrictToOutput);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::SetBackgroundColor(const DXGI_RGBA* pColor)
{
    if (!m_real1) return E_NOINTERFACE;
    return m_real1->SetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetBackgroundColor(DXGI_RGBA* pColor)
{
    if (!m_real1) return E_NOINTERFACE;
    return m_real1->GetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::SetRotation(DXGI_MODE_ROTATION Rotation)
{
    if (!m_real1) return E_NOINTERFACE;
    return m_real1->SetRotation(Rotation);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetRotation(DXGI_MODE_ROTATION* pRotation)
{
    if (!m_real1) return E_NOINTERFACE;
    return m_real1->GetRotation(pRotation);
}

// ── IDXGISwapChain2 ─────────────────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedSwapChain::SetSourceSize(UINT Width, UINT Height)
{
    if (!m_real2) return E_NOINTERFACE;
    return m_real2->SetSourceSize(Width, Height);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetSourceSize(UINT* pWidth, UINT* pHeight)
{
    if (!m_real2) return E_NOINTERFACE;
    return m_real2->GetSourceSize(pWidth, pHeight);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::SetMaximumFrameLatency(UINT MaxLatency)
{
    if (!m_real2) return E_NOINTERFACE;
    return m_real2->SetMaximumFrameLatency(MaxLatency);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetMaximumFrameLatency(UINT* pMaxLatency)
{
    if (!m_real2) return E_NOINTERFACE;
    return m_real2->GetMaximumFrameLatency(pMaxLatency);
}

HANDLE STDMETHODCALLTYPE WrappedSwapChain::GetFrameLatencyWaitableObject()
{
    if (!m_real2) return nullptr;
    return m_real2->GetFrameLatencyWaitableObject();
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix)
{
    if (!m_real2) return E_NOINTERFACE;
    return m_real2->SetMatrixTransform(pMatrix);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix)
{
    if (!m_real2) return E_NOINTERFACE;
    return m_real2->GetMatrixTransform(pMatrix);
}

// ── IDXGISwapChain3 ─────────────────────────────────────────────────────────

UINT STDMETHODCALLTYPE WrappedSwapChain::GetCurrentBackBufferIndex()
{
    if (!m_real3) return 0;
    return m_real3->GetCurrentBackBufferIndex();
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace,
                                                                   UINT* pColorSpaceSupport)
{
    if (!m_real3) return E_NOINTERFACE;
    return m_real3->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace)
{
    if (!m_real3) return E_NOINTERFACE;
    return m_real3->SetColorSpace1(ColorSpace);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height,
                                                           DXGI_FORMAT Format, UINT SwapChainFlags,
                                                           const UINT* pCreationNodeMask,
                                                           IUnknown* const* ppPresentQueue)
{
    if (!m_real3) return E_NOINTERFACE;
    Log("WrappedSwapChain::ResizeBuffers1 %ux%u fmt=%d flags=0x%X buffers=%u",
        Width, Height, static_cast<int>(Format), SwapChainFlags, BufferCount);
    HRESULT hr = m_real3->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags,
                                         pCreationNodeMask, ppPresentQueue);
    if (SUCCEEDED(hr) && !PG_IsSafeMode() && Width > 0 && Height > 0) {
        ID3D11Device* realDev = nullptr;
        m_real->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&realDev));
        if (realDev) {
            MaterialPipeline::Get().OnResize(realDev, Width, Height);
            realDev->Release();
        }
    }
    return hr;
}

// ── IDXGISwapChain4 ─────────────────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedSwapChain::SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type,
                                                           UINT Size, void* pMetaData)
{
    if (!m_real4) return E_NOINTERFACE;
    Log("WrappedSwapChain::SetHDRMetaData type=%d size=%u", static_cast<int>(Type), Size);
    return m_real4->SetHDRMetaData(Type, Size, pMetaData);
}

} // namespace SB::Proxy
