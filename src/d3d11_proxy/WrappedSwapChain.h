#pragma once
//=============================================================================
//  WrappedSwapChain — IDXGISwapChain wrapper for Present hook + HDR
//=============================================================================

#include <dxgi1_6.h>
#include <d3d11.h>

namespace SB::Proxy
{

class WrappedSwapChain : public IDXGISwapChain4
{
public:
    WrappedSwapChain(IDXGISwapChain* real);
    ~WrappedSwapChain();

    // Real object access
    IDXGISwapChain* GetReal() const { return m_real; }

    // Back-pointers to wrapped device/context (set by proxy_main after creation)
    void SetWrappedDevice(ID3D11Device* wrappedDev) { m_wrappedDevice = wrappedDev; }
    void SetWrappedContext(ID3D11DeviceContext* wrappedCtx) { m_wrappedContext = wrappedCtx; }

    // ── IUnknown ─────────────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG   STDMETHODCALLTYPE AddRef() override;
    ULONG   STDMETHODCALLTYPE Release() override;

    // ── IDXGIObject ──────────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override;
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override;
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override;

    // ── IDXGIDeviceSubObject ─────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppDevice) override;

    // ── IDXGISwapChain ───────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override;
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) override;
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget) override;
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget) override;
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) override;
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters) override;
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** ppOutput) override;
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) override;
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* pLastPresentCount) override;

    // ── IDXGISwapChain1 ──────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc) override;
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) override;
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* pHwnd) override;
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID refiid, void** ppUnk) override;
    HRESULT STDMETHODCALLTYPE Present1(UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters) override;
    BOOL    STDMETHODCALLTYPE IsTemporaryMonoSupported() override;
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput) override;
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* pColor) override;
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* pColor) override;
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION Rotation) override;
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* pRotation) override;

    // ── IDXGISwapChain2 ──────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT Width, UINT Height) override;
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* pWidth, UINT* pHeight) override;
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override;
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* pMaxLatency) override;
    HANDLE  STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override;
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix) override;
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix) override;

    // ── IDXGISwapChain3 ──────────────────────────────────────────────
    UINT    STDMETHODCALLTYPE GetCurrentBackBufferIndex() override;
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace, UINT* pColorSpaceSupport) override;
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue) override;

    // ── IDXGISwapChain4 ──────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, void* pMetaData) override;

private:
    IDXGISwapChain*      m_real;
    IDXGISwapChain1*     m_real1 = nullptr;
    IDXGISwapChain2*     m_real2 = nullptr;
    IDXGISwapChain3*     m_real3 = nullptr;
    IDXGISwapChain4*     m_real4 = nullptr;
    ID3D11Device*        m_wrappedDevice  = nullptr;  // back-pointer to WrappedDevice
    ID3D11DeviceContext* m_wrappedContext = nullptr;   // back-pointer to WrappedContext
    ULONG                m_refCount = 1;
};

// Release cached depth SRV (call during DLL shutdown)
void ReleaseDepthCache();

} // namespace SB::Proxy
