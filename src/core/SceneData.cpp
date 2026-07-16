#include "SceneData.h"
#include <RE/Skyrim.h>
#include <cmath>
#include <cstring>

namespace SB
{

// ── Matrix helpers (row-major float[16]) ────────────────────────────────

static void MatIdentity(float* m)
{
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void MatMultiply(float* out, const float* a, const float* b)
{
    float tmp[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            tmp[r * 4 + c] =
                a[r * 4 + 0] * b[0 * 4 + c] +
                a[r * 4 + 1] * b[1 * 4 + c] +
                a[r * 4 + 2] * b[2 * 4 + c] +
                a[r * 4 + 3] * b[3 * 4 + c];
        }
    }
    std::memcpy(out, tmp, sizeof(tmp));
}

// Invert a 4x4 matrix via cofactors (general case).
// Returns false if singular.
static bool MatInverse(float* out, const float* m)
{
    float inv[16];

    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15]
           + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];

    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15]
           - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];

    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15]
           + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];

    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14]
            - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];

    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15]
           - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];

    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15]
           + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];

    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15]
           - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];

    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14]
            + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];

    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15]
           + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];

    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15]
           - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];

    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15]
            + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];

    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14]
            - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];

    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11]
           - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];

    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11]
           + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];

    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11]
            - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];

    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10]
            + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (std::abs(det) < 1e-12f)
        return false;

    float invDet = 1.0f / det;
    for (int i = 0; i < 16; ++i)
        out[i] = inv[i] * invDet;
    return true;
}

// Build View matrix from 3x3 rotation rows + camera world position.
// Row-major: V = [R | -R*t ; 0 0 0 1]
static void BuildViewMatrix(float* view,
                             const Float4& row0, const Float4& row1, const Float4& row2,
                             const Float4& worldPos)
{
    // Rotation part
    view[0] = row0.x;  view[1] = row0.y;  view[2] = row0.z;
    view[4] = row1.x;  view[5] = row1.y;  view[6] = row1.z;
    view[8] = row2.x;  view[9] = row2.y;  view[10]= row2.z;

    // Translation: -R * t
    view[3]  = -(row0.x * worldPos.x + row0.y * worldPos.y + row0.z * worldPos.z);
    view[7]  = -(row1.x * worldPos.x + row1.y * worldPos.y + row1.z * worldPos.z);
    view[11] = -(row2.x * worldPos.x + row2.y * worldPos.y + row2.z * worldPos.z);

    // Bottom row
    view[12] = 0.f;  view[13] = 0.f;  view[14] = 0.f;  view[15] = 1.f;
}

// Standard perspective projection (non-reversed Z, row-major).
// Skyrim uses standard depth: near=0, far=1.
static void BuildProjMatrix(float* proj, float fovRad, float aspect, float nearZ, float farZ)
{
    std::memset(proj, 0, 16 * sizeof(float));

    float tanHalfFov = std::tan(fovRad * 0.5f);
    if (tanHalfFov < 1e-6f) tanHalfFov = 1e-6f;

    float h = 1.0f / tanHalfFov;
    float w = h / aspect;
    float range = farZ - nearZ;
    if (std::abs(range) < 1e-6f) range = 1e-6f;

    proj[0]  = w;
    proj[5]  = h;
    proj[10] = farZ / range;
    proj[11] = -(nearZ * farZ) / range;
    proj[14] = 1.0f;
    // proj[15] = 0 (perspective)
}

// ── Helpers ──────────────────────────────────────────────────────────────

static bool IsFinite(float v) { return std::isfinite(v); }

static float Vec3LenSq(const Float4& v)
{
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

// Returns true if the view rotation rows look like a valid orthonormal basis.
// Checks: each row has non-zero length, all elements are finite.
static bool ValidateViewRows(const Float4& r0, const Float4& r1, const Float4& r2)
{
    if (!IsFinite(r0.x) || !IsFinite(r0.y) || !IsFinite(r0.z)) return false;
    if (!IsFinite(r1.x) || !IsFinite(r1.y) || !IsFinite(r1.z)) return false;
    if (!IsFinite(r2.x) || !IsFinite(r2.y) || !IsFinite(r2.z)) return false;

    float len0 = Vec3LenSq(r0);
    float len1 = Vec3LenSq(r1);
    float len2 = Vec3LenSq(r2);

    // Each row should be roughly unit length (0.5 .. 2.0 squared = 0.25 .. 4.0)
    if (len0 < 0.25f || len0 > 4.0f) return false;
    if (len1 < 0.25f || len1 > 4.0f) return false;
    if (len2 < 0.25f || len2 > 4.0f) return false;

    return true;
}

// ── Update ──────────────────────────────────────────────────────────────

void SceneMatrices::Update(const AllData& data)
{
    ++m_frameIndex;

    const auto& cam = data.camera;
    const auto& cel = data.celestial;
    const auto& shd = data.shadow;

    // ── Validate camera rotation rows before anything else ──────────────
    // If camera data is uninitialized (loading screen, early startup),
    // the view rows will be zero or garbage. Bail out and keep m_valid=false
    // so compute renderers skip their dispatch.
    if (!ValidateViewRows(cam.ViewRow0, cam.ViewRow1, cam.ViewRow2)) {
        m_valid = false;
        return;
    }

    // ── Camera parameters ───────────────────────────────────────────────
    m_cameraPos[0] = cam.WorldPos.x;
    m_cameraPos[1] = cam.WorldPos.y;
    m_cameraPos[2] = cam.WorldPos.z;

    // Reject non-finite camera position
    if (!IsFinite(m_cameraPos[0]) || !IsFinite(m_cameraPos[1]) || !IsFinite(m_cameraPos[2])) {
        m_valid = false;
        return;
    }

    m_fov     = cam.Params.x;       // radians
    m_nearClip = cam.Params.y;
    m_farClip  = cam.Params.z;
    m_aspect   = cam.Params.w;

    // Sanity clamps
    if (!IsFinite(m_nearClip) || m_nearClip <= 0.f) m_nearClip = 1.0f;
    if (!IsFinite(m_farClip) || m_farClip <= m_nearClip) m_farClip = m_nearClip + 100000.f;
    if (!IsFinite(m_fov) || m_fov <= 0.f) m_fov = 1.13f;
    if (!IsFinite(m_aspect) || m_aspect <= 0.f) m_aspect = 1.777f;

    // ── Build current View matrix ───────────────────────────────────────
    BuildViewMatrix(m_view, cam.ViewRow0, cam.ViewRow1, cam.ViewRow2, cam.WorldPos);

    // ── Build Projection matrix ─────────────────────────────────────────
    BuildProjMatrix(m_proj, m_fov, m_aspect, m_nearClip, m_farClip);

    // ── ViewProj = View * Proj ──────────────────────────────────────────
    MatMultiply(m_viewProj, m_view, m_proj);

    // ── InvViewProj ─────────────────────────────────────────────────────
    if (!MatInverse(m_invViewProj, m_viewProj))
        MatIdentity(m_invViewProj);

    // ── PrevViewProj from previous frame camera data ────────────────────
    // Reconstruct previous View from PrevViewRow0/1 + derived row2 + PrevWorldPos
    {
        Float4 prevRow0 = cam.PrevViewRow0;
        Float4 prevRow1 = cam.PrevViewRow1;

        // Derive prevRow2 = cross(prevRow0, prevRow1)
        Float4 prevRow2;
        prevRow2.x = prevRow0.y * prevRow1.z - prevRow0.z * prevRow1.y;
        prevRow2.y = prevRow0.z * prevRow1.x - prevRow0.x * prevRow1.z;
        prevRow2.z = prevRow0.x * prevRow1.y - prevRow0.y * prevRow1.x;

        Float4 prevPos;
        prevPos.x = cam.PrevWorldPos.x;
        prevPos.y = cam.PrevWorldPos.y;
        prevPos.z = cam.PrevWorldPos.z;

        float prevFov = cam.PrevWorldPos.w;
        if (prevFov <= 0.f) prevFov = m_fov;

        float prevView[16];
        BuildViewMatrix(prevView, prevRow0, prevRow1, prevRow2, prevPos);

        float prevProj[16];
        BuildProjMatrix(prevProj, prevFov, m_aspect, m_nearClip, m_farClip);

        MatMultiply(m_prevViewProj, prevView, prevProj);
    }

    // ── Sun direction + color ───────────────────────────────────────────
    m_sunDir[0] = cel.SunDirection.x;
    m_sunDir[1] = cel.SunDirection.y;
    m_sunDir[2] = cel.SunDirection.z;

    // Normalize (should already be, but safety)
    float len = std::sqrt(m_sunDir[0]*m_sunDir[0] + m_sunDir[1]*m_sunDir[1] + m_sunDir[2]*m_sunDir[2]);
    if (len > 1e-6f) {
        m_sunDir[0] /= len;
        m_sunDir[1] /= len;
        m_sunDir[2] /= len;
    } else {
        m_sunDir[0] = 0.f;
        m_sunDir[1] = 1.f;
        m_sunDir[2] = 0.f;
    }

    m_sunColor[0] = cel.SunColor.x;
    m_sunColor[1] = cel.SunColor.y;
    m_sunColor[2] = cel.SunColor.z;

    // All matrices built successfully with validated inputs
    m_valid = true;
}

// ── Mid-frame update from NiCamera ──────────────────────────────────────
// Called by PhaseDispatcher during game rendering (PostGeometry, etc.)
// so that GPU effects have live camera matrices at mid-frame.
//
// Reads directly from RE::NiCamera and RE::Sky — no tracker dependency.
// PrevViewProj is NOT updated (retains last-frame value from Update()).

void SceneMatrices::UpdateFromNiCamera()
{
    auto* niCam = RE::Main::WorldRootCamera();
    if (!niCam) return;

    const auto& rd  = niCam->GetRuntimeData();
    const auto& rd2 = niCam->GetRuntimeData2();

    // Extract view rotation rows from worldToCam
    Float4 row0 = { rd.worldToCam[0][0], rd.worldToCam[0][1], rd.worldToCam[0][2], 0.f };
    Float4 row1 = { rd.worldToCam[1][0], rd.worldToCam[1][1], rd.worldToCam[1][2], 0.f };
    Float4 row2 = { rd.worldToCam[2][0], rd.worldToCam[2][1], rd.worldToCam[2][2], 0.f };

    if (!ValidateViewRows(row0, row1, row2)) {
        m_valid = false;
        return;
    }

    // Camera position
    auto& pos = niCam->world.translate;
    m_cameraPos[0] = pos.x;
    m_cameraPos[1] = pos.y;
    m_cameraPos[2] = pos.z;

    if (!IsFinite(pos.x) || !IsFinite(pos.y) || !IsFinite(pos.z)) {
        m_valid = false;
        return;
    }

    // Projection parameters
    auto* pcam = RE::PlayerCamera::GetSingleton();
    float fovDeg = pcam ? pcam->worldFOV : 65.f;
    float fovRad = fovDeg * 0.0174532925f;

    float n = rd2.viewFrustum.fNear;
    float f = rd2.viewFrustum.fFar;
    const auto& port = rd2.port;
    float portW = port.GetWidth();
    float portH = port.GetHeight();
    float aspect = (portH > 0.f) ? portW / portH : 1.777f;

    // Sanity clamps
    if (!IsFinite(n) || n <= 0.f) n = 1.0f;
    if (!IsFinite(f) || f <= n) f = n + 100000.f;
    if (!IsFinite(fovRad) || fovRad <= 0.f) fovRad = 1.13f;
    if (!IsFinite(aspect) || aspect <= 0.f) aspect = 1.777f;

    m_fov      = fovRad;
    m_nearClip = n;
    m_farClip  = f;
    m_aspect   = aspect;

    // Build View and Projection
    Float4 worldPos = { pos.x, pos.y, pos.z, 0.f };
    BuildViewMatrix(m_view, row0, row1, row2, worldPos);
    BuildProjMatrix(m_proj, m_fov, m_aspect, m_nearClip, m_farClip);

    // ViewProj = View * Proj
    MatMultiply(m_viewProj, m_view, m_proj);

    // InvViewProj
    if (!MatInverse(m_invViewProj, m_viewProj))
        MatIdentity(m_invViewProj);

    // PrevViewProj: keep whatever was set by last full Update() — correct for temporal effects

    // Sun direction from RE::Sky
    auto* sky = RE::Sky::GetSingleton();
    if (sky && sky->sun) {
        auto* sunLight = reinterpret_cast<RE::NiLight*>(sky->sun->light.get());
        if (sunLight) {
            auto& sunDir = sunLight->world.rotate;
            // Sun direction is the forward axis of the directional light
            m_sunDir[0] = sunDir.entry[0][2];
            m_sunDir[1] = sunDir.entry[1][2];
            m_sunDir[2] = sunDir.entry[2][2];

            float len = std::sqrt(m_sunDir[0]*m_sunDir[0] + m_sunDir[1]*m_sunDir[1] + m_sunDir[2]*m_sunDir[2]);
            if (len > 1e-6f) {
                m_sunDir[0] /= len;
                m_sunDir[1] /= len;
                m_sunDir[2] /= len;
            }
        }
    }

    ++m_frameIndex;
    m_valid = true;
}

} // namespace SB
