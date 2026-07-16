//=============================================================================
//  VolumetricClouds.cpp --- GPU volumetric cloud raymarching
//
//  Three-pass pipeline:
//    1. NoiseGen CS (init-time): shape 128^3 + detail 32^3 Worley/Perlin
//    2. CloudRaymarch CS (per-frame, quarter-res): density + inscattering
//    3. CloudComposite PS (fullscreen): bilateral upsample + blend
//=============================================================================

#include "VolumetricClouds.h"
#include "SRVInjector.h"
#include "D3D11Hook.h"
#include "SceneData.h"
#include "HiZPyramid.h"
#include "ComputeManager.h"
#include "RenderPassManager.h"

#include <SKSE/SKSE.h>
#include <cstring>
#include <cmath>

namespace SB
{

// ============================================================================
//  HLSL: Shape noise generation (128^3, 8x8x1 thread groups)
//  Perlin-Worley FBM for base cloud shape
// ============================================================================
static const char kShapeNoiseCS[] = R"HLSL(
RWTexture3D<float> ShapeNoise : register(u0);

// --- Pseudo-random hash functions (no external dependencies) ---------------
uint hash(uint x)
{
    x ^= x >> 16u;
    x *= 0x45d9f3bu;
    x ^= x >> 16u;
    x *= 0x45d9f3bu;
    x ^= x >> 16u;
    return x;
}

uint hash3(uint3 v)
{
    return hash(v.x ^ hash(v.y ^ hash(v.z)));
}

float hashFloat(uint x)
{
    return float(hash(x)) / 4294967295.0;
}

float3 hashFloat3(uint3 v)
{
    uint h = hash3(v);
    return float3(
        hashFloat(h),
        hashFloat(h ^ 0xA341316Cu),
        hashFloat(h ^ 0x9E3779B9u)
    );
}

// --- 3D gradient noise (Perlin-style) --------------------------------------
float gradientNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * (3.0 - 2.0 * f);  // smoothstep

    // 8 corner gradients
    float n000 = dot(hashFloat3(uint3(i) + uint3(0,0,0)) * 2.0 - 1.0, f - float3(0,0,0));
    float n100 = dot(hashFloat3(uint3(i) + uint3(1,0,0)) * 2.0 - 1.0, f - float3(1,0,0));
    float n010 = dot(hashFloat3(uint3(i) + uint3(0,1,0)) * 2.0 - 1.0, f - float3(0,1,0));
    float n110 = dot(hashFloat3(uint3(i) + uint3(1,1,0)) * 2.0 - 1.0, f - float3(1,1,0));
    float n001 = dot(hashFloat3(uint3(i) + uint3(0,0,1)) * 2.0 - 1.0, f - float3(0,0,1));
    float n101 = dot(hashFloat3(uint3(i) + uint3(1,0,1)) * 2.0 - 1.0, f - float3(1,0,1));
    float n011 = dot(hashFloat3(uint3(i) + uint3(0,1,1)) * 2.0 - 1.0, f - float3(0,1,1));
    float n111 = dot(hashFloat3(uint3(i) + uint3(1,1,1)) * 2.0 - 1.0, f - float3(1,1,1));

    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);
    float nxy0 = lerp(nx00, nx10, u.y);
    float nxy1 = lerp(nx01, nx11, u.y);
    return lerp(nxy0, nxy1, u.z);
}

// --- 3D Worley noise (cellular) --------------------------------------------
// NOTE: [unroll] removed from 3x3x3 loops — causes D3DCompile with
// OPTIMIZATION_LEVEL3 to hang for minutes. 27 iterations is fine as a loop.
float worleyNoise(float3 p, float cellCount)
{
    float3 cell = p * cellCount;
    float3 iCell = floor(cell);
    float3 fCell = frac(cell);

    float minDist = 1.0;

    [loop]
    for (int z = -1; z <= 1; z++)
    [loop]
    for (int y = -1; y <= 1; y++)
    [loop]
    for (int x = -1; x <= 1; x++)
    {
        float3 offset = float3(x, y, z);
        float3 neighbor = iCell + offset;

        // Tile wrapping
        float3 wrapped = fmod(neighbor + cellCount, cellCount);

        float3 featurePoint = hashFloat3(uint3(wrapped));
        float3 diff = offset + featurePoint - fCell;
        float dist = dot(diff, diff);
        minDist = min(minDist, dist);
    }

    return sqrt(minDist);
}

// --- Perlin FBM ------------------------------------------------------------
float perlinFBM(float3 p, int octaves)
{
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++)
    {
        value += amplitude * gradientNoise(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }

    return value;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= 128 || DTid.y >= 128)
        return;

    // Iterate over Z slices within this thread (128 / 1 = full column)
    for (uint z = 0; z < 128; z++)
    {
        float3 uvw = float3(DTid.xy, z) / 128.0;

        // Perlin FBM at multiple frequencies
        float perlin = perlinFBM(uvw * 8.0, 4) * 0.5 + 0.5;

        // Worley noise at 3 octaves, inverted (1 - worley = cloud-like)
        float worley0 = 1.0 - worleyNoise(uvw, 4.0);
        float worley1 = 1.0 - worleyNoise(uvw, 8.0);
        float worley2 = 1.0 - worleyNoise(uvw, 16.0);

        // Worley FBM
        float worleyFBM = worley0 * 0.625 + worley1 * 0.25 + worley2 * 0.125;

        // Perlin-Worley blend: use Perlin to modulate Worley
        float shape = saturate(lerp(worleyFBM, perlin, 0.3));

        ShapeNoise[uint3(DTid.xy, z)] = shape;
    }
}
)HLSL";

// ============================================================================
//  HLSL: Detail noise generation (32^3, 8x8x1 thread groups)
//  High-frequency Worley for erosion
// ============================================================================
static const char kDetailNoiseCS[] = R"HLSL(
RWTexture3D<float> DetailNoise : register(u0);

uint hash(uint x)
{
    x ^= x >> 16u;
    x *= 0x45d9f3bu;
    x ^= x >> 16u;
    x *= 0x45d9f3bu;
    x ^= x >> 16u;
    return x;
}

uint hash3(uint3 v)
{
    return hash(v.x ^ hash(v.y ^ hash(v.z)));
}

float hashFloat(uint x)
{
    return float(hash(x)) / 4294967295.0;
}

float3 hashFloat3(uint3 v)
{
    uint h = hash3(v);
    return float3(
        hashFloat(h),
        hashFloat(h ^ 0xA341316Cu),
        hashFloat(h ^ 0x9E3779B9u)
    );
}

float worleyNoise(float3 p, float cellCount)
{
    float3 cell = p * cellCount;
    float3 iCell = floor(cell);
    float3 fCell = frac(cell);

    float minDist = 1.0;

    [loop]
    for (int z = -1; z <= 1; z++)
    [loop]
    for (int y = -1; y <= 1; y++)
    [loop]
    for (int x = -1; x <= 1; x++)
    {
        float3 offset = float3(x, y, z);
        float3 neighbor = iCell + offset;
        float3 wrapped = fmod(neighbor + cellCount, cellCount);
        float3 featurePoint = hashFloat3(uint3(wrapped));
        float3 diff = offset + featurePoint - fCell;
        float dist = dot(diff, diff);
        minDist = min(minDist, dist);
    }

    return sqrt(minDist);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= 32 || DTid.y >= 32)
        return;

    for (uint z = 0; z < 32; z++)
    {
        float3 uvw = float3(DTid.xy, z) / 32.0;

        // 3-octave Worley at high frequency
        float w0 = 1.0 - worleyNoise(uvw, 8.0);
        float w1 = 1.0 - worleyNoise(uvw, 16.0);
        float w2 = 1.0 - worleyNoise(uvw, 32.0);

        float detail = w0 * 0.625 + w1 * 0.25 + w2 * 0.125;

        DetailNoise[uint3(DTid.xy, z)] = saturate(detail);
    }
}
)HLSL";

// ============================================================================
//  HLSL: Cloud raymarch compute shader (quarter-res, 8x8 thread groups)
//  Marches rays through 1500m-4000m cloud layer, samples noise for density,
//  computes inscatter (Henyey-Greenstein dual-lobe) + Beer's law extinction.
//  Temporal reprojection reuses previous frame where possible.
// ============================================================================
static const char kCloudRaymarchCS[] = R"HLSL(
cbuffer CloudCB : register(b0)
{
    float3 SunDirection;
    float  Time;
    float3 SunColor;
    float  CloudBase;       // 1500m
    float3 CameraPos;
    float  CloudTop;        // 4000m
    float3 WindOffset;
    float  Coverage;        // [0..1]
    float2 ScreenSize;      // quarter-res dimensions
    float  Density;
    float  FrameIndex;
    row_major float4x4 PrevViewProj;  // for temporal reprojection
    row_major float4x4 InvViewProj;   // current frame inverse VP
    // Fog parameters
    float  FogDensity;
    float  FogHeight;
    float  FogFalloff;
    float  FogAnisotropy;
    uint   FogEnabled;
    float3 fogPad;
};

Texture3D<float>  ShapeNoise  : register(t0);
Texture3D<float>  DetailNoise : register(t1);
Texture2D<float4> PrevCloud   : register(t2);  // previous frame cloud result
Texture2D<float>  DepthBuffer : register(t3);   // full-res depth for composite

SamplerState TrilinearSampler : register(s0);

RWTexture2D<float4> CloudOut : register(u0);

static const float PI = 3.14159265;

// Henyey-Greenstein phase function
float HGPhase(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(abs(denom), 1.5));
}

// Dual-lobe phase: forward (g=0.8) + back-scattering (g=-0.3)
float dualLobePhase(float cosTheta)
{
    float forward = HGPhase(cosTheta, 0.8);
    float back    = HGPhase(cosTheta, -0.3);
    return lerp(forward, back, 0.3);
}

// Beer's law extinction
float beerLaw(float density)
{
    return exp(-density);
}

// Powder sugar effect: enhances scattering at cloud edges
float powderEffect(float density)
{
    return 1.0 - exp(-density * 2.0);
}

// Remap value from [lo,hi] to [0,1]
float remap(float value, float lo, float hi)
{
    return saturate((value - lo) / (hi - lo));
}

// Height-based exponential fog density
float fogDensityAtHeight(float height)
{
    if (height > FogHeight) return 0.0;
    // Exponential falloff with height
    float normalizedH = max(height, 0.0);
    return FogDensity * exp(-normalizedH * FogFalloff);
}

// Sample cloud density at a world-space position
float sampleCloudDensity(float3 worldPos)
{
    float cloudThickness = CloudTop - CloudBase;
    float heightFraction = saturate((worldPos.y - CloudBase) / cloudThickness);

    // Height gradient: clouds are denser in the middle of the layer
    float heightGrad = saturate(remap(heightFraction, 0.0, 0.12) *
                                remap(heightFraction, 1.0, 0.8));

    // Noise sampling coordinates (with wind offset for animation)
    float3 noiseCoord = (worldPos + WindOffset) * 0.0003;  // scale to noise space

    // Shape noise (coarse cloud form)
    float shape = ShapeNoise.SampleLevel(TrilinearSampler, noiseCoord, 0).r;

    // Remap shape with coverage
    float shapeDensity = remap(shape, 1.0 - Coverage, 1.0);
    shapeDensity *= heightGrad;

    // Early out for empty regions
    if (shapeDensity <= 0.0)
        return 0.0;

    // Detail noise (high-frequency erosion)
    float3 detailCoord = (worldPos + WindOffset * 0.5) * 0.001;
    float detail = DetailNoise.SampleLevel(TrilinearSampler, detailCoord, 0).r;

    // Erode shape with detail (less erosion at dense core)
    float detailMod = lerp(detail, 1.0 - detail, saturate(heightFraction * 5.0));
    float cloudDensity = remap(shapeDensity, detailMod * 0.35, 1.0);

    // Fog-cloud transition: blend fog density into cloud base for smooth handoff
    float transitionZone = 200.0;  // 200m blend zone
    float transitionFactor = saturate((worldPos.y - CloudBase) / transitionZone);
    // Fog density fades out as cloud takes over
    float fogContrib = FogEnabled ? fogDensityAtHeight(worldPos.y) * (1.0 - transitionFactor) : 0.0;
    float finalDensity = max(cloudDensity * Density + fogContrib, 0.0);

    return finalDensity;
}

// Light marching: estimate inscatter along sun direction
float3 lightMarch(float3 pos)
{
    float3 lightDir = normalize(SunDirection);
    float  stepSize = (CloudTop - CloudBase) / 6.0;
    float  totalDensity = 0.0;

    for (int i = 0; i < 6; i++)
    {
        float3 samplePos = pos + lightDir * (float(i) + 0.5) * stepSize;

        // Skip samples outside cloud layer
        if (samplePos.y < CloudBase || samplePos.y > CloudTop)
            continue;

        totalDensity += sampleCloudDensity(samplePos) * stepSize;
    }

    float transmittance = beerLaw(totalDensity);
    float powder = powderEffect(totalDensity);

    // Combine Beer + powder for realistic cloud lighting
    float lightEnergy = transmittance * lerp(1.0, powder, 0.5);

    return SunColor * lightEnergy;
}

// Reconstruct world position from UV + depth (for reprojection check)
float3 uvToWorldPos(float2 uv, float depth)
{
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;
    float4 worldPos = mul(InvViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

// Ray-plane intersection: returns t such that (ro + rd*t).y == planeY
float rayPlaneIntersect(float3 ro, float3 rd, float planeY)
{
    if (abs(rd.y) < 1e-6) return -1.0;
    return (planeY - ro.y) / rd.y;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= (uint)ScreenSize.x || DTid.y >= (uint)ScreenSize.y)
        return;

    float2 uv = (float2(DTid.xy) + 0.5) / ScreenSize;

    // Reconstruct ray direction from UV
    float4 clipFar = float4(uv * 2.0 - 1.0, 1.0, 1.0);
    clipFar.y = -clipFar.y;
    float4 worldFar = mul(InvViewProj, clipFar);
    worldFar.xyz /= worldFar.w;

    float3 rayOrigin = CameraPos;
    float3 rayDir = normalize(worldFar.xyz - CameraPos);

    // ---- Temporal reprojection check ----
    // Reproject this pixel into previous frame's UV space
    float4 prevClip = mul(PrevViewProj, float4(worldFar.xyz, 1.0));
    float2 prevUV = (prevClip.xy / prevClip.w) * float2(0.5, -0.5) + 0.5;

    bool reprojValid = false;
    float4 prevResult = float4(0, 0, 0, 1);

    if (all(prevUV >= 0.0) && all(prevUV <= 1.0))
    {
        prevResult = PrevCloud.SampleLevel(TrilinearSampler, prevUV, 0);

        // Reject if UV is near border (likely disoccluded)
        float2 border = min(prevUV, 1.0 - prevUV);
        float borderDist = min(border.x, border.y);

        // Accept reprojection if border distance is sufficient
        // and frame index is odd (checkerboard temporal update)
        if (borderDist > 0.01 && fmod(FrameIndex + DTid.x + DTid.y, 2.0) > 0.5)
        {
            reprojValid = true;
        }
    }

    if (reprojValid)
    {
        CloudOut[DTid.xy] = prevResult;
        return;
    }

    // ---- Raymarch through cloud layer ----

    // Find intersection with cloud layer
    float tBase = rayPlaneIntersect(rayOrigin, rayDir, CloudBase);
    float tTop  = rayPlaneIntersect(rayOrigin, rayDir, CloudTop);

    // Handle camera inside cloud layer
    float tMin, tMax;
    if (rayOrigin.y < CloudBase)
    {
        // Below clouds: enter at base, exit at top
        if (tBase < 0.0) { CloudOut[DTid.xy] = float4(0, 0, 0, 1); return; }
        tMin = tBase;
        tMax = (tTop > 0.0) ? tTop : 100000.0;
    }
    else if (rayOrigin.y > CloudTop)
    {
        // Above clouds: enter at top, exit at base
        if (tTop < 0.0) { CloudOut[DTid.xy] = float4(0, 0, 0, 1); return; }
        tMin = tTop;
        tMax = (tBase > 0.0) ? tBase : 100000.0;
    }
    else
    {
        // Inside cloud layer
        tMin = 0.0;
        tMax = max(tBase, tTop);
        if (tMax < 0.0) tMax = 100000.0;
    }

    if (tMin < 0.0 || tMin >= tMax)
    {
        CloudOut[DTid.xy] = float4(0, 0, 0, 1);
        return;
    }

    // Clamp march distance
    tMax = min(tMax, tMin + 50000.0);  // max 50km march

    // Adaptive step count based on layer thickness traversed
    float marchDist = tMax - tMin;
    int stepCount = clamp((int)(marchDist / 50.0), 32, 128);
    float stepSize = marchDist / float(stepCount);

    // Phase function for sun scattering
    float cosAngle = dot(rayDir, normalize(SunDirection));
    float phase = dualLobePhase(cosAngle);

    // Accumulate inscatter and transmittance
    float3 inscatter = 0;
    float  transmittance = 1.0;

    // Blue noise-like offset per pixel to reduce banding
    float offset = frac(sin(dot(float2(DTid.xy), float2(12.9898, 78.233))) * 43758.5453);

    // ---- Fog volume march (below cloud base) ----
    if (FogEnabled && CameraPos.y < CloudBase)
    {
        float fogTMax = min(tMin, 20000.0);  // Fog up to cloud base or 20km
        if (fogTMax > 0)
        {
            int fogSteps = clamp((int)(fogTMax / 100.0), 8, 64);
            float fogDt = fogTMax / float(fogSteps);
            float fogOffset = frac(sin(dot(float2(DTid.xy) + 0.5, float2(12.9898, 78.233))) * 43758.5453);

            for (int fi = 0; fi < fogSteps; fi++)
            {
                if (transmittance < 0.01) break;

                float ft = (float(fi) + fogOffset) * fogDt;
                float3 fogPos = rayOrigin + rayDir * ft;

                float fogD = fogDensityAtHeight(fogPos.y);
                if (fogD > 0.0)
                {
                    float fogExtinction = fogD * fogDt;
                    float fogStepT = exp(-fogExtinction);

                    // Fog inscatter: simplified HG + sun
                    float fogPhase = HGPhase(cosAngle, FogAnisotropy);
                    float3 fogScatter = SunColor * fogD * fogPhase;

                    // Ambient contribution (sky scattered light)
                    float3 ambientFog = SunColor * 0.15 * fogD;

                    float3 fogContrib = (fogScatter + ambientFog) * (1.0 - fogStepT) / max(fogD, 1e-6);
                    inscatter += transmittance * fogContrib;
                    transmittance *= fogStepT;
                }
            }
        }
    }

    // ---- Cloud layer raymarch ----
    for (int i = 0; i < stepCount; i++)
    {
        if (transmittance < 0.01) break;  // early exit when opaque

        float t = tMin + (float(i) + offset) * stepSize;
        float3 samplePos = rayOrigin + rayDir * t;

        float density = sampleCloudDensity(samplePos);

        if (density > 0.0)
        {
            float extinction = density * stepSize;

            // Light march for inscatter
            float3 lightColor = lightMarch(samplePos);

            // Inscatter contribution: phase * light * (1 - exp(-extinction))
            float3 scattering = phase * lightColor * density;

            // Integrate with energy-conserving accumulation
            float stepTransmittance = beerLaw(extinction);
            float3 integScatter = scattering * (1.0 - stepTransmittance) / max(density, 1e-6);

            inscatter += transmittance * integScatter;
            transmittance *= stepTransmittance;
        }
    }

    CloudOut[DTid.xy] = float4(inscatter, transmittance);
}
)HLSL";

// ============================================================================
//  HLSL: Cloud composite pixel shader (fullscreen)
//  Bilateral upsample from quarter-res + blend using transmittance
// ============================================================================
static const char kCloudCompositePS[] = R"HLSL(
cbuffer CompositeCB : register(b0)
{
    float2 QuarterSize;    // quarter-res dimensions
    float2 FullSize;       // full-res dimensions
    float  DepthThreshold; // bilateral depth threshold
    float  Pad0, Pad1, Pad2;
};

Texture2D<float4> CloudTex   : register(t0);  // quarter-res cloud (inscatter.rgb, transmittance.a)
Texture2D<float>  DepthTex   : register(t1);  // full-res depth
Texture2D<float4> SceneColor : register(t2);  // current backbuffer

SamplerState PointSampler   : register(s0);
SamplerState LinearSampler  : register(s1);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main(VSOut input) : SV_Target
{
    float2 uv = input.uv;

    // Full-res depth at this pixel
    float depth = DepthTex.SampleLevel(PointSampler, uv, 0).r;

    // Bilateral upsample: sample 4 nearest quarter-res texels, weight by depth similarity
    float2 quarterUV = uv;
    float2 texelSize = 1.0 / QuarterSize;

    // Get quarter-res texel center
    float2 quarterPos = uv * QuarterSize - 0.5;
    float2 baseTexel = floor(quarterPos);
    float2 frac2 = quarterPos - baseTexel;

    float4 totalCloud = 0;
    float  totalWeight = 0;

    [unroll]
    for (int dy = 0; dy <= 1; dy++)
    [unroll]
    for (int dx = 0; dx <= 1; dx++)
    {
        float2 sampleTexel = baseTexel + float2(dx, dy);
        float2 sampleUV = (sampleTexel + 0.5) / QuarterSize;
        sampleUV = saturate(sampleUV);

        // Bilinear weight
        float2 d = float2(dx, dy);
        float bilinearWeight = (1.0 - abs(frac2.x - d.x)) * (1.0 - abs(frac2.y - d.y));

        // Depth at quarter-res texel (sample full-res depth at that location)
        float sampleDepth = DepthTex.SampleLevel(PointSampler, sampleUV, 0).r;

        // Bilateral depth weight (reject samples with very different depth)
        float depthDiff = abs(depth - sampleDepth);
        float depthWeight = exp(-depthDiff * depthDiff / (DepthThreshold * DepthThreshold + 1e-6));

        float weight = bilinearWeight * depthWeight;

        float4 cloud = CloudTex.SampleLevel(PointSampler, sampleUV, 0);
        totalCloud += cloud * weight;
        totalWeight += weight;
    }

    if (totalWeight > 0.0)
        totalCloud /= totalWeight;
    else
        totalCloud = CloudTex.SampleLevel(LinearSampler, uv, 0);

    // Skip sky pixels — at PostGeometry, sky hasn't been rendered yet
    // (scene RT contains clear color). Compositing onto these would darken
    // the sky when the game later renders the sky dome with alpha blending.
    // Reversed-Z: sky ≈ 0.0 (after HiZ CSCopy conversion)
    if (depth < 0.0001)
    {
        float3 scene = SceneColor.SampleLevel(PointSampler, uv, 0).rgb;
        return float4(scene, 1.0);  // passthrough
    }

    // Blend with scene: output = inscatter + scene * transmittance
    float3 scene = SceneColor.SampleLevel(PointSampler, uv, 0).rgb;
    float3 result = totalCloud.rgb + scene * totalCloud.a;

    return float4(result, 1.0);
}
)HLSL";

// ============================================================================
//  CB data structures (must match HLSL layouts)
// ============================================================================

struct CloudCBData
{
    float sunDirection[3];
    float time;
    float sunColor[3];
    float cloudBase;
    float cameraPos[3];
    float cloudTop;
    float windOffset[3];
    float coverage;
    float screenSize[2];
    float density;
    float frameIndex;
    float prevViewProj[16];
    float invViewProj[16];
    // Fog parameters
    float fogDensity;       // Base fog density
    float fogHeight;        // Fog layer height
    float fogFalloff;       // Exponential falloff rate
    float fogAnisotropy;    // Scattering anisotropy
    uint32_t fogEnabled;    // 0 or 1
    float fogPad[3];        // Alignment padding
};

struct CompositeCBData
{
    float quarterSize[2];
    float fullSize[2];
    float depthThreshold;
    float pad[3];
};

// ============================================================================
//  Initialize
// ============================================================================

bool VolumetricClouds::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;

    m_device  = dev;
    m_context = ctx;

    auto& cm  = ComputeManager::Get();
    auto& rpm = RenderPassManager::Get();
    auto& pl  = RenderPipeline::Get();

    if (!cm.IsInitialized() || !rpm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("VolumetricClouds: prerequisites not initialized");
        return false;
    }

    // Compile noise generation compute shaders
    m_shapeNoiseCS = cm.CompileShader("CloudShapeNoise", kShapeNoiseCS);
    if (!m_shapeNoiseCS) {
        SKSE::log::error("VolumetricClouds: failed to compile ShapeNoise CS");
        return false;
    }

    m_detailNoiseCS = cm.CompileShader("CloudDetailNoise", kDetailNoiseCS);
    if (!m_detailNoiseCS) {
        SKSE::log::error("VolumetricClouds: failed to compile DetailNoise CS");
        return false;
    }

    // Compile cloud raymarch compute shader
    m_raymarchCS = cm.CompileShader("CloudRaymarch", kCloudRaymarchCS);
    if (!m_raymarchCS) {
        SKSE::log::error("VolumetricClouds: failed to compile CloudRaymarch CS");
        return false;
    }

    // Register composite fullscreen pass
    m_compositePass = rpm.RegisterPass({
        .name = "CloudComposite",
        .psSource = kCloudCompositePS,
    });
    if (!m_compositePass) {
        SKSE::log::error("VolumetricClouds: failed to register CloudComposite pass");
        return false;
    }

    // Create cloud raymarch constant buffer
    m_cloudCB = cm.CreateConstantBuffer(sizeof(CloudCBData));
    if (!m_cloudCB) return false;

    // Create composite constant buffer
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(CompositeCBData);
        desc.Usage           = D3D11_USAGE_DYNAMIC;
        desc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&desc, nullptr, &m_compositeCB)))
            return false;
    }

    // Create trilinear wrap sampler for 3D noise reads
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        if (FAILED(dev->CreateSamplerState(&sd, &m_trilinearSampler)))
            return false;
    }

    // Defer noise generation to first ExecutePass — dispatching compute shaders
    // during kDataLoaded init corrupts D3D11 state and breaks Scaleform UI.
    // m_noiseGenerated = false already, so first ExecutePass will generate them.

    // Create quarter-res cloud render targets
    {
        ID3D11Texture2D* backTex = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backTex)))) {
            D3D11_TEXTURE2D_DESC bbDesc;
            backTex->GetDesc(&bbDesc);
            backTex->Release();

            if (!CreateCloudTextures(bbDesc.Width, bbDesc.Height))
                return false;
        } else {
            return false;
        }
    }

    // Register SRV at t27 for ENB shaders
    SRVInjector::Get().RegisterSRV(kCloudSRVSlot, m_cloudSRV);

    // Register as PreENB pipeline pass (runs before ENB for same-frame SRV visibility)
    // Register at PostGeometry with priority 80 (after core effects at 15-25,
    // before SceneCompositor at 90).  PostSky is ideal but currently never
    // fires because no sky shader hashes are registered for phase detection.
    // TODO: re-enable PostSky once sky shader hash discovery is implemented.
    m_pipelineHandle = pl.AddPass({
        .name     = "VolumetricClouds",
        .stage    = PipelineStage::PostGeometry,
        .priority = 80,
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    SKSE::log::info("VolumetricClouds: initialized (quarter-res {}x{}, shape 128^3, detail 32^3)",
        m_quarterW, m_quarterH);
    return true;
}

// ============================================================================
//  Shutdown
// ============================================================================

void VolumetricClouds::Shutdown()
{
    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };

    SafeRelease(m_shapeNoiseTex);
    SafeRelease(m_shapeNoiseSRV);
    SafeRelease(m_shapeNoiseUAV);
    SafeRelease(m_detailNoiseTex);
    SafeRelease(m_detailNoiseSRV);
    SafeRelease(m_detailNoiseUAV);
    SafeRelease(m_cloudTex);
    SafeRelease(m_cloudSRV);
    SafeRelease(m_cloudUAV);
    SafeRelease(m_cloudHistTex);
    SafeRelease(m_cloudHistSRV);
    SafeRelease(m_cloudCB);
    SafeRelease(m_compositeCB);
    SafeRelease(m_trilinearSampler);

    m_noiseGenerated = false;
    m_initialized = false;
}

// ============================================================================
//  Wind accumulation (called from main frame loop with WeatherTracker data)
// ============================================================================

void VolumetricClouds::AccumulateWind(float dx, float dy, float dz)
{
    m_windOffsetX += dx;
    m_windOffsetY += dy;
    m_windOffsetZ += dz;

    // Wrap to prevent float precision loss after hours of play.
    // Period is large enough to be visually seamless (noise tiles at ~3333 world units).
    constexpr float kWrapPeriod = 100000.0f;
    m_windOffsetX = std::fmod(m_windOffsetX, kWrapPeriod);
    m_windOffsetY = std::fmod(m_windOffsetY, kWrapPeriod);
    m_windOffsetZ = std::fmod(m_windOffsetZ, kWrapPeriod);
}

// ============================================================================
//  Generate noise textures (one-time at init)
// ============================================================================

bool VolumetricClouds::GenerateNoiseTextures()
{
    if (m_noiseGenerated) return true;

    auto& cm = ComputeManager::Get();
    HRESULT hr;

    // ---- Shape noise: 128^3 R8_UNORM ----
    {
        D3D11_TEXTURE3D_DESC desc = {};
        desc.Width     = 128;
        desc.Height    = 128;
        desc.Depth     = 128;
        desc.MipLevels = 1;
        desc.Format    = DXGI_FORMAT_R8_UNORM;
        desc.Usage     = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        hr = m_device->CreateTexture3D(&desc, nullptr, &m_shapeNoiseTex);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = 1;
        hr = m_device->CreateShaderResourceView(m_shapeNoiseTex, &srvDesc, &m_shapeNoiseSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.WSize = 128;
        hr = m_device->CreateUnorderedAccessView(m_shapeNoiseTex, &uavDesc, &m_shapeNoiseUAV);
        if (FAILED(hr)) return false;
    }

    // ---- Detail noise: 32^3 R8_UNORM ----
    {
        D3D11_TEXTURE3D_DESC desc = {};
        desc.Width     = 32;
        desc.Height    = 32;
        desc.Depth     = 32;
        desc.MipLevels = 1;
        desc.Format    = DXGI_FORMAT_R8_UNORM;
        desc.Usage     = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        hr = m_device->CreateTexture3D(&desc, nullptr, &m_detailNoiseTex);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = 1;
        hr = m_device->CreateShaderResourceView(m_detailNoiseTex, &srvDesc, &m_detailNoiseSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.WSize = 32;
        hr = m_device->CreateUnorderedAccessView(m_detailNoiseTex, &uavDesc, &m_detailNoiseUAV);
        if (FAILED(hr)) return false;
    }

    // ---- Dispatch noise generation ----
    cm.SaveCSState();

    // Shape noise: 128/8 = 16 groups in X and Y, Z is iterated in-shader
    {
        ID3D11UnorderedAccessView* uavs[] = { m_shapeNoiseUAV };
        cm.CSSetUAVs(0, 1, uavs);
        cm.Dispatch(m_shapeNoiseCS, 128 / 8, 128 / 8, 1);
        cm.CSClearUAVs(0, 1);
    }

    // Detail noise: 32/8 = 4 groups in X and Y, Z is iterated in-shader
    {
        ID3D11UnorderedAccessView* uavs[] = { m_detailNoiseUAV };
        cm.CSSetUAVs(0, 1, uavs);
        cm.Dispatch(m_detailNoiseCS, 32 / 8, 32 / 8, 1);
        cm.CSClearUAVs(0, 1);
    }

    cm.RestoreCSState();

    m_noiseGenerated = true;
    SKSE::log::info("VolumetricClouds: noise textures generated (shape 128^3, detail 32^3)");
    return true;
}

// ============================================================================
//  Create quarter-res cloud textures + history for temporal reprojection
// ============================================================================

bool VolumetricClouds::CreateCloudTextures(uint32_t screenW, uint32_t screenH)
{
    m_quarterW = screenW / 4;
    m_quarterH = screenH / 4;
    if (m_quarterW == 0) m_quarterW = 1;
    if (m_quarterH == 0) m_quarterH = 1;

    auto& cm = ComputeManager::Get();
    HRESULT hr;

    // Current frame cloud output
    {
        auto res = cm.CreateTexture2D(m_quarterW, m_quarterH,
                                       DXGI_FORMAT_R16G16B16A16_FLOAT,
                                       true, true, 1, false, "CloudOutput");
        if (!res.Valid()) return false;
        m_cloudTex = res.texture;
        m_cloudSRV = res.srv;
        m_cloudUAV = res.uav;
    }

    // Previous frame cloud history (for temporal reprojection, SRV only)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width            = m_quarterW;
        desc.Height           = m_quarterH;
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_DEFAULT;
        desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
        hr = m_device->CreateTexture2D(&desc, nullptr, &m_cloudHistTex);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        hr = m_device->CreateShaderResourceView(m_cloudHistTex, &srvDesc, &m_cloudHistSRV);
        if (FAILED(hr)) return false;
    }

    return true;
}

// ============================================================================
//  Per-frame execution (called from RenderPipeline as PreENB pass)
// ============================================================================

void VolumetricClouds::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    // Lazy noise generation: only when explicitly enabled
    if (!m_noiseGenerated) {
        if (!GenerateNoiseTextures()) {
            SKSE::log::error("VolumetricClouds: noise generation failed — disabling");
            m_enabled = false;
            return;
        }
    }

    auto& cm  = ComputeManager::Get();
    auto& rpm = RenderPassManager::Get();

    m_frameIndex++;

    // Acquire depth SRV — HiZ is now built at PostGeometry:1 (before this pass),
    // so it's fresh during mid-frame dispatch. Use it when available.
    ID3D11ShaderResourceView* depthSRV = D3D11Hook::GetGameDepthSRV();
    {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV()) {
            depthSRV = hiz.GetSRV();
        }
    }

    // ---- Pass 2: Cloud raymarch (quarter-res compute) ----
    {
        // Prepare CB data
        auto& scene = SceneMatrices::Get();
        CloudCBData cb = {};
        cb.sunDirection[0] = scene.SunDirection()[0];
        cb.sunDirection[1] = scene.SunDirection()[1];
        cb.sunDirection[2] = scene.SunDirection()[2];
        cb.time            = static_cast<float>(m_frameIndex) * 0.016f;
        cb.sunColor[0]     = scene.SunColor()[0];
        cb.sunColor[1]     = scene.SunColor()[1];
        cb.sunColor[2]     = scene.SunColor()[2];
        cb.cloudBase       = m_cloudBase;
        cb.cameraPos[0]    = scene.CameraPosX();
        cb.cameraPos[1]    = scene.CameraPosY();
        cb.cameraPos[2]    = scene.CameraPosZ();
        cb.cloudTop        = m_cloudTop;
        cb.windOffset[0]   = m_windOffsetX;
        cb.windOffset[1]   = m_windOffsetY;
        cb.windOffset[2]   = m_windOffsetZ;
        cb.coverage        = m_coverage;
        cb.screenSize[0]   = static_cast<float>(m_quarterW);
        cb.screenSize[1]   = static_cast<float>(m_quarterH);
        cb.density         = m_density;
        cb.frameIndex      = static_cast<float>(m_frameIndex);

        std::memcpy(cb.prevViewProj, scene.PrevViewProjMatrix(), 16 * sizeof(float));
        std::memcpy(cb.invViewProj,  scene.InvViewProjMatrix(),  16 * sizeof(float));

        cb.fogDensity    = m_fogDensity;
        cb.fogHeight     = m_fogHeight;
        cb.fogFalloff    = m_fogFalloff;
        cb.fogAnisotropy = m_fogAnisotropy;
        cb.fogEnabled    = m_fogEnabled ? 1u : 0u;
        cb.fogPad[0] = cb.fogPad[1] = cb.fogPad[2] = 0.0f;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_cloudCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_cloudCB, 0);
        }

        cm.SaveCSState();

        // Bind resources
        ID3D11ShaderResourceView* srvs[] = {
            m_shapeNoiseSRV,   // t0
            m_detailNoiseSRV,  // t1
            m_cloudHistSRV,    // t2 (previous frame)
            depthSRV,          // t3 (depth — acquired above)
        };
        cm.CSSetSRVs(0, 4, srvs);

        ID3D11SamplerState* samplers[] = { m_trilinearSampler };
        cm.CSSetSamplers(0, 1, samplers);

        cm.CSSetCBs(0, 1, &m_cloudCB);

        ID3D11UnorderedAccessView* uavs[] = { m_cloudUAV };
        cm.CSSetUAVs(0, 1, uavs);

        // Dispatch at quarter resolution
        UINT groupsX = (m_quarterW + 7) / 8;
        UINT groupsY = (m_quarterH + 7) / 8;
        cm.Dispatch(m_raymarchCS, groupsX, groupsY, 1);

        cm.CSClearSRVs(0, 4);
        cm.CSClearUAVs(0, 1);
        cm.RestoreCSState();

        // Copy current cloud output to history for next frame's temporal reprojection
        ctx.context->CopyResource(m_cloudHistTex, m_cloudTex);
    }

    // ---- Pass 3: Cloud composite (fullscreen PS) ----
    {
        // Acquire scene texture: mid-frame uses game's active RTV, Present uses swapchain
        ID3D11Texture2D* sceneTex = nullptr;
        bool ownSceneTex = false;
        ID3D11RenderTargetView* sceneRTV = nullptr;
        bool ownRTV = false;

        if (ctx.gameSceneRTV) {
            // Mid-frame path: extract texture from game's active RTV
            ID3D11Resource* res = nullptr;
            ctx.gameSceneRTV->GetResource(&res);
            if (res) {
                res->QueryInterface(__uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&sceneTex));
                res->Release();
            }
            ownSceneTex = true;
            sceneRTV = ctx.gameSceneRTV;
            sceneRTV->AddRef();
            ownRTV = true;
        } else {
            // Present-time path: use swapchain backbuffer
            auto* sc = ctx.swapChain;
            if (!sc) sc = D3D11Hook::GetSwapChain();
            if (sc) {
                sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                              reinterpret_cast<void**>(&sceneTex));
                ownSceneTex = true;
            }
        }

        if (!sceneTex) return;

        D3D11_TEXTURE2D_DESC texDesc;
        sceneTex->GetDesc(&texDesc);

        // Guard: only composite onto full-color scene RTs (same as SceneCompositor)
        {
            bool validFmt = (texDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                             texDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                             texDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                             texDesc.Format == DXGI_FORMAT_R11G11B10_FLOAT ||
                             texDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM);
            if (!validFmt) {
                if (ownSceneTex) sceneTex->Release();
                if (ownRTV) sceneRTV->Release();
                return;
            }
        }

        // Create temporary scene copy (read as SRV while writing to RTV)
        ID3D11Texture2D* sceneCopy = nullptr;
        ID3D11ShaderResourceView* sceneCopySRV = nullptr;
        {
            D3D11_TEXTURE2D_DESC copyDesc = texDesc;
            copyDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
            copyDesc.Usage          = D3D11_USAGE_DEFAULT;
            copyDesc.CPUAccessFlags = 0;
            copyDesc.MiscFlags      = 0;
            if (FAILED(m_device->CreateTexture2D(&copyDesc, nullptr, &sceneCopy))) {
                if (ownSceneTex) sceneTex->Release();
                if (ownRTV) sceneRTV->Release();
                return;
            }
            ctx.context->CopyResource(sceneCopy, sceneTex);

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = texDesc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            if (FAILED(m_device->CreateShaderResourceView(sceneCopy, &srvDesc, &sceneCopySRV))) {
                sceneCopy->Release();
                if (ownSceneTex) sceneTex->Release();
                if (ownRTV) sceneRTV->Release();
                return;
            }
        }

        // Create scene RTV if we don't already have one from mid-frame
        if (!sceneRTV) {
            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = texDesc.Format;
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            m_device->CreateRenderTargetView(sceneTex, &rtvDesc, &sceneRTV);
            ownRTV = true;
        }

        if (ownSceneTex) sceneTex->Release();

        if (sceneRTV) {
            // Update composite CB
            CompositeCBData compCB = {};
            compCB.quarterSize[0]  = static_cast<float>(m_quarterW);
            compCB.quarterSize[1]  = static_cast<float>(m_quarterH);
            compCB.fullSize[0]     = static_cast<float>(ctx.screenW);
            compCB.fullSize[1]     = static_cast<float>(ctx.screenH);
            compCB.depthThreshold  = 0.001f;

            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(ctx.context->Map(m_compositeCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                std::memcpy(mapped.pData, &compCB, sizeof(compCB));
                ctx.context->Unmap(m_compositeCB, 0);
            }

            // SRVs for composite pass: cloud + depth + scene
            ID3D11ShaderResourceView* srvs[] = {
                m_cloudSRV,    // t0: quarter-res cloud
                depthSRV,      // t1: depth (acquired at top of ExecutePass)
                sceneCopySRV,  // t2: scene color
            };

            // Samplers: point + linear
            ID3D11SamplerState* pointSampler = nullptr;
            {
                D3D11_SAMPLER_DESC sd = {};
                sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
                sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
                m_device->CreateSamplerState(&sd, &pointSampler);
            }
            ID3D11SamplerState* samplers[] = { pointSampler, m_trilinearSampler };

            rpm.Execute({
                .passID       = m_compositePass,
                .rtv          = sceneRTV,
                .srvs         = srvs,
                .srvCount     = 3,
                .samplers     = samplers,
                .samplerCount = 2,
                .cbData       = &compCB,
                .cbSize       = sizeof(compCB),
            });

            if (pointSampler) pointSampler->Release();
        }

        if (ownRTV && sceneRTV) sceneRTV->Release();
        if (sceneCopySRV) sceneCopySRV->Release();
        if (sceneCopy) sceneCopy->Release();
    }
}

} // namespace SB
