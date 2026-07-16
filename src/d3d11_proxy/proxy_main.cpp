//=============================================================================
//  proxy_main.cpp — Playground d3d11.dll Proxy
//
//  Loaded by Windows DLL search order (game directory wins over System32).
//  Wraps D3D11 device, context, and swap chain for full pipeline control.
//
//  Exports:
//    D3D11CreateDevice
//    D3D11CreateDeviceAndSwapChain
//    PG_GetProxyInterface  (for SKSE plugin communication)
//    SB_GetProxyInterface  (legacy alias)
//=============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

// We re-export D3D11CreateDevice* from this DLL, so we must not let the
// SDK header declare them as __declspec(dllimport).  The header gates the
// import annotation on the D3D11_CREATE_DEVICE_FLAG section — we simply
// provide our own forward declarations after including for types only.
#include <d3d11.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <string>
#include <vector>

#include "ProxyLog.h"
#include "ProxyAPI.h"
#include "MaterialPipeline.h"
#include "WrappedDevice.h"
#include "WrappedContext.h"
#include "WrappedSwapChain.h"
#include "RenderPhaseDetector.h"
#include "OcclusionCuller.h"

// ═══════════════════════════════════════════════════════════════════════════
//  Real d3d11.dll function pointers
// ═══════════════════════════════════════════════════════════════════════════

using PFN_D3D11CreateDevice = HRESULT(WINAPI*)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

using PFN_D3D11CreateDeviceAndSwapChain = HRESULT(WINAPI*)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

static HMODULE                            s_realDLL = nullptr;
static PFN_D3D11CreateDevice              s_realCreateDevice = nullptr;
static PFN_D3D11CreateDeviceAndSwapChain  s_realCreateDeviceAndSwapChain = nullptr;
static std::string                        s_realDLLPath;

// ═══════════════════════════════════════════════════════════════════════════
//  Trampoline targets for D3D11Core* exports — populated by LoadRealD3D11(),
//  jumped to by ASM stubs. ENB only exports these 4 + the 2 Create functions.
//  We do NOT export D3DKMT*, D3DPerformance*, OpenAdapter*, etc. (ENB doesn't).
// ═══════════════════════════════════════════════════════════════════════════
extern "C" {
    FARPROC g_d3d11Original_D3D11CoreCreateDevice = nullptr;
    FARPROC g_d3d11Original_D3D11CoreCreateLayeredDevice = nullptr;
    FARPROC g_d3d11Original_D3D11CoreGetLayeredDeviceSize = nullptr;
    FARPROC g_d3d11Original_D3D11CoreRegisterLayers = nullptr;

    // Force NVIDIA Optimus laptops to use the discrete GPU.
    // ENB exports this as a data export with value=1. We match it.
    // AMD has a similar mechanism via AmdPowerXpressRequestHighPerformance.
    __declspec(dllexport) DWORD NvOptimusEnablement = 1;
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Proxy configuration (read from d3d11_proxy.ini)
// ═══════════════════════════════════════════════════════════════════════════

struct ProxyConfig
{
    bool hdrEnabled     = false;
    float hdrMaxNits    = 1000.0f;
    float hdrPaperWhite = 203.0f;   // SDR reference white in nits
    bool logVerbose     = false;
    bool safeMode       = false;    // Safe mode: pure passthrough, no optimizations
};

static ProxyConfig s_config;

static void LoadConfig()
{
    // Look for config next to the proxy DLL
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);

    std::string iniPath(path);
    auto pos = iniPath.rfind('\\');
    if (pos != std::string::npos)
        iniPath = iniPath.substr(0, pos + 1) + "d3d11_proxy.ini";

    s_config.hdrEnabled = GetPrivateProfileIntA("HDR", "Enabled", 0, iniPath.c_str()) != 0;
    s_config.hdrMaxNits = static_cast<float>(GetPrivateProfileIntA("HDR", "MaxNits", 1000, iniPath.c_str()));
    s_config.hdrPaperWhite = static_cast<float>(GetPrivateProfileIntA("HDR", "PaperWhiteNits", 203, iniPath.c_str()));
    s_config.logVerbose = GetPrivateProfileIntA("Debug", "Verbose", 0, iniPath.c_str()) != 0;
    s_config.safeMode = GetPrivateProfileIntA("General", "SafeMode", 0, iniPath.c_str()) != 0;

    SB::Proxy::Log("Config: HDR=%s, MaxNits=%.0f, PaperWhite=%.0f, Verbose=%s, SafeMode=%s",
        s_config.hdrEnabled ? "ON" : "OFF",
        s_config.hdrMaxNits,
        s_config.hdrPaperWhite,
        s_config.logVerbose ? "ON" : "OFF",
        s_config.safeMode ? "ON" : "OFF");
}


// ═══════════════════════════════════════════════════════════════════════════
//  HDR support detection
// ═══════════════════════════════════════════════════════════════════════════

// Dynamically load CreateDXGIFactory1 — we must NOT have dxgi.dll in our
// import table, or it loads during DLL_PROCESS_ATTACH and corrupts the
// graphics initialization order. ENB's d3d11.dll does not import dxgi.dll.
using PFN_CreateDXGIFactory1 = HRESULT(WINAPI*)(REFIID, void**);

static bool CheckHDRSupport()
{
    HMODULE hDXGI = LoadLibraryA("dxgi.dll");
    if (!hDXGI) return false;

    auto pfnCreateFactory = reinterpret_cast<PFN_CreateDXGIFactory1>(
        GetProcAddress(hDXGI, "CreateDXGIFactory1"));
    if (!pfnCreateFactory) { FreeLibrary(hDXGI); return false; }

    IDXGIFactory1* factory = nullptr;
    HRESULT hr = pfnCreateFactory(__uuidof(IDXGIFactory1), (void**)&factory);
    if (FAILED(hr) || !factory) { FreeLibrary(hDXGI); return false; }

    IDXGIAdapter1* adapter = nullptr;
    factory->EnumAdapters1(0, &adapter);
    if (!adapter) { factory->Release(); FreeLibrary(hDXGI); return false; }

    IDXGIOutput* output = nullptr;
    adapter->EnumOutputs(0, &output);
    if (!output) { adapter->Release(); factory->Release(); FreeLibrary(hDXGI); return false; }

    IDXGIOutput6* output6 = nullptr;
    bool hdrCapable = false;
    if (SUCCEEDED(output->QueryInterface(&output6)))
    {
        DXGI_OUTPUT_DESC1 desc1;
        if (SUCCEEDED(output6->GetDesc1(&desc1)))
        {
            hdrCapable = (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
            SB::Proxy::Log("Display: BitsPerColor=%u, ColorSpace=%u, HDR=%s",
                desc1.BitsPerColor, desc1.ColorSpace,
                hdrCapable ? "YES" : "NO");
        }
        output6->Release();
    }

    output->Release();
    adapter->Release();
    factory->Release();
    // Don't FreeLibrary(hDXGI) — DXGI stays loaded for the rest of the process
    return hdrCapable;
}


// ═══════════════════════════════════════════════════════════════════════════
//  ProxyInterface (singleton, handed to SKSE plugin)
// ═══════════════════════════════════════════════════════════════════════════

static SB::Proxy::ProxyInterface s_proxyInterface = {};

// Callback registration functions
namespace SB::Proxy
{
    // Forward declarations for callback storage
    extern std::vector<PrePresentCallback>   g_prePresentCallbacks;   // WrappedSwapChain.cpp
    extern std::vector<OnResizeCallback>     g_resizeCallbacks;       // WrappedSwapChain.cpp
    extern std::vector<OnDrawCallback>       g_drawCallbacks;         // WrappedContext.cpp
    extern std::vector<OnRTChangeCallback>   g_rtChangeCallbacks;     // WrappedContext.cpp
    extern std::vector<OnShaderBindCallback> g_shaderBindCallbacks;   // WrappedContext.cpp
    extern uint32_t g_frameCount;
}

static void RegisterPrePresentCB(SB::Proxy::PrePresentCallback cb)
{
    if (cb) SB::Proxy::g_prePresentCallbacks.push_back(cb);
}

static void RegisterOnDrawCB(SB::Proxy::OnDrawCallback cb)
{
    if (cb) SB::Proxy::g_drawCallbacks.push_back(cb);
    SB::Proxy::Log("RegisterOnDraw callback registered");
}

static void RegisterOnRTChangeCB(SB::Proxy::OnRTChangeCallback cb)
{
    if (cb) SB::Proxy::g_rtChangeCallbacks.push_back(cb);
    SB::Proxy::Log("RegisterOnRTChange callback registered");
}

static void RegisterOnShaderBindCB(SB::Proxy::OnShaderBindCallback cb)
{
    if (cb) SB::Proxy::g_shaderBindCallbacks.push_back(cb);
    SB::Proxy::Log("RegisterOnShaderBind callback registered");
}

static void RegisterOnResizeCB(SB::Proxy::OnResizeCallback cb)
{
    if (cb) SB::Proxy::g_resizeCallbacks.push_back(cb);
    SB::Proxy::Log("RegisterOnResize callback registered");
}

// Phase change callbacks from SKSE plugin (uint8_t-based to avoid enum type dependency)
static std::vector<SB::Proxy::OnPhaseChangeCallback> s_phaseChangeCallbacks;
static bool s_phaseChangeRegistered = false;

static void RegisterOnPhaseChangeCB(SB::Proxy::OnPhaseChangeCallback cb)
{
    if (!cb) return;
    s_phaseChangeCallbacks.push_back(cb);

    // Register ONE proxy-side callback that dispatches to all SKSE callbacks
    if (!s_phaseChangeRegistered) {
        SB::Proxy::RenderPhaseDetector::Get().RegisterPhaseChangeCallback(
            [](SB::Proxy::RenderPhase oldPhase, SB::Proxy::RenderPhase newPhase) {
                for (auto& c : s_phaseChangeCallbacks)
                    if (c) c(static_cast<uint8_t>(oldPhase),
                             static_cast<uint8_t>(newPhase));
            });
        s_phaseChangeRegistered = true;
    }
    SB::Proxy::Log("RegisterOnPhaseChange callback registered");
}

static void SetHDREnabledCB(bool enabled)
{
    s_config.hdrEnabled = enabled;
    s_proxyInterface.hdrEnabled = enabled;
    SB::Proxy::Log("HDR %s at runtime", enabled ? "enabled" : "disabled");
    // Note: actual format change requires ResizeBuffers — deferred to next frame
}


// Safe mode query — used by wrappers to skip all intercepting logic
bool PG_IsSafeMode() { return s_config.safeMode; }

// Forward declaration — defined below DllMain
static void LazyInit();

// ═══════════════════════════════════════════════════════════════════════════
//  Exported: PG_GetProxyInterface / SB_GetProxyInterface (legacy alias)
// ═══════════════════════════════════════════════════════════════════════════

extern "C" __declspec(dllexport) SB::Proxy::ProxyInterface* PG_GetProxyInterface()
{
    return &s_proxyInterface;
}

extern "C" __declspec(dllexport) SB::Proxy::ProxyInterface* SB_GetProxyInterface()
{
    return &s_proxyInterface;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Exported: D3D11CreateDevice
// ═══════════════════════════════════════════════════════════════════════════

// Use internal names — the .def file maps them to the real export names
extern "C" HRESULT WINAPI SB_D3D11CreateDevice(
    IDXGIAdapter*            pAdapter,
    D3D_DRIVER_TYPE          DriverType,
    HMODULE                  Software,
    UINT                     Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT                     FeatureLevels,
    UINT                     SDKVersion,
    ID3D11Device**           ppDevice,
    D3D_FEATURE_LEVEL*       pFeatureLevel,
    ID3D11DeviceContext**    ppImmediateContext)
{
    LazyInit();
    SB::Proxy::Log("D3D11CreateDevice called (DriverType=%d, Flags=0x%X)", DriverType, Flags);

    if (!s_realCreateDevice)
    {
        SB::Proxy::Log("ERROR: Real D3D11CreateDevice not loaded!");
        return E_FAIL;
    }

    // Call real function
    ID3D11Device*        realDevice  = nullptr;
    ID3D11DeviceContext* realContext = nullptr;
    HRESULT hr = s_realCreateDevice(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        ppDevice ? &realDevice : nullptr,
        pFeatureLevel,
        ppImmediateContext ? &realContext : nullptr);

    if (FAILED(hr))
    {
        SB::Proxy::Log("Real D3D11CreateDevice failed: 0x%08X", hr);
        return hr;
    }

    // Wrap the returned objects
    SB::Proxy::WrappedContext* wrappedCtx = nullptr;
    if (realContext)
    {
        wrappedCtx = new SB::Proxy::WrappedContext(realContext);
        if (ppImmediateContext) *ppImmediateContext = wrappedCtx;
    }

    if (realDevice)
    {
        auto* wrappedDev = new SB::Proxy::WrappedDevice(realDevice, wrappedCtx);
        if (wrappedCtx) wrappedCtx->SetWrappedDevice(wrappedDev);
        if (ppDevice) *ppDevice = wrappedDev;

        // Store in proxy interface
        s_proxyInterface.device  = realDevice;
        s_proxyInterface.context = realContext;
    }

    SB::Proxy::Log("D3D11CreateDevice succeeded — device and context wrapped");
    return hr;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Exported: D3D11CreateDeviceAndSwapChain
// ═══════════════════════════════════════════════════════════════════════════

extern "C" HRESULT WINAPI SB_D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter*            pAdapter,
    D3D_DRIVER_TYPE          DriverType,
    HMODULE                  Software,
    UINT                     Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT                     FeatureLevels,
    UINT                     SDKVersion,
    const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain**         ppSwapChain,
    ID3D11Device**           ppDevice,
    D3D_FEATURE_LEVEL*       pFeatureLevel,
    ID3D11DeviceContext**    ppImmediateContext)
{
    LazyInit();
    SB::Proxy::Log("D3D11CreateDeviceAndSwapChain called");

    if (!s_realCreateDeviceAndSwapChain)
    {
        SB::Proxy::Log("ERROR: Real D3D11CreateDeviceAndSwapChain not loaded!");
        return E_FAIL;
    }

    // Potentially modify swap chain desc for HDR
    DXGI_SWAP_CHAIN_DESC modifiedDesc = {};
    if (pSwapChainDesc)
    {
        modifiedDesc = *pSwapChainDesc;

        SB::Proxy::Log("  Original format: %d, size: %ux%u, BufferCount: %u",
            modifiedDesc.BufferDesc.Format,
            modifiedDesc.BufferDesc.Width, modifiedDesc.BufferDesc.Height,
            modifiedDesc.BufferCount);

        // HDR format override
        if (s_config.hdrEnabled && s_proxyInterface.hdrCapable)
        {
            modifiedDesc.BufferDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            SB::Proxy::Log("  HDR: Overriding swap chain format to R16G16B16A16_FLOAT");
        }
    }

    // Call real function
    ID3D11Device*        realDevice  = nullptr;
    ID3D11DeviceContext* realContext = nullptr;
    IDXGISwapChain*      realSC      = nullptr;

    HRESULT hr = s_realCreateDeviceAndSwapChain(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        pSwapChainDesc ? &modifiedDesc : nullptr,
        ppSwapChain ? &realSC : nullptr,
        ppDevice ? &realDevice : nullptr,
        pFeatureLevel,
        ppImmediateContext ? &realContext : nullptr);

    if (FAILED(hr))
    {
        SB::Proxy::Log("Real D3D11CreateDeviceAndSwapChain failed: 0x%08X", hr);

        // If HDR format failed, retry without HDR override
        if (s_config.hdrEnabled && pSwapChainDesc)
        {
            SB::Proxy::Log("  Retrying without HDR format override...");
            hr = s_realCreateDeviceAndSwapChain(
                pAdapter, DriverType, Software, Flags,
                pFeatureLevels, FeatureLevels, SDKVersion,
                pSwapChainDesc,  // original desc
                ppSwapChain ? &realSC : nullptr,
                ppDevice ? &realDevice : nullptr,
                pFeatureLevel,
                ppImmediateContext ? &realContext : nullptr);

            if (SUCCEEDED(hr))
            {
                SB::Proxy::Log("  Succeeded without HDR — display may not support R16G16B16A16_FLOAT");
                s_config.hdrEnabled = false;
                s_proxyInterface.hdrEnabled = false;
            }
        }

        if (FAILED(hr))
        {
            SB::Proxy::Log("D3D11CreateDeviceAndSwapChain failed completely: 0x%08X", hr);
            return hr;
        }
    }

    // Set HDR color space on swap chain
    if (s_config.hdrEnabled && realSC)
    {
        IDXGISwapChain3* sc3 = nullptr;
        if (SUCCEEDED(realSC->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3)))
        {
            HRESULT csHr = sc3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
            SB::Proxy::Log("  SetColorSpace1(scRGB linear): 0x%08X", csHr);
            sc3->Release();
        }
    }

    // ── DIAGNOSTIC: Zero-wrap mode ─────────────────────────────────────
    // In safe mode: wrap NOTHING. Pure forwarding — just pass through the
    // real D3D11 objects unchanged. This tests whether merely loading as
    // d3d11.dll (DllMain, CheckHDRSupport, etc.) causes the invisible UI.
    if (s_config.safeMode)
    {
        if (ppDevice) *ppDevice = realDevice;
        if (ppImmediateContext) *ppImmediateContext = realContext;
        if (ppSwapChain) *ppSwapChain = realSC;

        // Store in proxy interface for SKSE plugin
        s_proxyInterface.device         = realDevice;
        s_proxyInterface.context        = realContext;
        s_proxyInterface.swapChain      = realSC;
        s_proxyInterface.backbufferFormat = pSwapChainDesc ? modifiedDesc.BufferDesc.Format
                                                           : DXGI_FORMAT_UNKNOWN;

        SB::Proxy::Log("  DIAGNOSTIC: SafeMode ZERO-WRAP — ALL objects returned unwrapped (pure forwarding)");
        SB::Proxy::Log("  Device: %p  Context: %p  SwapChain: %p", realDevice, realContext, realSC);
        return hr;
    }

    // Normal mode: full wrapping
    SB::Proxy::WrappedContext* wrappedCtx = nullptr;
    SB::Proxy::WrappedDevice* wrappedDev = nullptr;

    if (realContext)
    {
        wrappedCtx = new SB::Proxy::WrappedContext(realContext);
        if (ppImmediateContext) *ppImmediateContext = wrappedCtx;
    }
    if (realDevice)
    {
        wrappedDev = new SB::Proxy::WrappedDevice(realDevice, wrappedCtx);
        if (wrappedCtx) wrappedCtx->SetWrappedDevice(wrappedDev);
        if (ppDevice) *ppDevice = wrappedDev;
    }

    SB::Proxy::WrappedSwapChain* wrappedSC = nullptr;
    if (realSC)
    {
        wrappedSC = new SB::Proxy::WrappedSwapChain(realSC);
        if (wrappedDev) wrappedSC->SetWrappedDevice(wrappedDev);
        if (wrappedCtx) wrappedSC->SetWrappedContext(wrappedCtx);
        if (ppSwapChain) *ppSwapChain = wrappedSC;
    }

    // Store real objects in proxy interface for SKSE plugin access
    s_proxyInterface.device         = realDevice;
    s_proxyInterface.context        = realContext;
    s_proxyInterface.swapChain      = realSC;
    s_proxyInterface.backbufferFormat = pSwapChainDesc ? modifiedDesc.BufferDesc.Format
                                                       : DXGI_FORMAT_UNKNOWN;

    // Initialize render phase detector with backbuffer size
    if (pSwapChainDesc)
        SB::Proxy::RenderPhaseDetector::Get().SetBackbufferSize(
            modifiedDesc.BufferDesc.Width, modifiedDesc.BufferDesc.Height);

    // Initialize occlusion culler
    SB::Proxy::OcclusionCuller::Get().Initialize(realDevice, realContext);

    SB::Proxy::Log("Playground D3D11CreateDeviceAndSwapChain succeeded:");
    SB::Proxy::Log("  Device: %p (wrapped: %p)", realDevice, wrappedDev);
    SB::Proxy::Log("  Context: %p (wrapped: %p)", realContext, wrappedCtx);
    SB::Proxy::Log("  SwapChain: %p (wrapped: %p)", realSC, wrappedSC);
    SB::Proxy::Log("  Format: %d, HDR: %s",
        s_proxyInterface.backbufferFormat,
        s_config.hdrEnabled ? "active" : "inactive");

    return hr;
}


// ═══════════════════════════════════════════════════════════════════════════
//  DllMain — Load real d3d11.dll from System32
// ═══════════════════════════════════════════════════════════════════════════

static bool LoadRealD3D11()
{
    // Build System32 path to the real d3d11.dll
    char systemDir[MAX_PATH];
    GetSystemDirectoryA(systemDir, MAX_PATH);

    s_realDLLPath = std::string(systemDir) + "\\d3d11.dll";

    s_realDLL = LoadLibraryA(s_realDLLPath.c_str());
    if (!s_realDLL)
    {
        SB::Proxy::Log("FATAL: Failed to load real d3d11.dll from %s (error=%u)",
            s_realDLLPath.c_str(), GetLastError());
        return false;
    }

    // Resolve the 6 functions ENB resolves (matching ENB's pattern exactly)
    s_realCreateDevice = reinterpret_cast<PFN_D3D11CreateDevice>(
        GetProcAddress(s_realDLL, "D3D11CreateDevice"));
    s_realCreateDeviceAndSwapChain = reinterpret_cast<PFN_D3D11CreateDeviceAndSwapChain>(
        GetProcAddress(s_realDLL, "D3D11CreateDeviceAndSwapChain"));

    // D3D11Core* trampolines — DXGI calls these internally during device creation
    g_d3d11Original_D3D11CoreCreateDevice        = GetProcAddress(s_realDLL, "D3D11CoreCreateDevice");
    g_d3d11Original_D3D11CoreCreateLayeredDevice  = GetProcAddress(s_realDLL, "D3D11CoreCreateLayeredDevice");
    g_d3d11Original_D3D11CoreGetLayeredDeviceSize = GetProcAddress(s_realDLL, "D3D11CoreGetLayeredDeviceSize");
    g_d3d11Original_D3D11CoreRegisterLayers       = GetProcAddress(s_realDLL, "D3D11CoreRegisterLayers");

    if (!s_realCreateDevice || !s_realCreateDeviceAndSwapChain)
    {
        SB::Proxy::Log("FATAL: Failed to resolve D3D11 functions from real DLL");
        return false;
    }

    SB::Proxy::Log("Real d3d11.dll loaded from %s", s_realDLLPath.c_str());
    return true;
}

// Lazy initialization — called on first D3D11CreateDevice* call (NOT from DllMain)
static bool s_lazyInitDone = false;

static void LazyInit()
{
    if (s_lazyInitDone) return;
    s_lazyInitDone = true;

    // Initialize logging (safe outside DllMain)
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string logPath(exePath);
    auto pos = logPath.rfind('\\');
    if (pos != std::string::npos)
        logPath = logPath.substr(0, pos + 1) + "d3d11_proxy.log";
    else
        logPath = "d3d11_proxy.log";

    SB::Proxy::LogInit(logPath.c_str());
    SB::Proxy::Log("═══════════════════════════════════════════════════════");
    SB::Proxy::Log("  Playground d3d11 Proxy v1.0");
    SB::Proxy::Log("  Build: %s %s", __DATE__, __TIME__);
    SB::Proxy::Log("═══════════════════════════════════════════════════════");

    // Load config
    LoadConfig();

    // Load the real d3d11.dll — NOT done in DllMain (ENB doesn't either).
    // LoadLibraryA inside DllMain is dangerous (loader lock) and can corrupt
    // DXGI initialization order.
    if (!LoadRealD3D11())
    {
        SB::Proxy::Log("FATAL: Could not load real d3d11.dll — proxy cannot function");
        return;
    }

    // Check HDR capability — skip in safe mode to avoid any DXGI interaction
    if (s_config.safeMode)
    {
        SB::Proxy::Log("SafeMode: skipping HDR check (no DXGI factory creation)");
        s_proxyInterface.hdrCapable = false;
        s_config.hdrEnabled = false;
    }
    else
    {
        s_proxyInterface.hdrCapable = CheckHDRSupport();
        SB::Proxy::Log("HDR capable: %s", s_proxyInterface.hdrCapable ? "YES" : "NO");

        if (s_config.hdrEnabled && !s_proxyInterface.hdrCapable)
        {
            SB::Proxy::Log("HDR requested but display not capable — disabling");
            s_config.hdrEnabled = false;
        }
    }

    // Initialize proxy interface
    s_proxyInterface.version     = 1;
    s_proxyInterface.hdrEnabled  = s_config.hdrEnabled;
    s_proxyInterface.hdrMaxNits  = s_config.hdrMaxNits;
    s_proxyInterface.hdrPaperWhite = s_config.hdrPaperWhite;

    // Register callback functions
    s_proxyInterface.RegisterPrePresent = RegisterPrePresentCB;
    s_proxyInterface.RegisterOnDraw     = RegisterOnDrawCB;
    s_proxyInterface.RegisterOnRTChange = RegisterOnRTChangeCB;
    s_proxyInterface.RegisterOnShaderBind = RegisterOnShaderBindCB;
    s_proxyInterface.RegisterOnResize   = RegisterOnResizeCB;
    s_proxyInterface.SetHDREnabled      = SetHDREnabledCB;

    // Render phase query
    s_proxyInterface.GetPhaseName = []() -> const char* {
        return SB::Proxy::RenderPhaseDetector::Get().GetPhaseName();
    };

    // Material pipeline control
    s_proxyInterface.SetMaterialPipelineEnabled = [](bool enabled) {
        SB::Proxy::MaterialPipeline::Get().SetEnabled(enabled);
        s_proxyInterface.materialPipelineActive = enabled;
        SB::Proxy::Log("MaterialPipeline %s via API", enabled ? "enabled" : "disabled");
    };

    // Pre-UI scene capture (populated by WrappedSwapChain phase change callback)
    s_proxyInterface.preUISceneSRV   = nullptr;
    s_proxyInterface.preUISceneTex   = nullptr;
    s_proxyInterface.preUISceneValid = false;

    // State cache invalidation — called by SKSE plugin after modifying D3D11
    // state through the real (unwrapped) context to prevent stale cache skips
    s_proxyInterface.InvalidateStateCache = []() {
        if (SB::Proxy::WrappedContext::s_instance)
            SB::Proxy::WrappedContext::s_instance->ResetStateCache();
    };

    // Phase change callback — SKSE plugin registers to dispatch mid-frame effects
    s_proxyInterface.RegisterOnPhaseChange = RegisterOnPhaseChangeCB;

    SB::Proxy::Log("Playground proxy lazy-init complete");
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        // ── TRULY EMPTY DllMain (matching ENB's pattern) ────────────────
        // ENB's DllMain does NOTHING except CRT init. No LoadLibrary, no
        // GetSystemDirectory, no GetProcAddress, no DXGI calls.
        // Everything is deferred to LazyInit() on first D3D11CreateDevice* call.
        DisableThreadLibraryCalls(hinstDLL);
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        SB::Proxy::ReleaseDepthCache();
        SB::Proxy::MaterialPipeline::Get().Shutdown();

        if (s_realDLL)
        {
            FreeLibrary(s_realDLL);
            s_realDLL = nullptr;
        }

        SB::Proxy::LogShutdown();
    }

    return TRUE;
}
