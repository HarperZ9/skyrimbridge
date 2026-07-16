//=============================================================================
//  D3D11Hook.cpp — D3D11 integration for the GPU tier (slim standalone form)
//
//  Two modes, auto-detected:
//    1. Proxy mode: the SkyrimBridge d3d11 proxy is loaded (directly, or
//       chain-loaded through ENB's ProxyLibrary). Full mid-frame phase
//       dispatch, depth and G-buffer access.
//    2. Legacy mode: no proxy (for example plain ENB). Present-time vtable
//       hook only; mid-frame stages never fire and depth/G-buffer accessors
//       return null.
//
//  This is the de-tangled form of the parent renderer's hook: the debug GUI,
//  shader-compilation overlay, GPU feedback, TAA/TSR/motion-vector systems
//  are not part of the standalone. Hotkeys kept: F7 phase dispatch, F8
//  compute + trackers, F9 render passes, F10 frame capture, F11 profiler.
//=============================================================================

#include "D3D11Hook.h"

#include "SRVInjector.h"
#include "RenderPipeline.h"
#include "RenderPassManager.h"
#include "ComputeManager.h"
#include "VolumetricClouds.h"
#include "AtmosphereRenderer.h"
#include "HiZPyramid.h"
#include "BootDiagnostics.h"
#include "GPUProfiler.h"
#include "FrameCapture.h"
#include "D3D11StateBackup.h"
#include "PhaseDispatcher.h"

// Frame update (defined in main.cpp) — runs trackers and data collection
// when ENB's callback is not already driving them.
extern void RunStandaloneFrameUpdate();

#include <d3d11.h>
#include <dxgi.h>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <atomic>

// ── Proxy API (resolved at runtime from the d3d11 proxy) ─────────────
namespace SB::Proxy
{
    // Must match src/d3d11_proxy/ProxyAPI.h layout EXACTLY.
    struct ProxyInterface
    {
        uint32_t version;
        ID3D11Device* device;
        ID3D11DeviceContext* context;
        IDXGISwapChain* swapChain;
        bool hdrCapable;
        bool hdrEnabled;
        DXGI_FORMAT backbufferFormat;
        uint32_t drawCallsThisFrame;
        uint32_t rtSwitchesThisFrame;
        uint32_t shaderChangesThisFrame;
        uint32_t frameCount;
        void (*RegisterPrePresent)(void(*)(ID3D11DeviceContext*, IDXGISwapChain*));
        void (*RegisterOnDraw)(void(*)(uint32_t, uint32_t));
        void (*RegisterOnRTChange)(void(*)(uint32_t, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*));
        void (*RegisterOnShaderBind)(void(*)(ID3D11PixelShader*, ID3D11VertexShader*));
        void (*RegisterOnResize)(void(*)(uint32_t, uint32_t, DXGI_FORMAT));
        void (*SetHDREnabled)(bool);
        float hdrMaxNits;
        float hdrPaperWhite;
        uint8_t currentPhase;
        const char* (*GetPhaseName)();
        void (*SetMaterialPipelineEnabled)(bool);
        bool materialPipelineActive;
        uint32_t materialPatchedCount;
        uint32_t materialCandidateCount;
        uint32_t materialClassifiedCount;
        bool deferredActive;
        ID3D11ShaderResourceView* gBufferAlbedo;
        ID3D11ShaderResourceView* gBufferNormals;
        ID3D11ShaderResourceView* gBufferMaterial;
        ID3D11DepthStencilView* gameDepthDSV;
        ID3D11ShaderResourceView* gameDepthSRV;

        struct {
            uint32_t cbMapsIntercepted, cbUpdatesSkipped, cbUpdatesCommitted, cbTrackedBuffers;
            uint32_t srvCallsRedundant, srvCallsTotal;
            uint32_t blendCallsRedundant, blendCallsTotal;
            uint32_t dsCallsRedundant, dsCallsTotal;
            uint32_t rsCallsRedundant, rsCallsTotal;
            uint32_t occDrawsTested, occDrawsCulled;
        } optStats;

        ID3D11ShaderResourceView* preUISceneSRV;
        ID3D11Texture2D*          preUISceneTex;
        bool                      preUISceneValid;

        void (*InvalidateStateCache)();

        void (*RegisterOnPhaseChange)(void(*)(uint8_t, uint8_t));
    };
}

namespace D3D11Hook
{
    // ── State ────────────────────────────────────────────────────────────
    static bool s_initialized = false;

    static ID3D11Device* s_device = nullptr;
    static ID3D11DeviceContext* s_context = nullptr;
    static IDXGISwapChain* s_swapChain = nullptr;

    static HWND s_gameWindow = nullptr;
    static WNDPROC s_originalWndProc = nullptr;

    static SB::Proxy::ProxyInterface* s_proxy = nullptr;
    static bool s_proxyMode = false;

    // Isolation kill switches (validation aids):
    // F9 render passes, F8 compute + trackers, F7 mid-frame dispatch.
    static bool s_disableRenderPasses = false;
    static bool s_disableComputePasses = false;

    // Legacy present hook
    using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    static PresentFn s_originalPresent = nullptr;
    static void** s_swapChainVTable = nullptr;

    // Overlay crash containment
    static int  s_overlayErrorCount = 0;
    static bool s_overlayDisabled = false;

    // ── Window procedure hook: validation hotkeys only ──────────────────
    static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        if (uMsg == WM_KEYDOWN && wParam == VK_F9) {
            s_disableRenderPasses = !s_disableRenderPasses;
            SKSE::log::info("SkyrimBridge: render passes {} (F9)",
                s_disableRenderPasses ? "DISABLED" : "ENABLED");
        }
        if (uMsg == WM_KEYDOWN && wParam == VK_F8) {
            s_disableComputePasses = !s_disableComputePasses;
            SKSE::log::info("SkyrimBridge: compute passes {} (F8)",
                s_disableComputePasses ? "DISABLED" : "ENABLED");
        }
        if (uMsg == WM_KEYDOWN && wParam == VK_F7) {
            auto& pd = SB::PhaseDispatcher::Get();
            pd.SetEnabled(!pd.IsEnabled());
            SKSE::log::info("SkyrimBridge: mid-frame dispatch {} (F7)",
                pd.IsEnabled() ? "ENABLED" : "DISABLED");
        }
        if (uMsg == WM_KEYDOWN && wParam == VK_F10) {
            auto& cap = SB::FrameCapture::Get();
            if (cap.IsCapturing()) {
                cap.StopCapture();
                SKSE::log::info("SkyrimBridge: frame capture STOPPED (F10)");
            } else {
                cap.StartCapture(600);
                SKSE::log::info("SkyrimBridge: frame capture STARTED, 600 frames (F10)");
            }
        }
        if (uMsg == WM_KEYDOWN && wParam == VK_F11) {
            auto& prof = SB::GPUProfiler::Get();
            if (prof.IsInitialized()) {
                prof.SetEnabled(!prof.IsEnabled());
                SKSE::log::info("SkyrimBridge: GPU profiler {} (F11)",
                    prof.IsEnabled() ? "ENABLED" : "DISABLED");
            }
        }

        return CallWindowProcA(s_originalWndProc, hWnd, uMsg, wParam, lParam);
    }

    // ── Per-frame work (present time) ────────────────────────────────────
    static void DoOverlayWork(IDXGISwapChain* swapChain)
    {
        SB::D3D11StateBackup stateBackup;
        stateBackup.Save(s_context);

        // Trackers + data collection, unless ENB's callback drives them.
        if (!s_disableComputePasses) {
            RunStandaloneFrameUpdate();
        }

        // Execute PrePresent pipeline passes (each pass has its own flag).
        if (!s_disableRenderPasses) {
            auto& pipeline = SB::RenderPipeline::Get();
            if (pipeline.IsInitialized())
                pipeline.ExecuteStage(SB::PipelineStage::PrePresent, 0.0f, swapChain);
        }

        // Clear injected SRVs for the next frame.
        SB::SRVInjector::Get().ClearAll();

        // Restore ALL D3D11 state so the game's next frame is untouched.
        stateBackup.Restore(s_context);

        // Our passes desync the proxy's redundancy cache; resync it.
        if (s_proxy && s_proxy->InvalidateStateCache)
            s_proxy->InvalidateStateCache();
    }

    // ── Hooked Present (legacy mode) ─────────────────────────────────────
    static HRESULT __stdcall HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
    {
        if (!s_overlayDisabled) {
            __try {
                DoOverlayWork(swapChain);
                s_overlayErrorCount = 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                s_overlayErrorCount++;
                SB::BootDiag::LogError("HookedPresent", "ACCESS VIOLATION in DoOverlayWork");
                if (s_overlayErrorCount >= 3) {
                    s_overlayDisabled = true;
                    SB::BootDiag::LogError("HookedPresent", "OVERLAY DISABLED after 3 crashes");
                    SB::BootDiag::DumpReport();
                    SKSE::log::error("SkyrimBridge: overlay crashed {} times, auto-disabled",
                        s_overlayErrorCount);
                }
            }
        }

        return s_originalPresent(swapChain, syncInterval, flags);
    }

    // ── Proxy PrePresent callback ────────────────────────────────────────
    static void ProxyPrePresentCallback(ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
    {
        s_swapChain = sc;
        s_context = ctx;

        if (!s_overlayDisabled) {
            __try {
                DoOverlayWork(sc);
                s_overlayErrorCount = 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                s_overlayErrorCount++;
                SB::BootDiag::LogError("ProxyPrePresent", "ACCESS VIOLATION in DoOverlayWork");
                if (s_overlayErrorCount >= 3) {
                    s_overlayDisabled = true;
                    SB::BootDiag::LogError("ProxyPrePresent", "OVERLAY DISABLED after 3 crashes");
                    SB::BootDiag::DumpReport();
                    SKSE::log::error("SkyrimBridge: overlay crashed {} times, auto-disabled",
                        s_overlayErrorCount);
                }
            }
        }
    }

    // ── Proxy connection ─────────────────────────────────────────────────
    // The proxy may be loaded as d3d11.dll directly, or chain-loaded by ENB
    // (ENB owns d3d11.dll, [PROXY] ProxyLibrary points at ours). Probe the
    // known module names for the export.
    static bool TryProxyInit()
    {
        static const char* kCandidates[] = {
            "d3d11.dll", "d3d11_sb.dll", "skyrimbridge_d3d11.dll"
        };

        using GetProxyFn = SB::Proxy::ProxyInterface* (*)();
        GetProxyFn getProxy = nullptr;

        for (const char* name : kCandidates) {
            HMODULE mod = GetModuleHandleA(name);
            if (!mod) continue;
            getProxy = reinterpret_cast<GetProxyFn>(
                GetProcAddress(mod, "SB_GetProxyInterface"));
            if (getProxy) {
                SKSE::log::info("SkyrimBridge: proxy interface found in {}", name);
                break;
            }
        }

        if (!getProxy) {
            SKSE::log::info("SkyrimBridge: no d3d11 proxy detected");
            return false;
        }

        s_proxy = getProxy();
        if (!s_proxy || s_proxy->version < 1) {
            SKSE::log::error("SkyrimBridge: proxy interface invalid (version={})",
                s_proxy ? s_proxy->version : 0);
            return false;
        }

        s_device    = s_proxy->device;
        s_context   = s_proxy->context;
        s_swapChain = s_proxy->swapChain;

        if (!s_device || !s_context || !s_swapChain) {
            SKSE::log::error("SkyrimBridge: proxy has null D3D11 objects");
            return false;
        }

        if (s_proxy->RegisterPrePresent)
            s_proxy->RegisterPrePresent(ProxyPrePresentCallback);

        if (s_proxy->RegisterOnPhaseChange) {
            s_proxy->RegisterOnPhaseChange([](uint8_t oldPhase, uint8_t newPhase) {
                SB::PhaseDispatcher::Get().OnPhaseChange(oldPhase, newPhase);
            });
        }

        s_proxyMode = true;
        SKSE::log::info("SkyrimBridge: connected to d3d11 proxy v{}", s_proxy->version);
        return true;
    }

    // ── Legacy mode: hook the game's swap chain ──────────────────────────
    static bool HookGameSwapChain()
    {
        auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
        if (!renderer) {
            SKSE::log::error("SkyrimBridge: BSGraphics::Renderer not available");
            return false;
        }

        auto& rendererData = renderer->data;
        s_swapChain = rendererData.renderWindows[0].swapChain;
        s_device = rendererData.forwarder;
        s_context = rendererData.context;
        s_gameWindow = reinterpret_cast<HWND>(rendererData.renderWindows[0].hWnd);

        if (!s_swapChain || !s_device || !s_context || !s_gameWindow) {
            SKSE::log::error("SkyrimBridge: null D3D11 objects from renderer");
            return false;
        }

        s_swapChainVTable = *reinterpret_cast<void***>(s_swapChain);
        s_originalPresent = reinterpret_cast<PresentFn>(s_swapChainVTable[8]);

        DWORD oldProtect;
        if (VirtualProtect(&s_swapChainVTable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            s_swapChainVTable[8] = reinterpret_cast<void*>(&HookedPresent);
            VirtualProtect(&s_swapChainVTable[8], sizeof(void*), oldProtect, &oldProtect);
            SKSE::log::info("SkyrimBridge: Present hook installed via vtable (legacy)");
        } else {
            SKSE::log::error("SkyrimBridge: VirtualProtect failed");
            return false;
        }

        return true;
    }

    // ── Window hook (hotkeys) ────────────────────────────────────────────
    static bool InitWindowHook()
    {
        if (!s_gameWindow) {
            auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
            if (renderer)
                s_gameWindow = reinterpret_cast<HWND>(renderer->data.renderWindows[0].hWnd);
        }
        if (!s_gameWindow) {
            SKSE::log::error("SkyrimBridge: game window is null");
            return false;
        }

        SetLastError(0);
        s_originalWndProc = (WNDPROC)SetWindowLongPtrA(
            s_gameWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
        if (!s_originalWndProc && GetLastError() != 0) {
            SKSE::log::error("SkyrimBridge: failed to hook window procedure (error {})",
                GetLastError());
            return false;
        }
        if (!s_originalWndProc)
            s_originalWndProc = DefWindowProcA;
        return true;
    }

    // ── Public interface ─────────────────────────────────────────────────
    bool Init()
    {
        if (s_initialized)
            return true;

        if (TryProxyInit()) {
            InitWindowHook();
            s_initialized = true;
            SKSE::log::info("SkyrimBridge: D3D11 hook initialized (proxy mode)");
            return true;
        }

        SKSE::log::info("SkyrimBridge: using legacy mode (vtable hook)");
        if (!HookGameSwapChain()) {
            SKSE::log::error("SkyrimBridge: failed to hook game swap chain");
            return false;
        }
        InitWindowHook();

        s_initialized = true;
        SKSE::log::info("SkyrimBridge: D3D11 hook initialized (legacy mode)");
        return true;
    }

    void Shutdown()
    {
        if (!s_initialized)
            return;

        // GPU systems in reverse init order.
        SB::VolumetricClouds::Get().Shutdown();
        SB::AtmosphereRenderer::Get().Shutdown();
        SB::HiZPyramid::Get().Shutdown();
        SB::RenderPipeline::Get().Shutdown();
        SB::RenderPassManager::Get().Shutdown();
        SB::ComputeManager::Get().Shutdown();

        if (!s_proxyMode && s_swapChainVTable && s_originalPresent) {
            DWORD oldProtect;
            if (VirtualProtect(&s_swapChainVTable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                s_swapChainVTable[8] = reinterpret_cast<void*>(s_originalPresent);
                VirtualProtect(&s_swapChainVTable[8], sizeof(void*), oldProtect, &oldProtect);
            }
        }

        if (s_originalWndProc && s_gameWindow) {
            SetWindowLongPtrA(s_gameWindow, GWLP_WNDPROC, (LONG_PTR)s_originalWndProc);
        }

        s_initialized = false;
        s_proxyMode = false;
        s_proxy = nullptr;
    }

    // GUI surface: the standalone ships no overlay GUI. Kept for API
    // compatibility; visibility is always false.
    void ToggleGUI() {}
    bool IsGUIVisible() { return false; }
    void SetGUIVisible(bool) {}

    bool ShouldFreezeInput() { return false; }
    void UpdateInputFreeze() {}

    ID3D11Device* GetDevice() { return s_device; }
    ID3D11DeviceContext* GetContext() { return s_context; }
    IDXGISwapChain* GetSwapChain() { return s_swapChain; }

    bool IsProxyActive() { return s_proxyMode; }
    bool IsHDREnabled()  { return s_proxy ? s_proxy->hdrEnabled : false; }
    bool IsHDRCapable()  { return s_proxy ? s_proxy->hdrCapable : false; }

    bool IsMaterialPipelineActive() { return s_proxy ? s_proxy->materialPipelineActive : false; }
    void SetMaterialPipelineEnabled(bool enabled) {
        if (s_proxy && s_proxy->SetMaterialPipelineEnabled)
            s_proxy->SetMaterialPipelineEnabled(enabled);
    }
    uint32_t GetMaterialPatchedCount() { return s_proxy ? s_proxy->materialPatchedCount : 0; }
    uint32_t GetMaterialCandidateCount() { return s_proxy ? s_proxy->materialCandidateCount : 0; }
    uint32_t GetMaterialClassifiedCount() { return s_proxy ? s_proxy->materialClassifiedCount : 0; }

    ID3D11ShaderResourceView* GetGameDepthSRV()       { return s_proxy ? s_proxy->gameDepthSRV    : nullptr; }
    ID3D11ShaderResourceView* GetGBufferAlbedoSRV()   { return s_proxy ? s_proxy->gBufferAlbedo   : nullptr; }
    ID3D11ShaderResourceView* GetGBufferNormalsSRV()  { return s_proxy ? s_proxy->gBufferNormals  : nullptr; }
    ID3D11ShaderResourceView* GetGBufferMaterialSRV() { return s_proxy ? s_proxy->gBufferMaterial : nullptr; }

    ID3D11ShaderResourceView* GetPreUISceneSRV()  { return (s_proxy && s_proxy->preUISceneValid) ? s_proxy->preUISceneSRV : nullptr; }
    ID3D11Texture2D*          GetPreUISceneTex()  { return (s_proxy && s_proxy->preUISceneValid) ? s_proxy->preUISceneTex : nullptr; }
    bool                      IsPreUISceneValid() { return s_proxy && s_proxy->preUISceneValid; }

    InvalidateCacheFn GetInvalidateCacheFn()
    {
        return (s_proxy && s_proxy->InvalidateStateCache)
               ? s_proxy->InvalidateStateCache : nullptr;
    }
}
