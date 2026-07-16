#pragma once
//=============================================================================
//  D3D11Hook.h — D3D11 integration (proxy-aware + legacy vtable fallback)
//
//  Two modes:
//    1. Proxy mode: SkyrimBridge d3d11.dll proxy is loaded. Gets D3D11 objects
//       via SB_GetProxyInterface() and registers a PrePresent callback.
//    2. Legacy mode: No proxy (e.g. running with ENB). Falls back to vtable
//       hook on the game's swap chain (original behavior).
//=============================================================================

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct IDXGISwapChain;

namespace D3D11Hook
{
    // Initialize the D3D11 hook (auto-detects proxy vs legacy)
    bool Init();

    // Shutdown and restore original functions
    void Shutdown();

    // Toggle GUI visibility
    void ToggleGUI();

    // Check if GUI is visible
    bool IsGUIVisible();

    // Set GUI visibility
    void SetGUIVisible(bool a_visible);

    // ── Game input control ──────────────────────────────────────────────
    bool ShouldFreezeInput();
    void UpdateInputFreeze();

    // D3D11 resource accessors
    ID3D11Device*        GetDevice();
    ID3D11DeviceContext* GetContext();
    IDXGISwapChain*      GetSwapChain();

    // ── Proxy detection ─────────────────────────────────────────────────
    bool IsProxyActive();     // true if running with our d3d11.dll proxy
    bool IsHDREnabled();      // true if HDR output is active
    bool IsHDRCapable();      // true if display supports HDR

    // ── Material Pipeline (proxy only) ────────────────────────────────
    bool IsMaterialPipelineActive();
    void SetMaterialPipelineEnabled(bool enabled);
    uint32_t GetMaterialPatchedCount();
    uint32_t GetMaterialCandidateCount();
    uint32_t GetMaterialClassifiedCount();

    // ── G-buffer & Depth SRVs (proxy only) ────────────────────────────
    // Returns nullptr when proxy not active or resource not available.
    ID3D11ShaderResourceView* GetGameDepthSRV();
    ID3D11ShaderResourceView* GetGBufferAlbedoSRV();
    ID3D11ShaderResourceView* GetGBufferNormalsSRV();
    ID3D11ShaderResourceView* GetGBufferMaterialSRV();

    // ── Pre-UI scene capture (proxy only) ───────────────────────────
    // Returns the backbuffer snapshot taken before Scaleform UI rendered.
    // Post-processing systems should sample from this instead of copying
    // the live backbuffer to avoid processing HUD/menu pixels.
    // Returns nullptr if capture is not available this frame.
    ID3D11ShaderResourceView* GetPreUISceneSRV();
    ID3D11Texture2D*          GetPreUISceneTex();
    bool                      IsPreUISceneValid();

    // ── Proxy state cache invalidation ───────────────────────────────
    // Returns the proxy's InvalidateStateCache function pointer, or nullptr
    // if the proxy is not active.  Used by PhaseDispatcher for mid-frame
    // state cache resync after effect dispatch.
    using InvalidateCacheFn = void(*)();
    InvalidateCacheFn GetInvalidateCacheFn();
}
