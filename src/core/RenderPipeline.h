#pragma once
//=============================================================================
//  RenderPipeline — Pass orchestration framework for SkyrimBridge
//
//  Organizes custom render/compute passes into pipeline stages with
//  automatic execution ordering, managed render targets, and clean
//  integration into the frame lifecycle.
//
//  Pipeline stages (in frame order):
//
//    PostDepthPrepass  After depth-only rendering; Hi-Z ready
//    PostGeometry      After opaque geometry; G-buffer + depth valid
//    PostSky           After sky dome; clouds/atmosphere inject here
//    PreUI             Before Scaleform HUD; last chance for scene ops
//    PrePresent        Inside Present; ImGui/debug overlay only
//
//  Pass types:
//    Rasterized    Fullscreen VS+PS via RenderPassManager::Execute()
//    Compute       CS dispatch via ComputeManager or direct context calls
//    Custom        Arbitrary lambda for one-off operations
//
//  Managed RT pool:
//    Request a render target by name+format+scale.  The pipeline caches
//    them and handles lifetime.  Multiple passes can share RTs by name.
//
//  Usage:
//    auto& pl = RenderPipeline::Get();
//    auto rt = pl.GetOrCreateRT("MyBuffer", DXGI_FORMAT_R16G16B16A16_FLOAT);
//
//    pl.AddPass({
//        .name     = "SSAO",
//        .stage    = PipelineStage::PostGeometry,
//        .priority = 10,
//        .execute  = [&](PassContext& ctx) {
//            RenderPassManager::Get().Execute({ .passID = ssaoID, .rtv = rt.rtv, ... });
//        },
//    });
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>

namespace SB
{

// ─── Pipeline stages (frame execution order) ────────────────────────────
//
// Mid-frame stages fire from proxy phase-change callbacks DURING game
// rendering.  Effects dispatched here get live depth/scene data and run
// before UI is drawn, eliminating format mismatches and UI corruption.
//
// Present-time stages fire inside Present, after the game finishes.

enum class PipelineStage : uint8_t
{
    // ── Mid-frame (fired by PhaseDispatcher during game rendering) ────
    PostDepthPrepass,  // After depth-only rendering; Hi-Z ready
    PostGeometry,      // After opaque geometry; G-buffer + depth valid
    PostSky,           // After sky dome; clouds/atmosphere inject here
    PreUI,             // Before Scaleform HUD; last chance for scene ops

    // ── Present-time (fired from DoOverlayWork) ─────────────────────
    PrePresent,        // Inside Present — ImGui/debug overlay only
    Count
};

const char* PipelineStageName(PipelineStage stage);


// ─── Managed render target ──────────────────────────────────────────────

struct ManagedRT
{
    std::string             name;
    ID3D11Texture2D*        texture = nullptr;
    ID3D11RenderTargetView* rtv     = nullptr;
    ID3D11ShaderResourceView* srv   = nullptr;
    ID3D11UnorderedAccessView* uav  = nullptr;
    DXGI_FORMAT             format  = DXGI_FORMAT_UNKNOWN;
    uint32_t                width   = 0;
    uint32_t                height  = 0;

    bool Valid() const { return texture != nullptr; }
    void Release();
};


// ─── Pass context (provided to execute callbacks) ───────────────────────

struct PassContext
{
    ID3D11Device*         device    = nullptr;
    ID3D11DeviceContext*  context   = nullptr;
    IDXGISwapChain*       swapChain = nullptr;
    uint32_t              screenW   = 0;
    uint32_t              screenH   = 0;
    uint32_t              frameIndex = 0;
    float                 deltaTime  = 0.0f;

    // Game's currently-bound render target at the time of mid-frame dispatch.
    // At PostGeometry/PreUI, this is the scene RT the game was rendering to.
    // At PrePresent, these are nullptr (effects use swapChain->GetBuffer).
    // Effects that need scene color should CopyResource from the underlying
    // texture (via GetResource), then read via SRV.
    ID3D11RenderTargetView*  gameSceneRTV = nullptr;
    ID3D11DepthStencilView*  gameSceneDSV = nullptr;
};


// ─── Pass definition ────────────────────────────────────────────────────

using PassExecuteFn = std::function<void(PassContext& ctx)>;

struct PassDef
{
    const char*    name     = "unnamed";
    PipelineStage  stage    = PipelineStage::PostGeometry;
    int            priority = 0;      // Lower runs first within stage
    bool           enabled  = true;

    // The execute callback.  Called once per frame at the pass's stage.
    // The pipeline provides a PassContext with device/context/timing.
    PassExecuteFn  execute;
};

using PassHandle = uint32_t;  // 0 = invalid


// ─── Pipeline orchestrator ──────────────────────────────────────────────

class RenderPipeline
{
public:
    static RenderPipeline& Get()
    {
        static RenderPipeline instance;
        return instance;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                    IDXGISwapChain* sc);
    void Shutdown();

    // ── Pass management ──────────────────────────────────────────────

    /// Register a pass.  Returns a handle for enable/disable/removal.
    PassHandle AddPass(const PassDef& def);

    /// Enable or disable a pass by handle.
    void SetPassEnabled(PassHandle handle, bool enabled);

    /// Remove a pass entirely.
    void RemovePass(PassHandle handle);

    // ── Managed RT pool ──────────────────────────────────────────────

    /// Get or create a managed render target by name.
    /// If it already exists with matching format/size, returns the cached one.
    /// Scale is relative to backbuffer (1.0 = full-res, 0.5 = half-res).
    ManagedRT& GetOrCreateRT(const std::string& name,
                              DXGI_FORMAT format,
                              float scale = 1.0f,
                              bool uav = false);

    /// Get an existing RT by name (returns nullptr if not found).
    ManagedRT* FindRT(const std::string& name);

    // ── Execution (called by main.cpp / D3D11Hook.cpp) ───────────────

    /// Execute all passes in the given stage.  Called by the frame hooks.
    /// gameRTV/gameDSV: the game's currently-bound RT at mid-frame dispatch
    /// (captured by D3D11StateBackup). nullptr at PrePresent.
    void ExecuteStage(PipelineStage stage, float deltaTime = 0.0f,
                      IDXGISwapChain* sc = nullptr,
                      ID3D11RenderTargetView* gameRTV = nullptr,
                      ID3D11DepthStencilView* gameDSV = nullptr);

    // ── Queries ──────────────────────────────────────────────────────

    bool     IsInitialized() const { return m_initialized; }
    uint32_t GetPassCount()  const;
    uint32_t GetPassCount(PipelineStage stage) const;
    uint32_t GetRTCount()    const { return static_cast<uint32_t>(m_rtPool.size()); }
    uint32_t GetScreenW()    const { return m_screenW; }
    uint32_t GetScreenH()    const { return m_screenH; }

private:
    RenderPipeline() = default;

    bool m_initialized = false;

    ID3D11Device*        m_device    = nullptr;
    ID3D11DeviceContext* m_context   = nullptr;
    IDXGISwapChain*      m_swapChain = nullptr;
    uint32_t             m_screenW   = 0;
    uint32_t             m_screenH   = 0;
    uint32_t             m_frameIndex = 0;

    // Registered passes
    struct PassEntry
    {
        PassDef     def;
        PassHandle  handle = 0;
        bool        alive  = true;
    };
    std::vector<PassEntry> m_passes;
    PassHandle             m_nextHandle = 1;
    bool                   m_sorted     = false;
    // Guards m_passes: passes may be registered from a background
    // init thread while the render thread executes stages.
    std::mutex             m_passMutex;

    void SortPasses();

    // Managed render target pool
    std::unordered_map<std::string, ManagedRT> m_rtPool;
};

} // namespace SB
