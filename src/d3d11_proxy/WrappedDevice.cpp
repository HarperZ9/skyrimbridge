//=============================================================================
//  WrappedDevice.cpp — ID3D11Device wrapper implementation
//=============================================================================

#include "WrappedDevice.h"
#include "WrappedContext.h"
#include "ShaderManager.h"
#include "MaterialPipeline.h"
#include "ProxyLog.h"

#include <d3d11_4.h>
#include <dxgi1_2.h>

namespace SB::Proxy
{

// ── Constructor / Destructor ────────────────────────────────────────────────

WrappedDevice::WrappedDevice(ID3D11Device* real, WrappedContext* wrappedCtx)
    : m_real(real)
    , m_wrappedCtx(wrappedCtx)
    , m_refCount(1)
{
    Log("WrappedDevice created  real=%p  wrappedCtx=%p", m_real, m_wrappedCtx);
}

WrappedDevice::~WrappedDevice()
{
    Log("WrappedDevice destroyed  real=%p", m_real);
}

// ── IUnknown ────────────────────────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;

    if (riid == __uuidof(ID3D11Device) || riid == __uuidof(IUnknown))
    {
        *ppv = static_cast<ID3D11Device*>(this);
        AddRef();
        return S_OK;
    }

    // Block higher device versions — returning the real device here would break
    // the COM identity chain (callers get unwrapped objects, can escape the wrapper).
    // Skyrim SE targets DX11.0 and doesn't need Device1-5 methods.
    if (riid == __uuidof(ID3D11Device1) ||
        riid == __uuidof(ID3D11Device2) ||
        riid == __uuidof(ID3D11Device3) ||
        riid == __uuidof(ID3D11Device4) ||
        riid == __uuidof(ID3D11Device5))
    {
        Log("[WrappedDevice] QI for ID3D11Device1-5 blocked (returning E_NOINTERFACE)");
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // DXGI interfaces (IDXGIDevice, IDXGIDevice1, IDXGIObject, etc.)
    // Forward to the real device. This is needed for DXGI adapter/factory queries.
    // NOTE: This leaks the real device through the IDXGIDevice → QI(ID3D11Device) path.
    // If this causes issues, we'll need a WrappedDXGIDevice.
    if (riid == __uuidof(IDXGIDevice) || riid == __uuidof(IDXGIDevice1))
    {
        Log("[WrappedDevice] QI for IDXGIDevice — forwarding to real device (COM leak risk)");
    }

    return m_real->QueryInterface(riid, ppv);
}

ULONG STDMETHODCALLTYPE WrappedDevice::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

ULONG STDMETHODCALLTYPE WrappedDevice::Release()
{
    ULONG ref = InterlockedDecrement(&m_refCount);
    if (ref == 0)
        delete this;
    return ref;
}

// ── HOOKED: CreateTexture2D ─────────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateTexture2D(
    const D3D11_TEXTURE2D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Texture2D** ppTexture2D)
{
    if (pDesc &&
        (pDesc->BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL)))
    {
        Log("CreateTexture2D  %ux%u  fmt=%u  bind=0x%X  mips=%u  arraySize=%u",
            pDesc->Width, pDesc->Height, (UINT)pDesc->Format,
            pDesc->BindFlags, pDesc->MipLevels, pDesc->ArraySize);
    }
    return m_real->CreateTexture2D(pDesc, pInitialData, ppTexture2D);
}

// ── HOOKED: CreateRenderTargetView ──────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateRenderTargetView(
    ID3D11Resource* pResource,
    const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,
    ID3D11RenderTargetView** ppRTView)
{
    Log("CreateRenderTargetView  resource=%p  desc=%p", pResource, pDesc);
    return m_real->CreateRenderTargetView(pResource, pDesc, ppRTView);
}

// ── HOOKED: CreateDepthStencilView ──────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateDepthStencilView(
    ID3D11Resource* pResource,
    const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,
    ID3D11DepthStencilView** ppDepthStencilView)
{
    Log("CreateDepthStencilView  resource=%p  desc=%p", pResource, pDesc);
    return m_real->CreateDepthStencilView(pResource, pDesc, ppDepthStencilView);
}

// ── HOOKED: CreateVertexShader ──────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateVertexShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11VertexShader** ppVertexShader)
{
    Log("CreateVertexShader  bytecodeLen=%zu", BytecodeLength);
    HRESULT hr = m_real->CreateVertexShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader);
    if (SUCCEEDED(hr) && ppVertexShader && *ppVertexShader) {
        ShaderManager::Get().OnVertexShaderCreated(pShaderBytecode, BytecodeLength, *ppVertexShader);
    }
    return hr;
}

// ── HOOKED: CreatePixelShader ───────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreatePixelShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11PixelShader** ppPixelShader)
{
    Log("CreatePixelShader  bytecodeLen=%zu", BytecodeLength);
    HRESULT hr = m_real->CreatePixelShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);
    if (SUCCEEDED(hr) && ppPixelShader && *ppPixelShader) {
        ShaderManager::Get().OnPixelShaderCreated(pShaderBytecode, BytecodeLength, *ppPixelShader);
        MaterialPipeline::Get().OnPixelShaderCreated(
            m_real, pShaderBytecode, BytecodeLength, *ppPixelShader);
    }
    return hr;
}

// ── GetImmediateContext ─────────────────────────────────────────────────────

void STDMETHODCALLTYPE WrappedDevice::GetImmediateContext(ID3D11DeviceContext** ppImmediateContext)
{
    if (ppImmediateContext)
    {
        *ppImmediateContext = m_wrappedCtx;
        m_wrappedCtx->AddRef();
    }
}

// ── Forwarded: Resource creation ────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateBuffer(
    const D3D11_BUFFER_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Buffer** ppBuffer)
{
    return m_real->CreateBuffer(pDesc, pInitialData, ppBuffer);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateTexture1D(
    const D3D11_TEXTURE1D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Texture1D** ppTexture1D)
{
    return m_real->CreateTexture1D(pDesc, pInitialData, ppTexture1D);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateTexture3D(
    const D3D11_TEXTURE3D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Texture3D** ppTexture3D)
{
    return m_real->CreateTexture3D(pDesc, pInitialData, ppTexture3D);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateShaderResourceView(
    ID3D11Resource* pResource,
    const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc,
    ID3D11ShaderResourceView** ppSRView)
{
    return m_real->CreateShaderResourceView(pResource, pDesc, ppSRView);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateUnorderedAccessView(
    ID3D11Resource* pResource,
    const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc,
    ID3D11UnorderedAccessView** ppUAView)
{
    return m_real->CreateUnorderedAccessView(pResource, pDesc, ppUAView);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateInputLayout(
    const D3D11_INPUT_ELEMENT_DESC* pInputElementDescs,
    UINT NumElements,
    const void* pShaderBytecodeWithInputSignature,
    SIZE_T BytecodeLength,
    ID3D11InputLayout** ppInputLayout)
{
    return m_real->CreateInputLayout(pInputElementDescs, NumElements, pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateGeometryShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11GeometryShader** ppGeometryShader)
{
    return m_real->CreateGeometryShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppGeometryShader);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateGeometryShaderWithStreamOutput(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    const D3D11_SO_DECLARATION_ENTRY* pSODeclaration,
    UINT NumEntries,
    const UINT* pBufferStrides,
    UINT NumStrides,
    UINT RasterizedStream,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11GeometryShader** ppGeometryShader)
{
    return m_real->CreateGeometryShaderWithStreamOutput(pShaderBytecode, BytecodeLength, pSODeclaration, NumEntries, pBufferStrides, NumStrides, RasterizedStream, pClassLinkage, ppGeometryShader);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateHullShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11HullShader** ppHullShader)
{
    return m_real->CreateHullShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppHullShader);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateDomainShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11DomainShader** ppDomainShader)
{
    return m_real->CreateDomainShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppDomainShader);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateComputeShader(
    const void* pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage* pClassLinkage,
    ID3D11ComputeShader** ppComputeShader)
{
    return m_real->CreateComputeShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppComputeShader);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateClassLinkage(ID3D11ClassLinkage** ppLinkage)
{
    return m_real->CreateClassLinkage(ppLinkage);
}

// ── Forwarded: State objects ────────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateBlendState(
    const D3D11_BLEND_DESC* pBlendStateDesc,
    ID3D11BlendState** ppBlendState)
{
    return m_real->CreateBlendState(pBlendStateDesc, ppBlendState);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateDepthStencilState(
    const D3D11_DEPTH_STENCIL_DESC* pDepthStencilDesc,
    ID3D11DepthStencilState** ppDepthStencilState)
{
    return m_real->CreateDepthStencilState(pDepthStencilDesc, ppDepthStencilState);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateRasterizerState(
    const D3D11_RASTERIZER_DESC* pRasterizerDesc,
    ID3D11RasterizerState** ppRasterizerState)
{
    return m_real->CreateRasterizerState(pRasterizerDesc, ppRasterizerState);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateSamplerState(
    const D3D11_SAMPLER_DESC* pSamplerDesc,
    ID3D11SamplerState** ppSamplerState)
{
    return m_real->CreateSamplerState(pSamplerDesc, ppSamplerState);
}

// ── Forwarded: Query / Predicate / Counter ──────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateQuery(
    const D3D11_QUERY_DESC* pQueryDesc,
    ID3D11Query** ppQuery)
{
    return m_real->CreateQuery(pQueryDesc, ppQuery);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreatePredicate(
    const D3D11_QUERY_DESC* pPredicateDesc,
    ID3D11Predicate** ppPredicate)
{
    return m_real->CreatePredicate(pPredicateDesc, ppPredicate);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateCounter(
    const D3D11_COUNTER_DESC* pCounterDesc,
    ID3D11Counter** ppCounter)
{
    return m_real->CreateCounter(pCounterDesc, ppCounter);
}

// ── Forwarded: Deferred context / shared resource ───────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateDeferredContext(
    UINT ContextFlags,
    ID3D11DeviceContext** ppDeferredContext)
{
    return m_real->CreateDeferredContext(ContextFlags, ppDeferredContext);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::OpenSharedResource(
    HANDLE hResource,
    REFIID ReturnedInterface,
    void** ppResource)
{
    return m_real->OpenSharedResource(hResource, ReturnedInterface, ppResource);
}

// ── Forwarded: Format / multisample / counter checks ────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::CheckFormatSupport(
    DXGI_FORMAT Format,
    UINT* pFormatSupport)
{
    return m_real->CheckFormatSupport(Format, pFormatSupport);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CheckMultisampleQualityLevels(
    DXGI_FORMAT Format,
    UINT SampleCount,
    UINT* pNumQualityLevels)
{
    return m_real->CheckMultisampleQualityLevels(Format, SampleCount, pNumQualityLevels);
}

void STDMETHODCALLTYPE WrappedDevice::CheckCounterInfo(D3D11_COUNTER_INFO* pCounterInfo)
{
    m_real->CheckCounterInfo(pCounterInfo);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CheckCounter(
    const D3D11_COUNTER_DESC* pDesc,
    D3D11_COUNTER_TYPE* pType,
    UINT* pActiveCounters,
    LPSTR szName, UINT* pNameLength,
    LPSTR szUnits, UINT* pUnitsLength,
    LPSTR szDescription, UINT* pDescriptionLength)
{
    return m_real->CheckCounter(pDesc, pType, pActiveCounters, szName, pNameLength, szUnits, pUnitsLength, szDescription, pDescriptionLength);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CheckFeatureSupport(
    D3D11_FEATURE Feature,
    void* pFeatureSupportData,
    UINT FeatureSupportDataSize)
{
    return m_real->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
}

// ── Forwarded: Private data ─────────────────────────────────────────────────

HRESULT STDMETHODCALLTYPE WrappedDevice::GetPrivateData(
    REFGUID guid,
    UINT* pDataSize,
    void* pData)
{
    return m_real->GetPrivateData(guid, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::SetPrivateData(
    REFGUID guid,
    UINT DataSize,
    const void* pData)
{
    return m_real->SetPrivateData(guid, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedDevice::SetPrivateDataInterface(
    REFGUID guid,
    const IUnknown* pData)
{
    return m_real->SetPrivateDataInterface(guid, pData);
}

// ── Forwarded: Device info ──────────────────────────────────────────────────

D3D_FEATURE_LEVEL STDMETHODCALLTYPE WrappedDevice::GetFeatureLevel()
{
    return m_real->GetFeatureLevel();
}

UINT STDMETHODCALLTYPE WrappedDevice::GetCreationFlags()
{
    return m_real->GetCreationFlags();
}

HRESULT STDMETHODCALLTYPE WrappedDevice::GetDeviceRemovedReason()
{
    return m_real->GetDeviceRemovedReason();
}

HRESULT STDMETHODCALLTYPE WrappedDevice::SetExceptionMode(UINT RaiseFlags)
{
    return m_real->SetExceptionMode(RaiseFlags);
}

UINT STDMETHODCALLTYPE WrappedDevice::GetExceptionMode()
{
    return m_real->GetExceptionMode();
}

} // namespace SB::Proxy
