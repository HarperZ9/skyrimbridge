//=============================================================================
//  ShaderManager.cpp — Shader hash tracking + replacement implementation
//=============================================================================

#include "ShaderManager.h"
#include "ProxyLog.h"
#include <cstring>

namespace SB::Proxy
{

// ── FNV-1a 64-bit hash ────────────────────────────────────────────────────

uint64_t ShaderManager::HashBytecode(const void* data, SIZE_T length)
{
    constexpr uint64_t kFNVOffset = 14695981039346656037ULL;
    constexpr uint64_t kFNVPrime  = 1099511628211ULL;

    auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = kFNVOffset;
    for (SIZE_T i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= kFNVPrime;
    }
    return hash;
}

// ── Creation hooks ────────────────────────────────────────────────────────

void ShaderManager::OnVertexShaderCreated(
    const void* bytecode, SIZE_T length, ID3D11VertexShader* shader)
{
    if (!shader || !bytecode || length == 0) return;

    uint64_t hash = HashBytecode(bytecode, length);

    std::lock_guard lock(m_mutex);
    m_hashToVS[hash]    = shader;
    m_vsToHash[shader]  = hash;
    m_vsLength[shader]  = length;

    Log("ShaderManager: VS created  hash=%016llX  len=%zu  ptr=%p",
        hash, length, shader);
}

void ShaderManager::OnPixelShaderCreated(
    const void* bytecode, SIZE_T length, ID3D11PixelShader* shader)
{
    if (!shader || !bytecode || length == 0) return;

    uint64_t hash = HashBytecode(bytecode, length);

    std::lock_guard lock(m_mutex);
    m_hashToPS[hash]    = shader;
    m_psToHash[shader]  = hash;
    m_psLength[shader]  = length;

    // Store bytecode for runtime DXBC patching (MaterialPipeline, etc.)
    auto& stored = m_psBytecode[shader];
    stored.resize(length);
    std::memcpy(stored.data(), bytecode, length);

    Log("ShaderManager: PS created  hash=%016llX  len=%zu  ptr=%p",
        hash, length, shader);
}

// ── Bind-time replacement ─────────────────────────────────────────────────

ID3D11VertexShader* ShaderManager::GetReplacementVS(ID3D11VertexShader* original) const
{
    if (!original) return nullptr;

    std::lock_guard lock(m_mutex);

    auto hashIt = m_vsToHash.find(original);
    if (hashIt == m_vsToHash.end()) return nullptr;

    auto replIt = m_vsReplacements.find(hashIt->second);
    if (replIt == m_vsReplacements.end()) return nullptr;

    return replIt->second;
}

ID3D11PixelShader* ShaderManager::GetReplacementPS(ID3D11PixelShader* original) const
{
    if (!original) return nullptr;

    std::lock_guard lock(m_mutex);

    auto hashIt = m_psToHash.find(original);
    if (hashIt == m_psToHash.end()) return nullptr;

    auto replIt = m_psReplacements.find(hashIt->second);
    if (replIt == m_psReplacements.end()) return nullptr;

    return replIt->second;
}

// ── Replacement registration ──────────────────────────────────────────────

void ShaderManager::RegisterVSReplacement(uint64_t hash, ID3D11VertexShader* replacement)
{
    std::lock_guard lock(m_mutex);
    m_vsReplacements[hash] = replacement;
    Log("ShaderManager: VS replacement registered  hash=%016llX  ptr=%p",
        hash, replacement);
}

void ShaderManager::RegisterPSReplacement(uint64_t hash, ID3D11PixelShader* replacement)
{
    std::lock_guard lock(m_mutex);
    m_psReplacements[hash] = replacement;
    Log("ShaderManager: PS replacement registered  hash=%016llX  ptr=%p",
        hash, replacement);
}

void ShaderManager::RemoveVSReplacement(uint64_t hash)
{
    std::lock_guard lock(m_mutex);
    m_vsReplacements.erase(hash);
}

void ShaderManager::RemovePSReplacement(uint64_t hash)
{
    std::lock_guard lock(m_mutex);
    m_psReplacements.erase(hash);
}

// ── Query ─────────────────────────────────────────────────────────────────

uint64_t ShaderManager::GetVSHash(ID3D11VertexShader* shader) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_vsToHash.find(shader);
    return (it != m_vsToHash.end()) ? it->second : 0;
}

uint64_t ShaderManager::GetPSHash(ID3D11PixelShader* shader) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_psToHash.find(shader);
    return (it != m_psToHash.end()) ? it->second : 0;
}

std::vector<ShaderEntry> ShaderManager::GetTrackedShaders() const
{
    std::lock_guard lock(m_mutex);
    std::vector<ShaderEntry> result;
    result.reserve(m_vsToHash.size() + m_psToHash.size());

    for (auto& [shader, hash] : m_vsToHash) {
        ShaderEntry e;
        e.hash = hash;
        e.isVertex = true;
        auto lenIt = m_vsLength.find(shader);
        e.bytecodeLength = (lenIt != m_vsLength.end()) ? lenIt->second : 0;
        result.push_back(e);
    }
    for (auto& [shader, hash] : m_psToHash) {
        ShaderEntry e;
        e.hash = hash;
        e.isVertex = false;
        auto lenIt = m_psLength.find(shader);
        e.bytecodeLength = (lenIt != m_psLength.end()) ? lenIt->second : 0;
        result.push_back(e);
    }
    return result;
}

uint32_t ShaderManager::GetTrackedVSCount() const
{
    std::lock_guard lock(m_mutex);
    return static_cast<uint32_t>(m_vsToHash.size());
}

uint32_t ShaderManager::GetTrackedPSCount() const
{
    std::lock_guard lock(m_mutex);
    return static_cast<uint32_t>(m_psToHash.size());
}

uint32_t ShaderManager::GetReplacementVSCount() const
{
    std::lock_guard lock(m_mutex);
    return static_cast<uint32_t>(m_vsReplacements.size());
}

uint32_t ShaderManager::GetReplacementPSCount() const
{
    std::lock_guard lock(m_mutex);
    return static_cast<uint32_t>(m_psReplacements.size());
}

std::vector<uint8_t> ShaderManager::GetPSBytecode(ID3D11PixelShader* shader) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_psBytecode.find(shader);
    if (it != m_psBytecode.end()) return it->second;
    return {};
}

} // namespace SB::Proxy
