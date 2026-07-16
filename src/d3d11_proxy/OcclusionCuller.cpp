//=============================================================================
//  OcclusionCuller.cpp — CPU-side GPU occlusion culling
//
//  Per-frame flow:
//    Present:    Copy depth → staging[writeIdx], Map staging[readIdx] → CPU Hi-Z
//    DrawIndexed: Extract world pos from VS CB slot 2, project bounding sphere,
//                 test against CPU Hi-Z, skip if fully behind closest depth
//=============================================================================

#include "OcclusionCuller.h"
#include "CBDirtyTracker.h"
#include "RenderPhaseDetector.h"

#include <cstring>
#include <cmath>
#include <algorithm>

namespace SB::Proxy
{

// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool OcclusionCuller::Initialize(ID3D11Device* device, ID3D11DeviceContext* ctx)
{
    if (m_initialized) return true;
    if (!device || !ctx) return false;

    m_device  = device;
    m_context = ctx;

    // We defer staging texture creation until the first Present,
    // when we know the depth buffer dimensions.
    m_initialized = true;
    return true;
}

void OcclusionCuller::Shutdown()
{
    for (auto& stg : m_depthStaging) {
        if (stg) { stg->Release(); stg = nullptr; }
    }
    m_initialized = false;
    m_hizReady = false;
}


// ═══════════════════════════════════════════════════════════════════════════
//  OnPresent — Depth readback + CPU Hi-Z build
// ═══════════════════════════════════════════════════════════════════════════

void OcclusionCuller::OnPresent(ID3D11DeviceContext* ctx)
{
    if (!m_initialized || !m_enabled) return;

    // Reset per-frame stats
    m_drawsTested = 0;
    m_drawsCulled = 0;

    // ── Get the game's depth buffer ─────────────────────────────────────
    auto& rpd = RenderPhaseDetector::Get();
    ID3D11DepthStencilView* mainDSV = rpd.GetMainDSV();
    if (!mainDSV) return;

    ID3D11Resource* dsResource = nullptr;
    mainDSV->GetResource(&dsResource);
    if (!dsResource) return;

    ID3D11Texture2D* depthTex = nullptr;
    HRESULT hr = dsResource->QueryInterface(__uuidof(ID3D11Texture2D),
                                             reinterpret_cast<void**>(&depthTex));
    dsResource->Release();
    if (FAILED(hr) || !depthTex) return;

    D3D11_TEXTURE2D_DESC depthDesc;
    depthTex->GetDesc(&depthDesc);

    m_screenW = depthDesc.Width;
    m_screenH = depthDesc.Height;

    // ── Create staging textures on first use (or if size changed) ───────
    if (!m_depthStaging[0] || m_depthW != depthDesc.Width || m_depthH != depthDesc.Height)
    {
        for (auto& stg : m_depthStaging) {
            if (stg) { stg->Release(); stg = nullptr; }
        }

        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width      = depthDesc.Width;
        stagingDesc.Height     = depthDesc.Height;
        stagingDesc.MipLevels  = 1;
        stagingDesc.ArraySize  = 1;
        stagingDesc.Format     = DXGI_FORMAT_R32_FLOAT;
        stagingDesc.SampleDesc = {1, 0};
        stagingDesc.Usage      = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        for (int i = 0; i < 2; i++) {
            hr = m_device->CreateTexture2D(&stagingDesc, nullptr, &m_depthStaging[i]);
            if (FAILED(hr)) {
                depthTex->Release();
                return;
            }
        }

        m_depthW = depthDesc.Width;
        m_depthH = depthDesc.Height;
        m_hizReady = false;
    }

    // ── Check if depth texture has SRV bind flag for direct copy ────────
    // If the depth is typeless, we can't copy directly. Use a temporary
    // R32_FLOAT texture for the copy instead.
    bool needsFormatConversion = false;
    DXGI_FORMAT copyFormat = depthDesc.Format;
    if (depthDesc.Format == DXGI_FORMAT_R32_TYPELESS ||
        depthDesc.Format == DXGI_FORMAT_R24G8_TYPELESS ||
        depthDesc.Format == DXGI_FORMAT_R32G8X24_TYPELESS)
    {
        // For typeless depth, we need an intermediate copy with the correct format
        // CopyResource requires matching formats. For simplicity, we'll create an
        // SRV-capable copy and use CopySubresourceRegion with format reinterpret.
        // However D3D11 CopyResource only works with identical formats.
        // The staging texture is R32_FLOAT which won't match R24G8_TYPELESS.
        // For now, skip if format doesn't match R32_TYPELESS/R32_FLOAT.
        if (depthDesc.Format == DXGI_FORMAT_R32_TYPELESS) {
            needsFormatConversion = false;  // R32_TYPELESS → R32_FLOAT works with CopyResource
        } else {
            // R24G8 formats can't be directly copied to R32_FLOAT staging
            depthTex->Release();
            return;
        }
    }

    // ── Copy depth to staging[writeIdx] ─────────────────────────────────
    int writeIdx = m_readbackIdx;
    int readIdx  = 1 - m_readbackIdx;

    ctx->CopyResource(m_depthStaging[writeIdx], depthTex);
    depthTex->Release();

    // ── Read back staging[readIdx] from previous frame ──────────────────
    if (m_depthStaging[readIdx] && m_hizReady)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = ctx->Map(m_depthStaging[readIdx], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (SUCCEEDED(hr))
        {
            BuildCPUHiZ(static_cast<const float*>(mapped.pData),
                        m_depthW, m_depthH,
                        mapped.RowPitch / sizeof(float));
            ctx->Unmap(m_depthStaging[readIdx], 0);
        }
        // If Map fails with DO_NOT_WAIT, GPU isn't done yet — skip this frame
    }
    else if (!m_hizReady)
    {
        // First frame: mark ready so next frame we can read
        m_hizReady = true;
    }

    // Swap double buffer
    m_readbackIdx = readIdx;
}


// ═══════════════════════════════════════════════════════════════════════════
//  BuildCPUHiZ — Construct a CPU-side hierarchical-Z pyramid
// ═══════════════════════════════════════════════════════════════════════════

void OcclusionCuller::BuildCPUHiZ(const float* depthData, uint32_t width, uint32_t height, uint32_t rowPitch)
{
    // Mip 0: subsample the full depth buffer by 4x in each dimension
    // This gives us a ~480x270 base for 1920x1080, which is fast to process.
    constexpr uint32_t kSubsample = 4;
    uint32_t mip0W = (width + kSubsample - 1) / kSubsample;
    uint32_t mip0H = (height + kSubsample - 1) / kSubsample;

    auto& mip0 = m_hizMipChain[0];
    mip0.width  = mip0W;
    mip0.height = mip0H;
    mip0.data.resize(mip0W * mip0H);

    // Subsample: take MAX of each kSubsample x kSubsample block
    // (reversed-Z: max = closest surface = conservative for occlusion)
    for (uint32_t y = 0; y < mip0H; y++) {
        for (uint32_t x = 0; x < mip0W; x++) {
            float maxZ = 0.0f;
            uint32_t srcY0 = y * kSubsample;
            uint32_t srcX0 = x * kSubsample;
            uint32_t srcY1 = std::min(srcY0 + kSubsample, height);
            uint32_t srcX1 = std::min(srcX0 + kSubsample, width);
            for (uint32_t sy = srcY0; sy < srcY1; sy++) {
                for (uint32_t sx = srcX0; sx < srcX1; sx++) {
                    float d = depthData[sy * rowPitch + sx];
                    maxZ = std::max(maxZ, d);
                }
            }
            mip0.data[y * mip0W + x] = maxZ;
        }
    }

    // Build remaining mip levels (2x2 MAX downsample)
    m_hizW    = mip0W;
    m_hizH    = mip0H;
    m_hizMips = 1;

    uint32_t prevW = mip0W;
    uint32_t prevH = mip0H;

    for (uint32_t mip = 1; mip < kMaxHiZMips; mip++) {
        uint32_t curW = std::max(prevW / 2, 1u);
        uint32_t curH = std::max(prevH / 2, 1u);

        auto& curMip  = m_hizMipChain[mip];
        auto& prevMip = m_hizMipChain[mip - 1];

        curMip.width  = curW;
        curMip.height = curH;
        curMip.data.resize(curW * curH);

        for (uint32_t y = 0; y < curH; y++) {
            for (uint32_t x = 0; x < curW; x++) {
                uint32_t sx = x * 2;
                uint32_t sy = y * 2;
                float d00 = prevMip.data[sy * prevW + sx];
                float d10 = (sx + 1 < prevW) ? prevMip.data[sy * prevW + sx + 1] : d00;
                float d01 = (sy + 1 < prevH) ? prevMip.data[(sy + 1) * prevW + sx] : d00;
                float d11 = (sx + 1 < prevW && sy + 1 < prevH) ?
                            prevMip.data[(sy + 1) * prevW + sx + 1] : d00;

                curMip.data[y * curW + x] = std::max({d00, d10, d01, d11});
            }
        }

        m_hizMips++;
        prevW = curW;
        prevH = curH;

        if (curW == 1 && curH == 1) break;
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  TestRect — Test a screen-space rect against the CPU Hi-Z
// ═══════════════════════════════════════════════════════════════════════════

bool OcclusionCuller::TestRect(float minX, float minY, float maxX, float maxY, float nearDepth) const
{
    if (m_hizMips == 0) return false;  // No Hi-Z available, don't cull

    // Clamp to [0,1]
    minX = std::max(minX, 0.0f);
    minY = std::max(minY, 0.0f);
    maxX = std::min(maxX, 1.0f);
    maxY = std::min(maxY, 1.0f);

    if (minX >= maxX || minY >= maxY) return false;

    // Choose mip level where the rect covers approximately 2x2 to 4x4 texels
    float rectW = (maxX - minX) * m_hizW;
    float rectH = (maxY - minY) * m_hizH;
    float maxDim = std::max(rectW, rectH);

    // Mip level = log2(maxDim / 2)  (we want the rect to span ~2 texels)
    int mipLevel = 0;
    if (maxDim > 2.0f) {
        mipLevel = static_cast<int>(std::log2(maxDim / 2.0f));
    }
    mipLevel = std::max(0, std::min(mipLevel, static_cast<int>(m_hizMips) - 1));

    auto& mip = m_hizMipChain[mipLevel];

    // Convert UV rect to texel coordinates at this mip
    int x0 = static_cast<int>(minX * mip.width);
    int y0 = static_cast<int>(minY * mip.height);
    int x1 = static_cast<int>(std::ceil(maxX * mip.width));
    int y1 = static_cast<int>(std::ceil(maxY * mip.height));

    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, static_cast<int>(mip.width));
    y1 = std::min(y1, static_cast<int>(mip.height));

    // Check all covered texels: if ALL have depth > nearDepth (reversed-Z:
    // the Hi-Z stores max=closest, if the object's near point is farther than
    // the closest surface at every covered texel, the object is fully behind).
    //
    // Reversed-Z: z=1 near, z=0 far.
    // Object's nearDepth is the closest point of the bounding sphere (in depth).
    // Hi-Z stores MAX (closest surface).
    // Occluded if: nearDepth < min_of_hiz (object's nearest point is farther
    // than the farthest "closest surface" across the rect)
    //
    // Actually in reversed-Z: higher Z = closer. So:
    // - nearDepth = object's closest depth (highest Z value)
    // - Hi-Z MAX = closest surface (highest Z value)
    // Object is occluded if: nearDepth < ALL Hi-Z values in the rect
    //   i.e., the object's closest point is FARTHER than every blocker
    //   i.e., nearDepth < min(Hi-Z values in rect)

    float minHiZ = 1.0f;  // Start with closest possible
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float hizVal = mip.data[y * mip.width + x];
            minHiZ = std::min(minHiZ, hizVal);
        }
    }

    // Object is occluded if its nearest depth is farther than the farthest
    // blocker in the covered region
    return nearDepth < minHiZ;
}


// ═══════════════════════════════════════════════════════════════════════════
//  VS CB tracking
// ═══════════════════════════════════════════════════════════════════════════

void OcclusionCuller::OnVSSetConstantBuffers(UINT startSlot, UINT numBuffers,
                                              ID3D11Buffer* const* ppCBs)
{
    for (UINT i = 0; i < numBuffers; i++) {
        UINT slot = startSlot + i;
        if (slot < kMaxVSCBSlots) {
            m_boundVSCBs[slot] = ppCBs ? ppCBs[i] : nullptr;
        }
    }
}

void OcclusionCuller::OnClearState()
{
    std::memset(m_boundVSCBs, 0, sizeof(m_boundVSCBs));
}


// ═══════════════════════════════════════════════════════════════════════════
//  ExtractWorldPosition — Read world translation from VS CB slot 2
// ═══════════════════════════════════════════════════════════════════════════

bool OcclusionCuller::ExtractWorldPosition(float outPos[3]) const
{
    // BSLightingShader's cbPerGeometry is at VS slot 2.
    // The first 3 rows of the world matrix contain the 4x3 transform.
    // Row 3 (bytes 48-59) = translation (world position of the object).
    //
    // We read this from CBDirtyTracker's shadow copy to avoid any GPU sync.

    ID3D11Buffer* vsCB2 = (2 < kMaxVSCBSlots) ? m_boundVSCBs[2] : nullptr;
    if (!vsCB2) return false;

    auto& tracker = CBDirtyTracker::Get();

    // CBDirtyTracker stores shadow copies indexed by resource pointer.
    // We need to access the shadow data. Add a public accessor.
    // For now, use the staging buffer approach via GetShadowData.
    const uint8_t* shadow = tracker.GetShadowData(vsCB2);
    if (!shadow) return false;

    uint32_t cbSize = tracker.GetShadowSize(vsCB2);
    if (cbSize < 64) return false;  // Need at least 4x4 floats = 64 bytes

    // World matrix is 4x3 row-major starting at offset 0 in cbPerGeometry.
    // Translation is in row 3 (offset 48 = 12 floats * 4 bytes).
    const float* floatData = reinterpret_cast<const float*>(shadow);

    // Row 3 = translation (X, Y, Z)
    outPos[0] = floatData[12];  // _41 = translation X
    outPos[1] = floatData[13];  // _42 = translation Y
    outPos[2] = floatData[14];  // _43 = translation Z

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  EstimateBoundingRadius — Heuristic from index count
// ═══════════════════════════════════════════════════════════════════════════

float OcclusionCuller::EstimateBoundingRadius(uint32_t indexCount) const
{
    // Rough heuristic: more triangles usually means a bigger object.
    // These thresholds are tuned for Skyrim's mesh complexity.
    uint32_t triCount = indexCount / 3;

    if (triCount < 50)    return 30.0f;     // Small props, stones
    if (triCount < 200)   return 80.0f;     // Medium props, furniture
    if (triCount < 1000)  return 200.0f;    // Characters, large objects
    if (triCount < 5000)  return 500.0f;    // Buildings, terrain chunks
    return 1000.0f;                          // Very large meshes — don't cull aggressively
}


// ═══════════════════════════════════════════════════════════════════════════
//  ShouldCull — Main culling decision
// ═══════════════════════════════════════════════════════════════════════════

bool OcclusionCuller::ShouldCull(uint32_t indexCount)
{
    if (!m_enabled || !m_hizReady || m_hizMips == 0) return false;

    // Only cull during main geometry and alpha blend phases
    auto phase = RenderPhaseDetector::Get().GetCurrentPhase();
    if (phase != RenderPhase::GeometryMain && phase != RenderPhase::AlphaBlend)
        return false;

    // Don't try to cull very small draws (HUD elements, decals, etc.)
    if (indexCount < 36) return false;  // Less than 12 triangles

    m_drawsTested++;

    // Extract object world position from VS CB
    float worldPos[3];
    if (!ExtractWorldPosition(worldPos)) return false;

    // Get camera position (approximate: use 0,0,0 if not available)
    // In practice, the SKSE plugin has full camera data. For the proxy,
    // we estimate from the VS CB slot 0 (PerFrame) view matrix.
    // For now, assume camera at origin — the relative depth test still works.

    // Estimate bounding radius
    float radius = EstimateBoundingRadius(indexCount);

    // Project bounding sphere to screen-space rect.
    // We need the view-projection matrix. Since we're in the proxy without
    // full scene data, we use a simplified projection:
    // The object is at world position, we compute rough screen-space bounds.

    // Read view-projection from VS CB slot 12 (PerFrame) if available.
    // BSLightingShader puts ViewProj at cbPerFrame slot 12.
    // This is complex to extract reliably, so instead we use the depth buffer
    // directly: check the Hi-Z at the object's approximate screen position.

    // Simplified approach: we don't have the full VP matrix in the proxy,
    // so we test a conservative screen-space rect.
    // If the object's CB was mapped this frame, the world position is fresh.

    // For a correct implementation we'd need the VP matrix. Since this
    // data is available in the SKSE plugin (SceneMatrices), let's expose
    // it through ProxyAPI. For now, return false (don't cull) until the
    // VP matrix bridge is established.
    //
    // TODO: Add VP matrix to ProxyInterface, populated by SKSE plugin.
    // Once available, project bounding sphere → screen rect → TestRect().

    return false;  // Conservative: never cull until VP matrix is available
}

} // namespace SB::Proxy
