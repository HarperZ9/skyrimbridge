#pragma once
//=============================================================================
//  HiZPyramid — Compute-based hierarchical depth buffer
//
//  Builds a mip chain from the game's depth buffer using compute shaders.
//  Each mip stores MAX depth (reversed-Z: max = closest surface).
//  Exposed as SRV at register(t19) for ENB shaders (SSR, SSAO).
//=============================================================================

#include <cstdint>
#include <algorithm>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ComputeShader;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;
struct ID3D11Buffer;
struct IDXGISwapChain;

namespace SB
{
    class HiZPyramid
    {
    public:
        static HiZPyramid& Get()
        {
            static HiZPyramid instance;
            return instance;
        }

        bool Initialize(ID3D11Device* a_device, IDXGISwapChain* a_swapChain);
        void Shutdown();

        /// Build the mip chain from the game's depth buffer.
        /// Call from OnENBFrame (after game renders, before ENB passes).
        void BuildPyramid(ID3D11DeviceContext* a_ctx);

        /// Get SRV for the full mip chain (t19)
        ID3D11ShaderResourceView* GetSRV() const { return m_pyramidSRV; }

        uint32_t GetWidth()    const { return m_width; }
        uint32_t GetHeight()   const { return m_height; }
        uint32_t GetMipCount() const { return m_mipCount; }

        static constexpr uint32_t kSRVSlot = 19;

        bool IsInitialized() const { return m_initialized; }
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool a_enabled) { m_enabled = a_enabled; }

    private:
        HiZPyramid() = default;

        bool CompileComputeShaders(ID3D11Device* a_device);
        bool CreatePyramidTexture(ID3D11Device* a_device, uint32_t w, uint32_t h);
        bool AcquireDepthSRV(ID3D11DeviceContext* a_ctx);
        void ReleasePyramidTexture();

        bool m_initialized = false;
        bool m_enabled = true;

        ID3D11ComputeShader* m_copyCS = nullptr;       // Mip 0: depth → pyramid
        ID3D11ComputeShader* m_downsampleCS = nullptr;  // Mip 1+: 2×2 max downsample

        // Pyramid texture: R32_FLOAT with full mip chain
        ID3D11Texture2D* m_pyramidTex = nullptr;

        // Full-chain SRV for shader reads
        ID3D11ShaderResourceView* m_pyramidSRV = nullptr;

        // Per-mip UAVs and SRVs
        static constexpr uint32_t kMaxMips = 14;
        ID3D11UnorderedAccessView* m_mipUAV[kMaxMips]{};
        ID3D11ShaderResourceView* m_mipSRV[kMaxMips]{};

        // Params constant buffer for per-mip dispatch
        ID3D11Buffer* m_paramCB = nullptr;

        // Game's depth buffer SRV (fetched each frame)
        ID3D11ShaderResourceView* m_depthSRV = nullptr;
        bool m_ownDepthSRV = false;

        uint32_t m_width = 0, m_height = 0;
        uint32_t m_mipCount = 0;

        ID3D11Device* m_device = nullptr;
    };

} // namespace SB
