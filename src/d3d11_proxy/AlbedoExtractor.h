#pragma once
//=============================================================================
//  AlbedoExtractor — G-buffer albedo extraction via DXBC shader patching
//
//  Patches BSLightingShader pixel shaders at creation time to output raw
//  diffuse (t0 sample) to SV_Target1.  During the geometry pass, injects an
//  extra render target at MRT slot 1 to capture the albedo channel.
//
//  This enables downstream effects (PBR relighting, SSGI, SSS) that need
//  the albedo separated from baked-in lighting — something the Creation
//  Engine never provides natively.
//
//  Integration:
//    WrappedDevice::CreatePixelShader  → OnPixelShaderCreated (patch + cache)
//    WrappedContext::OMSetRenderTargets → InjectAlbedoRT (during geometry)
//    WrappedSwapChain::Present         → OnPresent (clear for next frame)
//
//  Author: Zain Dana Harper
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace SB::Proxy
{

class AlbedoExtractor
{
public:
    static AlbedoExtractor& Get()
    {
        static AlbedoExtractor inst;
        return inst;
    }

    // ── Lifecycle ─────────────────────────────────────────────────────

    // Call after device + swap chain are created.
    bool Initialize(ID3D11Device* realDevice, IDXGISwapChain* swapChain);
    void Shutdown();

    // Recreate RT on resize.
    void OnResize(ID3D11Device* realDevice, uint32_t width, uint32_t height);

    // ── Per-shader hook (from WrappedDevice::CreatePixelShader) ───────

    // Analyzes bytecode, patches BSLightingShader candidates, registers
    // replacement via ShaderManager.
    void OnPixelShaderCreated(ID3D11Device* realDevice,
                              const void* bytecode, SIZE_T length,
                              ID3D11PixelShader* shader);

    // ── Per-frame hooks ───────────────────────────────────────────────

    // Clear albedo RT at frame start (from Present).
    void OnPresent(ID3D11DeviceContext* ctx);

    // Inject albedo RT into MRT during geometry pass.
    // Returns true if RT was injected (caller should use modified RT array).
    // `rtvOut` must have room for numViews+1 entries.
    bool InjectAlbedoRT(UINT numViews,
                        ID3D11RenderTargetView* const* ppRTViews,
                        ID3D11RenderTargetView** rtvOut,
                        UINT& outNumViews) const;

    // ── State ─────────────────────────────────────────────────────────

    bool IsInitialized() const { return m_initialized; }
    bool IsEnabled()     const { return m_enabled; }
    void SetEnabled(bool e)    { m_enabled = e; }

    ID3D11ShaderResourceView* GetAlbedoSRV() const { return m_albedoSRV; }

    // ── Stats ─────────────────────────────────────────────────────────

    uint32_t GetPatchedCount()   const { return m_patchedCount; }
    uint32_t GetCandidateCount() const { return m_candidateCount; }
    uint32_t GetSkippedCount()   const { return m_skippedCount; }

private:
    AlbedoExtractor() = default;

    // ── Render target management ──────────────────────────────────────

    bool CreateAlbedoRT(ID3D11Device* dev, uint32_t w, uint32_t h);
    void ReleaseAlbedoRT();

    // ── DXBC analysis + patching ──────────────────────────────────────

    // Heuristic: does this shader sample from both t0 and t1?
    // (BSLightingShader always reads diffuse t0 + normal t1)
    static bool IsLightingShaderCandidate(const uint8_t* bytecode, SIZE_T length);

    // Patch OSGN (add SV_Target1) + SHEX (dcl_output o1, mov o1 from t0 sample).
    // Returns empty vector on failure.
    static std::vector<uint8_t> PatchForAlbedoOutput(const uint8_t* bytecode, SIZE_T length);

    // ── State ─────────────────────────────────────────────────────────

    ID3D11Texture2D*          m_albedoTex = nullptr;
    ID3D11RenderTargetView*   m_albedoRTV = nullptr;
    ID3D11ShaderResourceView* m_albedoSRV = nullptr;

    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    bool     m_initialized = false;
    bool     m_enabled     = false;   // Off by default; user/API enables

    // Patch tracking: original PS → patched PS (nullptr = analysis rejected it)
    std::unordered_map<ID3D11PixelShader*, ID3D11PixelShader*> m_patchCache;
    mutable std::mutex m_mutex;

    uint32_t m_patchedCount   = 0;
    uint32_t m_candidateCount = 0;
    uint32_t m_skippedCount   = 0;
};

} // namespace SB::Proxy
