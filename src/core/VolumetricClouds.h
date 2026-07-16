#pragma once
//=============================================================================
//  VolumetricClouds --- GPU volumetric cloud raymarching system
//
//  Three-pass pipeline:
//    1. NoiseGen CS (one-time init): Generates 3D noise textures
//       - Shape noise: 128^3 R8_UNORM (Worley + Perlin FBM)
//       - Detail noise: 32^3 R8_UNORM (high-frequency Worley)
//    2. CloudRaymarch CS (per-frame, quarter-res): Raymarches through
//       cloud layer (1500m-4000m altitude band), computes inscatter via
//       dual-lobe Henyey-Greenstein + Beer's law extinction + powder effect.
//       Temporal reprojection rejects stale pixels via motion-based test.
//       Output: R16G16B16A16_FLOAT (inscatter.rgb, transmittance.a)
//    3. CloudComposite PS (fullscreen via RenderPassManager): Bilateral
//       upsample from quarter-res + blend with scene via transmittance.
//
//  Registered as a PreENB pipeline pass in RenderPipeline.
//  Cloud output SRV bound at t27 for ENB shaders.
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include "ComputeManager.h"
#include "RenderPassManager.h"
#include "RenderPipeline.h"

namespace SB
{

class VolumetricClouds
{
public:
    static VolumetricClouds& Get()
    {
        static VolumetricClouds inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Settings
    float GetCoverage() const { return m_coverage; }
    void  SetCoverage(float c) { m_coverage = c; }
    float GetDensity() const { return m_density; }
    void  SetDensity(float d) { m_density = d; }
    float GetCloudBase() const { return m_cloudBase; }
    void  SetCloudBase(float h) { m_cloudBase = h; }
    float GetCloudTop() const { return m_cloudTop; }
    void  SetCloudTop(float h) { m_cloudTop = h; }
    bool  IsEnabled() const { return m_enabled; }
    void  SetEnabled(bool e) { m_enabled = e; }

    // SRV for cloud output (t27)
    ID3D11ShaderResourceView* GetCloudSRV() const { return m_cloudSRV; }

    static constexpr uint32_t kCloudSRVSlot = 27;  // t27

    // Fog settings
    float GetFogDensity() const { return m_fogDensity; }
    void SetFogDensity(float d) { m_fogDensity = d; }
    float GetFogHeight() const { return m_fogHeight; }
    void SetFogHeight(float h) { m_fogHeight = h; }
    float GetFogFalloff() const { return m_fogFalloff; }
    void SetFogFalloff(float f) { m_fogFalloff = f; }
    bool IsFogEnabled() const { return m_fogEnabled; }
    void SetFogEnabled(bool e) { m_fogEnabled = e; }
    float GetFogScatteringAnisotropy() const { return m_fogAnisotropy; }
    void SetFogScatteringAnisotropy(float g) { m_fogAnisotropy = g; }

    // Weather-driven coverage presets
    void SetWeatherCoverage(float coverage) { m_coverage = coverage; }

    // Wind offset accumulation (call each frame with weather wind data)
    void AccumulateWind(float dx, float dy, float dz);

private:
    VolumetricClouds() = default;

    bool m_initialized = false;
    bool m_enabled     = false;  // opt-in: heavy GPU work, must be explicitly enabled

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Settings
    float m_coverage  = 0.5f;     // Cloud coverage [0,1]
    float m_density   = 0.05f;    // Extinction density multiplier
    float m_cloudBase = 1500.0f;  // Base altitude in meters
    float m_cloudTop  = 4000.0f;  // Top altitude in meters

    // Accumulated wind offset (from WeatherTracker)
    float m_windOffsetX = 0.0f;
    float m_windOffsetY = 0.0f;
    float m_windOffsetZ = 0.0f;

    // Fog parameters
    bool  m_fogEnabled     = true;    // Enable height fog integration
    float m_fogDensity     = 0.0005f; // Base fog density at sea level
    float m_fogHeight      = 500.0f;  // Fog layer height (meters) — transitions into cloud base
    float m_fogFalloff     = 0.002f;  // Exponential height falloff (1/scale_height)
    float m_fogAnisotropy  = 0.6f;    // Fog scattering anisotropy (Henyey-Greenstein g)

    // ---- Noise textures (one-time generation) ----

    // Shape noise: 128^3 R8_UNORM
    ID3D11Texture3D*            m_shapeNoiseTex = nullptr;
    ID3D11ShaderResourceView*   m_shapeNoiseSRV = nullptr;
    ID3D11UnorderedAccessView*  m_shapeNoiseUAV = nullptr;

    // Detail noise: 32^3 R8_UNORM
    ID3D11Texture3D*            m_detailNoiseTex = nullptr;
    ID3D11ShaderResourceView*   m_detailNoiseSRV = nullptr;
    ID3D11UnorderedAccessView*  m_detailNoiseUAV = nullptr;

    ComputeShaderID m_shapeNoiseCS  = 0;
    ComputeShaderID m_detailNoiseCS = 0;

    bool m_noiseGenerated = false;

    // ---- Cloud raymarch (quarter-res, per-frame) ----

    // Current frame cloud output: quarter-res R16G16B16A16_FLOAT
    ID3D11Texture2D*            m_cloudTex       = nullptr;
    ID3D11ShaderResourceView*   m_cloudSRV       = nullptr;
    ID3D11UnorderedAccessView*  m_cloudUAV       = nullptr;

    // Previous frame cloud output (for temporal reprojection)
    ID3D11Texture2D*            m_cloudHistTex   = nullptr;
    ID3D11ShaderResourceView*   m_cloudHistSRV   = nullptr;

    uint32_t m_quarterW = 0;
    uint32_t m_quarterH = 0;

    ComputeShaderID m_raymarchCS = 0;
    ID3D11Buffer*   m_cloudCB    = nullptr;

    // Trilinear sampler for noise reads
    ID3D11SamplerState* m_trilinearSampler = nullptr;

    // ---- Composite pass (fullscreen PS) ----

    RenderPassID m_compositePass = 0;
    ID3D11Buffer* m_compositeCB  = nullptr;

    // Depth SRV acquired per-frame in ExecutePass (not cached)

    // Pipeline pass handle
    PassHandle m_pipelineHandle = 0;

    // Backbuffer RTV (created per-frame from swap chain)
    // (not cached --- acquired in ExecutePass)

    // Frame counter for temporal reprojection
    uint32_t m_frameIndex = 0;

    // ---- Internal methods ----

    bool GenerateNoiseTextures();
    bool CreateCloudTextures(uint32_t screenW, uint32_t screenH);
    void ExecutePass(PassContext& ctx);
};

} // namespace SB
