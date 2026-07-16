//=============================================================================
//  RenderPassManager.cpp — Full D3D11 rasterization pipeline
//
//  Supports:
//    - Fullscreen triangle (SV_VertexID, no VB) — default when no geometry set
//    - Mesh drawing with VB + optional IB + optional instancing
//    - MRT: up to 8 simultaneous render targets
//    - Configurable blend, depth-stencil, and rasterizer state
//    - Input layout creation from VS bytecode + element desc
//    - Full D3D11 state save/restore (IA, VS, RS, PS, OM)
//=============================================================================

#include "RenderPassManager.h"

#include <d3dcompiler.h>
#include <cstring>
#include <algorithm>

#include <SKSE/SKSE.h>

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Built-in fullscreen vertex shader
//
//  Generates a single triangle covering clip space from SV_VertexID:
//    id=0 → (-1, 1) uv=(0,0)    id=1 → (3, 1) uv=(2,0)
//    id=2 → (-1,-3) uv=(0,2)
//  The triangle extends past the screen edges; the rasterizer clips it.
//  This is the industry-standard fullscreen pass technique (no VB/IB).
// ═══════════════════════════════════════════════════════════════════════════

static const char* const kFullscreenVS = R"HLSL(
struct VSOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(uint vertexID : SV_VertexID)
{
    VSOutput o;
    // Generate fullscreen triangle from vertex ID
    o.texcoord = float2((vertexID << 1) & 2, vertexID & 2);
    o.position = float4(o.texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool RenderPassManager::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    if (m_initialized) return true;
    if (!dev || !ctx) return false;

    m_device  = dev;
    m_context = ctx;

    if (!CompileFullscreenVS())
        return false;

    // Create default rasterizer state: solid fill, no culling, no scissor
    D3D11_RASTERIZER_DESC rsDesc{};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    rsDesc.DepthClipEnable = TRUE;
    if (FAILED(dev->CreateRasterizerState(&rsDesc, &m_defaultRS))) {
        SKSE::log::warn("RenderPassManager: Failed to create default rasterizer state");
        // Non-fatal — we'll just not set one when exec.rasterizerState is null
    }

    m_initialized = true;
    SKSE::log::info("RenderPassManager: Initialized (fullscreen VS compiled, default RS created)");
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile the built-in fullscreen VS
// ═══════════════════════════════════════════════════════════════════════════

bool RenderPassManager::CompileFullscreenVS()
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;
    HRESULT hr = D3DCompile(kFullscreenVS, strlen(kFullscreenVS),
                            "SB_FullscreenVS", nullptr, nullptr,
                            "main", "vs_5_0", flags, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) {
            SKSE::log::error("RenderPassManager: Fullscreen VS compile failed: {}",
                             static_cast<const char*>(err->GetBufferPointer()));
            err->Release();
        }
        return false;
    }
    if (err) err->Release();

    hr = m_device->CreateVertexShader(blob->GetBufferPointer(),
                                       blob->GetBufferSize(),
                                       nullptr, &m_fullscreenVS);
    blob->Release();
    if (FAILED(hr)) {
        SKSE::log::error("RenderPassManager: CreateVertexShader failed (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  RegisterPass — compile VS+PS pair, optionally create input layout
// ═══════════════════════════════════════════════════════════════════════════

RenderPassID RenderPassManager::RegisterPass(const RenderPassDesc& desc)
{
    if (!m_initialized || !desc.psSource) return 0;

    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    PassEntry entry;
    entry.name = desc.name ? desc.name : "unnamed";

    // ── Compile VS (or use built-in fullscreen VS) ───────────────────
    if (desc.vsSource) {
        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        std::string vsName = entry.name + "_VS";
        HRESULT hr = D3DCompile(desc.vsSource, strlen(desc.vsSource),
                                vsName.c_str(), nullptr, nullptr,
                                desc.vsEntry, "vs_5_0", flags, 0,
                                &blob, &err);
        if (FAILED(hr)) {
            if (err) {
                SKSE::log::error("RenderPassManager: VS compile failed for '{}': {}",
                                 entry.name,
                                 static_cast<const char*>(err->GetBufferPointer()));
                err->Release();
            }
            return 0;
        }
        if (err) err->Release();

        hr = m_device->CreateVertexShader(blob->GetBufferPointer(),
                                           blob->GetBufferSize(),
                                           nullptr, &entry.vs);

        // Create input layout from VS bytecode + caller's element desc
        if (SUCCEEDED(hr) && desc.inputElements && desc.inputElementCount > 0) {
            hr = m_device->CreateInputLayout(
                desc.inputElements, desc.inputElementCount,
                blob->GetBufferPointer(), blob->GetBufferSize(),
                &entry.inputLayout);
            if (FAILED(hr)) {
                SKSE::log::error("RenderPassManager: CreateInputLayout failed for '{}'",
                                 entry.name);
                blob->Release();
                if (entry.vs) entry.vs->Release();
                return 0;
            }
        }

        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("RenderPassManager: CreateVertexShader failed for '{}'",
                             entry.name);
            return 0;
        }
    }
    // else: entry.vs stays nullptr → Execute uses m_fullscreenVS

    // ── Compile PS ───────────────────────────────────────────────────
    {
        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        std::string psName = entry.name + "_PS";
        HRESULT hr = D3DCompile(desc.psSource, strlen(desc.psSource),
                                psName.c_str(), nullptr, nullptr,
                                desc.psEntry, "ps_5_0", flags, 0,
                                &blob, &err);
        if (FAILED(hr)) {
            if (err) {
                SKSE::log::error("RenderPassManager: PS compile failed for '{}': {}",
                                 entry.name,
                                 static_cast<const char*>(err->GetBufferPointer()));
                err->Release();
            }
            if (entry.vs) entry.vs->Release();
            if (entry.inputLayout) entry.inputLayout->Release();
            return 0;
        }
        if (err) err->Release();

        hr = m_device->CreatePixelShader(blob->GetBufferPointer(),
                                          blob->GetBufferSize(),
                                          nullptr, &entry.ps);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("RenderPassManager: CreatePixelShader failed for '{}'",
                             entry.name);
            if (entry.vs) entry.vs->Release();
            if (entry.inputLayout) entry.inputLayout->Release();
            return 0;
        }
    }

    std::lock_guard lock(m_passMutex);
    m_passes.push_back(std::move(entry));
    RenderPassID id = static_cast<RenderPassID>(m_passes.size());  // 1-based

    SKSE::log::info("RenderPassManager: Registered pass '{}' (id={}, layout={})",
                    m_passes.back().name, id,
                    m_passes.back().inputLayout ? "yes" : "no");
    return id;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Execute — save state, bind pass resources, draw, restore
// ═══════════════════════════════════════════════════════════════════════════

void RenderPassManager::Execute(const PassExecution& exec)
{
    std::lock_guard lock(m_passMutex);
    if (!m_initialized || exec.passID == 0 || exec.passID > m_passes.size())
        return;

    const PassEntry& pass = m_passes[exec.passID - 1];

    // Determine draw mode
    bool isMeshDraw = (exec.vertexCount > 0 || exec.indexCount > 0);

    // ── Save full pipeline state ─────────────────────────────────────
    SavePipelineState();

    // ── Input Assembler ──────────────────────────────────────────────
    m_context->IASetPrimitiveTopology(exec.topology);

    if (isMeshDraw) {
        // Mesh mode: bind caller's input layout + vertex/index buffers
        m_context->IASetInputLayout(pass.inputLayout);

        if (exec.instanceBuffer && exec.instanceCount > 0) {
            // Two VB slots: 0=vertex data, 1=instance data
            ID3D11Buffer* buffers[2] = { exec.vertexBuffer, exec.instanceBuffer };
            UINT strides[2] = { exec.vbStride, exec.instanceStride };
            UINT offsets[2] = { exec.vbOffset, 0 };
            m_context->IASetVertexBuffers(0, 2, buffers, strides, offsets);
        } else {
            m_context->IASetVertexBuffers(0, 1, &exec.vertexBuffer,
                                          &exec.vbStride, &exec.vbOffset);
        }

        if (exec.indexBuffer)
            m_context->IASetIndexBuffer(exec.indexBuffer, exec.indexFormat, 0);
        else
            m_context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    } else {
        // Fullscreen triangle: no VB/IB, no input layout
        m_context->IASetInputLayout(nullptr);
        ID3D11Buffer* nullVB = nullptr;
        UINT zero = 0;
        m_context->IASetVertexBuffers(0, 1, &nullVB, &zero, &zero);
        m_context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    }

    // ── Vertex Shader ────────────────────────────────────────────────
    ID3D11VertexShader* vs = pass.vs ? pass.vs : m_fullscreenVS;
    m_context->VSSetShader(vs, nullptr, 0);

    // ── Rasterizer ───────────────────────────────────────────────────
    // Determine viewport from exec or auto-derive from first RT
    D3D11_VIEWPORT vp = exec.viewport;
    if (vp.Width == 0.0f) {
        // Pick first available RT to derive viewport dimensions
        ID3D11RenderTargetView* firstRTV = nullptr;
        if (exec.rtvCount > 0 && exec.rtvs)
            firstRTV = exec.rtvs[0];
        else
            firstRTV = exec.rtv;

        if (firstRTV) {
            ID3D11Resource* rtvRes = nullptr;
            firstRTV->GetResource(&rtvRes);
            if (rtvRes) {
                ID3D11Texture2D* rtvTex = nullptr;
                if (SUCCEEDED(rtvRes->QueryInterface(__uuidof(ID3D11Texture2D),
                                                      reinterpret_cast<void**>(&rtvTex)))) {
                    D3D11_TEXTURE2D_DESC texDesc;
                    rtvTex->GetDesc(&texDesc);
                    vp.TopLeftX = 0.0f;
                    vp.TopLeftY = 0.0f;
                    vp.Width    = static_cast<float>(texDesc.Width);
                    vp.Height   = static_cast<float>(texDesc.Height);
                    vp.MinDepth = 0.0f;
                    vp.MaxDepth = 1.0f;
                    rtvTex->Release();
                }
                rtvRes->Release();
            }
        }
    }
    m_context->RSSetViewports(1, &vp);

    // Rasterizer state: use caller's, or default (no cull, solid fill)
    m_context->RSSetState(exec.rasterizerState ? exec.rasterizerState : m_defaultRS);

    // ── Pixel Shader + resources ─────────────────────────────────────
    m_context->PSSetShader(pass.ps, nullptr, 0);

    if (exec.srvs && exec.srvCount > 0)
        m_context->PSSetShaderResources(0, exec.srvCount, exec.srvs);

    if (exec.samplers && exec.samplerCount > 0)
        m_context->PSSetSamplers(0, exec.samplerCount, exec.samplers);

    // ── Constant buffer ──────────────────────────────────────────────
    if (exec.cbData && exec.cbSize > 0) {
        // Grow dynamic CB if needed (round up to 16-byte alignment)
        uint32_t alignedSize = (exec.cbSize + 15u) & ~15u;
        if (alignedSize > m_dynamicCBSize) {
            if (m_dynamicCB) m_dynamicCB->Release();
            D3D11_BUFFER_DESC cbDesc = {};
            cbDesc.ByteWidth      = alignedSize;
            cbDesc.Usage           = D3D11_USAGE_DYNAMIC;
            cbDesc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
            cbDesc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &m_dynamicCB))) {
                RestorePipelineState();
                return;
            }
            m_dynamicCBSize = alignedSize;
        }

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context->Map(m_dynamicCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, exec.cbData, exec.cbSize);
            m_context->Unmap(m_dynamicCB, 0);
        }

        // Bind to both VS b0 and PS b0
        m_context->VSSetConstantBuffers(0, 1, &m_dynamicCB);
        m_context->PSSetConstantBuffers(0, 1, &m_dynamicCB);
    }

    // ── Output Merger ────────────────────────────────────────────────
    // Render targets: MRT or single
    if (exec.rtvCount > 0 && exec.rtvs) {
        m_context->OMSetRenderTargets(exec.rtvCount, exec.rtvs, exec.dsv);
    } else {
        m_context->OMSetRenderTargets(1, &exec.rtv, exec.dsv);
    }

    // Blend state (null = default opaque overwrite)
    m_context->OMSetBlendState(exec.blendState, exec.blendFactor, exec.sampleMask);

    // Depth-stencil state (null = depth testing disabled)
    m_context->OMSetDepthStencilState(exec.depthStencilState, exec.stencilRef);

    // ── Draw ─────────────────────────────────────────────────────────
    if (isMeshDraw) {
        if (exec.indexCount > 0) {
            if (exec.instanceCount > 0) {
                m_context->DrawIndexedInstanced(exec.indexCount, exec.instanceCount,
                    exec.startIndex, exec.baseVertex, 0);
            } else {
                m_context->DrawIndexed(exec.indexCount, exec.startIndex, exec.baseVertex);
            }
        } else {
            if (exec.instanceCount > 0) {
                m_context->DrawInstanced(exec.vertexCount, exec.instanceCount,
                    exec.startVertex, 0);
            } else {
                m_context->Draw(exec.vertexCount, exec.startVertex);
            }
        }
    } else {
        // Fullscreen triangle
        m_context->Draw(3, 0);
    }

    // ── Unbind resources (prevent stale bindings) ────────────────────
    if (exec.srvCount > 0) {
        ID3D11ShaderResourceView* nullSRVs[8] = {};
        m_context->PSSetShaderResources(0,
            (std::min)(exec.srvCount, 8u), nullSRVs);
    }

    // ── Restore full pipeline state ──────────────────────────────────
    RestorePipelineState();
}


// ═══════════════════════════════════════════════════════════════════════════
//  SavePipelineState — capture the full D3D11 state machine
// ═══════════════════════════════════════════════════════════════════════════

void RenderPassManager::SavePipelineState()
{
    auto& s = m_saved;

    // Input Assembler
    m_context->IAGetPrimitiveTopology(&s.topology);
    m_context->IAGetInputLayout(&s.inputLayout);
    m_context->IAGetVertexBuffers(0, 4, s.vertexBuffers, s.vbStrides, s.vbOffsets);
    m_context->IAGetIndexBuffer(&s.indexBuffer, &s.ibFormat, &s.ibOffset);

    // Vertex Shader
    s.vsCICount = 256;
    m_context->VSGetShader(&s.vs, s.vsCI, &s.vsCICount);
    m_context->VSGetConstantBuffers(0, 4, s.vsCBs);

    // Rasterizer
    m_context->RSGetState(&s.rsState);
    s.viewportCount = 4;
    m_context->RSGetViewports(&s.viewportCount, s.viewports);
    s.scissorCount = 4;
    m_context->RSGetScissorRects(&s.scissorCount, s.scissorRects);

    // Pixel Shader
    s.psCICount = 256;
    m_context->PSGetShader(&s.ps, s.psCI, &s.psCICount);
    m_context->PSGetConstantBuffers(0, 4, s.psCBs);
    m_context->PSGetShaderResources(0, 8, s.psSRVs);
    m_context->PSGetSamplers(0, 4, s.psSamplers);

    // Output Merger (8 RTVs for MRT)
    m_context->OMGetRenderTargets(8, s.rtvs, &s.dsv);
    m_context->OMGetBlendState(&s.blendState, s.blendFactor, &s.sampleMask);
    m_context->OMGetDepthStencilState(&s.depthStencilState, &s.stencilRef);

    s.saved = true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  RestorePipelineState — rebind everything exactly as it was
// ═══════════════════════════════════════════════════════════════════════════

void RenderPassManager::RestorePipelineState()
{
    auto& s = m_saved;
    if (!s.saved) return;

    // Input Assembler
    m_context->IASetPrimitiveTopology(s.topology);
    m_context->IASetInputLayout(s.inputLayout);
    m_context->IASetVertexBuffers(0, 4, s.vertexBuffers, s.vbStrides, s.vbOffsets);
    m_context->IASetIndexBuffer(s.indexBuffer, s.ibFormat, s.ibOffset);

    // Vertex Shader
    m_context->VSSetShader(s.vs, s.vsCI, s.vsCICount);
    m_context->VSSetConstantBuffers(0, 4, s.vsCBs);

    // Rasterizer
    m_context->RSSetState(s.rsState);
    if (s.viewportCount > 0)
        m_context->RSSetViewports(s.viewportCount, s.viewports);
    if (s.scissorCount > 0)
        m_context->RSSetScissorRects(s.scissorCount, s.scissorRects);

    // Pixel Shader
    m_context->PSSetShader(s.ps, s.psCI, s.psCICount);
    m_context->PSSetConstantBuffers(0, 4, s.psCBs);
    m_context->PSSetShaderResources(0, 8, s.psSRVs);
    m_context->PSSetSamplers(0, 4, s.psSamplers);

    // Output Merger (restore all 8 RT slots)
    m_context->OMSetRenderTargets(8, s.rtvs, s.dsv);
    m_context->OMSetBlendState(s.blendState, s.blendFactor, s.sampleMask);
    m_context->OMSetDepthStencilState(s.depthStencilState, s.stencilRef);

    // Release all COM references acquired by Get* calls
    s.Release();
}


// ═══════════════════════════════════════════════════════════════════════════
//  SavedState::Release — drop all COM refs from Get* calls
// ═══════════════════════════════════════════════════════════════════════════

void RenderPassManager::SavedState::Release()
{
    if (!saved) return;

    // IA
    if (inputLayout) inputLayout->Release();
    for (auto& vb : vertexBuffers) if (vb) vb->Release();
    if (indexBuffer) indexBuffer->Release();

    // VS
    if (vs) vs->Release();
    for (UINT i = 0; i < vsCICount; i++) if (vsCI[i]) vsCI[i]->Release();
    for (auto& cb : vsCBs) if (cb) cb->Release();

    // RS
    if (rsState) rsState->Release();

    // PS
    if (ps) ps->Release();
    for (UINT i = 0; i < psCICount; i++) if (psCI[i]) psCI[i]->Release();
    for (auto& cb : psCBs) if (cb) cb->Release();
    for (auto& srv : psSRVs) if (srv) srv->Release();
    for (auto& smp : psSamplers) if (smp) smp->Release();

    // OM (8 RTVs)
    for (auto& rtv : rtvs) if (rtv) rtv->Release();
    if (dsv) dsv->Release();
    if (blendState) blendState->Release();
    if (depthStencilState) depthStencilState->Release();

    // Zero everything
    *this = {};
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════════════

void RenderPassManager::Shutdown()
{
    for (auto& pass : m_passes) {
        if (pass.vs) pass.vs->Release();
        if (pass.ps) pass.ps->Release();
        if (pass.inputLayout) pass.inputLayout->Release();
    }
    m_passes.clear();

    if (m_fullscreenVS) { m_fullscreenVS->Release(); m_fullscreenVS = nullptr; }
    if (m_defaultRS)    { m_defaultRS->Release();     m_defaultRS    = nullptr; }
    if (m_dynamicCB)    { m_dynamicCB->Release();     m_dynamicCB    = nullptr; }
    m_dynamicCBSize = 0;

    m_saved.Release();
    m_initialized = false;
}

} // namespace SB
