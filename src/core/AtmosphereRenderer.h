#pragma once
//=============================================================================
//  AtmosphereRenderer — Physically-based Rayleigh+Mie atmosphere + sky
//
//  Precomputes transmittance and scattering LUTs, then replaces the game's
//  sky pixel shader with a physically-based sky evaluation.
//
//  Based on Bruneton (2008) / Hillaire (2020) atmospheric scattering model.
//=============================================================================

#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>
#include "ComputeManager.h"
#include "RenderPipeline.h"

namespace SB
{

class AtmosphereRenderer
{
public:
    static AtmosphereRenderer& Get()
    {
        static AtmosphereRenderer inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Call when sun position changes significantly (angle delta > threshold)
    void UpdateLUTs(float sunZenithCos, float sunAzimuth);

    // SRVs for sky shader or other consumers
    ID3D11ShaderResourceView* GetTransmittanceSRV() const { return m_transmittanceSRV; }
    ID3D11ShaderResourceView* GetScatteringSRV() const { return m_scatteringSRV; }
    ID3D11ShaderResourceView* GetAerialPerspectiveSRV() const { return m_aerialSRV; }

    static constexpr uint32_t kTransmittanceLUTSlot = 23;  // t23
    static constexpr uint32_t kScatteringLUTSlot    = 24;  // t24

    // Celestial settings
    void SetMoonPhase(float phase) { m_moonPhase = phase; }  // 0=new, 0.5=full, 1.0=new
    float GetMoonPhase() const { return m_moonPhase; }
    void SetMoonDirection(float zenithCos, float azimuth) { m_moonZenithCos = zenithCos; m_moonAzimuth = azimuth; }
    void SetStarIntensity(float i) { m_starIntensity = i; }
    float GetStarIntensity() const { return m_starIntensity; }
    void SetSunDiskIntensity(float i) { m_sunDiskIntensity = i; }
    float GetSunDiskIntensity() const { return m_sunDiskIntensity; }

    // Celestial output SRV (composited sky overlay)
    ID3D11ShaderResourceView* GetCelestialSRV() const { return m_celestialSRV; }
    static constexpr uint32_t kCelestialSRVSlot = 25;  // t25

    // Render celestial bodies (sun disk, moon, stars)
    void RenderCelestials(float sunZenithCos, float sunAzimuth);

private:
    AtmosphereRenderer() = default;

    bool m_initialized = false;
    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Atmosphere parameters
    float m_lastSunZenith = -999.0f;

    // LUTs
    // Transmittance: 256x64 R16G16B16A16_FLOAT (optical depth lookup)
    ID3D11Texture2D*            m_transmittanceTex = nullptr;
    ID3D11ShaderResourceView*   m_transmittanceSRV = nullptr;
    ID3D11UnorderedAccessView*  m_transmittanceUAV = nullptr;

    // Multi-scattering: 32x32 R16G16B16A16_FLOAT
    ID3D11Texture2D*            m_scatteringTex = nullptr;
    ID3D11ShaderResourceView*   m_scatteringSRV = nullptr;
    ID3D11UnorderedAccessView*  m_scatteringUAV = nullptr;

    // Aerial perspective: 32x32x32 volume (R16G16B16A16_FLOAT)
    ID3D11Texture3D*            m_aerialTex = nullptr;
    ID3D11ShaderResourceView*   m_aerialSRV = nullptr;
    ID3D11UnorderedAccessView*  m_aerialUAV = nullptr;

    // Compute shaders
    ComputeShaderID m_transmittanceCS = 0;
    ComputeShaderID m_scatteringCS    = 0;
    ComputeShaderID m_aerialCS        = 0;

    // Constants CB
    ID3D11Buffer* m_atmoCB = nullptr;

    // Linear sampler for LUT reads
    ID3D11SamplerState* m_linearSampler = nullptr;

    // Celestial parameters
    float m_moonPhase      = 0.5f;   // 0=new, 0.5=full, 1.0=new (normalized)
    float m_moonZenithCos  = -0.5f;  // Moon direction
    float m_moonAzimuth    = 3.14f;
    float m_starIntensity  = 1.0f;
    float m_sunDiskIntensity = 1.0f;

    // Celestial render target (full-res, R16G16B16A16_FLOAT)
    ID3D11Texture2D*           m_celestialTex = nullptr;
    ID3D11ShaderResourceView*  m_celestialSRV = nullptr;
    ID3D11UnorderedAccessView* m_celestialUAV = nullptr;
    ComputeShaderID            m_celestialCS  = 0;
    ID3D11Buffer*              m_celestialCB  = nullptr;

    uint32_t m_screenW = 0;
    uint32_t m_screenH = 0;

    // Sky replacement pass (registered in RenderPipeline)
    PassHandle m_skyPassHandle = 0;

    void ExecuteSkyPass(PassContext& ctx);
};

} // namespace SB
