#pragma once
//=============================================================================
//  ComputeManager.h — GPU compute dispatch infrastructure for SkyrimBridge
//
//  Provides shader compilation, resource creation, and state-safe dispatch.
//  All compute effects (histogram, Hi-Z, SSAO, SSR, etc.) build on this.
//=============================================================================

#include <d3d11.h>
#include <string>
#include <vector>

namespace SB
{

// Handle for a compiled compute shader (0 = invalid)
using ComputeShaderID = uint32_t;

class ComputeManager
{
public:
    static ComputeManager& Get();

    // Initialize with D3D11 device/context (from D3D11Hook or ENBGetRenderInfo)
    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx);
    bool IsInitialized() const { return m_initialized; }

    // ── Shader compilation ──────────────────────────────────────────────

    // Compile a compute shader from embedded HLSL source string
    // Returns 0 on failure, non-zero handle on success
    ComputeShaderID CompileShader(const char* name, const char* hlslSource,
                                  const char* entryPoint = "main",
                                  const char* target = "cs_5_0");

    ID3D11ComputeShader* GetShader(ComputeShaderID id) const;

    // ── Resource creation ───────────────────────────────────────────────

    struct TextureResource
    {
        ID3D11Texture2D*            texture = nullptr;
        ID3D11ShaderResourceView*   srv     = nullptr;
        ID3D11UnorderedAccessView*  uav     = nullptr;
        ID3D11Texture2D*            staging = nullptr;
        UINT width = 0, height = 0, mipLevels = 1;

        void Release();
        bool Valid() const { return texture != nullptr; }
    };

    // Create a 2D texture with optional SRV, UAV, and staging copy
    TextureResource CreateTexture2D(UINT width, UINT height,
                                     DXGI_FORMAT format,
                                     bool srv, bool uav,
                                     UINT mipLevels = 1,
                                     bool staging = false,
                                     const char* debugName = nullptr);

    // Create a per-mip UAV for a specific mip level of an existing texture
    ID3D11UnorderedAccessView* CreateMipUAV(ID3D11Texture2D* tex,
                                             DXGI_FORMAT format,
                                             UINT mipSlice);

    // Create a per-mip SRV that reads a single mip level
    ID3D11ShaderResourceView* CreateMipSRV(ID3D11Texture2D* tex,
                                            DXGI_FORMAT format,
                                            UINT mipSlice);

    struct BufferResource
    {
        ID3D11Buffer*               buffer  = nullptr;
        ID3D11ShaderResourceView*   srv     = nullptr;
        ID3D11UnorderedAccessView*  uav     = nullptr;
        ID3D11Buffer*               staging = nullptr;
        UINT elementCount = 0, elementStride = 0;

        void Release();
        bool Valid() const { return buffer != nullptr; }
    };

    // Create a structured buffer with optional SRV, UAV, and staging copy
    BufferResource CreateStructuredBuffer(UINT elementCount, UINT elementStride,
                                          bool srv, bool uav,
                                          bool staging = false,
                                          const char* debugName = nullptr);

    // Create a constant buffer
    ID3D11Buffer* CreateConstantBuffer(UINT sizeBytes);

    // ── State-safe compute dispatch ─────────────────────────────────────

    // Save full CS pipeline state (shader, SRVs, UAVs, CBs, samplers)
    void SaveCSState();

    // Restore saved CS pipeline state
    void RestoreCSState();

    // Save Output Merger state (RTVs, DSV, blend, depth-stencil)
    // MUST be called before creating a UAV on the backbuffer — D3D11 will
    // auto-unbind any RTV targeting the same resource when a UAV is bound.
    void SaveOMState();

    // Restore saved Output Merger state
    void RestoreOMState();

    // Dispatch a compute shader (call between SaveCSState/RestoreCSState)
    void Dispatch(ComputeShaderID shader,
                  UINT groupsX, UINT groupsY, UINT groupsZ);

    // Bind CS resources before Dispatch
    void CSSetSRVs(UINT startSlot, UINT count, ID3D11ShaderResourceView* const* srvs);
    void CSSetUAVs(UINT startSlot, UINT count, ID3D11UnorderedAccessView* const* uavs,
                   const UINT* initialCounts = nullptr);
    void CSSetCBs(UINT startSlot, UINT count, ID3D11Buffer* const* cbs);
    void CSSetSamplers(UINT startSlot, UINT count, ID3D11SamplerState* const* samplers);

    // Clear CS bindings (prevents D3D11 debug warnings about SRV/UAV conflicts)
    void CSClearSRVs(UINT startSlot, UINT count);
    void CSClearUAVs(UINT startSlot, UINT count);

    // ── Readback ────────────────────────────────────────────────────────

    // Copy GPU buffer to staging, map, and read into dst
    bool ReadbackBuffer(ID3D11Buffer* src, ID3D11Buffer* staging,
                        void* dst, UINT sizeBytes);

    // Copy GPU texture to staging, map, and read into dst
    bool ReadbackTexture(ID3D11Texture2D* src, ID3D11Texture2D* staging,
                         void* dst, UINT rowPitch, UINT height);

    // ── Cleanup ─────────────────────────────────────────────────────────
    void Shutdown();

    ID3D11Device*        GetDevice()  const { return m_device; }
    ID3D11DeviceContext* GetContext() const { return m_context; }

private:
    ComputeManager() = default;

    bool m_initialized = false;
    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Compiled shaders
    struct ShaderEntry
    {
        ID3D11ComputeShader* shader = nullptr;
        std::string name;
    };
    std::vector<ShaderEntry> m_shaders;
    ComputeShaderID m_nextID = 1;

    // Saved CS state for restore
    static constexpr UINT kMaxSavedSRVs     = 8;
    static constexpr UINT kMaxSavedUAVs     = 8;
    static constexpr UINT kMaxSavedCBs      = 4;
    static constexpr UINT kMaxSavedSamplers = 4;

    struct SavedCSState
    {
        ID3D11ComputeShader*        shader = nullptr;
        ID3D11ClassInstance*        classInstances[256] = {};
        UINT                        classInstanceCount = 256;
        ID3D11ShaderResourceView*   srvs[kMaxSavedSRVs] = {};
        ID3D11UnorderedAccessView*  uavs[kMaxSavedUAVs] = {};
        ID3D11Buffer*               cbs[kMaxSavedCBs] = {};
        ID3D11SamplerState*         samplers[kMaxSavedSamplers] = {};
        bool saved = false;
    };
    SavedCSState m_savedState;

    // Saved OM state for restore (RTVs, DSV, blend, depth-stencil)
    static constexpr UINT kMaxRTVs = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

    struct SavedOMState
    {
        ID3D11RenderTargetView*  rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        ID3D11DepthStencilView*  dsv = nullptr;
        ID3D11BlendState*        blendState = nullptr;
        FLOAT                    blendFactor[4] = {};
        UINT                     sampleMask = 0xFFFFFFFF;
        ID3D11DepthStencilState* depthStencilState = nullptr;
        UINT                     stencilRef = 0;
        bool saved = false;
    };
    SavedOMState m_savedOMState;
};

} // namespace SB
