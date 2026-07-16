#pragma once
//=============================================================================
//  WrappedContext — ID3D11DeviceContext wrapper
//
//  Hooks key methods for draw call tracking, RT redirection, shader tracking.
//  All other methods forward directly to the real context.
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <cstring>

namespace SB::Proxy
{

class WrappedContext : public ID3D11DeviceContext
{
public:
    WrappedContext(ID3D11DeviceContext* real);
    ~WrappedContext();

    // Global singleton — only one immediate context exists
    static WrappedContext* s_instance;

    ID3D11DeviceContext* GetReal() const { return m_real; }

    // Set by WrappedDevice after construction so GetDevice() returns the wrapper
    void SetWrappedDevice(ID3D11Device* wrappedDev) { m_wrappedDevice = wrappedDev; }

    // Per-frame stat reset (called from WrappedSwapChain::Present)
    void ResetFrameStats();

    // Invalidate state cache — must be called after external code modifies
    // D3D11 state through the real (unwrapped) context
    void ResetStateCache();

    // Frame statistics
    uint32_t GetDrawCalls()     const { return m_drawCalls; }
    uint32_t GetRTSwitches()    const { return m_rtSwitches; }
    uint32_t GetShaderChanges() const { return m_shaderChanges; }

    // State cache optimization statistics (per-frame, reset in ResetFrameStats)
    uint32_t GetRedundantSRVCalls()   const { return m_redundantSRV; }
    uint32_t GetRedundantBlendCalls() const { return m_redundantBlend; }
    uint32_t GetRedundantDSCalls()    const { return m_redundantDS; }
    uint32_t GetRedundantRSCalls()    const { return m_redundantRS; }
    uint32_t GetTotalSRVCalls()       const { return m_totalSRV; }
    uint32_t GetTotalBlendCalls()     const { return m_totalBlend; }
    uint32_t GetTotalDSCalls()        const { return m_totalDS; }
    uint32_t GetTotalRSCalls()        const { return m_totalRS; }

    // ── IUnknown ─────────────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG   STDMETHODCALLTYPE AddRef() override;
    ULONG   STDMETHODCALLTYPE Release() override;

    // ── ID3D11DeviceChild ────────────────────────────────────────────
    void    STDMETHODCALLTYPE GetDevice(ID3D11Device** ppDevice) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override;
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override;

    // ── ID3D11DeviceContext — Shader binding (HOOKED) ────────────────
    void STDMETHODCALLTYPE VSSetShader(ID3D11VertexShader* pVS, ID3D11ClassInstance* const* ppCI, UINT NumCI) override;
    void STDMETHODCALLTYPE PSSetShader(ID3D11PixelShader* pPS, ID3D11ClassInstance* const* ppCI, UINT NumCI) override;
    void STDMETHODCALLTYPE GSSetShader(ID3D11GeometryShader* pShader, ID3D11ClassInstance* const* ppCI, UINT NumCI) override;
    void STDMETHODCALLTYPE HSSetShader(ID3D11HullShader* pShader, ID3D11ClassInstance* const* ppCI, UINT NumCI) override;
    void STDMETHODCALLTYPE DSSetShader(ID3D11DomainShader* pShader, ID3D11ClassInstance* const* ppCI, UINT NumCI) override;
    void STDMETHODCALLTYPE CSSetShader(ID3D11ComputeShader* pShader, ID3D11ClassInstance* const* ppCI, UINT NumCI) override;

    // ── ID3D11DeviceContext — Draw calls (HOOKED) ────────────────────
    void STDMETHODCALLTYPE Draw(UINT VertexCount, UINT StartVertexLocation) override;
    void STDMETHODCALLTYPE DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) override;
    void STDMETHODCALLTYPE DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) override;
    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) override;
    void STDMETHODCALLTYPE DrawAuto() override;
    void STDMETHODCALLTYPE DrawIndexedInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) override;
    void STDMETHODCALLTYPE DrawInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) override;

    // ── ID3D11DeviceContext — Output merger (HOOKED) ─────────────────
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView* const* ppRTVs, ID3D11DepthStencilView* pDSV) override;
    void STDMETHODCALLTYPE OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView* const* ppRTVs, ID3D11DepthStencilView* pDSV, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUAVs, const UINT* pUAVInitialCounts) override;
    void STDMETHODCALLTYPE OMSetBlendState(ID3D11BlendState* pBS, const FLOAT BlendFactor[4], UINT SampleMask) override;
    void STDMETHODCALLTYPE OMSetDepthStencilState(ID3D11DepthStencilState* pDSS, UINT StencilRef) override;

    // ── ID3D11DeviceContext — Clear (HOOKED for phase detection) ─────
    void STDMETHODCALLTYPE ClearRenderTargetView(ID3D11RenderTargetView* pRTV, const FLOAT ColorRGBA[4]) override;
    void STDMETHODCALLTYPE ClearDepthStencilView(ID3D11DepthStencilView* pDSV, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) override;
    void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* pUAV, const UINT Values[4]) override;
    void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView* pUAV, const FLOAT Values[4]) override;

    // ── ID3D11DeviceContext — Resource binding (HOOKED for injection) ─
    void STDMETHODCALLTYPE PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs) override;
    void STDMETHODCALLTYPE PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs) override;
    void STDMETHODCALLTYPE PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override;

    // ── ID3D11DeviceContext — Map/Unmap (HOOKED for CB snooping) ─────
    HRESULT STDMETHODCALLTYPE Map(ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource) override;
    void    STDMETHODCALLTYPE Unmap(ID3D11Resource* pResource, UINT Subresource) override;

    // ── ID3D11DeviceContext — Compute (forwarded) ────────────────────
    void STDMETHODCALLTYPE Dispatch(UINT X, UINT Y, UINT Z) override;
    void STDMETHODCALLTYPE DispatchIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) override;
    void STDMETHODCALLTYPE CSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs) override;
    void STDMETHODCALLTYPE CSSetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUAVs, const UINT* pUAVInitialCounts) override;
    void STDMETHODCALLTYPE CSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE CSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs) override;

    // ── ID3D11DeviceContext — Input assembler (forwarded) ────────────
    void STDMETHODCALLTYPE IASetInputLayout(ID3D11InputLayout* pInputLayout) override;
    void STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppVBs, const UINT* pStrides, const UINT* pOffsets) override;
    void STDMETHODCALLTYPE IASetIndexBuffer(ID3D11Buffer* pIB, DXGI_FORMAT Format, UINT Offset) override;
    void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology) override;

    // ── ID3D11DeviceContext — VS/GS/HS/DS binding (forwarded) ────────
    void STDMETHODCALLTYPE VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs) override;
    void STDMETHODCALLTYPE VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs) override;
    void STDMETHODCALLTYPE VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs) override;
    void STDMETHODCALLTYPE GSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs) override;
    void STDMETHODCALLTYPE GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE HSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs) override;
    void STDMETHODCALLTYPE HSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE HSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs) override;
    void STDMETHODCALLTYPE DSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppSRVs) override;
    void STDMETHODCALLTYPE DSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE DSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppCBs) override;

    // ── ID3D11DeviceContext — Rasterizer / Stream Output ─────────────
    void STDMETHODCALLTYPE RSSetState(ID3D11RasterizerState* pRS) override;
    void STDMETHODCALLTYPE RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports) override;
    void STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects, const D3D11_RECT* pRects) override;
    void STDMETHODCALLTYPE SOSetTargets(UINT NumBuffers, ID3D11Buffer* const* ppSOTargets, const UINT* pOffsets) override;

    // ── ID3D11DeviceContext — Copy/Update ────────────────────────────
    void STDMETHODCALLTYPE CopySubresourceRegion(ID3D11Resource* pDst, UINT DstSub, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource* pSrc, UINT SrcSub, const D3D11_BOX* pSrcBox) override;
    void STDMETHODCALLTYPE CopyResource(ID3D11Resource* pDst, ID3D11Resource* pSrc) override;
    void STDMETHODCALLTYPE UpdateSubresource(ID3D11Resource* pDst, UINT DstSub, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) override;
    void STDMETHODCALLTYPE CopyStructureCount(ID3D11Buffer* pDstBuffer, UINT DstAlignedByteOffset, ID3D11UnorderedAccessView* pSrcView) override;

    // ── ID3D11DeviceContext — Misc ───────────────────────────────────
    void    STDMETHODCALLTYPE GenerateMips(ID3D11ShaderResourceView* pSRV) override;
    void    STDMETHODCALLTYPE SetResourceMinLOD(ID3D11Resource* pResource, FLOAT MinLOD) override;
    FLOAT   STDMETHODCALLTYPE GetResourceMinLOD(ID3D11Resource* pResource) override;
    void    STDMETHODCALLTYPE ResolveSubresource(ID3D11Resource* pDst, UINT DstSub, ID3D11Resource* pSrc, UINT SrcSub, DXGI_FORMAT Format) override;
    void    STDMETHODCALLTYPE ExecuteCommandList(ID3D11CommandList* pCL, BOOL RestoreContextState) override;
    void    STDMETHODCALLTYPE Begin(ID3D11Asynchronous* pAsync) override;
    void    STDMETHODCALLTYPE End(ID3D11Asynchronous* pAsync) override;
    HRESULT STDMETHODCALLTYPE GetData(ID3D11Asynchronous* pAsync, void* pData, UINT DataSize, UINT GetDataFlags) override;
    void    STDMETHODCALLTYPE SetPredication(ID3D11Predicate* pPredicate, BOOL PredicateValue) override;

    // ── ID3D11DeviceContext — Get state (all forwarded) ──────────────
    void STDMETHODCALLTYPE VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs) override;
    void STDMETHODCALLTYPE PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs) override;
    void STDMETHODCALLTYPE PSGetShader(ID3D11PixelShader** ppPS, ID3D11ClassInstance** ppCI, UINT* pNumCI) override;
    void STDMETHODCALLTYPE PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override;
    void STDMETHODCALLTYPE VSGetShader(ID3D11VertexShader** ppVS, ID3D11ClassInstance** ppCI, UINT* pNumCI) override;
    void STDMETHODCALLTYPE PSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs) override;
    void STDMETHODCALLTYPE IAGetInputLayout(ID3D11InputLayout** ppInputLayout) override;
    void STDMETHODCALLTYPE IAGetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppVBs, UINT* pStrides, UINT* pOffsets) override;
    void STDMETHODCALLTYPE IAGetIndexBuffer(ID3D11Buffer** pIB, DXGI_FORMAT* Format, UINT* Offset) override;
    void STDMETHODCALLTYPE GSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs) override;
    void STDMETHODCALLTYPE GSGetShader(ID3D11GeometryShader** ppGS, ID3D11ClassInstance** ppCI, UINT* pNumCI) override;
    void STDMETHODCALLTYPE IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* pTopology) override;
    void STDMETHODCALLTYPE VSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs) override;
    void STDMETHODCALLTYPE VSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override;
    void STDMETHODCALLTYPE GetPredication(ID3D11Predicate** ppPredicate, BOOL* pPredicateValue) override;
    void STDMETHODCALLTYPE GSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs) override;
    void STDMETHODCALLTYPE GSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override;
    void STDMETHODCALLTYPE OMGetRenderTargets(UINT NumViews, ID3D11RenderTargetView** ppRTVs, ID3D11DepthStencilView** ppDSV) override;
    void STDMETHODCALLTYPE OMGetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView** ppRTVs, ID3D11DepthStencilView** ppDSV, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView** ppUAVs) override;
    void STDMETHODCALLTYPE OMGetBlendState(ID3D11BlendState** ppBS, FLOAT BlendFactor[4], UINT* pSampleMask) override;
    void STDMETHODCALLTYPE OMGetDepthStencilState(ID3D11DepthStencilState** ppDSS, UINT* pStencilRef) override;
    void STDMETHODCALLTYPE SOGetTargets(UINT NumBuffers, ID3D11Buffer** ppSOTargets) override;
    void STDMETHODCALLTYPE RSGetState(ID3D11RasterizerState** ppRS) override;
    void STDMETHODCALLTYPE RSGetViewports(UINT* pNumViewports, D3D11_VIEWPORT* pViewports) override;
    void STDMETHODCALLTYPE RSGetScissorRects(UINT* pNumRects, D3D11_RECT* pRects) override;
    void STDMETHODCALLTYPE HSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs) override;
    void STDMETHODCALLTYPE HSGetShader(ID3D11HullShader** ppHS, ID3D11ClassInstance** ppCI, UINT* pNumCI) override;
    void STDMETHODCALLTYPE HSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override;
    void STDMETHODCALLTYPE HSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs) override;
    void STDMETHODCALLTYPE DSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs) override;
    void STDMETHODCALLTYPE DSGetShader(ID3D11DomainShader** ppDS, ID3D11ClassInstance** ppCI, UINT* pNumCI) override;
    void STDMETHODCALLTYPE DSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override;
    void STDMETHODCALLTYPE DSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs) override;
    void STDMETHODCALLTYPE CSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppSRVs) override;
    void STDMETHODCALLTYPE CSGetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView** ppUAVs) override;
    void STDMETHODCALLTYPE CSGetShader(ID3D11ComputeShader** ppCS, ID3D11ClassInstance** ppCI, UINT* pNumCI) override;
    void STDMETHODCALLTYPE CSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override;
    void STDMETHODCALLTYPE CSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppCBs) override;

    // ── ID3D11DeviceContext — Global state ───────────────────────────
    void    STDMETHODCALLTYPE ClearState() override;
    void    STDMETHODCALLTYPE Flush() override;
    D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE GetType() override;
    UINT    STDMETHODCALLTYPE GetContextFlags() override;
    HRESULT STDMETHODCALLTYPE FinishCommandList(BOOL RestoreDeferredContextState, ID3D11CommandList** ppCL) override;

private:
    ID3D11DeviceContext* m_real;
    ID3D11Device*        m_wrappedDevice = nullptr;  // back-pointer to WrappedDevice
    ULONG                m_refCount = 1;

    // Frame statistics
    uint32_t m_drawCalls     = 0;
    uint32_t m_rtSwitches    = 0;
    uint32_t m_shaderChanges = 0;

    // Current state tracking
    ID3D11PixelShader*  m_currentPS = nullptr;
    ID3D11VertexShader* m_currentVS = nullptr;

    // ── State cache for redundancy filtering ────────────────────────────
    // Tracks last-set state objects to skip redundant D3D11 API calls.
    // Safe because Skyrim uses a single immediate context and SKSE systems
    // use the real context (separate path) for non-overlapping resource slots.
    static constexpr uint32_t kMaxPSSRVSlots    = 16;
    static constexpr uint32_t kMaxPSSamplerSlots = 16;

    ID3D11BlendState*          m_cachedBlendState   = nullptr;
    FLOAT                      m_cachedBlendFactor[4] = {1.f, 1.f, 1.f, 1.f};
    UINT                       m_cachedSampleMask   = 0xFFFFFFFF;
    ID3D11DepthStencilState*   m_cachedDSState      = nullptr;
    UINT                       m_cachedStencilRef   = 0;
    ID3D11RasterizerState*     m_cachedRSState      = nullptr;
    ID3D11ShaderResourceView*  m_cachedPSSRVs[kMaxPSSRVSlots] = {};
    ID3D11SamplerState*        m_cachedPSSamplers[kMaxPSSamplerSlots] = {};

    // Per-frame redundancy counters
    uint32_t m_redundantSRV   = 0;
    uint32_t m_redundantBlend = 0;
    uint32_t m_redundantDS    = 0;
    uint32_t m_redundantRS    = 0;
    uint32_t m_totalSRV       = 0;
    uint32_t m_totalBlend     = 0;
    uint32_t m_totalDS        = 0;
    uint32_t m_totalRS        = 0;
};

} // namespace SB::Proxy
