#pragma once
//=============================================================================
//  RenderPhaseDetector — Detects Creation Engine rendering phases
//
//  Observes RT switches, shader binds, draw calls, and viewport changes
//  through the d3d11 proxy wrapper to classify the current rendering phase.
//
//  The Creation Engine (Skyrim) renders in distinct phases per frame:
//    DepthPrepass  — Depth-only rendering (no color RT, DSV only)
//    ShadowMap     — Shadow cascades (small viewport, depth-only)
//    GeometryMain  — Forward opaque geometry (main color RT + depth)
//    Decals        — Alpha-blended decals on opaque geometry
//    Sky           — Sky dome rendering (known shader hashes)
//    AlphaBlend    — Transparent geometry (alpha blending)
//    PostProcess   — Fullscreen passes (no depth, fullscreen tri draws)
//    UI            — HUD/menu rendering (different viewport or RT dims)
//    Unknown       — Default / unclassified
//
//  No SKSE / CommonLibSSE dependencies. Only Windows + D3D11 headers.
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace SB::Proxy
{

enum class RenderPhase : uint8_t
{
    Unknown = 0,
    DepthPrepass,
    ShadowMap,
    GeometryMain,
    Decals,
    Sky,
    AlphaBlend,
    PostProcess,
    UI
};

class RenderPhaseDetector
{
public:
    static RenderPhaseDetector& Get()
    {
        static RenderPhaseDetector inst;
        return inst;
    }

    // ── Called from WrappedContext hooks ───────────────────────────────

    void OnRTChange(uint32_t numRTVs, ID3D11RenderTargetView* const* rtvs,
                    ID3D11DepthStencilView* dsv);

    void OnShaderBind(uint64_t psHash, uint64_t vsHash);

    void OnDraw(uint32_t vertexOrIndexCount, bool isIndexed);

    void OnViewportChange(float width, float height);

    void OnClearDepth(ID3D11DepthStencilView* dsv);

    // Reset per-frame state (called from Present)
    void OnPresent();

    // ── Query ────────────────────────────────────────────────────────

    RenderPhase GetCurrentPhase() const { return m_currentPhase; }
    const char* GetPhaseName() const;
    static const char* PhaseName(RenderPhase phase);

    // ── Statistics ───────────────────────────────────────────────────

    uint32_t GetDrawsInPhase(RenderPhase phase) const;
    uint32_t GetPhaseTransitions() const { return m_transitions; }

    // ── Configuration ────────────────────────────────────────────────

    void SetBackbufferSize(uint32_t width, uint32_t height);

    // Register a pixel shader hash as a known sky shader.
    void RegisterSkyShaderHash(uint64_t hash);

    // Access the main depth stencil view detected during GeometryMain.
    // Returns nullptr if no main DSV was identified this frame.
    ID3D11DepthStencilView* GetMainDSV() const { return m_mainDSV; }

    // ── Phase change notification ────────────────────────────────────

    using PhaseChangeCallback = void(*)(RenderPhase oldPhase, RenderPhase newPhase);
    void RegisterPhaseChangeCallback(PhaseChangeCallback cb);

private:
    RenderPhaseDetector() = default;

    void SetPhase(RenderPhase newPhase);
    bool IsMainRT(ID3D11RenderTargetView* rtv) const;
    bool IsShadowMapViewport(float w, float h) const;

    RenderPhase m_currentPhase = RenderPhase::Unknown;

    // Backbuffer tracking
    uint32_t m_bbWidth  = 0;
    uint32_t m_bbHeight = 0;
    ID3D11RenderTargetView*  m_mainRTV = nullptr;  // first full-res color RT seen
    ID3D11DepthStencilView*  m_mainDSV = nullptr;  // main depth

    // Per-frame stats
    uint32_t m_drawsPerPhase[9] = {};
    uint32_t m_transitions = 0;

    // Sky shader identification
    std::unordered_set<uint64_t> m_skyShaderHashes;

    // Current bound state
    uint64_t m_currentPSHash = 0;
    uint64_t m_currentVSHash = 0;
    float    m_currentViewportW = 0;
    float    m_currentViewportH = 0;
    bool     m_hasColorRT = false;
    bool     m_hasDepth   = false;

    // Phase change callbacks
    std::vector<PhaseChangeCallback> m_phaseCallbacks;

    // Frame counter (for log spam control)
    uint32_t m_frameCount = 0;
    uint32_t m_totalDrawsThisFrame = 0;

    // Extended logging: first N "gameplay" frames (>500 draws/frame)
    // Threshold set above main menu (~166 draws) to capture actual world rendering.
    uint32_t m_gameplayFramesLogged = 0;
    static constexpr uint32_t kMaxGameplayFrameLogs = 30;
    static constexpr uint32_t kGameplayDrawThreshold = 500;
};

} // namespace SB::Proxy
