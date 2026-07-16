#pragma once
//=============================================================================
//  SRVInjector.h — Bind compute output textures into ENB shader passes
//
//  Registers SRVs at specific t-slots (t17+) and samplers at s-slots.
//  Called once per frame in OnENBFrame to inject before ENB's pipeline runs.
//  If ENB doesn't touch these high-numbered slots, the bindings persist.
//
//  Slot allocation:
//    t17  — Luminance histogram (R32_FLOAT 256x1)
//    t18  — Film LUT (R8G8B8A8 64^3 Texture3D)
//    t19  — Hi-Z depth pyramid (R32_FLOAT mipped)
//    t20  — SSAO / GTAO (R16_FLOAT full-res)
//    t21  — SSR (R16G16B16A16_FLOAT half-res)
//    t22  — TAA temporal history (R16G16B16A16_FLOAT)
//    t23  — Atmosphere transmittance LUT
//    t24  — Atmosphere scattering LUT
//    t25  — Material classification (R8_UINT full-res)
//    t26  — SSGI (R16G16B16A16_FLOAT)
//    t27  — Volumetric clouds (R16G16B16A16_FLOAT)
//    t28  — Contact shadows (R8_UNORM full-res, 1=lit 0=shadowed)
//    t29  — Skylighting (R16_FLOAT full-res, 0=occluded 1=sky)
//    s2   — Trilinear clamp sampler (for LUT)
//    s3   — TAA history sampler
//=============================================================================

#include <d3d11.h>

namespace SB
{

class SRVInjector
{
public:
    static SRVInjector& Get();

    // Initialize (just stores the context pointer)
    bool Initialize(ID3D11DeviceContext* ctx);
    bool IsInitialized() const { return m_context != nullptr; }

    // Register/unregister SRVs for PS injection
    void RegisterSRV(UINT slot, ID3D11ShaderResourceView* srv);
    void UnregisterSRV(UINT slot);

    // Register/unregister samplers for PS injection
    void RegisterSampler(UINT slot, ID3D11SamplerState* sampler);
    void UnregisterSampler(UINT slot);

    // Bind all registered SRVs and samplers to the PS stage.
    // Call this in OnENBFrame, before ENB processes its shader pipeline.
    void InjectAll();

    // Clear all PS-bound SRVs at registered slots (call in HookedPresent cleanup)
    void ClearAll();

    // ENB pass state tracking
    void SetENBPassActive(bool active) { m_enbPassActive = active; }
    bool IsENBPassActive() const { return m_enbPassActive; }

private:
    SRVInjector() = default;

    ID3D11DeviceContext* m_context = nullptr;
    bool m_enbPassActive = false;

    static constexpr UINT kMaxSlots = 32;
    ID3D11ShaderResourceView*  m_srvs[kMaxSlots] = {};
    bool                       m_srvActive[kMaxSlots] = {};
    ID3D11SamplerState*        m_samplers[kMaxSlots] = {};
    bool                       m_samplerActive[kMaxSlots] = {};
};

} // namespace SB
