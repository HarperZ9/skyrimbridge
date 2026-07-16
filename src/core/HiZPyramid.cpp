#include "HiZPyramid.h"
#include "D3D11Hook.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <cstring>
#include <cmath>
#include <algorithm>

#include <SKSE/SKSE.h>

// ── Embedded HLSL compute shaders ─────────────────────────────────────────

// Mip 0: Copy depth buffer to pyramid (1:1)
// Skyrim SE uses STANDARD depth (near=0, far=1).
// All effect shaders expect REVERSED-Z (near=1, far=0).
// We convert here so every downstream consumer gets reversed-Z.
static constexpr const char* kCopyCS = R"(
Texture2D<float> SrcDepth : register(t0);
RWTexture2D<float> DstMip : register(u0);

cbuffer HiZParams : register(b0)
{
    uint2 DstDimensions;
    uint2 Padding;
};

[numthreads(8, 8, 1)]
void CSCopy(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDimensions.x || DTid.y >= DstDimensions.y)
        return;
    // Convert standard depth (near=0, far=1) to reversed-Z (near=1, far=0)
    DstMip[DTid.xy] = 1.0 - SrcDepth[DTid.xy];
}
)";

// Mip 1+: Downsample 2×2 using MAX (reversed-Z: max = closest)
static constexpr const char* kDownsampleCS = R"(
Texture2D<float> SrcMip : register(t0);
RWTexture2D<float> DstMip : register(u0);

cbuffer HiZParams : register(b0)
{
    uint2 DstDimensions;
    uint2 Padding;
};

[numthreads(8, 8, 1)]
void CSDownsample(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDimensions.x || DTid.y >= DstDimensions.y)
        return;

    uint2 srcCoord = DTid.xy * 2;

    float d00 = SrcMip[srcCoord + uint2(0, 0)];
    float d10 = SrcMip[srcCoord + uint2(1, 0)];
    float d01 = SrcMip[srcCoord + uint2(0, 1)];
    float d11 = SrcMip[srcCoord + uint2(1, 1)];

    // Skyrim uses reversed-Z: near=1.0, far=0.0
    // MAX = closest surface (conservative occlusion)
    DstMip[DTid.xy] = max(max(d00, d10), max(d01, d11));
}
)";

namespace SB
{
    // ── Initialization ────────────────────────────────────────────────────

    bool HiZPyramid::Initialize(ID3D11Device* a_device, IDXGISwapChain* a_swapChain)
    {
        if (m_initialized) return true;
        if (!a_device || !a_swapChain) return false;

        m_device = a_device;

        // Get backbuffer dimensions (pyramid matches screen resolution)
        ID3D11Texture2D* backBuffer = nullptr;
        HRESULT hr = a_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (FAILED(hr) || !backBuffer) {
            SKSE::log::error("HiZPyramid: failed to get backbuffer");
            return false;
        }

        D3D11_TEXTURE2D_DESC bbDesc;
        backBuffer->GetDesc(&bbDesc);
        backBuffer->Release();

        if (!CompileComputeShaders(a_device)) return false;
        if (!CreatePyramidTexture(a_device, bbDesc.Width, bbDesc.Height)) return false;

        m_initialized = true;
        SKSE::log::info("HiZPyramid: initialized ({}x{}, {} mips, SRV at t{})",
            m_width, m_height, m_mipCount, kSRVSlot);
        return true;
    }

    bool HiZPyramid::CompileComputeShaders(ID3D11Device* a_device)
    {
        ID3DBlob* blob = nullptr;
        ID3DBlob* errBlob = nullptr;
        HRESULT hr;

        // Compile copy CS
        hr = D3DCompile(kCopyCS, std::strlen(kCopyCS), "SB_HiZCopyCS",
            nullptr, nullptr, "CSCopy", "cs_5_0", 0, 0, &blob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) {
                SKSE::log::error("HiZPyramid: copy CS compile failed: {}",
                    static_cast<const char*>(errBlob->GetBufferPointer()));
                errBlob->Release();
            }
            return false;
        }

        hr = a_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
            nullptr, &m_copyCS);
        blob->Release();
        if (FAILED(hr)) return false;

        // Compile downsample CS
        hr = D3DCompile(kDownsampleCS, std::strlen(kDownsampleCS), "SB_HiZDownsampleCS",
            nullptr, nullptr, "CSDownsample", "cs_5_0", 0, 0, &blob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) {
                SKSE::log::error("HiZPyramid: downsample CS compile failed: {}",
                    static_cast<const char*>(errBlob->GetBufferPointer()));
                errBlob->Release();
            }
            return false;
        }

        hr = a_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
            nullptr, &m_downsampleCS);
        blob->Release();
        if (FAILED(hr)) return false;

        return true;
    }

    bool HiZPyramid::CreatePyramidTexture(ID3D11Device* a_device, uint32_t w, uint32_t h)
    {
        m_width = w;
        m_height = h;
        m_mipCount = static_cast<uint32_t>(
            std::floor(std::log2(static_cast<float>((std::max)(w, h))))) + 1;
        if (m_mipCount > kMaxMips) m_mipCount = kMaxMips;

        HRESULT hr;

        // Pyramid texture
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = m_mipCount;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = a_device->CreateTexture2D(&desc, nullptr, &m_pyramidTex);
        if (FAILED(hr)) {
            SKSE::log::error("HiZPyramid: CreateTexture2D failed");
            return false;
        }

        // Full-chain SRV
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = m_mipCount;
        hr = a_device->CreateShaderResourceView(m_pyramidTex, &srvDesc, &m_pyramidSRV);
        if (FAILED(hr)) return false;

        // Per-mip UAVs and SRVs
        for (uint32_t i = 0; i < m_mipCount; i++) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = i;
            hr = a_device->CreateUnorderedAccessView(m_pyramidTex, &uavDesc, &m_mipUAV[i]);
            if (FAILED(hr)) return false;

            D3D11_SHADER_RESOURCE_VIEW_DESC mipSrvDesc{};
            mipSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
            mipSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            mipSrvDesc.Texture2D.MostDetailedMip = i;
            mipSrvDesc.Texture2D.MipLevels = 1;
            hr = a_device->CreateShaderResourceView(m_pyramidTex, &mipSrvDesc, &m_mipSRV[i]);
            if (FAILED(hr)) return false;
        }

        // Params constant buffer (16 bytes: uint2 + uint2 padding)
        D3D11_BUFFER_DESC cbDesc{};
        cbDesc.ByteWidth = 16;
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = a_device->CreateBuffer(&cbDesc, nullptr, &m_paramCB);
        if (FAILED(hr)) return false;

        return true;
    }

    // ── Depth Buffer Acquisition ──────────────────────────────────────────

    bool HiZPyramid::AcquireDepthSRV(ID3D11DeviceContext* a_ctx)
    {
        // Release previous
        if (m_depthSRV && m_ownDepthSRV) {
            m_depthSRV->Release();
            m_depthSRV = nullptr;
        }
        m_ownDepthSRV = false;

        // Get currently bound depth-stencil view
        ID3D11DepthStencilView* dsv = nullptr;
        a_ctx->OMGetRenderTargets(0, nullptr, &dsv);
        if (!dsv) return false;

        ID3D11Resource* depthRes = nullptr;
        dsv->GetResource(&depthRes);
        dsv->Release();
        if (!depthRes) return false;

        ID3D11Texture2D* depthTex = nullptr;
        depthRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&depthTex);
        depthRes->Release();
        if (!depthTex) return false;

        D3D11_TEXTURE2D_DESC depthDesc;
        depthTex->GetDesc(&depthDesc);

        // Determine SRV format from depth format
        DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
        if (depthDesc.Format == DXGI_FORMAT_R24G8_TYPELESS ||
            depthDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT)
            srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        else if (depthDesc.Format == DXGI_FORMAT_R32G8X24_TYPELESS ||
                 depthDesc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
            srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        else if (depthDesc.Format == DXGI_FORMAT_R32_TYPELESS ||
                 depthDesc.Format == DXGI_FORMAT_D32_FLOAT)
            srvFormat = DXGI_FORMAT_R32_FLOAT;
        else if (depthDesc.Format == DXGI_FORMAT_R16_TYPELESS ||
                 depthDesc.Format == DXGI_FORMAT_D16_UNORM)
            srvFormat = DXGI_FORMAT_R16_UNORM;

        if (srvFormat != DXGI_FORMAT_UNKNOWN &&
            (depthDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = srvFormat;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            m_device->CreateShaderResourceView(depthTex, &srvDesc, &m_depthSRV);
            m_ownDepthSRV = true;
        }

        depthTex->Release();
        return m_depthSRV != nullptr;
    }

    // ── Per-Frame Build ───────────────────────────────────────────────────

    void HiZPyramid::BuildPyramid(ID3D11DeviceContext* a_ctx)
    {
        if (!m_initialized || !m_enabled || !a_ctx) return;

        // Acquire depth buffer SRV — prefer proxy-captured depth
        if (!AcquireDepthSRV(a_ctx)) {
            // Fallback: use proxy-captured depth (always available during Present)
            auto* proxyDepth = D3D11Hook::GetGameDepthSRV();
            if (!proxyDepth) return;
            m_depthSRV = proxyDepth;
            m_ownDepthSRV = false;  // don't Release proxy's SRV
        }

        // Save existing CS state
        ID3D11ComputeShader* prevCS = nullptr;
        ID3D11ShaderResourceView* prevSRV = nullptr;
        ID3D11UnorderedAccessView* prevUAV = nullptr;
        ID3D11Buffer* prevCB = nullptr;
        a_ctx->CSGetShader(&prevCS, nullptr, nullptr);
        a_ctx->CSGetShaderResources(0, 1, &prevSRV);
        a_ctx->CSGetUnorderedAccessViews(0, 1, &prevUAV);
        a_ctx->CSGetConstantBuffers(0, 1, &prevCB);

        // Mip 0: Copy from depth buffer
        {
            a_ctx->CSSetShader(m_copyCS, nullptr, 0);
            a_ctx->CSSetShaderResources(0, 1, &m_depthSRV);
            a_ctx->CSSetUnorderedAccessViews(0, 1, &m_mipUAV[0], nullptr);

            struct { uint32_t w, h, pad0, pad1; } params = { m_width, m_height, 0, 0 };
            D3D11_MAPPED_SUBRESOURCE mapped;
            a_ctx->Map(m_paramCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            std::memcpy(mapped.pData, &params, sizeof(params));
            a_ctx->Unmap(m_paramCB, 0);
            a_ctx->CSSetConstantBuffers(0, 1, &m_paramCB);

            a_ctx->Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
        }

        // Mip 1..N: Downsample
        a_ctx->CSSetShader(m_downsampleCS, nullptr, 0);

        for (uint32_t mip = 1; mip < m_mipCount; mip++) {
            // Unbind previous UAV before binding its SRV
            ID3D11UnorderedAccessView* nullUAV = nullptr;
            a_ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

            // Bind previous mip as SRV, current mip as UAV
            a_ctx->CSSetShaderResources(0, 1, &m_mipSRV[mip - 1]);
            a_ctx->CSSetUnorderedAccessViews(0, 1, &m_mipUAV[mip], nullptr);

            uint32_t mipW = (std::max)(1u, m_width >> mip);
            uint32_t mipH = (std::max)(1u, m_height >> mip);

            struct { uint32_t w, h, pad0, pad1; } params = { mipW, mipH, 0, 0 };
            D3D11_MAPPED_SUBRESOURCE mapped;
            a_ctx->Map(m_paramCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            std::memcpy(mapped.pData, &params, sizeof(params));
            a_ctx->Unmap(m_paramCB, 0);

            a_ctx->Dispatch((mipW + 7) / 8, (mipH + 7) / 8, 1);
        }

        // Cleanup: unbind all CS resources
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        a_ctx->CSSetShaderResources(0, 1, &nullSRV);
        a_ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        a_ctx->CSSetShader(nullptr, nullptr, 0);
        ID3D11Buffer* nullCB = nullptr;
        a_ctx->CSSetConstantBuffers(0, 1, &nullCB);

        // Restore CS state
        a_ctx->CSSetShader(prevCS, nullptr, 0);
        a_ctx->CSSetShaderResources(0, 1, &prevSRV);
        a_ctx->CSSetUnorderedAccessViews(0, 1, &prevUAV, nullptr);
        a_ctx->CSSetConstantBuffers(0, 1, &prevCB);
        if (prevCS) prevCS->Release();
        if (prevSRV) prevSRV->Release();
        if (prevUAV) prevUAV->Release();
        if (prevCB) prevCB->Release();

        // Release depth SRV (re-acquired next frame)
        if (m_depthSRV && m_ownDepthSRV) {
            m_depthSRV->Release();
            m_depthSRV = nullptr;
            m_ownDepthSRV = false;
        }
    }

    // ── Shutdown ──────────────────────────────────────────────────────────

    void HiZPyramid::Shutdown()
    {
        ReleasePyramidTexture();

        if (m_copyCS)       { m_copyCS->Release();       m_copyCS = nullptr; }
        if (m_downsampleCS) { m_downsampleCS->Release();  m_downsampleCS = nullptr; }
        if (m_depthSRV && m_ownDepthSRV) { m_depthSRV->Release(); m_depthSRV = nullptr; }

        m_initialized = false;
        m_device = nullptr;
    }

    void HiZPyramid::ReleasePyramidTexture()
    {
        if (m_pyramidSRV) { m_pyramidSRV->Release(); m_pyramidSRV = nullptr; }
        if (m_paramCB)    { m_paramCB->Release();    m_paramCB = nullptr; }

        for (uint32_t i = 0; i < kMaxMips; i++) {
            if (m_mipUAV[i]) { m_mipUAV[i]->Release(); m_mipUAV[i] = nullptr; }
            if (m_mipSRV[i]) { m_mipSRV[i]->Release(); m_mipSRV[i] = nullptr; }
        }

        if (m_pyramidTex) { m_pyramidTex->Release(); m_pyramidTex = nullptr; }
    }

} // namespace SB
