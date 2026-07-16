#include "RenderTracker.h"
#include <RE/Skyrim.h>
#include <cmath>

namespace SB::RenderTracker
{
    static uint32_t s_frameCount = 0;

    RenderData Update(float a_deltaTime)
    {
        RenderData data{};
        s_frameCount++;

        data.FrameInfo.x = static_cast<float>(s_frameCount);
        data.FrameInfo.y = a_deltaTime;

        // Screen dimensions from BSGraphics::State
        auto* gfx = RE::BSGraphics::State::GetSingleton();
        if (gfx) {
            data.FrameInfo.z = static_cast<float>(gfx->screenWidth);
            data.FrameInfo.w = static_cast<float>(gfx->screenHeight);
        }

        // ── TAA jitter ──────────────────────────────────────────────────
        // R2 quasirandom sub-pixel jitter — DISABLED until TAAManager is tested.
        // Zero jitter prevents ENB shaders from sampling offset UVs.
        data.Jitter.x = 0.0f;
        data.Jitter.y = 0.0f;
        data.Jitter.z = static_cast<float>(s_frameCount % 16);

        // Time dilation factor from global time multiplier
        // < 1.0 = slow motion (Slow Time shout), > 1.0 = sped up, 1.0 = normal
        data.Jitter.w = RE::BSTimer::GetCurrentGlobalTimeMult();

        // DepthParams removed — derivable from SB_Camera_Params (near/far)

        // Stencil buffer metadata (classification scheme info for shaders)
        // Skyrim uses D24S8 or D32S8. Stencil classifications:
        //   0=sky, 1=terrain, 2=actor, etc. (engine-defined)
        data.StencilInfo.x = 1.f;  // stencil exists
        data.StencilInfo.y = 16.f; // SRV slot t16 (convention)
        data.StencilInfo.z = 8.f;  // 8-bit stencil

        // Game freeze/pause state from Main singleton
        if (auto* main = RE::Main::GetSingleton()) {
            data.StencilInfo.w = main->freezeTime ? 1.f : 0.f;
        }

        return data;
    }
}
