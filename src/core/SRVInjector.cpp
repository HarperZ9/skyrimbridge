#include "SRVInjector.h"

#include <SKSE/SKSE.h>

namespace SB
{

SRVInjector& SRVInjector::Get()
{
    static SRVInjector inst;
    return inst;
}

bool SRVInjector::Initialize(ID3D11DeviceContext* ctx)
{
    if (!ctx) return false;
    m_context = ctx;
    SKSE::log::info("SRVInjector: initialized");
    return true;
}

void SRVInjector::RegisterSRV(UINT slot, ID3D11ShaderResourceView* srv)
{
    if (slot >= kMaxSlots) return;
    m_srvs[slot]      = srv;
    m_srvActive[slot]  = (srv != nullptr);
}

void SRVInjector::UnregisterSRV(UINT slot)
{
    if (slot >= kMaxSlots) return;
    m_srvs[slot]      = nullptr;
    m_srvActive[slot]  = false;
}

void SRVInjector::RegisterSampler(UINT slot, ID3D11SamplerState* sampler)
{
    if (slot >= kMaxSlots) return;
    m_samplers[slot]      = sampler;
    m_samplerActive[slot] = (sampler != nullptr);
}

void SRVInjector::UnregisterSampler(UINT slot)
{
    if (slot >= kMaxSlots) return;
    m_samplers[slot]      = nullptr;
    m_samplerActive[slot] = false;
}

void SRVInjector::InjectAll()
{
    if (!m_context) return;

    // Bind each registered SRV individually at its slot
    for (UINT i = 0; i < kMaxSlots; ++i) {
        if (m_srvActive[i]) {
            m_context->PSSetShaderResources(i, 1, &m_srvs[i]);
        }
    }

    // Bind each registered sampler individually at its slot
    for (UINT i = 0; i < kMaxSlots; ++i) {
        if (m_samplerActive[i]) {
            m_context->PSSetSamplers(i, 1, &m_samplers[i]);
        }
    }
}

void SRVInjector::ClearAll()
{
    if (!m_context) return;

    ID3D11ShaderResourceView* nullSRV = nullptr;
    for (UINT i = 0; i < kMaxSlots; ++i) {
        if (m_srvActive[i]) {
            m_context->PSSetShaderResources(i, 1, &nullSRV);
        }
    }

    ID3D11SamplerState* nullSampler = nullptr;
    for (UINT i = 0; i < kMaxSlots; ++i) {
        if (m_samplerActive[i]) {
            m_context->PSSetSamplers(i, 1, &nullSampler);
        }
    }
}

} // namespace SB
