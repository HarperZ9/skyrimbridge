#pragma once
//=============================================================================
//  CBDirtyTracker — Constant buffer dirty tracking
//
//  Intercepts Map(WRITE_DISCARD)/Unmap on dynamic constant buffers.
//  Maintains a shadow copy per CB; if the new data matches the shadow,
//  the real Map/Unmap is skipped entirely (GPU already has correct data).
//
//  Expected savings: 60-80% of CB uploads (PerMaterial CB changes ~19%
//  of the time per draw call — RE verified via BSLightingShader disasm).
//=============================================================================

#include <d3d11.h>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace SB::Proxy
{

class CBDirtyTracker
{
public:
    static CBDirtyTracker& Get();

    // Called from WrappedContext::Map.
    // Returns true if this Map was intercepted (caller must NOT call real Map).
    // pMappedResource is filled with a staging buffer pointer.
    bool InterceptMap(ID3D11DeviceContext* realCtx, ID3D11Resource* pResource,
                      UINT Subresource, D3D11_MAP MapType, UINT MapFlags,
                      D3D11_MAPPED_SUBRESOURCE* pMappedResource);

    // Called from WrappedContext::Unmap.
    // Returns true if handled (caller must NOT call real Unmap).
    bool InterceptUnmap(ID3D11DeviceContext* realCtx, ID3D11Resource* pResource,
                        UINT Subresource);

    // Called once per frame from WrappedSwapChain::Present.
    // Resets per-frame counters and evicts stale entries (buffers not mapped
    // this frame may have been destroyed; eviction prevents stale-shadow bugs
    // if a new buffer is allocated at the same pointer).
    void OnFrameEnd();

    // Called from WrappedContext::ClearState
    void OnClearState();

    // Per-frame stats (valid until next OnFrameEnd)
    uint32_t GetMapCalls()         const { return m_mapCalls; }
    uint32_t GetSkippedUpdates()   const { return m_skippedUpdates; }
    uint32_t GetCommittedUpdates() const { return m_committedUpdates; }
    uint32_t GetTrackedBuffers()   const { return static_cast<uint32_t>(m_entries.size()); }

    // Shadow data access (for OcclusionCuller to read VS CB transforms)
    const uint8_t* GetShadowData(ID3D11Resource* pResource) const;
    uint32_t       GetShadowSize(ID3D11Resource* pResource) const;

private:
    CBDirtyTracker() = default;

    // Don't track CBs larger than this (diminishing returns, most engine CBs are < 1 KB)
    static constexpr uint32_t kMaxTrackedSize = 4096;

    struct CBEntry {
        uint32_t             byteWidth = 0;
        std::vector<uint8_t> shadow;          // Last committed data
        std::vector<uint8_t> staging;         // Current write buffer
        bool                 intercepted = false;  // True between Map and Unmap
        bool                 mappedThisFrame = false;
    };

    // Lazily register a CB on first Map encounter
    CBEntry* GetOrCreateEntry(ID3D11Resource* pResource);

    std::unordered_map<ID3D11Resource*, CBEntry> m_entries;

    // Per-frame counters
    uint32_t m_mapCalls         = 0;
    uint32_t m_skippedUpdates   = 0;
    uint32_t m_committedUpdates = 0;
};

} // namespace SB::Proxy
