#pragma once

//=============================================================================
//  ENBGuiIntegration — a live SkyrimBridge bar inside the ENB editor
//
//  ENBSeries 0.504 draws its editor windows with an embedded AntTweakBar and
//  exports the full Tw* API from d3d11.dll. This module resolves those
//  exports and creates one read-only bar ("SkyrimBridge") whose variables
//  point at a per-frame snapshot of AllData. ENB owns the draw: the bar is
//  visible whenever the editor is open (Shift+Enter) and hidden otherwise,
//  with no render hook of our own.
//
//  Failure is silent-by-design: if the Tw* exports are absent (no ENB, or a
//  future binary that drops them) the module logs once and every call
//  becomes a no-op.
//=============================================================================

namespace SB
{
    struct AllData;
}

namespace ENBGuiIntegration
{
    // Resolve the Tw* exports from the ENB module. Call after
    // ENBInterface::Init() succeeds. The bar itself is created lazily on
    // the first Update() calls, once ENB has initialized its ATB context.
    bool Init();

    // Per-frame from BeginFrame, after ENBInterface::PushAllData():
    // copy the fields the bar displays into the snapshot and create the
    // bar if it does not exist yet.
    void Update(const SB::AllData& a_data);

    // Device-reset handling: ATB objects must not survive the reset.
    void OnPreReset();
    void OnPostReset();

    // Destroy the bar and drop the resolved exports.
    void Shutdown();
}
