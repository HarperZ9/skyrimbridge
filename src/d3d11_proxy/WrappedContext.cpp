#include "WrappedContext.h"
#include "ShaderManager.h"
#include "RenderPhaseDetector.h"
#include "MaterialPipeline.h"
#include "CBDirtyTracker.h"
#include "OcclusionCuller.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"
#include <d3d11_4.h>
#include <vector>
#include <cstring>

namespace SB::Proxy
{

// ── Global callback lists (registered via ProxyAPI) ─────────────────────────
inline std::vector<OnDrawCallback>       g_drawCallbacks;
inline std::vector<OnRTChangeCallback>   g_rtChangeCallbacks;
inline std::vector<OnShaderBindCallback> g_shaderBindCallbacks;

// Defined in WrappedSwapChain.cpp — depth capture
extern void PreClearDepthCopy(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv);
extern void OnDepthUnbind(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* newDSV);

WrappedContext* WrappedContext::s_instance = nullptr;

// ============================================================================
//  Constructor / Destructor
// ============================================================================

WrappedContext::WrappedContext(ID3D11DeviceContext* real)
    : m_real(real)
{
    s_instance = this;
    Log("[WrappedContext] Created wrapper %p around real context %p", this, real);
}

WrappedContext::~WrappedContext()
{
    Log("[WrappedContext] Destroyed wrapper %p", this);
}

// ============================================================================
//  Frame stats
// ============================================================================

void WrappedContext::ResetFrameStats()
{
    m_drawCalls     = 0;
    m_rtSwitches    = 0;
    m_shaderChanges = 0;

    // Reset state cache redundancy counters
    m_redundantSRV   = 0;
    m_redundantBlend = 0;
    m_redundantDS    = 0;
    m_redundantRS    = 0;
    m_totalSRV       = 0;
    m_totalBlend     = 0;
    m_totalDS        = 0;
    m_totalRS        = 0;
}

void WrappedContext::ResetStateCache()
{
    m_cachedBlendState = nullptr;
    m_cachedBlendFactor[0] = m_cachedBlendFactor[1] = 1.f;
    m_cachedBlendFactor[2] = m_cachedBlendFactor[3] = 1.f;
    m_cachedSampleMask = 0xFFFFFFFF;
    m_cachedDSState    = nullptr;
    m_cachedStencilRef = 0;
    m_cachedRSState    = nullptr;
    std::memset(m_cachedPSSRVs, 0, sizeof(m_cachedPSSRVs));
    std::memset(m_cachedPSSamplers, 0, sizeof(m_cachedPSSamplers));
}

// ============================================================================
//  IUnknown
// ============================================================================

HRESULT STDMETHODCALLTYPE WrappedContext::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;

    if (riid == __uuidof(ID3D11DeviceContext) || riid == __uuidof(IUnknown) ||
        riid == __uuidof(ID3D11DeviceChild))
    {
        *ppv = this;
        AddRef();
        return S_OK;
    }

    // Block higher context versions — returning the real context would break
    // the COM identity chain (callers can call GetImmediateContext1/2/3 on the
    // real device, completely bypassing our wrapper).
    if (riid == __uuidof(ID3D11DeviceContext1) ||
        riid == __uuidof(ID3D11DeviceContext2) ||
        riid == __uuidof(ID3D11DeviceContext3) ||
        riid == __uuidof(ID3D11DeviceContext4))
    {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // ID3D11Multithread is safe to forward — it only has Get/SetMultithreadProtected,
    // no methods that return device/context pointers.
    return m_real->QueryInterface(riid, ppv);
}

ULONG STDMETHODCALLTYPE WrappedContext::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

ULONG STDMETHODCALLTYPE WrappedContext::Release()
{
    ULONG ref = InterlockedDecrement(&m_refCount);
    if (ref == 0)
        delete this;
    return ref;
}

// ============================================================================
//  ID3D11DeviceChild
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::GetDevice(ID3D11Device** ppDevice)
{
    if (!ppDevice) return;
    if (m_wrappedDevice) {
        m_wrappedDevice->AddRef();
        *ppDevice = m_wrappedDevice;
    } else {
        m_real->GetDevice(ppDevice);
    }
}

HRESULT STDMETHODCALLTYPE WrappedContext::GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData)
{
    return m_real->GetPrivateData(guid, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedContext::SetPrivateData(REFGUID guid, UINT DataSize, const void* pData)
{
    return m_real->SetPrivateData(guid, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedContext::SetPrivateDataInterface(REFGUID guid, const IUnknown* pData)
{
    return m_real->SetPrivateDataInterface(guid, pData);
}

// ============================================================================
//  Shader binding (HOOKED)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::VSSetShader(ID3D11VertexShader* pVS, ID3D11ClassInstance* const* ppCI, UINT NumCI)
{
    if (PG_IsSafeMode()) { m_real->VSSetShader(pVS, ppCI, NumCI); return; }

    m_currentVS = pVS;
    auto* replacement = ShaderManager::Get().GetReplacementVS(pVS);
    m_real->VSSetShader(replacement ? replacement : pVS, ppCI, NumCI);

    uint64_t vsHash = ShaderManager::Get().GetVSHash(pVS);
    uint64_t psHash = ShaderManager::Get().GetPSHash(m_currentPS);
    RenderPhaseDetector::Get().OnShaderBind(psHash, vsHash);
    for (auto& cb : g_shaderBindCallbacks)
        if (cb) cb(m_currentPS, pVS);
}

void STDMETHODCALLTYPE WrappedContext::PSSetShader(ID3D11PixelShader* pPS, ID3D11ClassInstance* const* ppCI, UINT NumCI)
{
    if (PG_IsSafeMode()) { m_real->PSSetShader(pPS, ppCI, NumCI); return; }

    if (pPS != m_currentPS)
    {
        m_currentPS = pPS;
        ++m_shaderChanges;
    }
    // Check for shader replacement
    auto* replacement = ShaderManager::Get().GetReplacementPS(pPS);
    m_real->PSSetShader(replacement ? replacement : pPS, ppCI, NumCI);

    // Fire shader bind callbacks + phase detector
    uint64_t psHash = ShaderManager::Get().GetPSHash(pPS);
    uint64_t vsHash = ShaderManager::Get().GetVSHash(m_currentVS);
    RenderPhaseDetector::Get().OnShaderBind(psHash, vsHash);
    for (auto& cb : g_shaderBindCallbacks)
        if (cb) cb(pPS, m_currentVS);
}

void STDMETHODCALLTYPE WrappedContext::GSSetShader(ID3D11GeometryShader* pShader, ID3D11ClassInstance* const* ppCI, UINT NumCI)
{
    m_real->GSSetShader(pShader, ppCI, NumCI);
}

void STDMETHODCALLTYPE WrappedContext::HSSetShader(ID3D11HullShader* pShader, ID3D11ClassInstance* const* ppCI, UINT NumCI)
{
    m_real->HSSetShader(pShader, ppCI, NumCI);
}

void STDMETHODCALLTYPE WrappedContext::DSSetShader(ID3D11DomainShader* pShader, ID3D11ClassInstance* const* ppCI, UINT NumCI)
{
    m_real->DSSetShader(pShader, ppCI, NumCI);
}

void STDMETHODCALLTYPE WrappedContext::CSSetShader(ID3D11ComputeShader* pShader, ID3D11ClassInstance* const* ppCI, UINT NumCI)
{
    m_real->CSSetShader(pShader, ppCI, NumCI);
}

// ============================================================================
//  Draw calls (HOOKED)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::Draw(UINT VertexCount, UINT StartVertexLocation)
{
    if (PG_IsSafeMode()) { m_real->Draw(VertexCount, StartVertexLocation); return; }

    ++m_drawCalls;
    RenderPhaseDetector::Get().OnDraw(VertexCount, false);
    for (auto& cb : g_drawCallbacks)
        if (cb) cb(VertexCount, 1);
    m_real->Draw(VertexCount, StartVertexLocation);
}

void STDMETHODCALLTYPE WrappedContext::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
    if (PG_IsSafeMode()) { m_real->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation); return; }

    ++m_drawCalls;
    RenderPhaseDetector::Get().OnDraw(IndexCount, true);
    if (OcclusionCuller::Get().ShouldCull(IndexCount))
        return;
    for (auto& cb : g_drawCallbacks)
        if (cb) cb(IndexCount, 1);
    m_real->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
}

void STDMETHODCALLTYPE WrappedContext::DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation)
{
    if (PG_IsSafeMode()) { m_real->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation); return; }

    ++m_drawCalls;
    RenderPhaseDetector::Get().OnDraw(VertexCountPerInstance, false);
    for (auto& cb : g_drawCallbacks)
        if (cb) cb(VertexCountPerInstance, InstanceCount);
    m_real->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}

void STDMETHODCALLTYPE WrappedContext::DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
{
    if (PG_IsSafeMode()) { m_real->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation); return; }

    ++m_drawCalls;
    RenderPhaseDetector::Get().OnDraw(IndexCountPerInstance, true);
    for (auto& cb : g_drawCallbacks)
        if (cb) cb(IndexCountPerInstance, InstanceCount);
    m_real->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}

void STDMETHODCALLTYPE WrappedContext::DrawAuto()
{
    if (PG_IsSafeMode()) { m_real->DrawAuto(); return; }

    ++m_drawCalls;
    m_real->DrawAuto();
}

void STDMETHODCALLTYPE WrappedContext::DrawIndexedInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
    if (PG_IsSafeMode()) { m_real->DrawIndexedInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs); return; }

    ++m_drawCalls;
    m_real->DrawIndexedInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}

void STDMETHODCALLTYPE WrappedContext::DrawInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
    if (PG_IsSafeMode()) { m_real->DrawInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs); return; }

    ++m_drawCalls;
    m_real->DrawInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}

// ============================================================================
//  Output merger (HOOKED)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView* const* ppRTVs, ID3D11DepthStencilView* pDSV)
{
    if (PG_IsSafeMode()) { m_real->OMSetRenderTargets(NumViews, ppRTVs, pDSV); return; }

    // Copy depth when the main DSV is being UNBOUND (= scene geometry just finished)
    OnDepthUnbind(m_real, pDSV);

    ++m_rtSwitches;
    RenderPhaseDetector::Get().OnRTChange(NumViews, ppRTVs, pDSV);
    for (auto& cb : g_rtChangeCallbacks)
        if (cb) cb(NumViews, ppRTVs, pDSV);

    // G-buffer MRT injection: during geometry pass, append albedo+normals+material RTs
    auto& phase = RenderPhaseDetector::Get();
    auto& material = MaterialPipeline::Get();
    if (phase.GetCurrentPhase() == RenderPhase::GeometryMain && pDSV != nullptr) {
        ID3D11RenderTargetView* rtvBuf[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        UINT outCount = 0;
        if (material.InjectGBufferRTs(NumViews, ppRTVs, rtvBuf, outCount)) {
            m_real->OMSetRenderTargets(outCount, rtvBuf, pDSV);
            return;
        }
    }

    m_real->OMSetRenderTargets(NumViews, ppRTVs, pDSV);
}

void STDMETHODCALLTYPE WrappedContext::OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView* const* ppRTVs, ID3D11DepthStencilView* pDSV, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUAVs, const UINT* pUAVInitialCounts)
{
    if (PG_IsSafeMode()) { m_real->OMSetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRTVs, pDSV, UAVStartSlot, NumUAVs, ppUAVs, pUAVInitialCounts); return; }

    OnDepthUnbind(m_real, pDSV);

    ++m_rtSwitches;
    RenderPhaseDetector::Get().OnRTChange(NumRTVs, ppRTVs, pDSV);
    for (auto& cb : g_rtChangeCallbacks)
        if (cb) cb(NumRTVs, ppRTVs, pDSV);
    m_real->OMSetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRTVs, pDSV, UAVStartSlot, NumUAVs, ppUAVs, pUAVInitialCounts);
}

void STDMETHODCALLTYPE WrappedContext::OMSetBlendState(ID3D11BlendState* pBS, const FLOAT BlendFactor[4], UINT SampleMask)
{
    if (PG_IsSafeMode()) { m_real->OMSetBlendState(pBS, BlendFactor, SampleMask); return; }

    ++m_totalBlend;

    // Normalize null blend factor to default {1,1,1,1}
    float bf[4] = {1.f, 1.f, 1.f, 1.f};
    if (BlendFactor) {
        bf[0] = BlendFactor[0]; bf[1] = BlendFactor[1];
        bf[2] = BlendFactor[2]; bf[3] = BlendFactor[3];
    }

    if (pBS == m_cachedBlendState && SampleMask == m_cachedSampleMask &&
        bf[0] == m_cachedBlendFactor[0] && bf[1] == m_cachedBlendFactor[1] &&
        bf[2] == m_cachedBlendFactor[2] && bf[3] == m_cachedBlendFactor[3])
    {
        ++m_redundantBlend;
        return;
    }

    m_cachedBlendState = pBS;
    m_cachedSampleMask = SampleMask;
    m_cachedBlendFactor[0] = bf[0]; m_cachedBlendFactor[1] = bf[1];
    m_cachedBlendFactor[2] = bf[2]; m_cachedBlendFactor[3] = bf[3];
    m_real->OMSetBlendState(pBS, BlendFactor, SampleMask);
}

void STDMETHODCALLTYPE WrappedContext::OMSetDepthStencilState(ID3D11DepthStencilState* pDSS, UINT StencilRef)
{
    if (PG_IsSafeMode()) { m_real->OMSetDepthStencilState(pDSS, StencilRef); return; }

    ++m_totalDS;
    if (pDSS == m_cachedDSState && StencilRef == m_cachedStencilRef)
    {
        ++m_redundantDS;
        return;
    }
    m_cachedDSState    = pDSS;
    m_cachedStencilRef = StencilRef;
    m_real->OMSetDepthStencilState(pDSS, StencilRef);
}

// ============================================================================
//  Clear (HOOKED — phase detection)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::ClearRenderTargetView(ID3D11RenderTargetView* pRTV, const FLOAT ColorRGBA[4])
{
    m_real->ClearRenderTargetView(pRTV, ColorRGBA);
}

void STDMETHODCALLTYPE WrappedContext::ClearDepthStencilView(ID3D11DepthStencilView* pDSV, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
    if (PG_IsSafeMode()) { m_real->ClearDepthStencilView(pDSV, ClearFlags, Depth, Stencil); return; }

    // Copy depth data BEFORE the clear — the game clears depth before Present,
    // so this is the last chance to capture valid depth for our compute shaders.
    PreClearDepthCopy(m_real, pDSV);

    RenderPhaseDetector::Get().OnClearDepth(pDSV);
    m_real->ClearDepthStencilView(pDSV, ClearFlags, Depth, Stencil);
}

void STDMETHODCALLTYPE WrappedContext::ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* pUAV, const UINT Values[4])
{
    m_real->ClearUnorderedAccessViewUint(pUAV, Values);
}

void STDMETHODCALLTYPE WrappedContext::ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView* pUAV, const FLOAT Values[4])
{
    m_real->ClearUnorderedAccessViewFloat(pUAV, Values);
}

// ============================================================================
//  Resource binding (HOOKED — injection hooks added later)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs)
{
    if (PG_IsSafeMode()) { m_real->PSSetShaderResources(StartSlot, NumViews, ppSRVs); return; }

    ++m_totalSRV;

    // Check if all SRVs in range match our cache
    if (ppSRVs && StartSlot + NumViews <= kMaxPSSRVSlots) {
        bool allMatch = true;
        for (UINT i = 0; i < NumViews; ++i) {
            if (m_cachedPSSRVs[StartSlot + i] != ppSRVs[i]) {
                allMatch = false;
                break;
            }
        }
        if (allMatch) {
            ++m_redundantSRV;
            return;
        }
        // Update cache
        for (UINT i = 0; i < NumViews; ++i)
            m_cachedPSSRVs[StartSlot + i] = ppSRVs[i];
    }

    m_real->PSSetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs)
{
    m_real->PSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
    m_real->PSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

// ============================================================================
//  Map / Unmap (HOOKED — CB dirty tracking)
//
//  For dynamic constant buffers with WRITE_DISCARD:
//    Map  → returns a staging buffer (real Map is NOT called)
//    Unmap → compares staging vs shadow; if identical, skips real Map/Unmap
//  Expected savings: 60-80% of CB uploads (engine re-uploads identical data).
// ============================================================================

HRESULT STDMETHODCALLTYPE WrappedContext::Map(ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource)
{
    if (PG_IsSafeMode()) return m_real->Map(pResource, Subresource, MapType, MapFlags, pMappedResource);

    if (CBDirtyTracker::Get().InterceptMap(m_real, pResource, Subresource, MapType, MapFlags, pMappedResource))
        return S_OK;
    return m_real->Map(pResource, Subresource, MapType, MapFlags, pMappedResource);
}

void STDMETHODCALLTYPE WrappedContext::Unmap(ID3D11Resource* pResource, UINT Subresource)
{
    if (PG_IsSafeMode()) { m_real->Unmap(pResource, Subresource); return; }

    if (CBDirtyTracker::Get().InterceptUnmap(m_real, pResource, Subresource))
        return;
    m_real->Unmap(pResource, Subresource);
}

// ============================================================================
//  Compute (forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::Dispatch(UINT X, UINT Y, UINT Z)
{
    m_real->Dispatch(X, Y, Z);
}

void STDMETHODCALLTYPE WrappedContext::DispatchIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
    m_real->DispatchIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}

void STDMETHODCALLTYPE WrappedContext::CSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs)
{
    m_real->CSSetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::CSSetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUAVs, const UINT* pUAVInitialCounts)
{
    m_real->CSSetUnorderedAccessViews(StartSlot, NumUAVs, ppUAVs, pUAVInitialCounts);
}

void STDMETHODCALLTYPE WrappedContext::CSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
    m_real->CSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::CSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs)
{
    m_real->CSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

// ============================================================================
//  Input assembler (forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::IASetInputLayout(ID3D11InputLayout* pInputLayout)
{
    m_real->IASetInputLayout(pInputLayout);
}

void STDMETHODCALLTYPE WrappedContext::IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppVBs, const UINT* pStrides, const UINT* pOffsets)
{
    m_real->IASetVertexBuffers(StartSlot, NumBuffers, ppVBs, pStrides, pOffsets);
}

void STDMETHODCALLTYPE WrappedContext::IASetIndexBuffer(ID3D11Buffer* pIB, DXGI_FORMAT Format, UINT Offset)
{
    m_real->IASetIndexBuffer(pIB, Format, Offset);
}

void STDMETHODCALLTYPE WrappedContext::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology)
{
    m_real->IASetPrimitiveTopology(Topology);
}

// ============================================================================
//  VS/GS/HS/DS binding (forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs)
{
    if (PG_IsSafeMode()) { m_real->VSSetConstantBuffers(StartSlot, NumBuffers, ppCBs); return; }

    OcclusionCuller::Get().OnVSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
    m_real->VSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs)
{
    m_real->VSSetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
    m_real->VSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs)
{
    m_real->GSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::GSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs)
{
    m_real->GSSetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
    m_real->GSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::HSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs)
{
    m_real->HSSetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::HSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
    m_real->HSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::HSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs)
{
    m_real->HSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::DSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs)
{
    m_real->DSSetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::DSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
{
    m_real->DSSetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::DSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs)
{
    m_real->DSSetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

// ============================================================================
//  Rasterizer / Stream Output (forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::RSSetState(ID3D11RasterizerState* pRS)
{
    if (PG_IsSafeMode()) { m_real->RSSetState(pRS); return; }

    ++m_totalRS;
    if (pRS == m_cachedRSState) {
        ++m_redundantRS;
        return;
    }
    m_cachedRSState = pRS;
    m_real->RSSetState(pRS);
}

void STDMETHODCALLTYPE WrappedContext::RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports)
{
    if (PG_IsSafeMode()) { m_real->RSSetViewports(NumViewports, pViewports); return; }

    if (NumViewports > 0 && pViewports)
        RenderPhaseDetector::Get().OnViewportChange(pViewports[0].Width, pViewports[0].Height);
    m_real->RSSetViewports(NumViewports, pViewports);
}

void STDMETHODCALLTYPE WrappedContext::RSSetScissorRects(UINT NumRects, const D3D11_RECT* pRects)
{
    m_real->RSSetScissorRects(NumRects, pRects);
}

void STDMETHODCALLTYPE WrappedContext::SOSetTargets(UINT NumBuffers, ID3D11Buffer* const* ppSOTargets, const UINT* pOffsets)
{
    m_real->SOSetTargets(NumBuffers, ppSOTargets, pOffsets);
}

// ============================================================================
//  Copy / Update (forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::CopySubresourceRegion(ID3D11Resource* pDst, UINT DstSub, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource* pSrc, UINT SrcSub, const D3D11_BOX* pSrcBox)
{
    m_real->CopySubresourceRegion(pDst, DstSub, DstX, DstY, DstZ, pSrc, SrcSub, pSrcBox);
}

void STDMETHODCALLTYPE WrappedContext::CopyResource(ID3D11Resource* pDst, ID3D11Resource* pSrc)
{
    m_real->CopyResource(pDst, pSrc);
}

void STDMETHODCALLTYPE WrappedContext::UpdateSubresource(ID3D11Resource* pDst, UINT DstSub, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
    m_real->UpdateSubresource(pDst, DstSub, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}

void STDMETHODCALLTYPE WrappedContext::CopyStructureCount(ID3D11Buffer* pDstBuffer, UINT DstAlignedByteOffset, ID3D11UnorderedAccessView* pSrcView)
{
    m_real->CopyStructureCount(pDstBuffer, DstAlignedByteOffset, pSrcView);
}

// ============================================================================
//  Misc (forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::GenerateMips(ID3D11ShaderResourceView* pSRV)
{
    m_real->GenerateMips(pSRV);
}

void STDMETHODCALLTYPE WrappedContext::SetResourceMinLOD(ID3D11Resource* pResource, FLOAT MinLOD)
{
    m_real->SetResourceMinLOD(pResource, MinLOD);
}

FLOAT STDMETHODCALLTYPE WrappedContext::GetResourceMinLOD(ID3D11Resource* pResource)
{
    return m_real->GetResourceMinLOD(pResource);
}

void STDMETHODCALLTYPE WrappedContext::ResolveSubresource(ID3D11Resource* pDst, UINT DstSub, ID3D11Resource* pSrc, UINT SrcSub, DXGI_FORMAT Format)
{
    m_real->ResolveSubresource(pDst, DstSub, pSrc, SrcSub, Format);
}

void STDMETHODCALLTYPE WrappedContext::ExecuteCommandList(ID3D11CommandList* pCL, BOOL RestoreContextState)
{
    m_real->ExecuteCommandList(pCL, RestoreContextState);
}

void STDMETHODCALLTYPE WrappedContext::Begin(ID3D11Asynchronous* pAsync)
{
    m_real->Begin(pAsync);
}

void STDMETHODCALLTYPE WrappedContext::End(ID3D11Asynchronous* pAsync)
{
    m_real->End(pAsync);
}

HRESULT STDMETHODCALLTYPE WrappedContext::GetData(ID3D11Asynchronous* pAsync, void* pData, UINT DataSize, UINT GetDataFlags)
{
    return m_real->GetData(pAsync, pData, DataSize, GetDataFlags);
}

void STDMETHODCALLTYPE WrappedContext::SetPredication(ID3D11Predicate* pPredicate, BOOL PredicateValue)
{
    m_real->SetPredication(pPredicate, PredicateValue);
}

// ============================================================================
//  Get state (all forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs)
{
    m_real->VSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs)
{
    m_real->PSGetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::PSGetShader(ID3D11PixelShader** ppPS, ID3D11ClassInstance** ppCI, UINT* pNumCI)
{
    m_real->PSGetShader(ppPS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE WrappedContext::PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
    m_real->PSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::VSGetShader(ID3D11VertexShader** ppVS, ID3D11ClassInstance** ppCI, UINT* pNumCI)
{
    m_real->VSGetShader(ppVS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE WrappedContext::PSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs)
{
    m_real->PSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::IAGetInputLayout(ID3D11InputLayout** ppInputLayout)
{
    m_real->IAGetInputLayout(ppInputLayout);
}

void STDMETHODCALLTYPE WrappedContext::IAGetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppVBs, UINT* pStrides, UINT* pOffsets)
{
    m_real->IAGetVertexBuffers(StartSlot, NumBuffers, ppVBs, pStrides, pOffsets);
}

void STDMETHODCALLTYPE WrappedContext::IAGetIndexBuffer(ID3D11Buffer** pIB, DXGI_FORMAT* Format, UINT* Offset)
{
    m_real->IAGetIndexBuffer(pIB, Format, Offset);
}

void STDMETHODCALLTYPE WrappedContext::GSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs)
{
    m_real->GSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::GSGetShader(ID3D11GeometryShader** ppGS, ID3D11ClassInstance** ppCI, UINT* pNumCI)
{
    m_real->GSGetShader(ppGS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE WrappedContext::IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* pTopology)
{
    m_real->IAGetPrimitiveTopology(pTopology);
}

void STDMETHODCALLTYPE WrappedContext::VSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs)
{
    m_real->VSGetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::VSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
    m_real->VSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::GetPredication(ID3D11Predicate** ppPredicate, BOOL* pPredicateValue)
{
    m_real->GetPredication(ppPredicate, pPredicateValue);
}

void STDMETHODCALLTYPE WrappedContext::GSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs)
{
    m_real->GSGetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::GSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
    m_real->GSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::OMGetRenderTargets(UINT NumViews, ID3D11RenderTargetView** ppRTVs, ID3D11DepthStencilView** ppDSV)
{
    m_real->OMGetRenderTargets(NumViews, ppRTVs, ppDSV);
}

void STDMETHODCALLTYPE WrappedContext::OMGetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView** ppRTVs, ID3D11DepthStencilView** ppDSV, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView** ppUAVs)
{
    m_real->OMGetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRTVs, ppDSV, UAVStartSlot, NumUAVs, ppUAVs);
}

void STDMETHODCALLTYPE WrappedContext::OMGetBlendState(ID3D11BlendState** ppBS, FLOAT BlendFactor[4], UINT* pSampleMask)
{
    m_real->OMGetBlendState(ppBS, BlendFactor, pSampleMask);
}

void STDMETHODCALLTYPE WrappedContext::OMGetDepthStencilState(ID3D11DepthStencilState** ppDSS, UINT* pStencilRef)
{
    m_real->OMGetDepthStencilState(ppDSS, pStencilRef);
}

void STDMETHODCALLTYPE WrappedContext::SOGetTargets(UINT NumBuffers, ID3D11Buffer** ppSOTargets)
{
    m_real->SOGetTargets(NumBuffers, ppSOTargets);
}

void STDMETHODCALLTYPE WrappedContext::RSGetState(ID3D11RasterizerState** ppRS)
{
    m_real->RSGetState(ppRS);
}

void STDMETHODCALLTYPE WrappedContext::RSGetViewports(UINT* pNumViewports, D3D11_VIEWPORT* pViewports)
{
    m_real->RSGetViewports(pNumViewports, pViewports);
}

void STDMETHODCALLTYPE WrappedContext::RSGetScissorRects(UINT* pNumRects, D3D11_RECT* pRects)
{
    m_real->RSGetScissorRects(pNumRects, pRects);
}

void STDMETHODCALLTYPE WrappedContext::HSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs)
{
    m_real->HSGetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::HSGetShader(ID3D11HullShader** ppHS, ID3D11ClassInstance** ppCI, UINT* pNumCI)
{
    m_real->HSGetShader(ppHS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE WrappedContext::HSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
    m_real->HSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::HSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs)
{
    m_real->HSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::DSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs)
{
    m_real->DSGetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::DSGetShader(ID3D11DomainShader** ppDS, ID3D11ClassInstance** ppCI, UINT* pNumCI)
{
    m_real->DSGetShader(ppDS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE WrappedContext::DSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
    m_real->DSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::DSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs)
{
    m_real->DSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

void STDMETHODCALLTYPE WrappedContext::CSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs)
{
    m_real->CSGetShaderResources(StartSlot, NumViews, ppSRVs);
}

void STDMETHODCALLTYPE WrappedContext::CSGetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView** ppUAVs)
{
    m_real->CSGetUnorderedAccessViews(StartSlot, NumUAVs, ppUAVs);
}

void STDMETHODCALLTYPE WrappedContext::CSGetShader(ID3D11ComputeShader** ppCS, ID3D11ClassInstance** ppCI, UINT* pNumCI)
{
    m_real->CSGetShader(ppCS, ppCI, pNumCI);
}

void STDMETHODCALLTYPE WrappedContext::CSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers)
{
    m_real->CSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}

void STDMETHODCALLTYPE WrappedContext::CSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs)
{
    m_real->CSGetConstantBuffers(StartSlot, NumBuffers, ppCBs);
}

// ============================================================================
//  Global state (forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::ClearState()
{
    if (PG_IsSafeMode()) { m_real->ClearState(); return; }

    ResetStateCache();
    m_currentPS = nullptr;
    m_currentVS = nullptr;
    CBDirtyTracker::Get().OnClearState();
    OcclusionCuller::Get().OnClearState();
    m_real->ClearState();
}

void STDMETHODCALLTYPE WrappedContext::Flush()
{
    m_real->Flush();
}

D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE WrappedContext::GetType()
{
    return m_real->GetType();
}

UINT STDMETHODCALLTYPE WrappedContext::GetContextFlags()
{
    return m_real->GetContextFlags();
}

HRESULT STDMETHODCALLTYPE WrappedContext::FinishCommandList(BOOL RestoreDeferredContextState, ID3D11CommandList** ppCL)
{
    return m_real->FinishCommandList(RestoreDeferredContextState, ppCL);
}

} // namespace SB::Proxy
