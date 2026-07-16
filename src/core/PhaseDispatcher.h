#pragma once
//=============================================================================
//  PhaseDispatcher — Maps proxy render phase transitions to pipeline stages
//
//  Registered as a phase-change callback with the d3d11 proxy.  When the
//  RenderPhaseDetector fires a phase transition (e.g., GeometryMain → Sky),
//  the PhaseDispatcher maps it to the appropriate PipelineStage and calls
//  RenderPipeline::ExecuteStage().
//
//  This enables mid-frame effect dispatch: effects run at the correct point
//  in the game's render pipeline with live depth, scene color, and G-buffer
//  data — instead of running everything at Present time with stale data.
//
//  Flow:
//    Game calls OMSetRenderTargets (switching from geometry to sky)
//    → WrappedContext detects phase change (GeometryMain → Sky)
//    → PhaseDispatcher::OnPhaseChange fires
//    → Saves full D3D11 state on REAL context
//    → Executes PostGeometry pipeline stage (SSGI, GTAO, etc.)
//    → Restores full D3D11 state
//    → Invalidates proxy state cache
//    → Control returns to WrappedContext
//    → Game's new OMSetRenderTargets applied to real context
//
//  Thread safety: Single-threaded. All calls happen on the render thread.
//=============================================================================

#include <d3d11.h>
#include <cstdint>

namespace SB
{

class PhaseDispatcher
{
public:
    static PhaseDispatcher& Get()
    {
        static PhaseDispatcher inst;
        return inst;
    }

    /// Initialize with the real (unwrapped) D3D11 context and the proxy's
    /// InvalidateStateCache function pointer.
    /// Call after TryProxyInit succeeds and RenderPipeline is initialized.
    bool Initialize(ID3D11DeviceContext* realContext,
                    void (*invalidateCache)());

    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    /// Called from the proxy's phase-change callback.
    /// oldPhase/newPhase are RenderPhase enum values (uint8_t).
    /// This fires synchronously during game rendering — must be fast.
    void OnPhaseChange(uint8_t oldPhase, uint8_t newPhase);

    /// Enable/disable mid-frame dispatch (F7 kill switch).
    bool IsEnabled() const     { return m_enabled; }
    void SetEnabled(bool v)    { m_enabled = v; }

    /// Statistics
    uint32_t GetDispatchCount() const { return m_dispatchCount; }

private:
    PhaseDispatcher() = default;

    bool m_initialized = false;
    bool m_enabled     = true;

    ID3D11DeviceContext* m_realContext    = nullptr;
    void (*m_invalidateCache)()          = nullptr;

    // Re-entrancy guard — prevents recursive dispatch if an effect
    // triggers a phase change through the wrapped context
    bool m_dispatching = false;

    uint32_t m_dispatchCount = 0;

    // Track which stages have been dispatched this frame to avoid duplicates
    // (a phase transition can fire multiple times per frame)
    bool m_dispatched[8] = {};  // indexed by PipelineStage

    /// Reset per-frame tracking.  Called when phase resets to Unknown (OnPresent).
    void ResetFrame();
};

} // namespace SB
