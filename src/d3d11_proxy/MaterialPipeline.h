#pragma once
//=============================================================================
//  MaterialPipeline — Full G-buffer extraction via DXBC shader patching
//
//  Extends the AlbedoExtractor concept to a 3-target G-buffer:
//    RT1 (o1): Albedo.rgb + opacity.a      — R8G8B8A8_UNORM  (t0 sample)
//    RT2 (o2): Normal.rgb + specMask.a     — R8G8B8A8_UNORM  (t1 sample)
//    RT3 (o3): Metallic.r + Roughness.g    — R8G8B8A8_UNORM  (immediate lit)
//              + SSS.b + MaterialID.a
//
//  Per-shader DXBC patching at CreatePixelShader time:
//    - Heuristic: shaders sampling both t0 (diffuse) + t1 (normal) = BSLighting
//    - Patches OSGN to add SV_Target1..3 declarations
//    - Patches SHEX to add dcl_output + mov/imm instructions
//    - o1 = raw diffuse sample, o2 = raw normal sample
//    - o3 = compile-time material properties as immediate literal
//
//  Material classification from DXBC analysis:
//    - Skin (t12 SSS map), Hair (t7 specular shift), Eye (t3 cubemap)
//    - Metal/EnvMap (texturecube decl), Terrain (6+ textures), Default
//
//  Integration:
//    WrappedDevice::CreatePixelShader  → OnPixelShaderCreated
//    WrappedContext::OMSetRenderTargets → InjectGBufferRTs
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

// Material types classified from DXBC bytecode analysis
enum class MaterialType : uint8_t
{
    Default     = 0,   // Standard opaque geometry
    Skin        = 1,   // FaceSkin / SkinTint (subsurface scattering)
    Hair        = 2,   // HairTint (anisotropic specular)
    Eye         = 3,   // Eye shader (parallax + env map)
    EnvMap      = 4,   // Environment-mapped (metallic/glossy)
    Terrain     = 5,   // Landscape blending (multi-texture)
    Parallax    = 6,   // Parallax / MultiLayer parallax
    TreeCanopy  = 7,   // Tree / vegetation canopy
    Snow        = 8,   // Snow accumulation shader
    Unknown     = 255
};

// Default material properties per type (metallic, roughness, SSS, normalized ID)
struct MaterialDefaults
{
    float metallic;
    float roughness;
    float sss;
    float id;   // materialType / 255.0
};

class MaterialPipeline
{
public:
    static MaterialPipeline& Get()
    {
        static MaterialPipeline inst;
        return inst;
    }

    // ── Lifecycle ─────────────────────────────────────────────────────

    bool Initialize(ID3D11Device* realDevice, IDXGISwapChain* swapChain);
    void Shutdown();
    void OnResize(ID3D11Device* realDevice, uint32_t width, uint32_t height);

    // ── Per-shader hook (from WrappedDevice::CreatePixelShader) ───────

    void OnPixelShaderCreated(ID3D11Device* realDevice,
                              const void* bytecode, SIZE_T length,
                              ID3D11PixelShader* shader);

    // ── Per-frame hooks ───────────────────────────────────────────────

    // Clear all G-buffer RTs at frame start (from Present).
    void OnPresent(ID3D11DeviceContext* ctx);

    // Inject G-buffer RTs into MRT during geometry pass.
    // `rtvOut` must have room for numViews + 3 entries.
    // Returns true if injection occurred (caller uses modified array).
    bool InjectGBufferRTs(UINT numViews,
                          ID3D11RenderTargetView* const* ppRTViews,
                          ID3D11RenderTargetView** rtvOut,
                          UINT& outNumViews) const;

    // ── State ─────────────────────────────────────────────────────────

    bool IsInitialized() const { return m_initialized; }
    bool IsEnabled()     const { return m_enabled; }
    void SetEnabled(bool e)    { m_enabled = e; }

    // ── G-buffer SRV accessors ────────────────────────────────────────

    ID3D11ShaderResourceView* GetAlbedoSRV()   const { return m_albedo.srv; }
    ID3D11ShaderResourceView* GetNormalSRV()   const { return m_normals.srv; }
    ID3D11ShaderResourceView* GetMaterialSRV() const { return m_material.srv; }

    // ── Stats ─────────────────────────────────────────────────────────

    uint32_t GetPatchedCount()    const { return m_patchedCount; }
    uint32_t GetCandidateCount()  const { return m_candidateCount; }
    uint32_t GetSkippedCount()    const { return m_skippedCount; }
    uint32_t GetClassifiedCount() const { return m_classifiedCount; }

    // Material type breakdown
    uint32_t GetCountByType(MaterialType t) const;

    // ── Static analysis functions (pure, no state) ────────────────────

    // Does this DXBC sample both t0 and t1? (BSLightingShader heuristic)
    static bool IsLightingShaderCandidate(const uint8_t* bytecode, SIZE_T length);

    // Classify material type from DXBC resource declarations
    static MaterialType ClassifyFromDXBC(const uint8_t* bytecode, SIZE_T length);

    // Get default material properties for a type
    static const MaterialDefaults& GetDefaults(MaterialType type);

    // Patch DXBC to output 3-target G-buffer: albedo (o1), normals (o2), material (o3).
    // Returns empty vector on failure.
    static std::vector<uint8_t> PatchForGBufferOutput(
        const uint8_t* bytecode, SIZE_T length, MaterialType matType);

private:
    MaterialPipeline() = default;

    // ── G-buffer render target ────────────────────────────────────────

    struct GBufferRT
    {
        ID3D11Texture2D*          tex = nullptr;
        ID3D11RenderTargetView*   rtv = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
    };

    bool CreateRT(ID3D11Device* dev, GBufferRT& rt, DXGI_FORMAT fmt,
                  uint32_t w, uint32_t h, const char* debugName);
    void DestroyRT(GBufferRT& rt);
    void CreateAllRTs(ID3D11Device* dev, uint32_t w, uint32_t h);
    void DestroyAllRTs();

    GBufferRT m_albedo;    // RT1: diffuse color
    GBufferRT m_normals;   // RT2: normal map
    GBufferRT m_material;  // RT3: material properties

    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    bool     m_initialized = false;
    bool     m_enabled     = false;   // off by default; user/API enables

    // Patch tracking
    std::unordered_map<ID3D11PixelShader*, ID3D11PixelShader*> m_patchCache;
    mutable std::mutex m_mutex;

    // Per-type counters
    uint32_t m_typeCount[16] = {};

    // Stats
    uint32_t m_patchedCount    = 0;
    uint32_t m_candidateCount  = 0;
    uint32_t m_skippedCount    = 0;
    uint32_t m_classifiedCount = 0;
};

} // namespace SB::Proxy
