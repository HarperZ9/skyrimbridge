#include "ComputeManager.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>

namespace SB
{

ComputeManager& ComputeManager::Get()
{
    static ComputeManager inst;
    return inst;
}

bool ComputeManager::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    if (m_initialized) return true;
    if (!dev || !ctx) return false;

    m_device  = dev;
    m_context = ctx;
    m_initialized = true;

    SKSE::log::info("ComputeManager: initialized");
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Shader compilation
// ═══════════════════════════════════════════════════════════════════════════

ComputeShaderID ComputeManager::CompileShader(const char* name,
                                               const char* hlslSource,
                                               const char* entryPoint,
                                               const char* target)
{
    if (!m_initialized || !hlslSource) return 0;

    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob  = nullptr;

    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(
        hlslSource, strlen(hlslSource),
        name, nullptr, nullptr,
        entryPoint, target, flags, 0,
        &shaderBlob, &errorBlob);

    if (FAILED(hr)) {
        if (errorBlob) {
            SKSE::log::error("ComputeManager: failed to compile '{}': {}",
                name, static_cast<const char*>(errorBlob->GetBufferPointer()));
            errorBlob->Release();
        } else {
            SKSE::log::error("ComputeManager: failed to compile '{}': HRESULT 0x{:08X}", name, hr);
        }
        if (shaderBlob) shaderBlob->Release();
        return 0;
    }
    if (errorBlob) errorBlob->Release();

    ID3D11ComputeShader* cs = nullptr;
    hr = m_device->CreateComputeShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr, &cs);
    shaderBlob->Release();

    if (FAILED(hr) || !cs) {
        SKSE::log::error("ComputeManager: CreateComputeShader failed for '{}'", name);
        return 0;
    }

    ComputeShaderID id = m_nextID++;
    m_shaders.push_back({cs, name ? name : ""});

    SKSE::log::info("ComputeManager: compiled '{}' => id {}", name, id);
    return id;
}

ID3D11ComputeShader* ComputeManager::GetShader(ComputeShaderID id) const
{
    if (id == 0 || id > m_shaders.size()) return nullptr;
    return m_shaders[id - 1].shader;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Resource creation
// ═══════════════════════════════════════════════════════════════════════════

void ComputeManager::TextureResource::Release()
{
    if (srv)     { srv->Release();     srv = nullptr; }
    if (uav)     { uav->Release();     uav = nullptr; }
    if (staging) { staging->Release(); staging = nullptr; }
    if (texture) { texture->Release(); texture = nullptr; }
}

void ComputeManager::BufferResource::Release()
{
    if (srv)     { srv->Release();     srv = nullptr; }
    if (uav)     { uav->Release();     uav = nullptr; }
    if (staging) { staging->Release(); staging = nullptr; }
    if (buffer)  { buffer->Release();  buffer = nullptr; }
}

ComputeManager::TextureResource ComputeManager::CreateTexture2D(
    UINT width, UINT height, DXGI_FORMAT format,
    bool wantSRV, bool wantUAV, UINT mipLevels,
    bool wantStaging, const char* debugName)
{
    TextureResource res{};
    if (!m_initialized) return res;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width      = width;
    desc.Height     = height;
    desc.MipLevels  = mipLevels;
    desc.ArraySize  = 1;
    desc.Format     = format;
    desc.SampleDesc = {1, 0};
    desc.Usage      = D3D11_USAGE_DEFAULT;
    desc.BindFlags  = 0;
    if (wantSRV) desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (wantUAV) desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &res.texture);
    if (FAILED(hr)) {
        SKSE::log::error("ComputeManager: CreateTexture2D failed ({}x{}, fmt={}) HRESULT 0x{:08X}",
            width, height, static_cast<int>(format), hr);
        return res;
    }

    if (wantSRV) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = mipLevels;
        HRESULT srvHr = m_device->CreateShaderResourceView(res.texture, &srvDesc, &res.srv);
        if (FAILED(srvHr)) {
            SKSE::log::error("ComputeManager: CreateSRV failed for texture (0x{:08X})", srvHr);
            res.Release();
            return res;
        }
    }

    if (wantUAV) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = format;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        HRESULT uavHr = m_device->CreateUnorderedAccessView(res.texture, &uavDesc, &res.uav);
        if (FAILED(uavHr)) {
            SKSE::log::error("ComputeManager: CreateUAV failed for texture (0x{:08X})", uavHr);
            res.Release();
            return res;
        }
    }

    if (wantStaging) {
        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage          = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags      = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        HRESULT stgHr = m_device->CreateTexture2D(&stagingDesc, nullptr, &res.staging);
        if (FAILED(stgHr)) {
            SKSE::log::error("ComputeManager: CreateTexture2D staging failed (0x{:08X})", stgHr);
        }
    }

    res.width     = width;
    res.height    = height;
    res.mipLevels = mipLevels;

    if (debugName) {
        SKSE::log::info("ComputeManager: created texture '{}' {}x{} mips={} srv={} uav={}",
            debugName, width, height, mipLevels, wantSRV, wantUAV);
    }

    return res;
}

ID3D11UnorderedAccessView* ComputeManager::CreateMipUAV(ID3D11Texture2D* tex,
                                                         DXGI_FORMAT format,
                                                         UINT mipSlice)
{
    if (!m_initialized || !tex) return nullptr;

    D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = mipSlice;

    ID3D11UnorderedAccessView* uav = nullptr;
    HRESULT hr = m_device->CreateUnorderedAccessView(tex, &desc, &uav);
    if (FAILED(hr)) {
        SKSE::log::error("ComputeManager::CreateMipUAV: failed for mip {} (0x{:08X})", mipSlice, hr);
        return nullptr;
    }
    return uav;
}

ID3D11ShaderResourceView* ComputeManager::CreateMipSRV(ID3D11Texture2D* tex,
                                                        DXGI_FORMAT format,
                                                        UINT mipSlice)
{
    if (!m_initialized || !tex) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MostDetailedMip = mipSlice;
    desc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = m_device->CreateShaderResourceView(tex, &desc, &srv);
    if (FAILED(hr)) {
        SKSE::log::error("ComputeManager::CreateMipSRV: failed for mip {} (0x{:08X})", mipSlice, hr);
        return nullptr;
    }
    return srv;
}

ComputeManager::BufferResource ComputeManager::CreateStructuredBuffer(
    UINT elementCount, UINT elementStride,
    bool wantSRV, bool wantUAV, bool wantStaging,
    const char* debugName)
{
    BufferResource res{};
    if (!m_initialized) return res;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth           = elementCount * elementStride;
    desc.Usage               = D3D11_USAGE_DEFAULT;
    desc.BindFlags           = 0;
    if (wantSRV) desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (wantUAV) desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = elementStride;

    HRESULT hr = m_device->CreateBuffer(&desc, nullptr, &res.buffer);
    if (FAILED(hr)) {
        SKSE::log::error("ComputeManager: CreateStructuredBuffer failed HRESULT 0x{:08X}", hr);
        return res;
    }

    if (wantSRV) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = elementCount;
        HRESULT srvHr = m_device->CreateShaderResourceView(res.buffer, &srvDesc, &res.srv);
        if (FAILED(srvHr)) {
            SKSE::log::error("ComputeManager: CreateSRV failed for buffer (0x{:08X})", srvHr);
            res.Release();
            return res;
        }
    }

    if (wantUAV) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = elementCount;
        HRESULT uavHr = m_device->CreateUnorderedAccessView(res.buffer, &uavDesc, &res.uav);
        if (FAILED(uavHr)) {
            SKSE::log::error("ComputeManager: CreateUAV failed for buffer (0x{:08X})", uavHr);
            res.Release();
            return res;
        }
    }

    if (wantStaging) {
        D3D11_BUFFER_DESC stagingDesc{};
        stagingDesc.ByteWidth      = elementCount * elementStride;
        stagingDesc.Usage          = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        HRESULT stgHr = m_device->CreateBuffer(&stagingDesc, nullptr, &res.staging);
        if (FAILED(stgHr)) {
            SKSE::log::error("ComputeManager: CreateBuffer staging failed (0x{:08X})", stgHr);
        }
    }

    res.elementCount  = elementCount;
    res.elementStride = elementStride;

    if (debugName) {
        SKSE::log::info("ComputeManager: created buffer '{}' {}x{} bytes",
            debugName, elementCount, elementStride);
    }

    return res;
}

ID3D11Buffer* ComputeManager::CreateConstantBuffer(UINT sizeBytes)
{
    if (!m_initialized) return nullptr;

    // CB size must be 16-byte aligned
    sizeBytes = (sizeBytes + 15) & ~15u;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth      = sizeBytes;
    desc.Usage          = D3D11_USAGE_DYNAMIC;
    desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    ID3D11Buffer* cb = nullptr;
    m_device->CreateBuffer(&desc, nullptr, &cb);
    return cb;
}

// ═══════════════════════════════════════════════════════════════════════════
//  CS state save/restore
// ═══════════════════════════════════════════════════════════════════════════

void ComputeManager::SaveCSState()
{
    if (!m_context) return;

    auto& s = m_savedState;
    s.classInstanceCount = 256;
    m_context->CSGetShader(&s.shader, s.classInstances, &s.classInstanceCount);
    m_context->CSGetShaderResources(0, kMaxSavedSRVs, s.srvs);
    m_context->CSGetUnorderedAccessViews(0, kMaxSavedUAVs, s.uavs);
    m_context->CSGetConstantBuffers(0, kMaxSavedCBs, s.cbs);
    m_context->CSGetSamplers(0, kMaxSavedSamplers, s.samplers);
    s.saved = true;
}

void ComputeManager::RestoreCSState()
{
    if (!m_context || !m_savedState.saved) return;

    auto& s = m_savedState;
    m_context->CSSetShader(s.shader, s.classInstances, s.classInstanceCount);
    m_context->CSSetShaderResources(0, kMaxSavedSRVs, s.srvs);
    m_context->CSSetUnorderedAccessViews(0, kMaxSavedUAVs, s.uavs, nullptr);
    m_context->CSSetConstantBuffers(0, kMaxSavedCBs, s.cbs);
    m_context->CSSetSamplers(0, kMaxSavedSamplers, s.samplers);

    // Release references from Get calls
    if (s.shader) s.shader->Release();
    for (UINT i = 0; i < s.classInstanceCount; ++i)
        if (s.classInstances[i]) s.classInstances[i]->Release();
    for (UINT i = 0; i < kMaxSavedSRVs; ++i)
        if (s.srvs[i]) s.srvs[i]->Release();
    for (UINT i = 0; i < kMaxSavedUAVs; ++i)
        if (s.uavs[i]) s.uavs[i]->Release();
    for (UINT i = 0; i < kMaxSavedCBs; ++i)
        if (s.cbs[i]) s.cbs[i]->Release();
    for (UINT i = 0; i < kMaxSavedSamplers; ++i)
        if (s.samplers[i]) s.samplers[i]->Release();

    s.saved = false;
}

// ═══════════════════════════════════════════════════════════════════════════
//  OM state save/restore (for backbuffer UAV usage)
// ═══════════════════════════════════════════════════════════════════════════

void ComputeManager::SaveOMState()
{
    if (!m_context) return;

    auto& s = m_savedOMState;
    m_context->OMGetRenderTargets(kMaxRTVs, s.rtvs, &s.dsv);
    m_context->OMGetBlendState(&s.blendState, s.blendFactor, &s.sampleMask);
    m_context->OMGetDepthStencilState(&s.depthStencilState, &s.stencilRef);
    s.saved = true;
}

void ComputeManager::RestoreOMState()
{
    if (!m_context || !m_savedOMState.saved) return;

    auto& s = m_savedOMState;
    m_context->OMSetRenderTargets(kMaxRTVs, s.rtvs, s.dsv);
    m_context->OMSetBlendState(s.blendState, s.blendFactor, s.sampleMask);
    m_context->OMSetDepthStencilState(s.depthStencilState, s.stencilRef);

    // Release references from Get calls
    for (UINT i = 0; i < kMaxRTVs; ++i)
        if (s.rtvs[i]) s.rtvs[i]->Release();
    if (s.dsv) s.dsv->Release();
    if (s.blendState) s.blendState->Release();
    if (s.depthStencilState) s.depthStencilState->Release();

    s = {};
}

// ═══════════════════════════════════════════════════════════════════════════
//  Dispatch + resource binding
// ═══════════════════════════════════════════════════════════════════════════

void ComputeManager::Dispatch(ComputeShaderID shader,
                               UINT groupsX, UINT groupsY, UINT groupsZ)
{
    auto* cs = GetShader(shader);
    if (!cs || !m_context) return;

    m_context->CSSetShader(cs, nullptr, 0);
    m_context->Dispatch(groupsX, groupsY, groupsZ);
}

void ComputeManager::CSSetSRVs(UINT startSlot, UINT count,
                                 ID3D11ShaderResourceView* const* srvs)
{
    if (m_context) m_context->CSSetShaderResources(startSlot, count, srvs);
}

void ComputeManager::CSSetUAVs(UINT startSlot, UINT count,
                                 ID3D11UnorderedAccessView* const* uavs,
                                 const UINT* initialCounts)
{
    if (m_context) m_context->CSSetUnorderedAccessViews(startSlot, count, uavs, initialCounts);
}

void ComputeManager::CSSetCBs(UINT startSlot, UINT count,
                                ID3D11Buffer* const* cbs)
{
    if (m_context) m_context->CSSetConstantBuffers(startSlot, count, cbs);
}

void ComputeManager::CSSetSamplers(UINT startSlot, UINT count,
                                    ID3D11SamplerState* const* samplers)
{
    if (m_context) m_context->CSSetSamplers(startSlot, count, samplers);
}

void ComputeManager::CSClearSRVs(UINT startSlot, UINT count)
{
    if (!m_context) return;
    ID3D11ShaderResourceView* nullSRVs[8] = {};
    while (count > 0) {
        UINT batch = (count > 8) ? 8 : count;
        m_context->CSSetShaderResources(startSlot, batch, nullSRVs);
        startSlot += batch;
        count -= batch;
    }
}

void ComputeManager::CSClearUAVs(UINT startSlot, UINT count)
{
    if (!m_context) return;
    ID3D11UnorderedAccessView* nullUAVs[8] = {};
    while (count > 0) {
        UINT batch = (count > 8) ? 8 : count;
        m_context->CSSetUnorderedAccessViews(startSlot, batch, nullUAVs, nullptr);
        startSlot += batch;
        count -= batch;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Readback
// ═══════════════════════════════════════════════════════════════════════════

bool ComputeManager::ReadbackBuffer(ID3D11Buffer* src, ID3D11Buffer* staging,
                                     void* dst, UINT sizeBytes)
{
    if (!m_context || !src || !staging || !dst) return false;

    m_context->CopyResource(staging, src);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = m_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    std::memcpy(dst, mapped.pData, sizeBytes);
    m_context->Unmap(staging, 0);
    return true;
}

bool ComputeManager::ReadbackTexture(ID3D11Texture2D* src, ID3D11Texture2D* staging,
                                      void* dst, UINT rowPitch, UINT height)
{
    if (!m_context || !src || !staging || !dst) return false;

    m_context->CopyResource(staging, src);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = m_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    auto* srcRow = static_cast<const uint8_t*>(mapped.pData);
    auto* dstRow = static_cast<uint8_t*>(dst);
    UINT copyPitch = (rowPitch < mapped.RowPitch) ? rowPitch : mapped.RowPitch;
    for (UINT y = 0; y < height; ++y) {
        std::memcpy(dstRow, srcRow, copyPitch);
        srcRow += mapped.RowPitch;
        dstRow += rowPitch;
    }

    m_context->Unmap(staging, 0);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════════════

void ComputeManager::Shutdown()
{
    for (auto& entry : m_shaders) {
        if (entry.shader) entry.shader->Release();
    }
    m_shaders.clear();
    m_nextID = 1;
    m_initialized = false;
}

} // namespace SB
