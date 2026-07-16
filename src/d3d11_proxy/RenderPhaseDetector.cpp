//=============================================================================
//  RenderPhaseDetector.cpp — Render phase classification implementation
//=============================================================================

#include "RenderPhaseDetector.h"
#include "ProxyLog.h"

namespace SB::Proxy
{

// ── Phase name table ─────────────────────────────────────────────────────

static constexpr const char* kPhaseNames[] = {
    "Unknown",
    "DepthPrepass",
    "ShadowMap",
    "GeometryMain",
    "Decals",
    "Sky",
    "AlphaBlend",
    "PostProcess",
    "UI"
};

static_assert(sizeof(kPhaseNames) / sizeof(kPhaseNames[0]) == 9,
              "kPhaseNames must match RenderPhase enum count");

const char* RenderPhaseDetector::GetPhaseName() const
{
    return PhaseName(m_currentPhase);
}

const char* RenderPhaseDetector::PhaseName(RenderPhase phase)
{
    auto idx = static_cast<uint8_t>(phase);
    if (idx < 9)
        return kPhaseNames[idx];
    return "Invalid";
}

// ── Configuration ────────────────────────────────────────────────────────

void RenderPhaseDetector::SetBackbufferSize(uint32_t width, uint32_t height)
{
    m_bbWidth  = width;
    m_bbHeight = height;
    Log("RenderPhaseDetector: backbuffer size set to %ux%u", width, height);
}

void RenderPhaseDetector::RegisterSkyShaderHash(uint64_t hash)
{
    m_skyShaderHashes.insert(hash);
    Log("RenderPhaseDetector: registered sky shader hash %016llX", hash);
}

void RenderPhaseDetector::RegisterPhaseChangeCallback(PhaseChangeCallback cb)
{
    if (cb)
        m_phaseCallbacks.push_back(cb);
}

// ── Per-frame reset ──────────────────────────────────────────────────────

void RenderPhaseDetector::OnPresent()
{
    // Log summary for first 60 frames (loading screen)
    bool shouldLog = (m_frameCount < 60);

    // Also log first 30 "gameplay" frames (>500 draws = actual world rendering,
    // above main menu threshold of ~166 draws)
    if (!shouldLog && m_totalDrawsThisFrame > kGameplayDrawThreshold &&
        m_gameplayFramesLogged < kMaxGameplayFrameLogs) {
        shouldLog = true;
        ++m_gameplayFramesLogged;
    }

    if (shouldLog) {
        Log("RenderPhaseDetector: frame %u  totalDraws=%u  transitions=%u  draws=[Dp=%u Sh=%u Geo=%u Dec=%u Sky=%u AB=%u PP=%u UI=%u Unk=%u]",
            m_frameCount, m_totalDrawsThisFrame, m_transitions,
            m_drawsPerPhase[static_cast<uint8_t>(RenderPhase::DepthPrepass)],
            m_drawsPerPhase[static_cast<uint8_t>(RenderPhase::ShadowMap)],
            m_drawsPerPhase[static_cast<uint8_t>(RenderPhase::GeometryMain)],
            m_drawsPerPhase[static_cast<uint8_t>(RenderPhase::Decals)],
            m_drawsPerPhase[static_cast<uint8_t>(RenderPhase::Sky)],
            m_drawsPerPhase[static_cast<uint8_t>(RenderPhase::AlphaBlend)],
            m_drawsPerPhase[static_cast<uint8_t>(RenderPhase::PostProcess)],
            m_drawsPerPhase[static_cast<uint8_t>(RenderPhase::UI)],
            m_drawsPerPhase[static_cast<uint8_t>(RenderPhase::Unknown)]);
    }

    // Notify listeners of frame boundary (current phase → Unknown).
    // PhaseDispatcher uses this to reset per-frame dispatch flags.
    // Must happen BEFORE resetting state so the callback sees the transition.
    SetPhase(RenderPhase::Unknown);

    // Reset per-frame state (m_currentPhase already Unknown from SetPhase)
    for (auto& c : m_drawsPerPhase)
        c = 0;
    m_transitions = 0;
    m_totalDrawsThisFrame = 0;
    m_mainRTV = nullptr;
    m_mainDSV = nullptr;
    m_hasColorRT = false;
    m_hasDepth   = false;
    m_currentPSHash = 0;
    m_currentVSHash = 0;
    m_currentViewportW = 0;
    m_currentViewportH = 0;

    ++m_frameCount;
}

// ── RT change ────────────────────────────────────────────────────────────

void RenderPhaseDetector::OnRTChange(
    uint32_t numRTVs, ID3D11RenderTargetView* const* rtvs,
    ID3D11DepthStencilView* dsv)
{
    // Determine what's bound
    m_hasDepth = (dsv != nullptr);
    m_hasColorRT = false;

    ID3D11RenderTargetView* firstRTV = nullptr;
    for (uint32_t i = 0; i < numRTVs; ++i) {
        if (rtvs && rtvs[i]) {
            m_hasColorRT = true;
            if (!firstRTV)
                firstRTV = rtvs[i];
        }
    }

    // Depth-only (no color RT bound)
    if (!m_hasColorRT && m_hasDepth) {
        // Distinguish ShadowMap vs DepthPrepass based on viewport size
        if (m_bbWidth > 0 && IsShadowMapViewport(m_currentViewportW, m_currentViewportH)) {
            SetPhase(RenderPhase::ShadowMap);
        } else {
            SetPhase(RenderPhase::DepthPrepass);
        }
        return;
    }

    // Color RT bound — identify what kind of pass
    if (m_hasColorRT && firstRTV) {
        // First time seeing a full-resolution color RT with depth → main RT
        if (m_mainRTV == nullptr && m_hasDepth && m_bbWidth > 0) {
            // Check if this RTV is full-resolution (we can't query the desc from
            // the RTV pointer alone without AddRef/GetResource overhead, so we
            // rely on the viewport matching the backbuffer dimensions)
            bool fullRes = (m_currentViewportW >= m_bbWidth * 0.9f &&
                            m_currentViewportH >= m_bbHeight * 0.9f);
            if (fullRes) {
                m_mainRTV = firstRTV;
                m_mainDSV = dsv;
                SetPhase(RenderPhase::GeometryMain);
                return;
            }
        }

        // Recognizable main RT
        if (IsMainRT(firstRTV)) {
            if (m_hasDepth) {
                SetPhase(RenderPhase::GeometryMain);
            } else {
                SetPhase(RenderPhase::PostProcess);
            }
            return;
        }

        // Color RT bound but not the main one, and no depth → PostProcess or UI
        if (!m_hasDepth) {
            // If viewport doesn't match backbuffer, could be UI
            if (m_bbWidth > 0 &&
                (m_currentViewportW < m_bbWidth * 0.9f ||
                 m_currentViewportH < m_bbHeight * 0.9f)) {
                SetPhase(RenderPhase::UI);
            } else {
                SetPhase(RenderPhase::PostProcess);
            }
        }
    }
}

// ── Shader bind ──────────────────────────────────────────────────────────

void RenderPhaseDetector::OnShaderBind(uint64_t psHash, uint64_t vsHash)
{
    if (psHash != 0)
        m_currentPSHash = psHash;
    if (vsHash != 0)
        m_currentVSHash = vsHash;

    // Check sky shader hashes
    if (m_currentPSHash != 0 &&
        m_skyShaderHashes.find(m_currentPSHash) != m_skyShaderHashes.end()) {
        SetPhase(RenderPhase::Sky);
    }
}

// ── Draw call ────────────────────────────────────────────────────────────

void RenderPhaseDetector::OnDraw(uint32_t vertexOrIndexCount, bool isIndexed)
{
    ++m_totalDrawsThisFrame;

    // Track draws in current phase
    auto idx = static_cast<uint8_t>(m_currentPhase);
    if (idx < 9)
        m_drawsPerPhase[idx]++;

    // Detect fullscreen triangle: Draw(3, 0) non-indexed → PostProcess.
    // Skyrim's BSImageSpaceShader always uses Draw(3, 0) for fullscreen passes.
    // NOTE: Skyrim keeps the DSV bound during post-processing (it only disables
    // depth testing via DepthStencilState), so we do NOT require !m_hasDepth.
    // Guard: require enough GeometryMain draws to rule out early setup passes.
    if (!isIndexed && vertexOrIndexCount == 3) {
        if (m_currentPhase == RenderPhase::GeometryMain &&
            m_drawsPerPhase[static_cast<uint8_t>(RenderPhase::GeometryMain)] >= 10) {
            SetPhase(RenderPhase::PostProcess);
        }
    }

    // Detect UI: Scaleform uses DrawIndexed (not Draw(3,0)).
    // BSImageSpaceShader always uses non-indexed Draw(3,0).
    // The first DrawIndexed during PostProcess signals UI rendering start.
    // No depth check — Skyrim may keep DSV bound during UI too.
    if (isIndexed && m_currentPhase == RenderPhase::PostProcess) {
        SetPhase(RenderPhase::UI);
    }
}

// ── Viewport change ──────────────────────────────────────────────────────

void RenderPhaseDetector::OnViewportChange(float width, float height)
{
    m_currentViewportW = width;
    m_currentViewportH = height;

    // If viewport is significantly smaller than backbuffer → ShadowMap
    if (m_bbWidth > 0 && IsShadowMapViewport(width, height)) {
        // Only transition to ShadowMap if we're in a depth-only configuration
        if (!m_hasColorRT && m_hasDepth) {
            SetPhase(RenderPhase::ShadowMap);
        }
    }
}

// ── Clear depth ──────────────────────────────────────────────────────────

void RenderPhaseDetector::OnClearDepth(ID3D11DepthStencilView* dsv)
{
    // Clearing the main DSV often signals a major phase boundary
    // (e.g., shadow maps done → main geometry starting)
    if (m_mainDSV != nullptr && dsv == m_mainDSV) {
        if (m_frameCount < 60) {
            Log("RenderPhaseDetector: main DSV cleared (phase=%s)",
                PhaseName(m_currentPhase));
        }
    }
}

// ── Statistics ───────────────────────────────────────────────────────────

uint32_t RenderPhaseDetector::GetDrawsInPhase(RenderPhase phase) const
{
    auto idx = static_cast<uint8_t>(phase);
    if (idx < 9)
        return m_drawsPerPhase[idx];
    return 0;
}

// ── Internal ─────────────────────────────────────────────────────────────

void RenderPhaseDetector::SetPhase(RenderPhase newPhase)
{
    if (newPhase == m_currentPhase)
        return;

    RenderPhase oldPhase = m_currentPhase;
    m_currentPhase = newPhase;
    ++m_transitions;

    // Log transitions for first 60 frames + gameplay frames
    if (m_frameCount < 60 ||
        (m_totalDrawsThisFrame > kGameplayDrawThreshold &&
         m_gameplayFramesLogged < kMaxGameplayFrameLogs)) {
        Log("RenderPhaseDetector: %s -> %s  (frame %u, transition #%u, draws=%u)",
            PhaseName(oldPhase), PhaseName(newPhase),
            m_frameCount, m_transitions, m_totalDrawsThisFrame);
    }

    // Fire callbacks
    for (auto cb : m_phaseCallbacks) {
        if (cb)
            cb(oldPhase, newPhase);
    }
}

bool RenderPhaseDetector::IsMainRT(ID3D11RenderTargetView* rtv) const
{
    return (m_mainRTV != nullptr && rtv == m_mainRTV);
}

bool RenderPhaseDetector::IsShadowMapViewport(float w, float h) const
{
    if (m_bbWidth == 0 || m_bbHeight == 0)
        return false;
    return (w < m_bbWidth * 0.8f || h < m_bbHeight * 0.8f);
}

} // namespace SB::Proxy
