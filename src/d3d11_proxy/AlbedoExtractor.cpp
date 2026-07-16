//=============================================================================
//  AlbedoExtractor.cpp — G-buffer albedo extraction via DXBC shader patching
//
//  Skyrim's BSLightingShader bakes albedo × lighting into a single output.
//  This system patches each lighting PS at creation time to additionally
//  output raw diffuse color (the t0 texture sample) to SV_Target1, then
//  injects an extra render target during the geometry pass to capture it.
//
//  DXBC patching summary:
//    1. OSGN: Add SV_Target1 element (24 bytes, reuses "SV_Target" string)
//    2. SHEX: Add  dcl_output o1.xyzw
//    3. SHEX: Add  mov o1.xyzw, rN.xyzw   (before ret, where rN = first t0 sample dest)
//    4. SHEX: Rebuild DXBC with adjusted chunk offsets
//
//  Author: Zain Dana Harper
//=============================================================================

#include "AlbedoExtractor.h"
#include "ShaderManager.h"
#include "ProxyLog.h"

#include <cstring>
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

    // Operand types (bits [19:12])
    constexpr uint32_t OT_TEMP     = 0;
    constexpr uint32_t OT_OUTPUT   = 2;
    constexpr uint32_t OT_RESOURCE = 7;

    inline uint32_t InsnOpcode(uint32_t token) { return token & 0x7FF; }
    inline uint32_t InsnLength(uint32_t token)
    {
        uint32_t len = (token >> 24) & 0x7F;
        return len ? len : 1;
    }

    // Check if an instruction's operands reference a resource at register `reg`
    inline bool InsnUsesResource(const uint32_t* tokens, uint32_t len, uint32_t reg)
    {
        for (uint32_t i = 1; i + 1 < len; ++i) {
            uint32_t opType  = (tokens[i] >> 12) & 0xFF;
            uint32_t idxDim  = (tokens[i] >> 20) & 3;
            if (opType == OT_RESOURCE && idxDim == 1 && tokens[i + 1] == reg)
                return true;
        }
        return false;
    }

    // Get the destination temp register index from the first operand (-1 if not a temp)
    inline int InsnDestTempReg(const uint32_t* tokens, uint32_t len)
    {
        if (len < 3) return -1;
        uint32_t opType = (tokens[1] >> 12) & 0xFF;
        uint32_t idxDim = (tokens[1] >> 20) & 3;
        if (opType == OT_TEMP && idxDim == 1)
            return static_cast<int>(tokens[2]);
        return -1;
    }

    // Check if this is any kind of sample instruction
    inline bool IsSampleOp(uint32_t opcode)
    {
        return opcode == OP_SAMPLE   || opcode == OP_SAMPLE_L ||
               opcode == OP_SAMPLE_D || opcode == OP_SAMPLE_B ||
               opcode == OP_SAMPLE_C;
    }

} // namespace dxbc


// ═══════════════════════════════════════════════════════════════════════════
//  Lifecycle
// ═══════════════════════════════════════════════════════════════════════════

bool AlbedoExtractor::Initialize(ID3D11Device* realDevice, IDXGISwapChain* swapChain)
{
    if (m_initialized) return true;
    if (!realDevice || !swapChain) return false;

    // Get backbuffer dimensions
    DXGI_SWAP_CHAIN_DESC scDesc{};
    if (FAILED(swapChain->GetDesc(&scDesc)))
        return false;

    if (!CreateAlbedoRT(realDevice, scDesc.BufferDesc.Width, scDesc.BufferDesc.Height))
        return false;

    m_initialized = true;
    Log("AlbedoExtractor: initialized (%ux%u R8G8B8A8)", m_width, m_height);
    return true;
}

void AlbedoExtractor::Shutdown()
{
    // Release patched shaders
    {
        std::lock_guard lock(m_mutex);
        for (auto& [orig, patched] : m_patchCache) {
            if (patched) patched->Release();
        }
        m_patchCache.clear();
    }

    ReleaseAlbedoRT();
    m_initialized = false;
}

void AlbedoExtractor::OnResize(ID3D11Device* realDevice, uint32_t width, uint32_t height)
{
    if (width == m_width && height == m_height) return;

    ReleaseAlbedoRT();
    CreateAlbedoRT(realDevice, width, height);
    Log("AlbedoExtractor: resized to %ux%u", m_width, m_height);
}


// ═══════════════════════════════════════════════════════════════════════════
//  Render target management
// ═══════════════════════════════════════════════════════════════════════════

bool AlbedoExtractor::CreateAlbedoRT(ID3D11Device* dev, uint32_t w, uint32_t h)
{
    if (!dev || w == 0 || h == 0) return false;

    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width  = w;
    texDesc.Height = h;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage     = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = dev->CreateTexture2D(&texDesc, nullptr, &m_albedoTex);
    if (FAILED(hr)) return false;

    hr = dev->CreateRenderTargetView(m_albedoTex, nullptr, &m_albedoRTV);
    if (FAILED(hr)) {
        m_albedoTex->Release(); m_albedoTex = nullptr;
        return false;
    }

    hr = dev->CreateShaderResourceView(m_albedoTex, nullptr, &m_albedoSRV);
    if (FAILED(hr)) {
        m_albedoRTV->Release(); m_albedoRTV = nullptr;
        m_albedoTex->Release(); m_albedoTex = nullptr;
        return false;
    }

    m_width  = w;
    m_height = h;
    return true;
}

void AlbedoExtractor::ReleaseAlbedoRT()
{
    if (m_albedoSRV) { m_albedoSRV->Release(); m_albedoSRV = nullptr; }
    if (m_albedoRTV) { m_albedoRTV->Release(); m_albedoRTV = nullptr; }
    if (m_albedoTex) { m_albedoTex->Release(); m_albedoTex = nullptr; }
    m_width = m_height = 0;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame hooks
// ═══════════════════════════════════════════════════════════════════════════

void AlbedoExtractor::OnPresent(ID3D11DeviceContext* ctx)
{
    if (!m_initialized || !m_enabled || !m_albedoRTV || !ctx) return;

    const float clearColor[4] = { 0.f, 0.f, 0.f, 0.f };
    ctx->ClearRenderTargetView(m_albedoRTV, clearColor);
}

bool AlbedoExtractor::InjectAlbedoRT(UINT numViews,
                                     ID3D11RenderTargetView* const* ppRTViews,
                                     ID3D11RenderTargetView** rtvOut,
                                     UINT& outNumViews) const
{
    if (!m_initialized || !m_enabled || !m_albedoRTV) return false;

    // Only inject when there's exactly 1 RT (main color buffer) — the geometry pass
    // pattern in Skyrim. MRT setups (>1 RT) are usually post-process or ENB.
    if (numViews != 1 || !ppRTViews || !ppRTViews[0]) return false;

    // Don't exceed D3D11 MRT limit
    if (numViews + 1 > D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) return false;

    // Copy existing RTVs
    for (UINT i = 0; i < numViews; ++i)
        rtvOut[i] = ppRTViews[i];

    // Add our albedo RT at the next slot
    rtvOut[numViews] = m_albedoRTV;
    outNumViews = numViews + 1;

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shader creation hook
// ═══════════════════════════════════════════════════════════════════════════

void AlbedoExtractor::OnPixelShaderCreated(ID3D11Device* realDevice,
                                           const void* bytecode, SIZE_T length,
                                           ID3D11PixelShader* shader)
{
    if (!m_initialized || !realDevice || !bytecode || length == 0 || !shader)
        return;

    auto* bytes = static_cast<const uint8_t*>(bytecode);

    // Quick heuristic: is this a BSLightingShader candidate?
    if (!IsLightingShaderCandidate(bytes, length)) {
        ++m_skippedCount;
        return;
    }
    ++m_candidateCount;

    // Attempt DXBC patching
    auto patched = PatchForAlbedoOutput(bytes, length);
    if (patched.empty()) {
        ++m_skippedCount;
        return;
    }

    // Create the patched pixel shader via the REAL device (bypass wrapper)
    ID3D11PixelShader* patchedPS = nullptr;
    HRESULT hr = realDevice->CreatePixelShader(
        patched.data(), patched.size(), nullptr, &patchedPS);

    if (FAILED(hr) || !patchedPS) {
        Log("AlbedoExtractor: CreatePixelShader failed for patch (hr=0x%08X)", hr);
        ++m_skippedCount;
        return;
    }

    // Register as replacement in ShaderManager (applies at bind time)
    uint64_t hash = ShaderManager::HashBytecode(bytecode, length);
    ShaderManager::Get().RegisterPSReplacement(hash, patchedPS);

    // Cache for cleanup
    {
        std::lock_guard lock(m_mutex);
        m_patchCache[shader] = patchedPS;
    }

    ++m_patchedCount;
    if (m_patchedCount <= 10 || (m_patchedCount % 50) == 0) {
        Log("AlbedoExtractor: patched shader #%u (hash=%016llX)", m_patchedCount, hash);
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  BSLightingShader heuristic
// ═══════════════════════════════════════════════════════════════════════════

bool AlbedoExtractor::IsLightingShaderCandidate(const uint8_t* bytecode, SIZE_T length)
{
    // BSLightingShader always samples from t0 (diffuse) and t1 (normal).
    // Post-process shaders typically use t0 alone or don't sample at all.
    // This heuristic has high precision for BSLightingShader with minimal
    // false positives from other shader types.

    if (length < 32) return false;

    uint32_t magic = *reinterpret_cast<const uint32_t*>(bytecode);
    if (magic != dxbc::kMagic) return false;

    uint32_t totalSize  = *reinterpret_cast<const uint32_t*>(bytecode + 24);
    uint32_t chunkCount = *reinterpret_cast<const uint32_t*>(bytecode + 28);
    if (totalSize > length || chunkCount > 16) return false;

    const uint32_t* chunkOffsets = reinterpret_cast<const uint32_t*>(bytecode + 32);

    // Find SHEX/SHDR chunk
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

        uint32_t pos = 2;  // Skip version + length
        while (pos < numDWORDs) {
            uint32_t token = tokens[pos];
            uint32_t opcode = dxbc::InsnOpcode(token);
            uint32_t len    = dxbc::InsnLength(token);
            if (len == 0 || pos + len > numDWORDs) break;

            if (dxbc::IsSampleOp(opcode)) {
                if (dxbc::InsnUsesResource(tokens + pos, len, 0)) hasT0Sample = true;
                if (dxbc::InsnUsesResource(tokens + pos, len, 1)) hasT1Sample = true;

                if (hasT0Sample && hasT1Sample)
                    return true;  // Both diffuse + normal sampled → BSLightingShader
            }

            pos += len;
        }
        break;  // Only one SHEX/SHDR chunk
    }

    return false;
}


// ═══════════════════════════════════════════════════════════════════════════
//  DXBC Patching — Add albedo output to SV_Target1
// ═══════════════════════════════════════════════════════════════════════════

std::vector<uint8_t> AlbedoExtractor::PatchForAlbedoOutput(
    const uint8_t* bytecode, SIZE_T length)
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

    // ── 3. Validate OSGN: must have SV_Target0, must NOT already have SV_Target1

    const uint8_t* osgnData = bytecode + osgnOff;
    uint32_t osgnElements = *reinterpret_cast<const uint32_t*>(osgnData);
    // uint32_t osgnPad = *reinterpret_cast<const uint32_t*>(osgnData + 4); // always 8

    if (osgnElements == 0 || osgnElements > 8) return {};

    // Check existing outputs — find SV_Target0's name offset and mask
    uint32_t target0NameOff = 0;
    uint32_t target0Mask    = 0;
    bool     hasTarget0     = false;
    bool     hasTarget1     = false;

    for (uint32_t e = 0; e < osgnElements; ++e) {
        uint32_t elemBase = 8 + e * 24;
        if (elemBase + 24 > osgnSize) return {};

        uint32_t nameOff   = *reinterpret_cast<const uint32_t*>(osgnData + elemBase);
        uint32_t semIdx    = *reinterpret_cast<const uint32_t*>(osgnData + elemBase + 4);
        uint32_t sysVal    = *reinterpret_cast<const uint32_t*>(osgnData + elemBase + 8);
        uint32_t reg       = *reinterpret_cast<const uint32_t*>(osgnData + elemBase + 16);
        uint32_t mask      = *reinterpret_cast<const uint32_t*>(osgnData + elemBase + 20);

        // SV_Target has systemValue = 0 and semantic name starts with "SV_T"
        if (sysVal == 0 && reg == 0 && semIdx == 0) {
            target0NameOff = nameOff;
            target0Mask    = mask;
            hasTarget0     = true;
        }
        if (sysVal == 0 && semIdx == 1) {
            hasTarget1 = true;
        }
    }

    if (!hasTarget0)  return {};   // Not a standard PS with SV_Target0
    if (hasTarget1)   return {};   // Already has SV_Target1 (already patched or MRT shader)

    // ── 4. Scan SHEX for first t0 sample destination register ────────────

    const uint32_t* shexTokens = reinterpret_cast<const uint32_t*>(bytecode + shexOff);
    uint32_t shexDWORDs = shexSize / 4;
    if (shexDWORDs < 2) return {};

    // Find: dcl_temps, first non-decl instruction, last ret, first t0 sample dest
    uint32_t dclTempsPos   = UINT32_MAX;
    uint32_t dclTempsValue = 0;
    uint32_t firstInsnPos  = UINT32_MAX;
    uint32_t lastRetPos    = UINT32_MAX;
    int      t0SampleDest  = -1;

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

        // First non-declaration instruction (opcodes < ~0x42 are ALU/flow)
        if (firstInsnPos == UINT32_MAX && opcode < 0x42)
            firstInsnPos = pos;

        if (opcode == dxbc::OP_RET)
            lastRetPos = pos;

        // First sample from t0
        if (t0SampleDest < 0 && dxbc::IsSampleOp(opcode)) {
            if (dxbc::InsnUsesResource(shexTokens + pos, len, 0))
                t0SampleDest = dxbc::InsnDestTempReg(shexTokens + pos, len);
        }

        pos += len;
    }

    if (t0SampleDest < 0 || lastRetPos == UINT32_MAX || firstInsnPos == UINT32_MAX)
        return {};

    // Safety: ensure referenced register is within declared temp count
    if (dclTempsPos != UINT32_MAX && static_cast<uint32_t>(t0SampleDest) >= dclTempsValue)
        return {};

    // ── 5. Build patched OSGN ─────────────────────────────────────────

    // Add one element (24 bytes) for SV_Target1
    uint32_t newOsgnSize = osgnSize + 24;
    std::vector<uint8_t> newOsgn(newOsgnSize);
    std::memcpy(newOsgn.data(), osgnData, osgnSize);

    // Increment element count
    *reinterpret_cast<uint32_t*>(newOsgn.data()) = osgnElements + 1;

    // Write new element at end of element array
    uint32_t newElemBase = 8 + osgnElements * 24;
    auto* newElem = newOsgn.data() + newElemBase;

    *reinterpret_cast<uint32_t*>(newElem + 0)  = target0NameOff;  // reuse "SV_Target" string
    *reinterpret_cast<uint32_t*>(newElem + 4)  = 1;               // semantic index = 1
    *reinterpret_cast<uint32_t*>(newElem + 8)  = 0;               // system value = 0 (SV_Target)
    *reinterpret_cast<uint32_t*>(newElem + 12) = 3;               // component type = float
    *reinterpret_cast<uint32_t*>(newElem + 16) = 1;               // output register = o1
    *reinterpret_cast<uint32_t*>(newElem + 20) = target0Mask;     // same mask as SV_Target0

    // ── 6. Build patched SHEX ─────────────────────────────────────────

    // Add: dcl_output o1.xyzw (3 DWORDs) + mov o1, rN (5 DWORDs) = 8 DWORDs total
    constexpr uint32_t kAddedDecls = 3;  // dcl_output
    constexpr uint32_t kAddedInsns = 5;  // mov

    uint32_t newShexDWORDs = shexDWORDs + kAddedDecls + kAddedInsns;
    std::vector<uint32_t> newShex;
    newShex.reserve(newShexDWORDs);

    // Copy version + length + declarations up to first instruction
    for (uint32_t i = 0; i < firstInsnPos; ++i) {
        uint32_t val = shexTokens[i];
        // Patch dcl_temps if we need an extra temp (we don't — we reuse t0 sample dest)
        newShex.push_back(val);
    }

    // Insert: dcl_output o1.xyzw
    // Opcode: DCL_OUTPUT (0x65), length = 3
    // Operand: 4-component, mask mode, mask=0xF, type=OUTPUT(2), 1D index, immediate32
    newShex.push_back(dxbc::OP_DCL_OUTPUT | (3 << 24));
    newShex.push_back(
        2              // 4-component
        | (0 << 2)    // mask mode
        | (0xF << 4)  // xyzw mask
        | (dxbc::OT_OUTPUT << 12)
        | (1 << 20)   // 1D index
        | (0 << 22)   // immediate32
    );
    newShex.push_back(1);  // register = o1

    // Copy instructions up to last ret
    for (uint32_t i = firstInsnPos; i < lastRetPos; ++i) {
        newShex.push_back(shexTokens[i]);
    }

    // Insert: mov o1.xyzw, rN.xyzw
    // Opcode: MOV (0x36), length = 5
    newShex.push_back(dxbc::OP_MOV | (5 << 24));
    // dst operand: output o1.xyzw (mask mode)
    newShex.push_back(
        2              // 4-component
        | (0 << 2)    // mask mode
        | (0xF << 4)  // xyzw mask
        | (dxbc::OT_OUTPUT << 12)
        | (1 << 20)   // 1D index
        | (0 << 22)   // immediate32
    );
    newShex.push_back(1);  // o1
    // src operand: temp rN.xyzw (swizzle mode)
    newShex.push_back(
        2                        // 4-component
        | (1 << 2)              // swizzle mode
        | (0 << 4)              // x → x
        | (1 << 6)              // y → y
        | (2 << 8)              // z → z
        | (3 << 10)             // w → w
        | (dxbc::OT_TEMP << 12)
        | (1 << 20)             // 1D index
        | (0 << 22)             // immediate32
    );
    newShex.push_back(static_cast<uint32_t>(t0SampleDest));  // rN

    // Copy ret and anything after
    for (uint32_t i = lastRetPos; i < shexDWORDs; ++i) {
        newShex.push_back(shexTokens[i]);
    }

    // Update SHEX length token (index 1) = total DWORDs
    if (newShex.size() >= 2) {
        newShex[1] = static_cast<uint32_t>(newShex.size());
    }

    // ── 7. Rebuild DXBC ──────────────────────────────────────────────

    uint32_t newShexBytes = static_cast<uint32_t>(newShex.size() * 4);
    int32_t osgnDelta = static_cast<int32_t>(newOsgnSize) - static_cast<int32_t>(osgnSize);
    int32_t shexDelta = static_cast<int32_t>(newShexBytes) - static_cast<int32_t>(shexSize);
    uint32_t newTotalSize = totalSize + osgnDelta + shexDelta;

    std::vector<uint8_t> result(newTotalSize);
    auto* out = result.data();

    // Copy header (magic + checksum + version + totalSize + chunkCount)
    std::memcpy(out, bytecode, 32);
    *reinterpret_cast<uint32_t*>(out + 24) = newTotalSize;
    // Zero the checksum (release D3D runtime doesn't validate it)
    std::memset(out + 4, 0, 16);

    // Copy chunk offset table (will be updated below)
    uint32_t headerSize = 32 + chunkCount * 4;
    std::memcpy(out + 32, bytecode + 32, chunkCount * 4);

    // Rebuild chunks sequentially, writing each at the correct position
    uint32_t writePos = headerSize;
    auto* outOffsets = reinterpret_cast<uint32_t*>(out + 32);

    for (uint32_t i = 0; i < chunkCount; ++i) {
        uint32_t srcOff = chunkOffsets[i];
        if (srcOff + 8 > length) continue;

        uint32_t chunkFourCC = *reinterpret_cast<const uint32_t*>(bytecode + srcOff);
        uint32_t chunkSize   = *reinterpret_cast<const uint32_t*>(bytecode + srcOff + 4);

        outOffsets[i] = writePos;

        if (i == osgnChunkIdx) {
            // Write patched OSGN
            *reinterpret_cast<uint32_t*>(out + writePos)     = chunkFourCC;
            *reinterpret_cast<uint32_t*>(out + writePos + 4) = newOsgnSize;
            std::memcpy(out + writePos + 8, newOsgn.data(), newOsgnSize);
            writePos += 8 + newOsgnSize;
        } else if (i == shexChunkIdx) {
            // Write patched SHEX
            *reinterpret_cast<uint32_t*>(out + writePos)     = chunkFourCC;
            *reinterpret_cast<uint32_t*>(out + writePos + 4) = newShexBytes;
            std::memcpy(out + writePos + 8, newShex.data(), newShexBytes);
            writePos += 8 + newShexBytes;
        } else {
            // Copy chunk as-is
            uint32_t fullChunkSize = 8 + chunkSize;
            if (srcOff + fullChunkSize <= length) {
                std::memcpy(out + writePos, bytecode + srcOff, fullChunkSize);
                writePos += fullChunkSize;
            }
        }
    }

    // Finalize size
    result.resize(writePos);
    *reinterpret_cast<uint32_t*>(result.data() + 24) = writePos;

    return result;
}

} // namespace SB::Proxy
