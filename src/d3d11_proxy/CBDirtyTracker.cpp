#include "CBDirtyTracker.h"
#include <cstring>

namespace SB::Proxy
{

CBDirtyTracker& CBDirtyTracker::Get()
{
    static CBDirtyTracker instance;
    return instance;
}

CBDirtyTracker::CBEntry* CBDirtyTracker::GetOrCreateEntry(ID3D11Resource* pResource)
{
    auto it = m_entries.find(pResource);
    if (it != m_entries.end())
        return &it->second;

    // Query resource type — only track buffers
    D3D11_RESOURCE_DIMENSION dim;
    pResource->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_BUFFER)
        return nullptr;

    auto* buf = static_cast<ID3D11Buffer*>(pResource);
    D3D11_BUFFER_DESC desc;
    buf->GetDesc(&desc);

    // Only track dynamic constant buffers within size limit
    if (!(desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER))
        return nullptr;
    if (desc.Usage != D3D11_USAGE_DYNAMIC)
        return nullptr;
    if (desc.ByteWidth == 0 || desc.ByteWidth > kMaxTrackedSize)
        return nullptr;

    auto& entry = m_entries[pResource];
    entry.byteWidth = desc.ByteWidth;
    entry.shadow.resize(desc.ByteWidth, 0);
    entry.staging.resize(desc.ByteWidth, 0);
    entry.intercepted = false;
    entry.mappedThisFrame = false;
    return &entry;
}

bool CBDirtyTracker::InterceptMap(ID3D11DeviceContext* realCtx, ID3D11Resource* pResource,
                                   UINT Subresource, D3D11_MAP MapType, UINT MapFlags,
                                   D3D11_MAPPED_SUBRESOURCE* pMappedResource)
{
    // Only intercept WRITE_DISCARD on subresource 0
    if (MapType != D3D11_MAP_WRITE_DISCARD || Subresource != 0)
        return false;

    auto* entry = GetOrCreateEntry(pResource);
    if (!entry)
        return false;

    ++m_mapCalls;
    entry->mappedThisFrame = true;

    // Return our staging buffer instead of calling real Map.
    // The engine writes into staging; we compare on Unmap.
    pMappedResource->pData      = entry->staging.data();
    pMappedResource->RowPitch   = entry->byteWidth;
    pMappedResource->DepthPitch = 0;
    entry->intercepted = true;
    return true;
}

bool CBDirtyTracker::InterceptUnmap(ID3D11DeviceContext* realCtx, ID3D11Resource* pResource,
                                     UINT Subresource)
{
    if (Subresource != 0)
        return false;

    auto it = m_entries.find(pResource);
    if (it == m_entries.end() || !it->second.intercepted)
        return false;

    auto& entry = it->second;
    entry.intercepted = false;

    // Compare what the engine just wrote (staging) vs last committed data (shadow).
    if (std::memcmp(entry.staging.data(), entry.shadow.data(), entry.byteWidth) == 0) {
        // Data unchanged — skip the real Map/Unmap entirely.
        // The GPU buffer still holds the correct data from the last real commit.
        ++m_skippedUpdates;
        return true;
    }

    // Data changed — commit to GPU via real Map(WRITE_DISCARD) + memcpy + Unmap
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = realCtx->Map(pResource, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        std::memcpy(mapped.pData, entry.staging.data(), entry.byteWidth);
        realCtx->Unmap(pResource, 0);
        // Update shadow to match what we just committed
        std::memcpy(entry.shadow.data(), entry.staging.data(), entry.byteWidth);
    }
    ++m_committedUpdates;
    return true;
}

void CBDirtyTracker::OnFrameEnd()
{
    // Evict entries that weren't mapped this frame.
    // Handles the case where a buffer is destroyed and a new buffer is created
    // at the same pointer — the stale shadow would be incorrect.
    for (auto it = m_entries.begin(); it != m_entries.end(); ) {
        if (!it->second.mappedThisFrame) {
            it = m_entries.erase(it);
        } else {
            it->second.mappedThisFrame = false;
            ++it;
        }
    }

    // Reset per-frame counters
    m_mapCalls         = 0;
    m_skippedUpdates   = 0;
    m_committedUpdates = 0;
}

void CBDirtyTracker::OnClearState()
{
    // ClearState doesn't destroy buffers, but any in-flight intercepts are invalid
    for (auto& [_, entry] : m_entries)
        entry.intercepted = false;
}

const uint8_t* CBDirtyTracker::GetShadowData(ID3D11Resource* pResource) const
{
    auto it = m_entries.find(pResource);
    if (it == m_entries.end()) return nullptr;
    if (it->second.shadow.empty()) return nullptr;
    return it->second.shadow.data();
}

uint32_t CBDirtyTracker::GetShadowSize(ID3D11Resource* pResource) const
{
    auto it = m_entries.find(pResource);
    if (it == m_entries.end()) return 0;
    return it->second.byteWidth;
}

} // namespace SB::Proxy
