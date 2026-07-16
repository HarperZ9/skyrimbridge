#pragma once
//=============================================================================
//  ShaderManager — Vertex/Pixel shader hash tracking + replacement
//
//  Integrates with WrappedDevice (CreateVertexShader/CreatePixelShader) and
//  WrappedContext (VSSetShader/PSSetShader) to:
//    1. Hash all compiled shader bytecode at creation time (FNV-1a 64-bit)
//    2. Track hash → shader pointer mappings
//    3. Allow shader replacement: register a replacement shader for a hash,
//       and the wrapper will silently substitute it at bind time
//
//  This enables vertex shader injection for engine improvements like
//  Light Limit Fix, grass lighting, etc. without BSShader vtable hooks.
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace SB::Proxy
{

struct ShaderEntry
{
    uint64_t             hash;
    SIZE_T               bytecodeLength;
    bool                 isVertex;        // true = VS, false = PS
};

class ShaderManager
{
public:
    static ShaderManager& Get()
    {
        static ShaderManager inst;
        return inst;
    }

    // ── Creation hooks (called from WrappedDevice) ──────────────────────

    // Called AFTER real CreateVertexShader succeeds. Records hash → VS mapping.
    void OnVertexShaderCreated(const void* bytecode, SIZE_T length,
                               ID3D11VertexShader* shader);

    // Called AFTER real CreatePixelShader succeeds. Records hash → PS mapping.
    void OnPixelShaderCreated(const void* bytecode, SIZE_T length,
                              ID3D11PixelShader* shader);

    // ── Bind-time replacement (called from WrappedContext) ──────────────

    // Returns replacement VS if one is registered for this shader's hash.
    // Returns nullptr if no replacement — caller should use original.
    ID3D11VertexShader* GetReplacementVS(ID3D11VertexShader* original) const;

    // Returns replacement PS if one is registered for this shader's hash.
    ID3D11PixelShader*  GetReplacementPS(ID3D11PixelShader* original) const;

    // ── Replacement registration ────────────────────────────────────────

    // Register a replacement shader for a given bytecode hash.
    // The replacement shader must be created separately (e.g. from modified HLSL).
    void RegisterVSReplacement(uint64_t hash, ID3D11VertexShader* replacement);
    void RegisterPSReplacement(uint64_t hash, ID3D11PixelShader* replacement);

    // Remove a previously registered replacement.
    void RemoveVSReplacement(uint64_t hash);
    void RemovePSReplacement(uint64_t hash);

    // ── Query ───────────────────────────────────────────────────────────

    // Get the hash for a shader pointer (0 if not tracked).
    uint64_t GetVSHash(ID3D11VertexShader* shader) const;
    uint64_t GetPSHash(ID3D11PixelShader* shader) const;

    // Get all tracked shader entries (for debug GUI enumeration).
    std::vector<ShaderEntry> GetTrackedShaders() const;

    // Stats
    uint32_t GetTrackedVSCount() const;
    uint32_t GetTrackedPSCount() const;
    uint32_t GetReplacementVSCount() const;
    uint32_t GetReplacementPSCount() const;

    // ── Bytecode access ─────────────────────────────────────────────────

    // Get stored PS bytecode (empty vector if not found).
    std::vector<uint8_t> GetPSBytecode(ID3D11PixelShader* shader) const;

    // ── Hash utility ────────────────────────────────────────────────────

    // FNV-1a 64-bit hash of arbitrary data.
    static uint64_t HashBytecode(const void* data, SIZE_T length);

private:
    ShaderManager() = default;

    mutable std::mutex m_mutex;

    // Hash → shader pointer (forward lookup: what shader has this hash?)
    std::unordered_map<uint64_t, ID3D11VertexShader*> m_hashToVS;
    std::unordered_map<uint64_t, ID3D11PixelShader*>  m_hashToPS;

    // Shader pointer → hash (reverse lookup: what hash does this shader have?)
    std::unordered_map<ID3D11VertexShader*, uint64_t> m_vsToHash;
    std::unordered_map<ID3D11PixelShader*, uint64_t>  m_psToHash;

    // Shader pointer → bytecode length (for debug info)
    std::unordered_map<ID3D11VertexShader*, SIZE_T>   m_vsLength;
    std::unordered_map<ID3D11PixelShader*, SIZE_T>    m_psLength;

    // Hash → replacement shader (bind-time substitution)
    std::unordered_map<uint64_t, ID3D11VertexShader*> m_vsReplacements;
    std::unordered_map<uint64_t, ID3D11PixelShader*>  m_psReplacements;

    // Shader pointer → bytecode (for runtime DXBC patching)
    std::unordered_map<ID3D11PixelShader*, std::vector<uint8_t>> m_psBytecode;
};

} // namespace SB::Proxy
