//=============================================================================
//  MaterialPipeline.cpp — Full G-buffer extraction via DXBC shader patching
//
//  Skyrim's BSLightingShader bakes albedo * lighting into a single output.
//  This system patches each lighting PS at creation time to output:
//    o1 = raw diffuse (t0 sample)   → albedo G-buffer
//    o2 = raw normal (t1 sample)    → normal G-buffer
//    o3 = material properties       → material G-buffer (immediate literal)
//
//  DXBC patching summary:
//    1. OSGN: Add SV_Target1/2/3 elements (3 × 24 = 72 bytes)
//    2. SHEX: Add dcl_output o1/o2/o3
//    3. SHEX: Add mov o1, rA  (t0 sample dest)
//    4. SHEX: Add mov o2, rB  (t1 sample dest)
//    5. SHEX: Add mov o3, l(metallic, roughness, sss, matID)  (immediate)
//    6. Rebuild DXBC with adjusted chunk offsets
//
//  Material classification from DXBC resource declarations:
//    t12 → Skin, t7 → Hair, textureCube → EnvMap/Metal, 6+ tex → Terrain
//
//  Author: Zain Dana Harper
//=============================================================================

#include "MaterialPipeline.h"
#include "ShaderManager.h"
#include "ProxyLog.h"

#include <cstring>
#include <cmath>
#include <algorithm>

namespace SB::Proxy
{

// ═══════════════════════════════════════════════════════════════════════════
//  DXBC Constants
// ═══════════════════════════════════════════════════════════════════════════

namespace dxbc
{
    constexpr uint32_t kMagic = 0x43425844;  // 'DXBC'

    // Chunk FourCCs
    constexpr uint32_t kSHEX = 0x58454853;  // 'SHEX' (SM5.0)
    constexpr uint32_t kSHDR = 0x52444853;  // 'SHDR' (SM4.x)
    constexpr uint32_t kOSGN = 0x4E47534F;  // 'OSGN' (output signature)

    // SM5.0 Opcodes
    constexpr uint32_t OP_SAMPLE       = 0x45;
    constexpr uint32_t OP_SAMPLE_L     = 0x48;
    constexpr uint32_t OP_SAMPLE_D     = 0x49;
    constexpr uint32_t OP_SAMPLE_B     = 0x4A;
    constexpr uint32_t OP_SAMPLE_C     = 0x46;
    constexpr uint32_t OP_MOV          = 0x36;
    constexpr uint32_t OP_RET          = 0x3E;
    constexpr uint32_t OP_DCL_TEMPS    = 0x68;
    constexpr uint32_t OP_DCL_OUTPUT   = 0x65;
    constexpr uint32_t OP_DCL_RESOURCE = 0x58;

    // Operand types (bits [19:12])
    constexpr uint32_t OT_TEMP         = 0;
    constexpr uint32_t OT_OUTPUT       = 2;
    constexpr uint32_t OT_IMMEDIATE32  = 4;
    constexpr uint32_t OT_RESOURCE     = 7;

    // Resource dimensions (bits [15:11] of dcl_resource opcode token)
    constexpr uint32_t RES_DIM_TEXTURE2D   = 3;
    constexpr uint32_t RES_DIM_TEXTURECUBE = 6;

    inline uint32_t InsnOpcode(uint32_t token) { return token & 0x7FF; }
    inline uint32_t InsnLength(uint32_t token)
    {
        uint32_t len = (token >> 24) & 0x7F;
        return len ? len : 1;
    }

    // Resource dimension from dcl_resource opcode token
    inline uint32_t ResourceDim(uint32_t opcodeToken)
    {
        return (opcodeToken >> 11) & 0x1F;
    }

    inline bool InsnUsesResource(const uint32_t* tokens, uint32_t len, uint32_t reg)
    {
        for (uint32_t i = 1; i + 1 < len; ++i) {
            uint32_t opType = (tokens[i] >> 12) & 0xFF;
            uint32_t idxDim = (tokens[i] >> 20) & 3;
            if (opType == OT_RESOURCE && idxDim == 1 && tokens[i + 1] == reg)
                return true;
        }
        return false;
    }

    inline int InsnDestTempReg(const uint32_t* tokens, uint32_t len)
    {
        if (len < 3) return -1;
        uint32_t opType = (tokens[1] >> 12) & 0xFF;
        uint32_t idxDim = (tokens[1] >> 20) & 3;
        if (opType == OT_TEMP && idxDim == 1)
            return static_cast<int>(tokens[2]);
        return -1;
    }

    // Extract resource register from a dcl_resource instruction operand
    inline int DclResourceReg(const uint32_t* tokens, uint32_t len)
    {
        if (len < 3) return -1;
        // Operand token at [1], register at [2]
        uint32_t opType = (tokens[1] >> 12) & 0xFF;
        if (opType == OT_RESOURCE)
            return static_cast<int>(tokens[2]);
        return -1;
    }

    inline bool IsSampleOp(uint32_t opcode)
    {
        return opcode == OP_SAMPLE   || opcode == OP_SAMPLE_L ||
               opcode == OP_SAMPLE_D || opcode == OP_SAMPLE_B ||
               opcode == OP_SAMPLE_C;
    }

    // Float → uint32_t bit pattern (for embedding immediate literals in DXBC)
    inline uint32_t FloatBits(float f)
    {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        return u;
    }

    // Build output operand token: 4-component, mask mode, xyzw, OUTPUT, 1D immediate
    inline uint32_t OutputOperand()
    {
        return 2              // 4-component
            | (0 << 2)       // mask mode
            | (0xF << 4)     // xyzw mask
            | (OT_OUTPUT << 12)
            | (1 << 20)      // 1D index
            | (0 << 22);     // immediate32
    }

    // Build temp source operand token: 4-component, swizzle mode, identity, TEMP, 1D
    inline uint32_t TempSrcOperand()
    {
        return 2              // 4-component
            | (1 << 2)       // swizzle mode
            | (0 << 4)       // x → x
            | (1 << 6)       // y → y
            | (2 << 8)       // z → z
            | (3 << 10)      // w → w
            | (OT_TEMP << 12)
            | (1 << 20)      // 1D index
            | (0 << 22);     // immediate32
    }

    // Build immediate32 source operand token: 4-component, swizzle, IMMEDIATE32, 0D
    inline uint32_t Imm32SrcOperand()
    {
        return 2              // 4-component
            | (1 << 2)       // swizzle mode
            | (0 << 4)       // x
            | (1 << 6)       // y
            | (2 << 8)       // z
            | (3 << 10)      // w
            | (OT_IMMEDIATE32 << 12)
            | (0 << 20);     // 0D index (no register number follows)
    }

} // namespace dxbc


// ═══════════════════════════════════════════════════════════════════════════
//  Material defaults table
// ═══════════════════════════════════════════════════════════════════════════

static constexpr MaterialDefaults kDefaults[] = {
    // metallic  roughness  sss    id (type / 255.0)
    {  0.04f,    0.50f,     0.00f, 0.0f / 255.0f  },   // Default
    {  0.04f,    0.40f,     1.00f, 1.0f / 255.0f  },   // Skin
    {  0.04f,    0.30f,     0.30f, 2.0f / 255.0f  },   // Hair
    {  0.04f,    0.25f,     0.00f, 3.0f / 255.0f  },   // Eye
    {  0.90f,    0.15f,     0.00f, 4.0f / 255.0f  },   // EnvMap (metal/glossy)
    {  0.04f,    0.70f,     0.00f, 5.0f / 255.0f  },   // Terrain
    {  0.04f,    0.55f,     0.00f, 6.0f / 255.0f  },   // Parallax
    {  0.04f,    0.60f,     0.15f, 7.0f / 255.0f  },   // TreeCanopy (slight SSS)
    {  0.04f,    0.65f,     0.00f, 8.0f / 255.0f  },   // Snow
};

const MaterialDefaults& MaterialPipeline::GetDefaults(MaterialType type)
{
    auto idx = static_cast<uint8_t>(type);
    if (idx < std::size(kDefaults))
        return kDefaults[idx];
    return kDefaults[0];  // fallback to Default
}


// ═══════════════════════════════════════════════════════════════════════════
//  Lifecycle
// ═══════════════════════════════════════════════════════════════════════════

bool MaterialPipeline::Initialize(ID3D11Device* realDevice, IDXGISwapChain* swapChain)
{
    if (m_initialized) return true;
    if (!realDevice || !swapChain) return false;

    DXGI_SWAP_CHAIN_DESC scDesc{};
    if (FAILED(swapChain->GetDesc(&scDesc)))
        return false;

    CreateAllRTs(realDevice, scDesc.BufferDesc.Width, scDesc.BufferDesc.Height);
    if (!m_albedo.rtv) return false;

    m_initialized = true;
    Log("MaterialPipeline: initialized %ux%u (3-target G-buffer)", m_width, m_height);
    return true;
}

void MaterialPipeline::Shutdown()
{
    {
        std::lock_guard lock(m_mutex);
        for (auto& [orig, patched] : m_patchCache) {
            if (patched) patched->Release();
        }
        m_patchCache.clear();
    }

    DestroyAllRTs();
    m_initialized = false;
}

void MaterialPipeline::OnResize(ID3D11Device* realDevice, uint32_t width, uint32_t height)
{
    if (width == m_width && height == m_height) return;

    DestroyAllRTs();
    CreateAllRTs(realDevice, width, height);
    Log("MaterialPipeline: resized to %ux%u", m_width, m_height);
}


// ═══════════════════════════════════════════════════════════════════════════
//  G-buffer render target management
// ═══════════════════════════════════════════════════════════════════════════

bool MaterialPipeline::CreateRT(ID3D11Device* dev, GBufferRT& rt, DXGI_FORMAT fmt,
                                uint32_t w, uint32_t h, const char* debugName)
{
    if (!dev || w == 0 || h == 0) return false;

    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width      = w;
    texDesc.Height     = h;
    texDesc.MipLevels  = 1;
    texDesc.ArraySize  = 1;
    texDesc.Format     = fmt;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage      = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags  = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = dev->CreateTexture2D(&texDesc, nullptr, &rt.tex);
    if (FAILED(hr)) {
        Log("MaterialPipeline: CreateTexture2D failed for %s (hr=0x%08X)", debugName, hr);
        return false;
    }

    hr = dev->CreateRenderTargetView(rt.tex, nullptr, &rt.rtv);
    if (FAILED(hr)) {
        rt.tex->Release(); rt.tex = nullptr;
        return false;
    }

    hr = dev->CreateShaderResourceView(rt.tex, nullptr, &rt.srv);
    if (FAILED(hr)) {
        rt.rtv->Release(); rt.rtv = nullptr;
        rt.tex->Release(); rt.tex = nullptr;
        return false;
    }

    Log("MaterialPipeline: created %s (%ux%u fmt=%d)", debugName, w, h, (int)fmt);
    return true;
}

void MaterialPipeline::DestroyRT(GBufferRT& rt)
{
    if (rt.srv) { rt.srv->Release(); rt.srv = nullptr; }
    if (rt.rtv) { rt.rtv->Release(); rt.rtv = nullptr; }
    if (rt.tex) { rt.tex->Release(); rt.tex = nullptr; }
}

void MaterialPipeline::CreateAllRTs(ID3D11Device* dev, uint32_t w, uint32_t h)
{
    CreateRT(dev, m_albedo,   DXGI_FORMAT_R8G8B8A8_UNORM, w, h, "GBuffer_Albedo");
    CreateRT(dev, m_normals,  DXGI_FORMAT_R8G8B8A8_UNORM, w, h, "GBuffer_Normals");
    CreateRT(dev, m_material, DXGI_FORMAT_R8G8B8A8_UNORM, w, h, "GBuffer_Material");
    m_width  = w;
    m_height = h;
}

void MaterialPipeline::DestroyAllRTs()
{
    DestroyRT(m_albedo);
    DestroyRT(m_normals);
    DestroyRT(m_material);
    m_width = m_height = 0;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame hooks
// ═══════════════════════════════════════════════════════════════════════════

void MaterialPipeline::OnPresent(ID3D11DeviceContext* ctx)
{
    if (!m_initialized || !m_enabled || !ctx) return;

    const float black[4] = { 0.f, 0.f, 0.f, 0.f };
    if (m_albedo.rtv)   ctx->ClearRenderTargetView(m_albedo.rtv,   black);
    if (m_normals.rtv)  ctx->ClearRenderTargetView(m_normals.rtv,  black);
    if (m_material.rtv) ctx->ClearRenderTargetView(m_material.rtv, black);
}

bool MaterialPipeline::InjectGBufferRTs(UINT numViews,
                                        ID3D11RenderTargetView* const* ppRTViews,
                                        ID3D11RenderTargetView** rtvOut,
                                        UINT& outNumViews) const
{
    if (!m_initialized || !m_enabled) return false;
    if (!m_albedo.rtv || !m_normals.rtv || !m_material.rtv) return false;

    // Only inject when the game binds exactly 1 RT (the main lit color buffer)
    // This is the standard forward geometry pattern in Skyrim.
    if (numViews != 1 || !ppRTViews || !ppRTViews[0]) return false;

    // Need 3 extra slots (1 + 3 = 4, well within the 8-RT D3D11 limit)
    if (numViews + 3 > D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) return false;

    // Copy existing RTVs
    for (UINT i = 0; i < numViews; ++i)
        rtvOut[i] = ppRTViews[i];

    // Add G-buffer targets at slots 1, 2, 3
    rtvOut[numViews]     = m_albedo.rtv;
    rtvOut[numViews + 1] = m_normals.rtv;
    rtvOut[numViews + 2] = m_material.rtv;
    outNumViews = numViews + 3;

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shader creation hook
// ═══════════════════════════════════════════════════════════════════════════

void MaterialPipeline::OnPixelShaderCreated(ID3D11Device* realDevice,
                                            const void* bytecode, SIZE_T length,
                                            ID3D11PixelShader* shader)
{
    if (!realDevice || !bytecode || length == 0 || !shader)
        return;

    auto* bytes = static_cast<const uint8_t*>(bytecode);

    // Quick heuristic: is this a BSLightingShader candidate?
    if (!IsLightingShaderCandidate(bytes, length)) {
        ++m_skippedCount;
        return;
    }
    ++m_candidateCount;

    // Classify material type from DXBC resource declarations
    MaterialType matType = ClassifyFromDXBC(bytes, length);
    if (matType != MaterialType::Unknown)
        ++m_classifiedCount;

    // Track per-type counts
    auto typeIdx = static_cast<uint8_t>(matType);
    if (typeIdx < std::size(m_typeCount))
        m_typeCount[typeIdx]++;

    // Attempt full G-buffer DXBC patching
    auto patched = PatchForGBufferOutput(bytes, length, matType);
    if (patched.empty()) {
        ++m_skippedCount;
        return;
    }

    // Create the patched pixel shader via the REAL device (bypass wrapper)
    ID3D11PixelShader* patchedPS = nullptr;
    HRESULT hr = realDevice->CreatePixelShader(
        patched.data(), patched.size(), nullptr, &patchedPS);

    if (FAILED(hr) || !patchedPS) {
        Log("MaterialPipeline: CreatePixelShader failed for patch (hr=0x%08X)", hr);
        ++m_skippedCount;
        return;
    }

    // Register as replacement in ShaderManager (applies at bind time).
    // AddRef so both ShaderManager and m_patchCache hold independent references.
    uint64_t hash = ShaderManager::HashBytecode(bytecode, length);
    patchedPS->AddRef();
    ShaderManager::Get().RegisterPSReplacement(hash, patchedPS);

    // Cache for cleanup (holds the original refcount from CreatePixelShader)
    {
        std::lock_guard lock(m_mutex);
        m_patchCache[shader] = patchedPS;
    }

    ++m_patchedCount;
    if (m_patchedCount <= 10 || (m_patchedCount % 50) == 0) {
        Log("MaterialPipeline: patched shader #%u (hash=%016llX matType=%u)",
            m_patchedCount, hash, (unsigned)matType);
    }
}

uint32_t MaterialPipeline::GetCountByType(MaterialType t) const
{
    auto idx = static_cast<uint8_t>(t);
    if (idx < std::size(m_typeCount))
        return m_typeCount[idx];
    return 0;
}


// ═══════════════════════════════════════════════════════════════════════════
//  BSLightingShader heuristic (same as AlbedoExtractor)
// ═══════════════════════════════════════════════════════════════════════════

bool MaterialPipeline::IsLightingShaderCandidate(const uint8_t* bytecode, SIZE_T length)
{
    if (length < 32) return false;

    uint32_t magic = *reinterpret_cast<const uint32_t*>(bytecode);
    if (magic != dxbc::kMagic) return false;

    uint32_t totalSize  = *reinterpret_cast<const uint32_t*>(bytecode + 24);
    uint32_t chunkCount = *reinterpret_cast<const uint32_t*>(bytecode + 28);
    if (totalSize > length || chunkCount > 16) return false;

    const uint32_t* chunkOffsets = reinterpret_cast<const uint32_t*>(bytecode + 32);

    for (uint32_t i = 0; i < chunkCount; ++i) {
        uint32_t off = chunkOffsets[i];
        if (off + 8 > length) continue;

        uint32_t fourCC = *reinterpret_cast<const uint32_t*>(bytecode + off);
        uint32_t size   = *reinterpret_cast<const uint32_t*>(bytecode + off + 4);

        if (fourCC != dxbc::kSHEX && fourCC != dxbc::kSHDR) continue;

        const uint32_t* tokens = reinterpret_cast<const uint32_t*>(bytecode + off + 8);
        uint32_t numDWORDs = size / 4;
        if (numDWORDs < 2) return false;

        bool hasT0Sample = false;
        bool hasT1Sample = false;

        uint32_t pos = 2;
        while (pos < numDWORDs) {
            uint32_t token  = tokens[pos];
            uint32_t opcode = dxbc::InsnOpcode(token);
            uint32_t len    = dxbc::InsnLength(token);
            if (len == 0 || pos + len > numDWORDs) break;

            if (dxbc::IsSampleOp(opcode)) {
                if (dxbc::InsnUsesResource(tokens + pos, len, 0)) hasT0Sample = true;
                if (dxbc::InsnUsesResource(tokens + pos, len, 1)) hasT1Sample = true;

                if (hasT0Sample && hasT1Sample)
                    return true;
            }

            pos += len;
        }
        break;
    }

    return false;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Material classification from DXBC
// ═══════════════════════════════════════════════════════════════════════════

MaterialType MaterialPipeline::ClassifyFromDXBC(const uint8_t* bytecode, SIZE_T length)
{
    // Scan dcl_resource instructions to determine which texture slots are used
    // and whether any are cubemap resources.
    //
    // BSLightingShader technique identification:
    //   t12 → Skin (subsurface scattering map)
    //   t7  → Hair (specular shift map)
    //   t3 as cube → Eye (env cubemap with parallax)
    //   any textureCube → EnvMap (metal/glossy)
    //   6+ texture2d → Terrain
    //   t3 declared → Parallax / Glow / MultiLayer
    //   only t0,t1 → Default

    if (length < 32) return MaterialType::Default;

    uint32_t magic = *reinterpret_cast<const uint32_t*>(bytecode);
    if (magic != dxbc::kMagic) return MaterialType::Default;

    uint32_t totalSize  = *reinterpret_cast<const uint32_t*>(bytecode + 24);
    uint32_t chunkCount = *reinterpret_cast<const uint32_t*>(bytecode + 28);
    if (totalSize > length || chunkCount > 16) return MaterialType::Default;

    const uint32_t* chunkOffsets = reinterpret_cast<const uint32_t*>(bytecode + 32);

    // Find SHEX/SHDR chunk
    for (uint32_t ci = 0; ci < chunkCount; ++ci) {
        uint32_t off = chunkOffsets[ci];
        if (off + 8 > length) continue;

        uint32_t fourCC = *reinterpret_cast<const uint32_t*>(bytecode + off);
        uint32_t size   = *reinterpret_cast<const uint32_t*>(bytecode + off + 4);

        if (fourCC != dxbc::kSHEX && fourCC != dxbc::kSHDR) continue;

        const uint32_t* tokens = reinterpret_cast<const uint32_t*>(bytecode + off + 8);
        uint32_t numDWORDs = size / 4;
        if (numDWORDs < 2) return MaterialType::Default;

        // Collect resource declarations
        bool hasTexSlot[16] = {};
        bool hasCubeSlot[16] = {};
        uint32_t tex2dCount = 0;
        uint32_t cubeCount  = 0;

        uint32_t pos = 2;
        while (pos < numDWORDs) {
            uint32_t token  = tokens[pos];
            uint32_t opcode = dxbc::InsnOpcode(token);
            uint32_t len    = dxbc::InsnLength(token);
            if (len == 0 || pos + len > numDWORDs) break;

            if (opcode == dxbc::OP_DCL_RESOURCE) {
                uint32_t dim = dxbc::ResourceDim(token);
                int reg = dxbc::DclResourceReg(tokens + pos, len);
                if (reg >= 0 && reg < 16) {
                    hasTexSlot[reg] = true;
                    if (dim == dxbc::RES_DIM_TEXTURECUBE) {
                        hasCubeSlot[reg] = true;
                        cubeCount++;
                    } else if (dim == dxbc::RES_DIM_TEXTURE2D) {
                        tex2dCount++;
                    }
                }
            }

            // Stop scanning after declarations end (first non-dcl instruction)
            if (opcode < 0x42) break;

            pos += len;
        }

        // Classification rules (ordered by specificity)
        if (hasTexSlot[12])
            return MaterialType::Skin;       // t12 = SSS map → Skin

        if (hasTexSlot[7])
            return MaterialType::Hair;       // t7 = specular shift → Hair

        if (hasCubeSlot[3])
            return MaterialType::Eye;        // t3 as cubemap → Eye

        if (cubeCount > 0)
            return MaterialType::EnvMap;     // any cubemap → metallic/glossy

        if (tex2dCount >= 6)
            return MaterialType::Terrain;    // 6+ textures → terrain blending

        if (hasTexSlot[3])
            return MaterialType::Parallax;   // t3 = height/glow/layer

        return MaterialType::Default;
    }

    return MaterialType::Default;
}


// ═══════════════════════════════════════════════════════════════════════════
//  DXBC Patching — 3-target G-buffer output
// ═══════════════════════════════════════════════════════════════════════════

std::vector<uint8_t> MaterialPipeline::PatchForGBufferOutput(
    const uint8_t* bytecode, SIZE_T length, MaterialType matType)
{
    // ── 1. Parse DXBC header ─────────────────────────────────────────────

    if (length < 32) return {};

    uint32_t magic = *reinterpret_cast<const uint32_t*>(bytecode);
    if (magic != dxbc::kMagic) return {};

    uint32_t totalSize  = *reinterpret_cast<const uint32_t*>(bytecode + 24);
    uint32_t chunkCount = *reinterpret_cast<const uint32_t*>(bytecode + 28);
    if (totalSize > length || chunkCount > 16) return {};

    const uint32_t* chunkOffsets = reinterpret_cast<const uint32_t*>(bytecode + 32);

    // ── 2. Find OSGN and SHEX chunks ─────────────────────────────────────

    uint32_t osgnChunkIdx = UINT32_MAX;
    uint32_t shexChunkIdx = UINT32_MAX;
    uint32_t osgnOff = 0, osgnSize = 0;
    uint32_t shexOff = 0, shexSize = 0;

    for (uint32_t i = 0; i < chunkCount; ++i) {
        uint32_t off = chunkOffsets[i];
        if (off + 8 > length) continue;

        uint32_t fourCC = *reinterpret_cast<const uint32_t*>(bytecode + off);
        uint32_t size   = *reinterpret_cast<const uint32_t*>(bytecode + off + 4);

        if (fourCC == dxbc::kOSGN) {
            osgnChunkIdx = i;
            osgnOff  = off + 8;
            osgnSize = size;
        } else if (fourCC == dxbc::kSHEX || fourCC == dxbc::kSHDR) {
            shexChunkIdx = i;
            shexOff  = off + 8;
            shexSize = size;
        }
    }

    if (osgnChunkIdx == UINT32_MAX || shexChunkIdx == UINT32_MAX)
        return {};
    if (osgnSize < 8 || shexSize < 8)
        return {};

    // ── 3. Validate OSGN ─────────────────────────────────────────────────

    const uint8_t* osgnData = bytecode + osgnOff;
    uint32_t osgnElements = *reinterpret_cast<const uint32_t*>(osgnData);

    if (osgnElements == 0 || osgnElements > 8) return {};

    uint32_t target0NameOff = 0;
    uint32_t target0Mask    = 0;
    bool     hasTarget0     = false;
    uint32_t maxSemIdx      = 0;   // track highest existing SV_Target index

    for (uint32_t e = 0; e < osgnElements; ++e) {
        uint32_t elemBase = 8 + e * 24;
        if (elemBase + 24 > osgnSize) return {};

        uint32_t nameOff = *reinterpret_cast<const uint32_t*>(osgnData + elemBase);
        uint32_t semIdx  = *reinterpret_cast<const uint32_t*>(osgnData + elemBase + 4);
        uint32_t sysVal  = *reinterpret_cast<const uint32_t*>(osgnData + elemBase + 8);
        uint32_t reg     = *reinterpret_cast<const uint32_t*>(osgnData + elemBase + 16);
        uint32_t mask    = *reinterpret_cast<const uint32_t*>(osgnData + elemBase + 20);

        if (sysVal == 0 && reg == 0 && semIdx == 0) {
            target0NameOff = nameOff;
            target0Mask    = mask;
            hasTarget0     = true;
        }
        if (sysVal == 0 && semIdx > maxSemIdx)
            maxSemIdx = semIdx;
    }

    if (!hasTarget0)    return {};   // not a standard PS
    if (maxSemIdx >= 1) return {};   // already has extra targets (MRT or already patched)

    // ── 4. Scan SHEX for t0 + t1 sample destinations ────────────────────

    const uint32_t* shexTokens = reinterpret_cast<const uint32_t*>(bytecode + shexOff);
    uint32_t shexDWORDs = shexSize / 4;
    if (shexDWORDs < 2) return {};

    uint32_t dclTempsPos   = UINT32_MAX;
    uint32_t dclTempsValue = 0;
    uint32_t firstInsnPos  = UINT32_MAX;
    uint32_t lastRetPos    = UINT32_MAX;
    int      t0SampleDest  = -1;
    int      t1SampleDest  = -1;

    uint32_t pos = 2;
    while (pos < shexDWORDs) {
        uint32_t token  = shexTokens[pos];
        uint32_t opcode = dxbc::InsnOpcode(token);
        uint32_t len    = dxbc::InsnLength(token);
        if (len == 0 || pos + len > shexDWORDs) break;

        if (opcode == dxbc::OP_DCL_TEMPS && pos + 1 < shexDWORDs) {
            dclTempsPos   = pos;
            dclTempsValue = shexTokens[pos + 1];
        }

        if (firstInsnPos == UINT32_MAX && opcode < 0x42)
            firstInsnPos = pos;

        if (opcode == dxbc::OP_RET)
            lastRetPos = pos;

        if (dxbc::IsSampleOp(opcode)) {
            // First t0 sample destination
            if (t0SampleDest < 0 && dxbc::InsnUsesResource(shexTokens + pos, len, 0))
                t0SampleDest = dxbc::InsnDestTempReg(shexTokens + pos, len);
            // First t1 sample destination
            if (t1SampleDest < 0 && dxbc::InsnUsesResource(shexTokens + pos, len, 1))
                t1SampleDest = dxbc::InsnDestTempReg(shexTokens + pos, len);
        }

        pos += len;
    }

    // t0 (albedo) is required; t1 (normals) is optional
    if (t0SampleDest < 0 || lastRetPos == UINT32_MAX || firstInsnPos == UINT32_MAX)
        return {};

    // Safety: ensure referenced registers are within declared temp count
    if (dclTempsPos != UINT32_MAX) {
        if (static_cast<uint32_t>(t0SampleDest) >= dclTempsValue)
            return {};
        if (t1SampleDest >= 0 && static_cast<uint32_t>(t1SampleDest) >= dclTempsValue)
            t1SampleDest = -1;  // invalid, skip normals output
    }

    // Determine how many outputs we're adding
    bool hasNormals = (t1SampleDest >= 0);
    uint32_t numNewOutputs = hasNormals ? 3 : 2;  // albedo + [normals] + material

    // ── 5. Build patched OSGN ────────────────────────────────────────────

    uint32_t newOsgnSize = osgnSize + numNewOutputs * 24;
    std::vector<uint8_t> newOsgn(newOsgnSize);
    std::memcpy(newOsgn.data(), osgnData, osgnSize);

    *reinterpret_cast<uint32_t*>(newOsgn.data()) = osgnElements + numNewOutputs;

    auto writeOsgnElement = [&](uint32_t idx, uint32_t semIdx, uint32_t reg) {
        uint32_t elemBase = 8 + (osgnElements + idx) * 24;
        auto* elem = newOsgn.data() + elemBase;
        *reinterpret_cast<uint32_t*>(elem + 0)  = target0NameOff;  // reuse "SV_Target"
        *reinterpret_cast<uint32_t*>(elem + 4)  = semIdx;
        *reinterpret_cast<uint32_t*>(elem + 8)  = 0;               // SV_Target systemValue
        *reinterpret_cast<uint32_t*>(elem + 12) = 3;               // float
        *reinterpret_cast<uint32_t*>(elem + 16) = reg;
        *reinterpret_cast<uint32_t*>(elem + 20) = target0Mask;
    };

    if (hasNormals) {
        writeOsgnElement(0, 1, 1);  // SV_Target1 → o1 (albedo)
        writeOsgnElement(1, 2, 2);  // SV_Target2 → o2 (normals)
        writeOsgnElement(2, 3, 3);  // SV_Target3 → o3 (material)
    } else {
        writeOsgnElement(0, 1, 1);  // SV_Target1 → o1 (albedo)
        writeOsgnElement(1, 2, 2);  // SV_Target2 → o2 (material, skip normals)
    }

    // ── 6. Build patched SHEX ────────────────────────────────────────────

    // Calculate added instruction sizes
    // dcl_output: 3 DWORDs each
    // mov from temp: 5 DWORDs each (opcode + 2 dst + 2 src)
    // mov from immediate: 8 DWORDs (opcode + 2 dst + 1 src operand + 4 floats)
    uint32_t addedDecls = numNewOutputs * 3;
    uint32_t addedInsns = 0;
    if (hasNormals) {
        addedInsns = 5 + 5 + 8;  // mov o1,rA + mov o2,rB + mov o3,l(...)
    } else {
        addedInsns = 5 + 8;      // mov o1,rA + mov o2,l(...)
    }

    uint32_t newShexDWORDs = shexDWORDs + addedDecls + addedInsns;
    std::vector<uint32_t> newShex;
    newShex.reserve(newShexDWORDs);

    // Copy header + declarations up to first instruction
    for (uint32_t i = 0; i < firstInsnPos; ++i)
        newShex.push_back(shexTokens[i]);

    // Insert dcl_output declarations
    auto emitDclOutput = [&](uint32_t reg) {
        newShex.push_back(dxbc::OP_DCL_OUTPUT | (3 << 24));
        newShex.push_back(dxbc::OutputOperand());
        newShex.push_back(reg);
    };

    if (hasNormals) {
        emitDclOutput(1);  // dcl_output o1.xyzw (albedo)
        emitDclOutput(2);  // dcl_output o2.xyzw (normals)
        emitDclOutput(3);  // dcl_output o3.xyzw (material)
    } else {
        emitDclOutput(1);  // dcl_output o1.xyzw (albedo)
        emitDclOutput(2);  // dcl_output o2.xyzw (material)
    }

    // Copy instructions up to last ret
    for (uint32_t i = firstInsnPos; i < lastRetPos; ++i)
        newShex.push_back(shexTokens[i]);

    // Helper: emit mov oN.xyzw, rM.xyzw
    auto emitMovFromTemp = [&](uint32_t outReg, uint32_t tempReg) {
        newShex.push_back(dxbc::OP_MOV | (5 << 24));
        newShex.push_back(dxbc::OutputOperand());
        newShex.push_back(outReg);
        newShex.push_back(dxbc::TempSrcOperand());
        newShex.push_back(tempReg);
    };

    // Helper: emit mov oN.xyzw, l(a, b, c, d)
    auto emitMovFromImm = [&](uint32_t outReg, float a, float b, float c, float d) {
        newShex.push_back(dxbc::OP_MOV | (8 << 24));
        newShex.push_back(dxbc::OutputOperand());
        newShex.push_back(outReg);
        newShex.push_back(dxbc::Imm32SrcOperand());
        newShex.push_back(dxbc::FloatBits(a));
        newShex.push_back(dxbc::FloatBits(b));
        newShex.push_back(dxbc::FloatBits(c));
        newShex.push_back(dxbc::FloatBits(d));
    };

    // Get material defaults for the immediate literal
    auto& mat = GetDefaults(matType);

    if (hasNormals) {
        emitMovFromTemp(1, static_cast<uint32_t>(t0SampleDest));  // o1 = albedo
        emitMovFromTemp(2, static_cast<uint32_t>(t1SampleDest));  // o2 = normals
        emitMovFromImm(3, mat.metallic, mat.roughness, mat.sss, mat.id);  // o3 = material
    } else {
        emitMovFromTemp(1, static_cast<uint32_t>(t0SampleDest));  // o1 = albedo
        emitMovFromImm(2, mat.metallic, mat.roughness, mat.sss, mat.id);  // o2 = material
    }

    // Copy ret and anything after
    for (uint32_t i = lastRetPos; i < shexDWORDs; ++i)
        newShex.push_back(shexTokens[i]);

    // Update SHEX length token (index 1)
    if (newShex.size() >= 2)
        newShex[1] = static_cast<uint32_t>(newShex.size());

    // ── 7. Rebuild DXBC ──────────────────────────────────────────────────

    uint32_t newShexBytes = static_cast<uint32_t>(newShex.size() * 4);
    int32_t osgnDelta = static_cast<int32_t>(newOsgnSize) - static_cast<int32_t>(osgnSize);
    int32_t shexDelta = static_cast<int32_t>(newShexBytes) - static_cast<int32_t>(shexSize);
    uint32_t newTotalSize = totalSize + osgnDelta + shexDelta;

    std::vector<uint8_t> result(newTotalSize);
    auto* out = result.data();

    // Copy header
    std::memcpy(out, bytecode, 32);
    *reinterpret_cast<uint32_t*>(out + 24) = newTotalSize;
    std::memset(out + 4, 0, 16);  // zero checksum

    // Copy chunk offset table
    uint32_t headerSize = 32 + chunkCount * 4;
    std::memcpy(out + 32, bytecode + 32, chunkCount * 4);

    // Rebuild chunks sequentially
    uint32_t writePos = headerSize;
    auto* outOffsets = reinterpret_cast<uint32_t*>(out + 32);

    for (uint32_t i = 0; i < chunkCount; ++i) {
        uint32_t srcOff = chunkOffsets[i];
        if (srcOff + 8 > length) continue;

        uint32_t chunkFourCC = *reinterpret_cast<const uint32_t*>(bytecode + srcOff);
        uint32_t chunkSize   = *reinterpret_cast<const uint32_t*>(bytecode + srcOff + 4);

        outOffsets[i] = writePos;

        if (i == osgnChunkIdx) {
            *reinterpret_cast<uint32_t*>(out + writePos)     = chunkFourCC;
            *reinterpret_cast<uint32_t*>(out + writePos + 4) = newOsgnSize;
            std::memcpy(out + writePos + 8, newOsgn.data(), newOsgnSize);
            writePos += 8 + newOsgnSize;
        } else if (i == shexChunkIdx) {
            *reinterpret_cast<uint32_t*>(out + writePos)     = chunkFourCC;
            *reinterpret_cast<uint32_t*>(out + writePos + 4) = newShexBytes;
            std::memcpy(out + writePos + 8, newShex.data(), newShexBytes);
            writePos += 8 + newShexBytes;
        } else {
            uint32_t fullChunkSize = 8 + chunkSize;
            if (srcOff + fullChunkSize <= length) {
                std::memcpy(out + writePos, bytecode + srcOff, fullChunkSize);
                writePos += fullChunkSize;
            }
        }
    }

    result.resize(writePos);
    *reinterpret_cast<uint32_t*>(result.data() + 24) = writePos;

    return result;
}

} // namespace SB::Proxy
