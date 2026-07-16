#pragma once
//=============================================================================
//  SceneMatrices — Reconstructed scene matrices for GPU rendering systems
//
//  Updated once per frame from CameraTracker + CelestialTracker data.
//  Provides full 4x4 matrices, camera position, sun direction, and
//  clip planes that rendering systems (SSGI, VolumetricClouds, TSR, etc.)
//  need for correct 3D transforms.
//
//  Data flow:
//    Trackers → AllData → SceneMatrices::Update(data) → rendering systems read
//=============================================================================

#include "BridgeData.h"
#include <cstdint>

namespace SB
{

class SceneMatrices
{
public:
    static SceneMatrices& Get()
    {
        static SceneMatrices inst;
        return inst;
    }

    // Call once per frame after all trackers have updated.
    // Reconstructs full 4x4 matrices from the minimal CameraData.
    void Update(const AllData& data);

    /// Lightweight mid-frame update: reads NiCamera + RE::Sky directly.
    /// Called by PhaseDispatcher before dispatching PostGeometry/PostSky/PreUI
    /// so that effects have live camera data during game rendering.
    /// Does NOT update PrevViewProj (uses last frame's value from Update()).
    /// Does NOT touch CameraTracker statics (no temporal side effects).
    void UpdateFromNiCamera();

    // ── Camera matrices (row-major float[16]) ─────────────────────────
    const float* ViewMatrix()        const { return m_view; }
    const float* ProjMatrix()        const { return m_proj; }
    const float* ViewProjMatrix()    const { return m_viewProj; }
    const float* InvViewProjMatrix() const { return m_invViewProj; }
    const float* PrevViewProjMatrix()const { return m_prevViewProj; }

    // ── Camera position + clip planes ─────────────────────────────────
    float CameraPosX() const { return m_cameraPos[0]; }
    float CameraPosY() const { return m_cameraPos[1]; }
    float CameraPosZ() const { return m_cameraPos[2]; }
    const float* CameraPos() const { return m_cameraPos; }
    float NearClip()   const { return m_nearClip; }
    float FarClip()    const { return m_farClip; }
    float FOV()        const { return m_fov; }
    float AspectRatio()const { return m_aspect; }

    // ── Sun / lighting ────────────────────────────────────────────────
    const float* SunDirection() const { return m_sunDir; }
    const float* SunColor()     const { return m_sunColor; }

    // ── Frame state ───────────────────────────────────────────────────
    uint32_t FrameIndex() const { return m_frameIndex; }

    // True after Update() succeeds with valid camera data.
    // Compute renderers MUST check this before dispatching.
    bool IsValid() const { return m_valid; }

private:
    SceneMatrices() = default;

    // 4x4 matrices in row-major order (M[row][col] = m[row*4+col])
    float m_view[16]        = {};
    float m_proj[16]        = {};
    float m_viewProj[16]    = {};
    float m_invViewProj[16] = {};
    float m_prevViewProj[16]= {};

    float m_cameraPos[3] = {};
    float m_nearClip = 5.0f;
    float m_farClip  = 100000.0f;
    float m_fov      = 1.13f;
    float m_aspect   = 1.777f;

    float m_sunDir[3]   = {0.f, 1.f, 0.f};
    float m_sunColor[3] = {1.f, 1.f, 1.f};

    uint32_t m_frameIndex = 0;
    bool     m_valid      = false;
};

} // namespace SB
