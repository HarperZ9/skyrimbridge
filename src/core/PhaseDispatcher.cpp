#include "PhaseDispatcher.h"
#include "D3D11StateBackup.h"
#include "RenderPipeline.h"
#include "SceneData.h"
#include "FrameCapture.h"
#include <SKSE/SKSE.h>

namespace SB
{

// ── RenderPhase values (must match d3d11_proxy/RenderPhaseDetector.h) ────
// We use raw uint8_t to avoid including proxy headers in the SKSE plugin.
namespace Phase {
    constexpr uint8_t Unknown       = 0;
    constexpr uint8_t DepthPrepass  = 1;
    constexpr uint8_t ShadowMap     = 2;
    constexpr uint8_t GeometryMain  = 3;
    constexpr uint8_t Decals        = 4;
    constexpr uint8_t Sky           = 5;
    constexpr uint8_t AlphaBlend    = 6;
    constexpr uint8_t PostProcess   = 7;
    constexpr uint8_t UI            = 8;
}


bool PhaseDispatcher::Initialize(ID3D11DeviceContext* realContext,
                                  void (*invalidateCache)())
{
    if (m_initialized) return true;
    if (!realContext) return false;

    m_realContext     = realContext;
    m_invalidateCache = invalidateCache;
    m_initialized    = true;
    m_enabled        = true;

    ResetFrame();

    SKSE::log::info("PhaseDispatcher: initialized (mid-frame dispatch enabled)");
    return true;
}

void PhaseDispatcher::Shutdown()
{
    m_initialized     = false;
    m_realContext      = nullptr;
    m_invalidateCache = nullptr;
}


void PhaseDispatcher::ResetFrame()
{
    for (auto& d : m_dispatched)
        d = false;
}


void PhaseDispatcher::OnPhaseChange(uint8_t oldPhase, uint8_t newPhase)
{
    // Diagnostic: log first 30 callbacks to verify proxy→SKSE wiring
    {
        static int s_cbCount = 0;
        if (s_cbCount++ < 30) {
            SKSE::log::info("PhaseDispatcher::OnPhaseChange({} -> {}) init={} en={} dispatching={}",
                oldPhase, newPhase, m_initialized, m_enabled, m_dispatching);
        }
    }

    if (!m_initialized || !m_enabled) return;

    // Re-entrancy guard: if an effect triggers a state change that fires
    // another phase transition, don't recurse
    if (m_dispatching) return;

    // Phase → Unknown means frame reset (OnPresent)
    if (newPhase == Phase::Unknown) {
        ResetFrame();
        return;
    }

    auto& pipeline = RenderPipeline::Get();
    if (!pipeline.IsInitialized()) return;

    // Map phase transitions to pipeline stages.
    // Each stage fires AT MOST ONCE per frame (guarded by m_dispatched[]).
    PipelineStage stage = PipelineStage::Count;  // sentinel = no dispatch

    // DepthPrepass → anything else = depth prepass complete
    if (oldPhase == Phase::DepthPrepass && newPhase != Phase::DepthPrepass) {
        stage = PipelineStage::PostDepthPrepass;
    }
    // GeometryMain → Sky/AlphaBlend/Decals/PostProcess = opaque geometry complete
    else if (oldPhase == Phase::GeometryMain &&
             (newPhase == Phase::Sky || newPhase == Phase::AlphaBlend ||
              newPhase == Phase::Decals || newPhase == Phase::PostProcess)) {
        stage = PipelineStage::PostGeometry;
    }
    // Sky → AlphaBlend/PostProcess = sky rendering complete
    else if (oldPhase == Phase::Sky &&
             (newPhase == Phase::AlphaBlend || newPhase == Phase::PostProcess)) {
        stage = PipelineStage::PostSky;
    }
    // PostProcess → UI = last chance before HUD rendering
    else if (oldPhase == Phase::PostProcess && newPhase == Phase::UI) {
        stage = PipelineStage::PreUI;
    }

    // Log phase transition to frame capture
    {
        auto& cap = FrameCapture::Get();
        if (cap.IsCapturing()) {
            CapturedPhase cp;
            cp.oldPhase     = oldPhase;
            cp.newPhase     = newPhase;
            cp.mappedStage  = (stage != PipelineStage::Count)
                                  ? static_cast<uint8_t>(stage) : 0xFF;
            cp.dispatched   = (stage != PipelineStage::Count);
            cap.LogPhase(cp);
        }
    }

    if (stage == PipelineStage::Count) return;  // no matching transition

    auto stageIdx = static_cast<uint8_t>(stage);
    if (stageIdx >= 8) return;

    // Don't dispatch the same stage twice per frame
    if (m_dispatched[stageIdx]) return;
    m_dispatched[stageIdx] = true;

    // ── Dispatch! ────────────────────────────────────────────────────
    m_dispatching = true;

    // Log first 60 dispatches, PLUS always log non-PostDepthPrepass stages
    // (PostGeometry/PreUI are rare and critical to verify)
    if (m_dispatchCount < 60 || stage != PipelineStage::PostDepthPrepass) {
        SKSE::log::info("PhaseDispatcher: dispatching {} (phase {}->{}, dispatch #{})",
            PipelineStageName(stage), oldPhase, newPhase, m_dispatchCount);
    }

    // Update SceneMatrices from NiCamera so effects have live camera data.
    // This reads NiCamera directly — no dependency on tracker system.
    SceneMatrices::Get().UpdateFromNiCamera();

    // Save full D3D11 state on the REAL context.
    // backup.rtvs[0] / backup.dsv are the game's currently-bound targets —
    // effects use these to read scene color and write composite output.
    D3D11StateBackup backup;
    backup.Save(m_realContext);

    // Unbind all OM targets to prevent D3D11 read-write hazards.
    // The game's DSV is still bound after Save(), and effects need to
    // read the same depth texture via SRV.  D3D11 silently unbinds SRVs
    // when the same resource is bound as both DSV and SRV.  Clearing the
    // OM bindings first allows effects to read depth without conflict.
    {
        ID3D11RenderTargetView* nullRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        m_realContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                          nullRTVs, nullptr);
    }

    // Execute all passes registered at this stage, passing the game's
    // active RTV/DSV so effects can target the correct render target.
    pipeline.ExecuteStage(stage, 0.0f, nullptr, backup.rtvs[0], backup.dsv);

    // Restore state so the game's next phase starts correctly
    backup.Restore(m_realContext);

    // Invalidate proxy's state cache — our effects modified state on the
    // real context, so the proxy's redundancy filter is out of sync
    if (m_invalidateCache)
        m_invalidateCache();

    ++m_dispatchCount;
    m_dispatching = false;
}

} // namespace SB
