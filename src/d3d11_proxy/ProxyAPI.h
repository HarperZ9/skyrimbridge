#pragma once
//=============================================================================
//  ProxyAPI — Communication interface between Playground d3d11 proxy and
//  SKSE plugin.
//
//  The SKSE plugin finds this via:
//    auto mod = GetModuleHandleA("d3d11.dll");
//    auto fn  = (PG_GetProxyInterfaceFn)GetProcAddress(mod, "PG_GetProxyInterface");
//    auto* pi = fn();
//=============================================================================

#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>

// Forward-declare RenderPhase (defined in RenderPhaseDetector.h)
namespace SB::Proxy { enum class RenderPhase : uint8_t; }

namespace SB::Proxy
{

// Callback types for SKSE plugin registration
using PrePresentCallback   = void(*)(ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
using OnDrawCallback       = void(*)(uint32_t vertexOrIndexCount, uint32_t instances);
using OnRTChangeCallback   = void(*)(uint32_t numRTVs, ID3D11RenderTargetView* const* rtvs,
                                     ID3D11DepthStencilView* dsv);
using OnShaderBindCallback = void(*)(ID3D11PixelShader* ps, ID3D11VertexShader* vs);
using OnResizeCallback     = void(*)(uint32_t width, uint32_t height, DXGI_FORMAT format);

// Phase change callback — fires synchronously during game rendering when
// RenderPhaseDetector transitions between render phases.  The SKSE plugin
// uses this to dispatch mid-frame compute/render passes at the correct
// pipeline stage (e.g., PostGeometry for SSGI, PreUI for tonemapping).
// oldPhase/newPhase are RenderPhase enum values cast to uint8_t.
using OnPhaseChangeCallback = void(*)(uint8_t oldPhase, uint8_t newPhase);

// Per-frame optimization statistics (populated in Present, read by SKSE debug GUI)
struct OptimizationStats
{
    // CB dirty tracking
    uint32_t cbMapsIntercepted;     // CB Map calls tracked by dirty tracker
    uint32_t cbUpdatesSkipped;      // Updates skipped (data unchanged)
    uint32_t cbUpdatesCommitted;    // Updates committed (data changed)
    uint32_t cbTrackedBuffers;      // Currently tracked buffer count

    // State cache redundancy filtering
    uint32_t srvCallsRedundant;
    uint32_t srvCallsTotal;
    uint32_t blendCallsRedundant;
    uint32_t blendCallsTotal;
    uint32_t dsCallsRedundant;
    uint32_t dsCallsTotal;
    uint32_t rsCallsRedundant;
    uint32_t rsCallsTotal;

    // Occlusion culling
    uint32_t occDrawsTested;
    uint32_t occDrawsCulled;
};

struct ProxyInterface
{
    // ── Version ──────────────────────────────────────────────────────
    uint32_t              version;           // API version (1)

    // ── Core D3D11 objects (real, unwrapped) ─────────────────────────
    ID3D11Device*         device;
    ID3D11DeviceContext*  context;
    IDXGISwapChain*       swapChain;

    // ── HDR state ────────────────────────────────────────────────────
    bool                  hdrCapable;        // display supports HDR
    bool                  hdrEnabled;        // HDR output active
    DXGI_FORMAT           backbufferFormat;  // current swap chain format

    // ── Frame statistics ─────────────────────────────────────────────
    uint32_t              drawCallsThisFrame;
    uint32_t              rtSwitchesThisFrame;
    uint32_t              shaderChangesThisFrame;
    uint32_t              frameCount;

    // ── Callback registration ────────────────────────────────────────
    void (*RegisterPrePresent)(PrePresentCallback cb);
    void (*RegisterOnDraw)(OnDrawCallback cb);
    void (*RegisterOnRTChange)(OnRTChangeCallback cb);
    void (*RegisterOnShaderBind)(OnShaderBindCallback cb);
    void (*RegisterOnResize)(OnResizeCallback cb);

    // ── HDR control ──────────────────────────────────────────────────
    void (*SetHDREnabled)(bool enabled);
    float                 hdrMaxNits;
    float                 hdrPaperWhite;

    // ── Render phase detection ───────────────────────────────────────
    RenderPhase           currentPhase;        // updated every RT switch / shader bind
    const char*         (*GetPhaseName)();      // human-readable current phase

    // ── Material Pipeline (3-target G-buffer extraction) ────────────────
    void (*SetMaterialPipelineEnabled)(bool enabled);
    bool                  materialPipelineActive;    // true when extraction is running
    uint32_t              materialPatchedCount;      // shaders successfully patched
    uint32_t              materialCandidateCount;    // shaders identified as BSLighting
    uint32_t              materialClassifiedCount;   // shaders with material type classified

    // ── G-buffer SRVs ────────────────────────────────────────────────
    bool                  deferredActive;
    ID3D11ShaderResourceView* gBufferAlbedo;     // RT1: diffuse.rgb + opacity.a
    ID3D11ShaderResourceView* gBufferNormals;    // RT2: normal.rgb + specMask.a
    ID3D11ShaderResourceView* gBufferMaterial;   // RT3: metallic.r + roughness.g + sss.b + matID.a
    ID3D11DepthStencilView*   gameDepthDSV;
    ID3D11ShaderResourceView* gameDepthSRV;

    // ── Optimization statistics (updated each frame in Present) ──────
    OptimizationStats         optStats;

    // ── Pre-UI scene capture (for UI-safe post-processing) ──────────
    // Captured when RenderPhaseDetector transitions to UI phase.
    // Contains the scene backbuffer BEFORE Scaleform HUD/menus are drawn.
    // Post-processing should operate on this instead of the live backbuffer
    // to avoid bloom/DoF/color grading affecting UI elements.
    ID3D11ShaderResourceView* preUISceneSRV;       // null if not captured this frame
    ID3D11Texture2D*          preUISceneTex;        // the texture behind the SRV
    bool                      preUISceneValid;      // true if capture succeeded this frame

    // ── State cache invalidation ─────────────────────────────────────
    // MUST be called after any code modifies D3D11 state through the real
    // (unwrapped) context. The proxy caches BlendState, DepthStencilState,
    // RasterizerState, and PS SRVs — bypassing the wrapper desyncs the cache
    // and causes the game's subsequent state calls to be incorrectly skipped.
    void (*InvalidateStateCache)();

    // ── Phase change callback registration ──────────────────────────
    // Register a callback that fires synchronously during game rendering
    // when RenderPhaseDetector transitions between phases.  This enables
    // mid-frame effect dispatch (SSGI at PostGeometry, clouds at PostSky,
    // tonemapping at PreUI) instead of running everything at Present time.
    void (*RegisterOnPhaseChange)(OnPhaseChangeCallback cb);
};

} // namespace SB::Proxy

// Safe mode check — when true, all proxy optimizations/interceptions are disabled
// (pure passthrough except Present callbacks). Set from d3d11_proxy.ini [General] SafeMode=1
bool PG_IsSafeMode();

// Exported C functions (both old SB_ and new PG_ names for transition)
extern "C" __declspec(dllexport) SB::Proxy::ProxyInterface* SB_GetProxyInterface();
extern "C" __declspec(dllexport) SB::Proxy::ProxyInterface* PG_GetProxyInterface();
