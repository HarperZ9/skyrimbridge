#pragma once
//=============================================================================
//  RenderPassManager — Full D3D11 rasterization pipeline for custom passes
//
//  Bypasses ENB's 128-technique limit entirely.  Each registered pass is an
//  independent VS+PS (or custom VS+PS) draw with its own SRVs, RTs, CB,
//  blend/depth/rasterizer state, and optional geometry (VB/IB/instancing).
//
//  Fullscreen usage (default):
//    auto id = rpm.RegisterPass({ .name = "Vignette", .psSource = kPS });
//    rpm.Execute({ .passID = id, .rtv = backbufferRTV, .srvs = &srv, .srvCount = 1 });
//
//  Mesh drawing:
//    auto id = rpm.RegisterPass({
//        .name = "MeshPass", .vsSource = kVS, .psSource = kPS,
//        .inputElements = layout, .inputElementCount = 3,
//    });
//    rpm.Execute({
//        .passID = id, .rtv = rtv,
//        .vertexBuffer = vb, .vbStride = 32,
//        .indexBuffer = ib, .indexCount = 1024,
//    });
//
//  MRT:
//    ID3D11RenderTargetView* targets[3] = { rtv0, rtv1, rtv2 };
//    rpm.Execute({ .passID = id, .rtvs = targets, .rtvCount = 3 });
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>

namespace SB
{

// Handle for a registered render pass (0 = invalid)
using RenderPassID = uint32_t;


// ─── Pass registration ──────────────────────────────────────────────────

struct RenderPassDesc
{
    const char* name     = nullptr;
    const char* vsSource = nullptr;  // null → built-in fullscreen VS
    const char* psSource = nullptr;  // required
    const char* vsEntry  = "main";
    const char* psEntry  = "main";

    // Input layout for mesh drawing.  Null = no layout (fullscreen passes).
    // Elements are used with the compiled VS bytecode to create an input layout.
    const D3D11_INPUT_ELEMENT_DESC* inputElements    = nullptr;
    uint32_t                        inputElementCount = 0;
};


// ─── Pass execution ─────────────────────────────────────────────────────

struct PassExecution
{
    RenderPassID              passID   = 0;

    // ── Render targets ─────────────────────────────────────────────
    // Single RT: set rtv.  MRT: set rtvs + rtvCount (rtv is ignored).
    ID3D11RenderTargetView*   rtv      = nullptr;
    ID3D11RenderTargetView**  rtvs     = nullptr;
    uint32_t                  rtvCount = 0;  // 0 = single-RT mode via rtv

    // ── Shader resources ───────────────────────────────────────────
    ID3D11ShaderResourceView** srvs    = nullptr;   // PS SRVs (bound at t0+)
    uint32_t                  srvCount = 0;
    ID3D11SamplerState**      samplers    = nullptr; // PS samplers (bound at s0+)
    uint32_t                  samplerCount = 0;
    const void*               cbData   = nullptr;    // CB data (bound to VS b0 + PS b0)
    uint32_t                  cbSize   = 0;

    // ── Depth-stencil ──────────────────────────────────────────────
    ID3D11DepthStencilView*   dsv              = nullptr;
    ID3D11DepthStencilState*  depthStencilState = nullptr;  // null = depth off
    uint32_t                  stencilRef        = 0;

    // ── Blend state ────────────────────────────────────────────────
    ID3D11BlendState*         blendState    = nullptr;  // null = opaque overwrite
    float                     blendFactor[4] = {1.f, 1.f, 1.f, 1.f};
    uint32_t                  sampleMask    = 0xFFFFFFFF;

    // ── Rasterizer ─────────────────────────────────────────────────
    ID3D11RasterizerState*    rasterizerState = nullptr;  // null = default (solid, no cull)
    D3D11_VIEWPORT            viewport = {};              // width==0 → auto from first RT

    // ── Geometry (mesh drawing) ────────────────────────────────────
    // All zero = fullscreen triangle via Draw(3,0).
    ID3D11Buffer*             vertexBuffer  = nullptr;
    uint32_t                  vbStride      = 0;
    uint32_t                  vbOffset      = 0;
    ID3D11Buffer*             indexBuffer   = nullptr;
    DXGI_FORMAT               indexFormat   = DXGI_FORMAT_R16_UINT;
    uint32_t                  vertexCount   = 0;
    uint32_t                  indexCount    = 0;
    uint32_t                  startVertex   = 0;
    uint32_t                  startIndex    = 0;
    int32_t                   baseVertex    = 0;

    // ── Instancing ─────────────────────────────────────────────────
    ID3D11Buffer*             instanceBuffer = nullptr;
    uint32_t                  instanceStride = 0;
    uint32_t                  instanceCount  = 0;  // 0 = no instancing

    // ── Topology ───────────────────────────────────────────────────
    D3D11_PRIMITIVE_TOPOLOGY  topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};


// ─── Manager ────────────────────────────────────────────────────────────

class RenderPassManager
{
public:
    static RenderPassManager& Get()
    {
        static RenderPassManager instance;
        return instance;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx);
    void Shutdown();

    /// Register a VS+PS pass.  Returns 0 on failure.
    /// If inputElements are provided, an input layout is created from VS bytecode.
    RenderPassID RegisterPass(const RenderPassDesc& desc);

    /// Execute a registered pass: save state, bind, draw, restore.
    void Execute(const PassExecution& exec);

    /// Get the built-in fullscreen vertex shader (for sharing)
    ID3D11VertexShader* GetFullscreenVS() const { return m_fullscreenVS; }

    bool IsInitialized() const { return m_initialized; }

    // Pass count for diagnostics
    uint32_t GetPassCount() const { return static_cast<uint32_t>(m_passes.size()); }

private:
    // Guards m_passes for background-thread registration.
    std::mutex m_passMutex;
    RenderPassManager() = default;

    bool CompileFullscreenVS();

    // Pipeline state save/restore
    void SavePipelineState();
    void RestorePipelineState();

    bool m_initialized = false;
    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Built-in fullscreen vertex shader (SV_VertexID → position + UV)
    ID3D11VertexShader* m_fullscreenVS = nullptr;

    // Default rasterizer state: solid fill, no culling (safe for fullscreen + mesh)
    ID3D11RasterizerState* m_defaultRS = nullptr;

    // Dynamic constant buffer (resized as needed)
    ID3D11Buffer* m_dynamicCB     = nullptr;
    uint32_t      m_dynamicCBSize = 0;

    // Registered passes
    struct PassEntry
    {
        std::string          name;
        ID3D11VertexShader*  vs          = nullptr;
        ID3D11PixelShader*   ps          = nullptr;
        ID3D11InputLayout*   inputLayout = nullptr;
    };
    std::vector<PassEntry> m_passes;

    // Saved pipeline state (full D3D11 state machine)
    struct SavedState
    {
        // Input Assembler
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ID3D11InputLayout*       inputLayout = nullptr;
        ID3D11Buffer*            vertexBuffers[4] = {};
        UINT                     vbStrides[4] = {};
        UINT                     vbOffsets[4] = {};
        ID3D11Buffer*            indexBuffer = nullptr;
        DXGI_FORMAT              ibFormat = DXGI_FORMAT_UNKNOWN;
        UINT                     ibOffset = 0;

        // Vertex Shader
        ID3D11VertexShader*      vs = nullptr;
        ID3D11Buffer*            vsCBs[4] = {};
        ID3D11ClassInstance*     vsCI[256] = {};
        UINT                     vsCICount = 256;

        // Rasterizer
        ID3D11RasterizerState*   rsState = nullptr;
        D3D11_VIEWPORT           viewports[4] = {};
        UINT                     viewportCount = 4;
        D3D11_RECT               scissorRects[4] = {};
        UINT                     scissorCount = 4;

        // Pixel Shader
        ID3D11PixelShader*       ps = nullptr;
        ID3D11ClassInstance*     psCI[256] = {};
        UINT                     psCICount = 256;
        ID3D11Buffer*            psCBs[4] = {};
        ID3D11ShaderResourceView* psSRVs[8] = {};
        ID3D11SamplerState*      psSamplers[4] = {};

        // Output Merger (8 RTVs for MRT support)
        ID3D11RenderTargetView*  rtvs[8] = {};
        ID3D11DepthStencilView*  dsv = nullptr;
        ID3D11BlendState*        blendState = nullptr;
        float                    blendFactor[4] = {};
        UINT                     sampleMask = 0;
        ID3D11DepthStencilState* depthStencilState = nullptr;
        UINT                     stencilRef = 0;

        bool saved = false;

        void Release();
    };
    SavedState m_saved;
};

} // namespace SB
