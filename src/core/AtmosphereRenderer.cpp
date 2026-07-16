//=============================================================================
//  AtmosphereRenderer.cpp — Physically-based atmosphere + sky rendering
//
//  Three precomputed LUTs:
//    1. Transmittance (256x64): optical depth from any altitude/angle
//    2. Multi-scattering (32x32): 2nd+ order scattering contribution
//    3. Aerial perspective (32x32x32): in-scattering for distant objects
//
//  Sky evaluation runs as a PreENB pipeline pass — writes to a managed RT
//  that can be composited over the game sky or used for sky replacement.
//=============================================================================

#include "AtmosphereRenderer.h"
#include "SRVInjector.h"

#include <SKSE/SKSE.h>
#include <dxgi.h>
#include <cstring>
#include <cmath>

namespace SB
{

// ── Atmosphere constants CB ───────────────────────────────────────────────
struct AtmosphereCB
{
    float earthRadius;       // 6360 km
    float atmosphereRadius;  // 6460 km
    float pad0, pad1;

    float rayleighScaleH;    // 8.0 km
    float mieScaleH;         // 1.2 km
    float mieG;              // 0.8 (anisotropy)
    float sunIntensity;      // 20.0

    float rayleighR, rayleighG, rayleighB;
    float ozoneScale;

    float sunZenithCos;
    float sunAzimuth;
    float lutWidth, lutHeight;
};

// ── Celestial constant buffer (sun disk, moon, stars) ─────────────────────
struct CelestialCB
{
    float sunZenithCos, sunAzimuth;
    float moonZenithCos, moonAzimuth;
    float moonPhase;
    float starIntensity;
    float sunDiskIntensity;
    float sunIntensity;
    uint32_t screenW, screenH;
    float pad[2];
};

// ── Transmittance LUT compute shader ──────────────────────────────────────
// Maps (altitude, zenith angle) → optical depth through the atmosphere
static const char kTransmittanceCS[] = R"HLSL(
cbuffer AtmoCB : register(b0)
{
    float EarthRadius;
    float AtmoRadius;
    float pad0, pad1;
    float RayleighScaleH;
    float MieScaleH;
    float MieG;
    float SunIntensity;
    float RayleighR, RayleighG, RayleighB;
    float OzoneScale;
    float SunZenithCos;
    float SunAzimuth;
    float LutWidth, LutHeight;
};

RWTexture2D<float4> TransmittanceLUT : register(u0);

// Ray-sphere intersection (returns distance to far hit, -1 if miss)
float RaySphereIntersect(float3 ro, float3 rd, float3 center, float radius)
{
    float3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    return -b + sqrt(disc);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= (uint)LutWidth || DTid.y >= (uint)LutHeight)
        return;

    // Map texel to (altitude, cos zenith angle)
    float u = (float(DTid.x) + 0.5) / LutWidth;
    float v = (float(DTid.y) + 0.5) / LutHeight;

    float altitude = lerp(0.0, AtmoRadius - EarthRadius, u);
    float cosZenith = lerp(-1.0, 1.0, v);

    float3 origin = float3(0, EarthRadius + altitude, 0);
    float3 dir = float3(sqrt(max(0, 1.0 - cosZenith * cosZenith)), cosZenith, 0);

    // March through atmosphere, accumulate optical depth
    float tMax = RaySphereIntersect(origin, dir, float3(0,0,0), AtmoRadius);
    if (tMax < 0) tMax = 0;

    const int STEPS = 64;
    float dt = tMax / float(STEPS);

    float3 rayleighOD = 0;
    float  mieOD = 0;

    for (int i = 0; i < STEPS; i++)
    {
        float t = (float(i) + 0.5) * dt;
        float3 pos = origin + dir * t;
        float h = length(pos) - EarthRadius;
        h = max(h, 0);

        float rayleighDensity = exp(-h / RayleighScaleH);
        float mieDensity = exp(-h / MieScaleH);

        rayleighOD += rayleighDensity * dt;
        mieOD += mieDensity * dt;
    }

    float3 rayleighCoeff = float3(RayleighR, RayleighG, RayleighB);
    float  mieCoeff = 21e-6;  // Mie extinction

    float3 transmittance = exp(-(rayleighCoeff * rayleighOD + mieCoeff * mieOD));

    TransmittanceLUT[DTid.xy] = float4(transmittance, 1.0);
}
)HLSL";

// ── Multi-scattering LUT compute shader ───────────────────────────────────
// Approximates 2nd+ order scattering contribution (Hillaire 2020)
static const char kMultiScatterCS[] = R"HLSL(
cbuffer AtmoCB : register(b0)
{
    float EarthRadius;
    float AtmoRadius;
    float pad0, pad1;
    float RayleighScaleH;
    float MieScaleH;
    float MieG;
    float SunIntensity;
    float RayleighR, RayleighG, RayleighB;
    float OzoneScale;
    float SunZenithCos;
    float SunAzimuth;
    float LutWidth, LutHeight;
};

Texture2D<float4> TransmittanceLUT : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> MultiScatterLUT : register(u0);

float3 SampleTransmittance(float altitude, float cosZenith)
{
    float u = saturate(altitude / (AtmoRadius - EarthRadius));
    float v = saturate(cosZenith * 0.5 + 0.5);
    return TransmittanceLUT.SampleLevel(LinearSampler, float2(u, v), 0).rgb;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float u = (float(DTid.x) + 0.5) / 32.0;
    float v = (float(DTid.y) + 0.5) / 32.0;

    float altitude = u * (AtmoRadius - EarthRadius);
    float sunCosZenith = v * 2.0 - 1.0;

    // Integrate multi-scattering by sampling transmittance in multiple directions
    float3 result = 0;
    const int DIRS = 16;

    for (int i = 0; i < DIRS; i++)
    {
        float cosTheta = float(i) / float(DIRS - 1) * 2.0 - 1.0;
        float weight = 1.0 / float(DIRS);

        // Ground-reflected + atmospheric contribution
        float3 T = SampleTransmittance(altitude, cosTheta);

        // Phase-weighted scattering
        float phase = 0.25 / 3.14159265; // isotropic
        result += T * phase * weight;
    }

    // Scale by sun transmittance at this altitude
    float3 sunT = SampleTransmittance(altitude, sunCosZenith);
    result *= sunT * SunIntensity;

    MultiScatterLUT[DTid.xy] = float4(result, 1.0);
}
)HLSL";

// ── Aerial perspective compute shader ─────────────────────────────────────
// 3D LUT: (x=screen U, y=screen V, z=distance) → inscattering + extinction
static const char kAerialCS[] = R"HLSL(
cbuffer AtmoCB : register(b0)
{
    float EarthRadius;
    float AtmoRadius;
    float pad0, pad1;
    float RayleighScaleH;
    float MieScaleH;
    float MieG;
    float SunIntensity;
    float RayleighR, RayleighG, RayleighB;
    float OzoneScale;
    float SunZenithCos;
    float SunAzimuth;
    float LutWidth, LutHeight;
};

Texture2D<float4> TransmittanceLUT : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture3D<float4> AerialLUT : register(u0);

float3 SampleTransmittance(float altitude, float cosZenith)
{
    float u = saturate(altitude / (AtmoRadius - EarthRadius));
    float v = saturate(cosZenith * 0.5 + 0.5);
    return TransmittanceLUT.SampleLevel(LinearSampler, float2(u, v), 0).rgb;
}

// Henyey-Greenstein phase function
float HGPhase(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(abs(denom), 1.5));
}

[numthreads(4, 4, 4)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float3 uvw = (float3(DTid) + 0.5) / 32.0;

    // Map UVW to view parameters
    float viewCosZenith = uvw.y * 2.0 - 1.0;
    float maxDist = 128000.0;  // 128 km max distance
    float dist = uvw.z * uvw.z * maxDist;  // quadratic depth distribution

    float altitude = 0.01;  // Camera near ground

    float3 origin = float3(0, EarthRadius + altitude, 0);
    float3 viewDir = float3(sqrt(max(0, 1.0 - viewCosZenith * viewCosZenith)), viewCosZenith, 0);
    float3 sunDir = float3(sqrt(max(0, 1.0 - SunZenithCos * SunZenithCos)), SunZenithCos, 0);

    // Ray march to 'dist', accumulate in-scattering
    const int STEPS = 16;
    float dt = dist / float(STEPS);
    float3 inscatter = 0;
    float3 extinction = 1;
    float3 rayleighCoeff = float3(RayleighR, RayleighG, RayleighB);

    for (int i = 0; i < STEPS; i++)
    {
        float t = (float(i) + 0.5) * dt;
        float3 pos = origin + viewDir * t;
        float h = length(pos) - EarthRadius;
        h = max(h, 0);

        float rayleighD = exp(-h / RayleighScaleH);
        float mieD = exp(-h / MieScaleH);

        float3 localExtinction = rayleighCoeff * rayleighD + 21e-6 * mieD;
        float3 stepExtinction = exp(-localExtinction * dt);

        // Sun transmittance at this point
        float sunCosZ = dot(normalize(pos), sunDir);
        float3 sunT = SampleTransmittance(h, sunCosZ);

        // Phase functions
        float cosAngle = dot(viewDir, sunDir);
        float rayleighPhase = 3.0 / (16.0 * 3.14159265) * (1.0 + cosAngle * cosAngle);
        float miePhase = HGPhase(cosAngle, MieG);

        float3 scattering = sunT * SunIntensity *
            (rayleighCoeff * rayleighD * rayleighPhase + 21e-6 * mieD * miePhase);

        // Accumulate with extinction
        inscatter += extinction * scattering * dt;
        extinction *= stepExtinction;
    }

    AerialLUT[DTid] = float4(inscatter, 1.0 - dot(extinction, 1.0/3.0));
}
)HLSL";

// ── Celestial body compute shader (sun disk, moon, stars) ─────────────────
static const char kCelestialCS[] = R"HLSL(
cbuffer CelestialCB : register(b0)
{
    float SunZenithCos, SunAzimuth;
    float MoonZenithCos, MoonAzimuth;
    float MoonPhase;
    float StarIntensity;
    float SunDiskIntensity;
    float SunLuminance;
    uint2 ScreenDims;
    float2 pad;
};

Texture2D<float4> TransmittanceLUT : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> CelestialOutput : register(u0);

// Convert pixel to view direction on unit sphere (hemispherical sky mapping)
float3 pixelToViewDir(uint2 pixel, uint2 dims)
{
    float2 uv = (float2(pixel) + 0.5) / float2(dims);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    // ~90 deg vertical FOV sky dome mapping
    float3 dir = normalize(float3(ndc.x * 1.5, ndc.y * 1.5 + 0.5, 1.0));
    return dir;
}

float3 sphericalToDir(float zenithCos, float azimuth)
{
    float sinZenith = sqrt(max(0, 1.0 - zenithCos * zenithCos));
    return float3(sinZenith * cos(azimuth), zenithCos, sinZenith * sin(azimuth));
}

// PCG hash for procedural star placement
uint pcgHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float pcgFloat(uint x) { return float(pcgHash(x)) / 4294967295.0; }

float3 SampleTransmittance(float altitude, float cosZenith)
{
    float u = saturate(altitude / 100000.0);  // 100km atmosphere
    float v = saturate(cosZenith * 0.5 + 0.5);
    return TransmittanceLUT.SampleLevel(LinearSampler, float2(u, v), 0).rgb;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y) return;

    float3 viewDir = pixelToViewDir(DTid.xy, ScreenDims);
    float3 sunDir = sphericalToDir(SunZenithCos, SunAzimuth);
    float3 moonDir = sphericalToDir(MoonZenithCos, MoonAzimuth);

    float4 result = float4(0, 0, 0, 0);  // additive: rgb=light, a=0 (transparent)

    // View direction below horizon — skip
    if (viewDir.y < -0.01)
    {
        CelestialOutput[DTid.xy] = result;
        return;
    }

    float3 transmittance = SampleTransmittance(0.01, viewDir.y);

    // ── Sun disk ──
    float sunAngle = acos(saturate(dot(viewDir, sunDir)));
    float sunRadius = 0.00465;  // angular radius in radians (~0.27 deg)
    if (sunAngle < sunRadius * 3.0)  // include corona
    {
        if (sunAngle < sunRadius)
        {
            // Limb darkening: I(theta) = I0 * (1 - u*(1 - cos(theta))), u ~ 0.6
            float mu = cos(sunAngle / sunRadius * 1.5707);
            float limbDark = 1.0 - 0.6 * (1.0 - mu);
            float3 sunColor = float3(1.0, 0.95, 0.85) * SunLuminance * limbDark * SunDiskIntensity;
            result.rgb += sunColor * transmittance;
        }
        else
        {
            // Corona glow: exponential falloff beyond solar disk edge
            float corona = exp(-(sunAngle - sunRadius) / (sunRadius * 0.5));
            float3 coronaColor = float3(1.0, 0.9, 0.7) * SunLuminance * 0.1 * corona * SunDiskIntensity;
            result.rgb += coronaColor * transmittance;
        }
    }

    // ── Moon ──
    float moonAngle = acos(saturate(dot(viewDir, moonDir)));
    float moonRadius = 0.00452;  // ~0.26 deg
    if (moonAngle < moonRadius && moonDir.y > -0.05)
    {
        // Phase illumination: 0=new (dark), 0.5=full, 1.0=new (dark)
        float illumination = 0.5 * (1.0 + cos(6.28318 * MoonPhase));
        float moonAlbedo = 0.12;

        // Simple phase shading: lit side vs dark side
        float moonU = (moonAngle / moonRadius);
        float phaseFactor = lerp(illumination, 0.001, step(0.5, moonU));

        float3 moonColor = float3(0.85, 0.85, 0.9) * moonAlbedo * phaseFactor * SunLuminance;

        // Earthshine on dark side: very faint blue (~0.1% of full moon)
        float3 earthshine = float3(0.1, 0.12, 0.15) * 0.001 * (1.0 - illumination);

        result.rgb += (moonColor + earthshine) * transmittance;
    }

    // ── Stars ──
    // Only visible at night (sun below horizon) and above horizon
    float nightFactor = saturate(-SunZenithCos * 2.0);  // 1 when sun well below horizon
    if (nightFactor > 0.01 && viewDir.y > 0.0 && StarIntensity > 0.0)
    {
        // Tile celestial sphere into cells for star placement
        float theta = atan2(viewDir.z, viewDir.x);
        float phi = asin(viewDir.y);

        float starScale = 200.0;  // controls star density
        float2 starCell = floor(float2(theta, phi) * starScale);
        uint starHash = pcgHash(uint(starCell.x * 1597 + starCell.y * 51749));
        float starBrightness = pcgFloat(starHash);

        // Only ~3% of cells have visible stars (sparse field)
        if (starBrightness > 0.97)
        {
            float magnitude = (starBrightness - 0.97) / 0.03;  // 0 to 1
            float intensity = magnitude * magnitude * StarIntensity * nightFactor;

            // Horizon extinction
            intensity *= saturate(viewDir.y * 5.0);

            // Color variation based on star temperature
            float tempHash = pcgFloat(starHash ^ 0x12345678u);
            float3 starColor;
            if (tempHash < 0.3)
                starColor = float3(0.8, 0.85, 1.0);       // blue-white (hot)
            else if (tempHash < 0.7)
                starColor = float3(1.0, 0.98, 0.9);       // white-yellow
            else
                starColor = float3(1.0, 0.8, 0.6);        // orange (cool)

            result.rgb += starColor * intensity * transmittance * 0.5;
        }
    }

    CelestialOutput[DTid.xy] = result;
}
)HLSL";

// ── Initialize ────────────────────────────────────────────────────────────

bool AtmosphereRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    m_device = dev;
    m_context = ctx;

    // Get screen dimensions from swap chain
    DXGI_SWAP_CHAIN_DESC scDesc;
    if (sc && SUCCEEDED(sc->GetDesc(&scDesc))) {
        m_screenW = scDesc.BufferDesc.Width;
        m_screenH = scDesc.BufferDesc.Height;
    } else {
        m_screenW = 1920;
        m_screenH = 1080;
    }

    auto& cm = ComputeManager::Get();
    if (!cm.IsInitialized()) return false;

    // Compile compute shaders
    m_transmittanceCS = cm.CompileShader("AtmoTransmittance", kTransmittanceCS);
    m_scatteringCS    = cm.CompileShader("AtmoMultiScatter", kMultiScatterCS);
    m_aerialCS        = cm.CompileShader("AtmoAerial", kAerialCS);

    if (!m_transmittanceCS || !m_scatteringCS || !m_aerialCS) {
        SKSE::log::error("AtmosphereRenderer: failed to compile compute shaders");
        return false;
    }

    HRESULT hr;

    // Create Transmittance LUT (256x64)
    {
        auto res = cm.CreateTexture2D(256, 64, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                      true, true, 1, false, "TransmittanceLUT");
        if (!res.Valid()) return false;
        m_transmittanceTex = res.texture;
        m_transmittanceSRV = res.srv;
        m_transmittanceUAV = res.uav;
    }

    // Create Multi-scattering LUT (32x32)
    {
        auto res = cm.CreateTexture2D(32, 32, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                      true, true, 1, false, "MultiScatterLUT");
        if (!res.Valid()) return false;
        m_scatteringTex = res.texture;
        m_scatteringSRV = res.srv;
        m_scatteringUAV = res.uav;
    }

    // Create Aerial perspective LUT (32x32x32 Texture3D)
    {
        D3D11_TEXTURE3D_DESC desc = {};
        desc.Width = desc.Height = desc.Depth = 32;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        hr = dev->CreateTexture3D(&desc, nullptr, &m_aerialTex);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = 1;
        hr = dev->CreateShaderResourceView(m_aerialTex, &srvDesc, &m_aerialSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.WSize = 32;
        hr = dev->CreateUnorderedAccessView(m_aerialTex, &uavDesc, &m_aerialUAV);
        if (FAILED(hr)) return false;
    }

    // Constants CB
    m_atmoCB = cm.CreateConstantBuffer(sizeof(AtmosphereCB));
    if (!m_atmoCB) return false;

    // Linear sampler for LUT reads
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        hr = dev->CreateSamplerState(&sd, &m_linearSampler);
        if (FAILED(hr)) return false;
    }

    // Register SRVs for injection
    SRVInjector::Get().RegisterSRV(kTransmittanceLUTSlot, m_transmittanceSRV);
    SRVInjector::Get().RegisterSRV(kScatteringLUTSlot, m_scatteringSRV);

    // ── Celestial body resources ──────────────────────────────────────────
    // Compile celestial compute shader
    m_celestialCS = cm.CompileShader("CelestialBodies", kCelestialCS);
    if (!m_celestialCS) {
        SKSE::log::error("AtmosphereRenderer: failed to compile celestial CS");
        return false;
    }

    // Create celestial render target (full-res R16G16B16A16_FLOAT with SRV + UAV)
    {
        auto res = cm.CreateTexture2D(m_screenW, m_screenH, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                      true, true, 1, false, "CelestialRT");
        if (!res.Valid()) {
            SKSE::log::error("AtmosphereRenderer: failed to create celestial RT ({}x{})", m_screenW, m_screenH);
            return false;
        }
        m_celestialTex = res.texture;
        m_celestialSRV = res.srv;
        m_celestialUAV = res.uav;
    }

    // Create celestial constant buffer
    m_celestialCB = cm.CreateConstantBuffer(sizeof(CelestialCB));
    if (!m_celestialCB) {
        SKSE::log::error("AtmosphereRenderer: failed to create celestial CB");
        return false;
    }

    // Register celestial SRV for injection at t25
    SRVInjector::Get().RegisterSRV(kCelestialSRVSlot, m_celestialSRV);

    // Initial LUT computation (sun at noon)
    UpdateLUTs(1.0f, 0.0f);

    m_initialized = true;
    SKSE::log::info("AtmosphereRenderer: initialized (transmittance 256x64, scatter 32x32, aerial 32^3, celestial {}x{})",
                    m_screenW, m_screenH);
    return true;
}

void AtmosphereRenderer::Shutdown()
{
    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    SafeRelease(m_transmittanceTex);
    SafeRelease(m_transmittanceSRV);
    SafeRelease(m_transmittanceUAV);
    SafeRelease(m_scatteringTex);
    SafeRelease(m_scatteringSRV);
    SafeRelease(m_scatteringUAV);
    SafeRelease(m_aerialTex);
    SafeRelease(m_aerialSRV);
    SafeRelease(m_aerialUAV);
    SafeRelease(m_atmoCB);
    SafeRelease(m_linearSampler);
    SafeRelease(m_celestialTex);
    SafeRelease(m_celestialSRV);
    SafeRelease(m_celestialUAV);
    SafeRelease(m_celestialCB);
    m_initialized = false;
}

// ── Update LUTs when sun position changes ─────────────────────────────────

void AtmosphereRenderer::UpdateLUTs(float sunZenithCos, float sunAzimuth)
{
    if (!m_initialized) return;

    // Skip recomputation if sun hasn't moved significantly
    float delta = std::abs(sunZenithCos - m_lastSunZenith);
    if (delta < 0.01f && m_lastSunZenith > -900.0f) return;
    m_lastSunZenith = sunZenithCos;

    auto& cm = ComputeManager::Get();

    // Update CB with Earth atmosphere parameters
    AtmosphereCB cb;
    cb.earthRadius      = 6360000.0f;   // meters
    cb.atmosphereRadius = 6460000.0f;
    cb.rayleighScaleH   = 8000.0f;
    cb.mieScaleH        = 1200.0f;
    cb.mieG             = 0.8f;
    cb.sunIntensity     = 20.0f;
    // Rayleigh scattering coefficients at sea level (per meter)
    cb.rayleighR        = 5.802e-6f;
    cb.rayleighG        = 13.558e-6f;
    cb.rayleighB        = 33.1e-6f;
    cb.ozoneScale       = 1.0f;
    cb.sunZenithCos     = sunZenithCos;
    cb.sunAzimuth       = sunAzimuth;
    cb.lutWidth         = 256.0f;
    cb.lutHeight        = 64.0f;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_atmoCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &cb, sizeof(cb));
        m_context->Unmap(m_atmoCB, 0);
    }

    cm.SaveCSState();

    // Pass 1: Transmittance LUT
    {
        cm.CSSetCBs(0, 1, &m_atmoCB);
        ID3D11UnorderedAccessView* uavs[] = { m_transmittanceUAV };
        cm.CSSetUAVs(0, 1, uavs);
        cm.Dispatch(m_transmittanceCS, 256 / 8, 64 / 8, 1);
        cm.CSClearUAVs(0, 1);
    }

    // Pass 2: Multi-scattering LUT (reads transmittance)
    {
        cm.CSSetCBs(0, 1, &m_atmoCB);
        ID3D11ShaderResourceView* srvs[] = { m_transmittanceSRV };
        cm.CSSetSRVs(0, 1, srvs);
        ID3D11SamplerState* samplers[] = { m_linearSampler };
        cm.CSSetSamplers(0, 1, samplers);
        ID3D11UnorderedAccessView* uavs[] = { m_scatteringUAV };
        cm.CSSetUAVs(0, 1, uavs);
        cm.Dispatch(m_scatteringCS, 32 / 8, 32 / 8, 1);
        cm.CSClearSRVs(0, 1);
        cm.CSClearUAVs(0, 1);
    }

    // Pass 3: Aerial perspective LUT (reads transmittance)
    {
        cm.CSSetCBs(0, 1, &m_atmoCB);
        ID3D11ShaderResourceView* srvs[] = { m_transmittanceSRV };
        cm.CSSetSRVs(0, 1, srvs);
        ID3D11SamplerState* samplers[] = { m_linearSampler };
        cm.CSSetSamplers(0, 1, samplers);
        ID3D11UnorderedAccessView* uavs[] = { m_aerialUAV };
        cm.CSSetUAVs(0, 1, uavs);
        cm.Dispatch(m_aerialCS, 32 / 4, 32 / 4, 32 / 4);
        cm.CSClearSRVs(0, 1);
        cm.CSClearUAVs(0, 1);
    }

    cm.RestoreCSState();

    // Render celestial bodies after LUTs are updated (they sample transmittance)
    RenderCelestials(sunZenithCos, sunAzimuth);
}

// ── Render celestial bodies (sun disk, moon, stars) ───────────────────────

void AtmosphereRenderer::RenderCelestials(float sunZenithCos, float sunAzimuth)
{
    if (!m_initialized || !m_celestialCS || !m_celestialCB || !m_celestialUAV)
        return;

    auto& cm = ComputeManager::Get();

    // Fill celestial constant buffer
    CelestialCB cb;
    cb.sunZenithCos    = sunZenithCos;
    cb.sunAzimuth      = sunAzimuth;
    cb.moonZenithCos   = m_moonZenithCos;
    cb.moonAzimuth     = m_moonAzimuth;
    cb.moonPhase       = m_moonPhase;
    cb.starIntensity   = m_starIntensity;
    cb.sunDiskIntensity = m_sunDiskIntensity;
    cb.sunIntensity    = 20.0f;  // Match atmosphere sun luminance
    cb.screenW         = m_screenW;
    cb.screenH         = m_screenH;
    cb.pad[0] = cb.pad[1] = 0.0f;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(m_context->Map(m_celestialCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    std::memcpy(mapped.pData, &cb, sizeof(cb));
    m_context->Unmap(m_celestialCB, 0);

    cm.SaveCSState();

    // Bind resources: CB at b0, transmittance LUT at t0 + sampler at s0, output UAV at u0
    cm.CSSetCBs(0, 1, &m_celestialCB);
    ID3D11ShaderResourceView* srvs[] = { m_transmittanceSRV };
    cm.CSSetSRVs(0, 1, srvs);
    ID3D11SamplerState* samplers[] = { m_linearSampler };
    cm.CSSetSamplers(0, 1, samplers);
    ID3D11UnorderedAccessView* uavs[] = { m_celestialUAV };
    cm.CSSetUAVs(0, 1, uavs);

    // Dispatch: 8x8 thread groups over full screen resolution
    uint32_t groupsX = (m_screenW + 7) / 8;
    uint32_t groupsY = (m_screenH + 7) / 8;
    cm.Dispatch(m_celestialCS, groupsX, groupsY, 1);

    cm.CSClearSRVs(0, 1);
    cm.CSClearUAVs(0, 1);

    cm.RestoreCSState();
}

void AtmosphereRenderer::ExecuteSkyPass(PassContext& ctx)
{
    // TODO: Replace game sky shader with physically-based evaluation
    // using the precomputed LUTs. For now, LUTs are available to any
    // shader that samples t23/t24.
}

} // namespace SB
