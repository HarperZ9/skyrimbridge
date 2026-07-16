#pragma once
//=============================================================================
//  D3D11StateBackup — Complete D3D11 pipeline state save/restore
//
//  Used by PhaseDispatcher (mid-frame effect dispatch) and DoOverlayWork
//  (Present-time overlay) to safely save and restore the full D3D11 pipeline
//  state around custom rendering operations.
//
//  The game relies on D3D11 state persisting between API calls.  Any custom
//  rendering (compute dispatch, fullscreen PS, etc.) must save state before
//  and restore it after to avoid corrupting subsequent game rendering.
//=============================================================================

#include <d3d11.h>
#include <cstring>

namespace SB
{

struct D3D11StateBackup
{
    // Output Merger
    static constexpr UINT kMaxRTVs = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
    ID3D11RenderTargetView*  rtvs[kMaxRTVs] = {};
    ID3D11DepthStencilView*  dsv = nullptr;
    ID3D11BlendState*        blendState = nullptr;
    FLOAT                    blendFactor[4] = {};
    UINT                     sampleMask = 0;
    ID3D11DepthStencilState* depthStencilState = nullptr;
    UINT                     stencilRef = 0;

    // Rasterizer
    ID3D11RasterizerState*   rasterizerState = nullptr;
    UINT                     numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT           viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT                     numScissorRects = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_RECT               scissorRects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};

    // Input Assembler
    ID3D11InputLayout*       inputLayout = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer*            indexBuffer = nullptr;
    DXGI_FORMAT              indexFormat = DXGI_FORMAT_UNKNOWN;
    UINT                     indexOffset = 0;

    // Vertex Shader
    ID3D11VertexShader*      vs = nullptr;

    // Pixel Shader + resources
    static constexpr UINT kMaxPSSRVs = 20;
    static constexpr UINT kMaxPSCBs  = 8;
    ID3D11PixelShader*       ps = nullptr;
    ID3D11ShaderResourceView* psSRVs[kMaxPSSRVs] = {};
    ID3D11SamplerState*      psSamplers[4] = {};
    ID3D11Buffer*            psCBs[kMaxPSCBs] = {};

    // Vertex Shader + resources
    static constexpr UINT kMaxVSCBs = 4;
    ID3D11Buffer*            vsCBs[kMaxVSCBs] = {};

    // Compute Shader
    static constexpr UINT kMaxCSUAVs = 4;
    static constexpr UINT kMaxCSSRVs = 4;
    ID3D11ComputeShader*     cs = nullptr;
    ID3D11UnorderedAccessView* csUAVs[kMaxCSUAVs] = {};
    ID3D11ShaderResourceView*  csSRVs[kMaxCSSRVs] = {};

    void Save(ID3D11DeviceContext* ctx)
    {
        ctx->OMGetRenderTargets(kMaxRTVs, rtvs, &dsv);
        ctx->OMGetBlendState(&blendState, blendFactor, &sampleMask);
        ctx->OMGetDepthStencilState(&depthStencilState, &stencilRef);

        ctx->RSGetState(&rasterizerState);
        ctx->RSGetViewports(&numViewports, viewports);
        ctx->RSGetScissorRects(&numScissorRects, scissorRects);

        ctx->IAGetInputLayout(&inputLayout);
        ctx->IAGetPrimitiveTopology(&topology);
        ctx->IAGetIndexBuffer(&indexBuffer, &indexFormat, &indexOffset);

        ctx->VSGetShader(&vs, nullptr, nullptr);

        ctx->PSGetShader(&ps, nullptr, nullptr);
        ctx->PSGetShaderResources(0, kMaxPSSRVs, psSRVs);
        ctx->PSGetSamplers(0, 4, psSamplers);
        ctx->PSGetConstantBuffers(0, kMaxPSCBs, psCBs);

        ctx->VSGetConstantBuffers(0, kMaxVSCBs, vsCBs);

        ctx->CSGetShader(&cs, nullptr, nullptr);
        ctx->CSGetUnorderedAccessViews(0, kMaxCSUAVs, csUAVs);
        ctx->CSGetShaderResources(0, kMaxCSSRVs, csSRVs);
    }

    void Restore(ID3D11DeviceContext* ctx)
    {
        ctx->OMSetRenderTargets(kMaxRTVs, rtvs, dsv);
        ctx->OMSetBlendState(blendState, blendFactor, sampleMask);
        ctx->OMSetDepthStencilState(depthStencilState, stencilRef);

        ctx->RSSetState(rasterizerState);
        ctx->RSSetViewports(numViewports, viewports);
        ctx->RSSetScissorRects(numScissorRects, scissorRects);

        ctx->IASetInputLayout(inputLayout);
        ctx->IASetPrimitiveTopology(topology);
        ctx->IASetIndexBuffer(indexBuffer, indexFormat, indexOffset);

        ctx->VSSetShader(vs, nullptr, 0);

        ctx->PSSetShader(ps, nullptr, 0);
        ctx->PSSetShaderResources(0, kMaxPSSRVs, psSRVs);
        ctx->PSSetSamplers(0, 4, psSamplers);
        ctx->PSSetConstantBuffers(0, kMaxPSCBs, psCBs);

        ctx->VSSetConstantBuffers(0, kMaxVSCBs, vsCBs);

        ctx->CSSetShader(cs, nullptr, 0);
        ctx->CSSetUnorderedAccessViews(0, kMaxCSUAVs, csUAVs, nullptr);
        ctx->CSSetShaderResources(0, kMaxCSSRVs, csSRVs);

        Release();
    }

    void Release()
    {
        for (UINT i = 0; i < kMaxRTVs; ++i)
            if (rtvs[i]) { rtvs[i]->Release(); rtvs[i] = nullptr; }
        if (dsv) { dsv->Release(); dsv = nullptr; }
        if (blendState) { blendState->Release(); blendState = nullptr; }
        if (depthStencilState) { depthStencilState->Release(); depthStencilState = nullptr; }
        if (rasterizerState) { rasterizerState->Release(); rasterizerState = nullptr; }
        if (inputLayout) { inputLayout->Release(); inputLayout = nullptr; }
        if (indexBuffer) { indexBuffer->Release(); indexBuffer = nullptr; }
        if (vs) { vs->Release(); vs = nullptr; }
        if (ps) { ps->Release(); ps = nullptr; }
        for (UINT i = 0; i < kMaxPSSRVs; ++i)
            if (psSRVs[i]) { psSRVs[i]->Release(); psSRVs[i] = nullptr; }
        for (UINT i = 0; i < 4; ++i)
            if (psSamplers[i]) { psSamplers[i]->Release(); psSamplers[i] = nullptr; }
        for (UINT i = 0; i < kMaxPSCBs; ++i)
            if (psCBs[i]) { psCBs[i]->Release(); psCBs[i] = nullptr; }
        for (UINT i = 0; i < kMaxVSCBs; ++i)
            if (vsCBs[i]) { vsCBs[i]->Release(); vsCBs[i] = nullptr; }
        if (cs) { cs->Release(); cs = nullptr; }
        for (UINT i = 0; i < kMaxCSUAVs; ++i)
            if (csUAVs[i]) { csUAVs[i]->Release(); csUAVs[i] = nullptr; }
        for (UINT i = 0; i < kMaxCSSRVs; ++i)
            if (csSRVs[i]) { csSRVs[i]->Release(); csSRVs[i] = nullptr; }
    }
};

} // namespace SB
