#pragma once
//=============================================================================
//  OcclusionCuller — CPU-side GPU occlusion culling via depth readback
//
//  Reads back the depth buffer each frame (1-frame delay, double-buffered),
//  builds a CPU Hi-Z pyramid, and tests draw calls against it.
//
//  Integration points:
//    - WrappedContext::DrawIndexed: Query ShouldCull() before forwarding
//    - WrappedContext::VSSetConstantBuffers: Track bound VS CBs
//    - WrappedSwapChain::Present: Call OnPresent() for depth readback
//
//  Requires: RenderPhaseDetector (skip culling during DepthPrepass/ShadowMap)
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <vector>
#include <array>

namespace SB::Proxy
{

class OcclusionCuller
{
public:
    static OcclusionCuller& Get()
    {
        static OcclusionCuller inst;
        return inst;
    }

    // Initialize with the real device (called from WrappedDevice creation)
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* ctx);
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool v) { m_enabled = v; }

    // ── Per-frame operations ──────────────────────────────────────────

    // Called from Present: kick off depth readback for next frame
    void OnPresent(ID3D11DeviceContext* ctx);

    // ── Draw call culling ─────────────────────────────────────────────

    // Called from WrappedContext::VSSetConstantBuffers to track bound VS CBs
    void OnVSSetConstantBuffers(UINT startSlot, UINT numBuffers,
                                 ID3D11Buffer* const* ppCBs);

    // Test whether a DrawIndexed call should be culled.
    // Returns true if the object is fully occluded and the draw can be skipped.
    // Only tests during GeometryMain/AlphaBlend phases; never culls
    // DepthPrepass, ShadowMap, or PostProcess draws.
    bool ShouldCull(uint32_t indexCount);

    // Called on ClearState to reset tracked VS CB bindings
    void OnClearState();

    // ── Statistics ────────────────────────────────────────────────────

    uint32_t GetDrawsTested()  const { return m_drawsTested; }
    uint32_t GetDrawsCulled()  const { return m_drawsCulled; }
    uint32_t GetDrawsPassed()  const { return m_drawsTested - m_drawsCulled; }
    uint32_t GetHiZWidth()     const { return m_hizW; }
    uint32_t GetHiZHeight()    const { return m_hizH; }
    uint32_t GetHiZMipCount()  const { return m_hizMips; }

private:
    OcclusionCuller() = default;

    // Build CPU Hi-Z pyramid from mapped depth data
    void BuildCPUHiZ(const float* depthData, uint32_t width, uint32_t height, uint32_t rowPitch);

    // Test a screen-space bounding rect against the CPU Hi-Z
    // minX/minY/maxX/maxY in [0,1] UV space, nearDepth in [0,1] reversed-Z
    bool TestRect(float minX, float minY, float maxX, float maxY, float nearDepth) const;

    // Extract world-space position from currently-bound VS CB
    bool ExtractWorldPosition(float outPos[3]) const;

    // Estimate bounding sphere radius from index count
    float EstimateBoundingRadius(uint32_t indexCount) const;

    bool m_initialized = false;
    bool m_enabled     = false;  // Off by default — experimental
    bool m_hizReady    = false;  // True after first successful readback

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // ── Depth readback (double-buffered) ──────────────────────────────
    ID3D11Texture2D* m_depthStaging[2] = {};
    int              m_readbackIdx = 0;  // Ping-pong: write to [readbackIdx], read from [1-readbackIdx]
    uint32_t         m_depthW = 0;
    uint32_t         m_depthH = 0;

    // ── CPU Hi-Z pyramid ──────────────────────────────────────────────
    static constexpr uint32_t kMaxHiZMips = 12;
    struct HiZMip {
        std::vector<float> data;
        uint32_t width  = 0;
        uint32_t height = 0;
    };
    HiZMip   m_hizMipChain[kMaxHiZMips];
    uint32_t m_hizW    = 0;
    uint32_t m_hizH    = 0;
    uint32_t m_hizMips = 0;

    // ── VS constant buffer tracking ───────────────────────────────────
    static constexpr uint32_t kMaxVSCBSlots = 8;
    ID3D11Buffer* m_boundVSCBs[kMaxVSCBSlots] = {};

    // ── Per-frame stats ───────────────────────────────────────────────
    uint32_t m_drawsTested = 0;
    uint32_t m_drawsCulled = 0;

    // ── Screen dimensions (for projection) ────────────────────────────
    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;
};

} // namespace SB::Proxy
