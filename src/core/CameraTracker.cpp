#include "CameraTracker.h"
#include <RE/Skyrim.h>
#include <cmath>
#include <cstring>

namespace SB::CameraTracker
{
    // Previous frame data for motion vectors / temporal effects
    static Float4 s_prevWorldPos{};
    static Float4 s_prevViewRow0{};
    static Float4 s_prevViewRow1{};
    static float  s_prevFOV = 0.f;
    static bool   s_hasPrev = false;

    // ── Main update ─────────────────────────────────────────────────────
    // Optimized: pass only view rotation 3x3 + worldPos + projection params.
    // Shaders derive Proj, ViewProj, InvViewProj, InvView via helpers.
    CameraData Update()
    {
        CameraData data{};

        // ── Camera state from PlayerCamera ──────────────────────────────
        auto* pcam = RE::PlayerCamera::GetSingleton();
        float fovDeg = 65.f;
        float cameraState = 0.f;
        if (pcam) {
            fovDeg = pcam->worldFOV;
            cameraState = static_cast<float>(pcam->currentState ?
                pcam->currentState->id : 0);
        }

        // ── Camera world position from NiCamera ─────────────────────────
        auto* niCam = RE::Main::WorldRootCamera();
        if (niCam) {
            auto& camPos = niCam->world.translate;
            data.WorldPos = { camPos.x, camPos.y, camPos.z, cameraState };
        }

        // ── View rotation 3x3 + projection params from NiCamera ────────
        if (niCam) {
            const auto& rd = niCam->GetRuntimeData();
            const auto& rd2 = niCam->GetRuntimeData2();

            // worldToCam[4][4] — extract only the 3x3 rotation (rows 0-2, cols 0-2)
            const auto& m = rd.worldToCam;
            data.ViewRow0 = { m[0][0], m[0][1], m[0][2], 0.f };
            data.ViewRow1 = { m[1][0], m[1][1], m[1][2], 0.f };
            data.ViewRow2 = { m[2][0], m[2][1], m[2][2], 0.f };

            // Near/far from viewFrustum
            float n = rd2.viewFrustum.fNear;
            float f = rd2.viewFrustum.fFar;

            // Aspect ratio from viewport
            const auto& port = rd2.port;
            float portW = port.GetWidth();
            float portH = port.GetHeight();
            float aspect = (portH > 0.f) ? portW / portH : 1.f;

            // FOV in RADIANS (Marty: "FOV in rad (!)")
            float fovRad = fovDeg * 0.0174532925f;  // deg → rad

            data.Params = { fovRad, n, f, aspect };
        }

        // ── Previous frame data (for motion vectors / temporal) ─────────
        if (s_hasPrev) {
            data.PrevWorldPos = s_prevWorldPos;
            data.PrevWorldPos.w = s_prevFOV;  // pack prev FOV into .w
            data.PrevViewRow0 = s_prevViewRow0;
            data.PrevViewRow1 = s_prevViewRow1;
        } else {
            // First frame: prev = current (no motion)
            data.PrevWorldPos = data.WorldPos;
            data.PrevWorldPos.w = data.Params.x;
            data.PrevViewRow0 = data.ViewRow0;
            data.PrevViewRow1 = data.ViewRow1;
        }

        // Store current for next frame
        s_prevWorldPos = data.WorldPos;
        s_prevViewRow0 = data.ViewRow0;
        s_prevViewRow1 = data.ViewRow1;
        s_prevFOV = data.Params.x;
        s_hasPrev = true;

        return data;
    }
}
