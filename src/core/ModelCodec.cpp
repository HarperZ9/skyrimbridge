//=============================================================================
//  ModelCodec.cpp — foreign static mesh (OBJ / glTF) -> Skyrim SE NIF
//
//  The NIF byte layout reproduces a real shipping SSE static mesh exactly
//  (BSFadeNode -> BSTriShape + BSLightingShaderProperty + BSShaderTextureSet,
//  vertexDesc 0x0003B00007650408, 32-byte full-precision vertex), verified
//  field-by-field against ground truth, so the output is engine-loadable by
//  construction. Validated offline by tests/validate_model_codec.py.
//=============================================================================

#include "ModelCodec.h"
#include "ConvexHull.h"
#include "CompressedMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <unordered_map>

namespace SB::ModelCodec
{
    // ── half float ───────────────────────────────────────────────────────
    std::uint16_t FloatToHalf(float f)
    {
        std::uint32_t x; std::memcpy(&x, &f, 4);
        std::uint32_t sign = (x >> 16) & 0x8000u;
        std::int32_t  exp  = static_cast<std::int32_t>((x >> 23) & 0xFF) - 127 + 15;
        std::uint32_t mant = x & 0x7FFFFF;
        if (((x >> 23) & 0xFF) == 0xFF)                       // Inf / NaN
            return static_cast<std::uint16_t>(sign | 0x7C00u | (mant ? 0x200u : 0));
        if (exp >= 0x1F) return static_cast<std::uint16_t>(sign | 0x7C00u);   // overflow -> Inf
        if (exp <= 0) {                                       // subnormal / zero
            if (exp < -10) return static_cast<std::uint16_t>(sign);
            mant |= 0x800000;
            int shift = 14 - exp;
            std::uint32_t half = mant >> shift;
            if ((mant >> (shift - 1)) & 1) ++half;            // round to nearest
            return static_cast<std::uint16_t>(sign | half);
        }
        std::uint16_t half = static_cast<std::uint16_t>(sign | (exp << 10) | (mant >> 13));
        if (mant & 0x1000) ++half;                            // round to nearest even-ish
        return half;
    }

    float HalfToFloat(std::uint16_t h)
    {
        std::uint32_t sign = (h & 0x8000u) << 16;
        std::uint32_t exp  = (h >> 10) & 0x1F;
        std::uint32_t mant = h & 0x3FF;
        std::uint32_t out;
        if (exp == 0) {
            if (mant == 0) out = sign;
            else {
                exp = 127 - 15 + 1;
                while (!(mant & 0x400)) { mant <<= 1; --exp; }
                mant &= 0x3FF;
                out = sign | (exp << 23) | (mant << 13);
            }
        } else if (exp == 0x1F) {
            out = sign | 0x7F800000u | (mant << 13);
        } else {
            out = sign | ((exp + 127 - 15) << 23) | (mant << 13);
        }
        float f; std::memcpy(&f, &out, 4); return f;
    }

    // ── little-endian writers ────────────────────────────────────────────
    namespace
    {
        struct Buf
        {
            std::vector<std::uint8_t> v;
            void u8(std::uint8_t x) { v.push_back(x); }
            void u16(std::uint16_t x) { v.push_back(x & 0xFF); v.push_back(x >> 8); }
            void u32(std::uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xFF); }
            void i32(std::int32_t x) { u32(static_cast<std::uint32_t>(x)); }
            void u64(std::uint64_t x) { for (int i = 0; i < 8; ++i) v.push_back((x >> (8 * i)) & 0xFF); }
            void f32(float x) { std::uint32_t b; std::memcpy(&b, &x, 4); u32(b); }
            void raw(const void* p, std::size_t n) { auto* b = static_cast<const std::uint8_t*>(p); v.insert(v.end(), b, b + n); }
            void sized(const std::string& s) { u32(static_cast<std::uint32_t>(s.size())); raw(s.data(), s.size()); }
            void shortstr(const std::string& s) { u8(static_cast<std::uint8_t>(s.size())); raw(s.data(), s.size()); }
        };

        Vec3 sub(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        Vec3 cross(const Vec3& a, const Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
        float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        Vec3 norm(const Vec3& a) { float l = std::sqrt(dot(a, a)); return l > 1e-12f ? Vec3{ a.x / l, a.y / l, a.z / l } : Vec3{ 0, 0, 1 }; }

        std::uint8_t packSnorm(float v) { return static_cast<std::uint8_t>(std::clamp((v + 1.0f) * 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f); }
    }

    // ── normals + tangents ───────────────────────────────────────────────
    void FinalizeMesh(Mesh& m)
    {
        const std::size_t nv = m.positions.size();
        if (m.normals.size() != nv) {
            m.normals.assign(nv, { 0, 0, 0 });
            for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
                std::uint32_t a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
                if (a >= nv || b >= nv || c >= nv) continue;
                Vec3 fn = cross(sub(m.positions[b], m.positions[a]), sub(m.positions[c], m.positions[a]));
                for (auto idx : { a, b, c }) { m.normals[idx].x += fn.x; m.normals[idx].y += fn.y; m.normals[idx].z += fn.z; }
            }
            for (auto& n : m.normals) n = norm(n);
        }
        if (m.uvs.size() != nv) m.uvs.assign(nv, { 0, 0 });
    }

    // Per-vertex tangent (Lengyel) + bitangent sign, aligned to the stored normal.
    static void ComputeTangents(const Mesh& m, std::vector<Vec3>& tan, std::vector<float>& sign)
    {
        const std::size_t nv = m.positions.size();
        std::vector<Vec3> tanAcc(nv, { 0, 0, 0 }), bitAcc(nv, { 0, 0, 0 });
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            std::uint32_t a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
            if (a >= nv || b >= nv || c >= nv) continue;
            Vec3 e1 = sub(m.positions[b], m.positions[a]), e2 = sub(m.positions[c], m.positions[a]);
            float du1 = m.uvs[b].x - m.uvs[a].x, dv1 = m.uvs[b].y - m.uvs[a].y;
            float du2 = m.uvs[c].x - m.uvs[a].x, dv2 = m.uvs[c].y - m.uvs[a].y;
            float d = du1 * dv2 - du2 * dv1;
            float r = std::fabs(d) > 1e-12f ? 1.0f / d : 0.0f;
            Vec3 t{ (e1.x * dv2 - e2.x * dv1) * r, (e1.y * dv2 - e2.y * dv1) * r, (e1.z * dv2 - e2.z * dv1) * r };
            Vec3 bt{ (e2.x * du1 - e1.x * du2) * r, (e2.y * du1 - e1.y * du2) * r, (e2.z * du1 - e1.z * du2) * r };
            for (auto idx : { a, b, c }) {
                tanAcc[idx].x += t.x; tanAcc[idx].y += t.y; tanAcc[idx].z += t.z;
                bitAcc[idx].x += bt.x; bitAcc[idx].y += bt.y; bitAcc[idx].z += bt.z;
            }
        }
        tan.resize(nv); sign.resize(nv);
        for (std::size_t i = 0; i < nv; ++i) {
            Vec3 n = m.normals[i];
            Vec3 t = tanAcc[i];
            // Gram-Schmidt orthonormalize against the normal.
            float nd = dot(n, t);
            t = { t.x - n.x * nd, t.y - n.y * nd, t.z - n.z * nd };
            if (dot(t, t) < 1e-12f) {                          // degenerate UVs: arbitrary basis
                Vec3 ax = std::fabs(n.x) < 0.9f ? Vec3{ 1, 0, 0 } : Vec3{ 0, 1, 0 };
                t = norm(cross(n, ax));
            } else {
                t = norm(t);
            }
            tan[i] = t;
            sign[i] = (dot(cross(n, t), bitAcc[i]) < 0.0f) ? -1.0f : 1.0f;
        }
    }

    // ── NIF writer ───────────────────────────────────────────────────────
    std::vector<std::uint8_t> WriteNIF(const Mesh& mesh, bool treeMode, bool collision,
                                       int collisionPieces, std::uint32_t collisionMaterial,
                                       bool meshCollision)
    {
        Mesh m = mesh;
        // 0 = the shipped default (WOOD, 0x1DD9C611). That hash is
        // SKY_HAV_MAT_WOOD, not stone (an earlier comment mislabeled it).
        const std::uint32_t collMat = collisionMaterial ? collisionMaterial : 0x1DD9C611u;
        FinalizeMesh(m);
        const std::size_t nv = m.positions.size();
        const std::size_t nt = m.indices.size() / 3;
        if (nv == 0 || nt == 0 || nv > 0xFFFF || nt > 0xFFFF) return {};   // u16 limits (honest null: no split)

        std::vector<Vec3> tan; std::vector<float> sign;
        ComputeTangents(m, tan, sign);

        // Bounding sphere: AABB center + max radius.
        Vec3 lo = m.positions[0], hi = m.positions[0];
        for (auto& p : m.positions) {
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
        }
        Vec3 center{ (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f };
        float radius = 0;
        for (auto& p : m.positions) { Vec3 d = sub(p, center); radius = std::max(radius, std::sqrt(dot(d, d))); }

        // Tree mode: procedural wind weights. Derived empirically from real
        // animated tree shapes (Aspens Ablaze, vanilla Dawnguard glade tree;
        // receipt: tests/validate_tree_wind.py): the engine's Tree_Anim
        // vertex shader reads the vertex color, grayscale R=G=B, as the sway
        // weight. Real assets sit at 127 on near-rigid geometry and rise to
        // 255 at canopy extremities (value grows with height AND radial
        // distance from the trunk). We reproduce that with distance from the
        // trunk base (horizontal center at the lowest point), eased so the
        // lower trunk stays stiff. Alpha stays 255, the vanilla-accepted
        // constant (aspens ship a constant 68; its semantics are an honest
        // null, unrecovered).
        std::vector<std::uint8_t> wind;
        if (treeMode) {
            const Vec3 base{ center.x, center.y, lo.z };
            float maxd = 1e-6f;
            for (auto& p : m.positions) {
                Vec3 d = sub(p, base);
                maxd = std::max(maxd, std::sqrt(dot(d, d)));
            }
            wind.reserve(nv);
            for (auto& p : m.positions) {
                Vec3 d = sub(p, base);
                float e = std::sqrt(dot(d, d)) / maxd;          // 0 base .. 1 tip
                float w = 0.5f + 0.5f * std::pow(e, 1.5f);      // 127 .. 255, stiff low trunk
                wind.push_back(static_cast<std::uint8_t>(w * 255.0f + 0.5f));
            }
        }

        // ── collision (F18/F19 + decomposition) ──────────────────────────
        // One convex hull (F19), or a bhkListShape of convex pieces from an
        // approximate convex decomposition when collisionPieces >= 2, which
        // approximates concavity a single hull cannot
        // (docs/COLLISION-INVESTIGATION-F18.md). Degenerate pieces are
        // dropped; no valid piece -> no collision.
        struct HullPiece { std::vector<Vec3> verts; std::vector<std::array<float, 4>> planes; };
        std::vector<HullPiece> pieces;

        // Exact mesh-collision mode: emit a bhkCompressedMeshShapeData chain
        // instead of convex. A CMSD alone does not collide; the chain ships
        // with an EMPTY MOPP placeholder and needs NifSkope "Update MOPP
        // Code" to finalize before in-game use (docs/MOPP-INVESTIGATION.md,
        // docs/CMSD-FORMAT.md). File-output only, never live spawn.
        std::vector<std::uint8_t> cmsdBytes;
        bool meshColl = false;
        if (collision && meshCollision) {
            cmsdBytes = CompressedMesh::Build(m, collMat);
            meshColl = !cmsdBytes.empty();
        }

        if (collision && !meshCollision) {
            std::vector<std::vector<Vec3>> groups;
            if (collisionPieces >= 2) {
                // Densify so large flat triangles do not leave seams between
                // pieces; cap the cloud to keep decomposition tractable
                // (this runs on the frame thread for SpawnModel).
                std::vector<Vec3> cloud = m.positions;
                const bool dense = (nv + nt * 4) <= 20000;
                if (dense) cloud.reserve(nv + nt * 4);
                for (std::size_t i = 0; dense && i < nt; ++i) {
                    const Vec3& a = m.positions[m.indices[i * 3 + 0]];
                    const Vec3& b = m.positions[m.indices[i * 3 + 1]];
                    const Vec3& c = m.positions[m.indices[i * 3 + 2]];
                    cloud.push_back({ (a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3 });
                    cloud.push_back({ (a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2 });
                    cloud.push_back({ (b.x + c.x) / 2, (b.y + c.y) / 2, (b.z + c.z) / 2 });
                    cloud.push_back({ (c.x + a.x) / 2, (c.y + a.y) / 2, (c.z + a.z) / 2 });
                }
                Hull::ConvexDecompose(cloud, collisionPieces, 0.05, groups);
            } else {
                groups.push_back(m.positions);
            }
            for (auto& g : groups) {
                HullPiece hp;
                if (Hull::ConvexHull(g, hp.verts, hp.planes))
                    pieces.push_back(std::move(hp));
            }
        }
        const int nPieces = static_cast<int>(pieces.size());
        const bool haveCollision = meshColl || nPieces > 0;
        const bool useList = nPieces >= 2;
        const int convexBase = 4;
        // convex layout: [convex xN][list?][rigid][collObj][bsx]
        // mesh   layout: [cmsd][cms][mopp][rigid][collObj][bsx]
        const int cmsdIdx = 4, cmsIdx = 5, moppIdx = 6;
        const int listIdx    = useList ? convexBase + nPieces : -1;
        const int rigidIdx   = meshColl ? convexBase + 3
                                        : convexBase + nPieces + (useList ? 1 : 0);
        const int collObjIdx = rigidIdx + 1;
        const int bsxIdx     = collObjIdx + 1;

        // ── block bodies ──────────────────────────────────────────────────
        const std::int32_t ROOT_NAME = 0, SHAPE_NAME = 1;

        Buf b0;   // root node
        b0.i32(ROOT_NAME);
        b0.u32(haveCollision ? 1 : 0);                       // extra data list
        if (haveCollision) b0.i32(bsxIdx);                   //   -> BSXFlags
        b0.i32(-1);                                          // controller
        b0.u32(0x0000000E);                                  // flags
        b0.f32(0); b0.f32(0); b0.f32(0);                     // translation
        b0.f32(1); b0.f32(0); b0.f32(0); b0.f32(0); b0.f32(1); b0.f32(0); b0.f32(0); b0.f32(0); b0.f32(1);  // rotation (identity)
        b0.f32(1.0f);                                        // scale
        b0.i32(haveCollision ? collObjIdx : -1);             // collision -> bhkCollisionObject
        b0.u32(1); b0.i32(1);                                // 1 child -> block 1
        b0.u32(0);                                           // 0 effects

        Buf b1;   // BSTriShape
        b1.i32(SHAPE_NAME); b1.u32(0); b1.i32(-1);
        b1.u32(0x0000000E);
        b1.f32(0); b1.f32(0); b1.f32(0);
        b1.f32(1); b1.f32(0); b1.f32(0); b1.f32(0); b1.f32(1); b1.f32(0); b1.f32(0); b1.f32(0); b1.f32(1);
        b1.f32(1.0f);
        b1.i32(-1);                                          // collision
        b1.f32(center.x); b1.f32(center.y); b1.f32(center.z); b1.f32(radius);   // bounding sphere
        b1.i32(-1);                                          // skin
        b1.i32(2);                                           // shaderProperty -> block 2
        b1.i32(-1);                                          // alphaProperty
        b1.u64(0x0003B00007650408ull);                      // vertexDesc (VERTEX|UV|NORMAL|TANGENT|COLOR, full precision, 32B)
        b1.u16(static_cast<std::uint16_t>(nt));
        b1.u16(static_cast<std::uint16_t>(nv));
        b1.u32(static_cast<std::uint32_t>(nv * 32 + nt * 6));   // dataSize
        for (std::size_t i = 0; i < nv; ++i) {
            const Vec3& p = m.positions[i];
            const Vec3& n = m.normals[i];
            const Vec3& t = tan[i];
            Vec3 bit = cross(n, t);
            bit = { bit.x * sign[i], bit.y * sign[i], bit.z * sign[i] };
            b1.f32(p.x); b1.f32(p.y); b1.f32(p.z);          // position @0
            b1.f32(bit.x);                                   // bitangent.x @12
            b1.u16(FloatToHalf(m.uvs[i].x)); b1.u16(FloatToHalf(m.uvs[i].y));   // UV @16
            b1.u8(packSnorm(n.x)); b1.u8(packSnorm(n.y)); b1.u8(packSnorm(n.z)); b1.u8(packSnorm(bit.y));   // normal + bit.y @20
            b1.u8(packSnorm(t.x)); b1.u8(packSnorm(t.y)); b1.u8(packSnorm(t.z)); b1.u8(packSnorm(bit.z));   // tangent + bit.z @24
            const std::uint8_t vc = treeMode ? wind[i] : 255;   // vertex color @28: wind weight in tree mode
            b1.u8(vc); b1.u8(vc); b1.u8(vc); b1.u8(255);
        }
        for (std::size_t i = 0; i < nt; ++i) {
            b1.u16(static_cast<std::uint16_t>(m.indices[i * 3 + 0]));
            b1.u16(static_cast<std::uint16_t>(m.indices[i * 3 + 1]));
            b1.u16(static_cast<std::uint16_t>(m.indices[i * 3 + 2]));
        }
        b1.u32(0);   // particleDataSize trailer (0 for static; matches real SSE BSTriShape byte-for-byte)

        Buf b2;   // BSLightingShaderProperty
        b2.u32(0);                                           // shaderType = Default
        b2.i32(-1); b2.u32(0); b2.i32(-1);                   // name, extra, controller
        b2.u32(0x82400301);                                  // SLSF1 (known-good opaque)
        b2.u32(treeMode ? 0x28008071u : 0x08008071u);        // SLSF2: vertex colors (+ Tree_Anim in tree mode)
        b2.f32(0); b2.f32(0);                                // uvOffset
        b2.f32(1); b2.f32(1);                                // uvScale
        b2.i32(3);                                           // textureSet -> block 3
        b2.f32(0); b2.f32(0); b2.f32(0);                     // emissive
        b2.f32(1.0f);                                        // emissiveMultiple
        b2.u32(3);                                           // textureClampMode (wrap S+T)
        b2.f32(1.0f);                                        // alpha
        b2.f32(0.0f);                                        // refractionStrength
        b2.f32(30.0f);                                       // glossiness
        b2.f32(1); b2.f32(1); b2.f32(1);                     // specularColor
        b2.f32(1.0f);                                        // specularStrength
        b2.f32(0.3f);                                        // lightingEffect1
        b2.f32(2.0f);                                        // lightingEffect2

        Buf b3;   // BSShaderTextureSet
        b3.u32(9);
        std::string slots[9] = { m.diffuse, m.normalMap, "", "", "", "", "", "", "" };
        for (auto& s : slots) b3.sized(s);

        // ── collision block bodies (present only when a piece hulled) ────
        // Layout recovered and verified against real MOPP-free convex NIFs
        // and a real bhkListShape (docs/COLLISION-INVESTIGATION-F18.md).
        // Vertices and plane offsets are in Havok units (game units / ~70).
        std::vector<Buf> convexBufs;
        Buf cmsdBuf, cmsBuf, moppBuf, listBuf, rigidBuf, collObjBuf, bsxBuf;
        if (haveCollision && !meshColl) {
            constexpr float kInvHavokScale = 1.0f / 69.99125f;
            for (auto& piece : pieces) {
                Buf cb;
                cb.u32(collMat);                             // SkyrimHavokMaterial (footstep/impact feel)
                cb.f32(0.05f);                               // convex radius
                for (int k = 0; k < 2; ++k) { cb.u32(0); cb.u32(0); cb.u32(0x80000000u); }
                cb.u32(static_cast<std::uint32_t>(piece.verts.size()));
                for (auto& v : piece.verts) {
                    cb.f32(v.x * kInvHavokScale); cb.f32(v.y * kInvHavokScale);
                    cb.f32(v.z * kInvHavokScale); cb.f32(0.0f);
                }
                cb.u32(static_cast<std::uint32_t>(piece.planes.size()));
                for (auto& p : piece.planes) {
                    cb.f32(p[0]); cb.f32(p[1]); cb.f32(p[2]);
                    cb.f32(p[3] * kInvHavokScale);
                }
                convexBufs.push_back(std::move(cb));
            }

            if (useList) {                                   // bhkListShape wrapping the pieces
                listBuf.u32(static_cast<std::uint32_t>(nPieces));
                for (int i = 0; i < nPieces; ++i) listBuf.i32(convexBase + i);
                listBuf.u32(collMat);                        // material
                for (int k = 0; k < 2; ++k) { listBuf.u32(0); listBuf.u32(0); listBuf.u32(0x80000000u); }
                listBuf.u32(static_cast<std::uint32_t>(nPieces));   // numInts == numSubShapes
                for (int i = 0; i < nPieces; ++i) listBuf.u32(0);   // per-child filters
            }
        }
        if (meshColl) {
            // Exact mesh-collision chain: CMSD (geometry) -> CMS (wrapper) ->
            // MOPP (empty placeholder). Layouts recovered byte-exact from
            // real files (docs/CMSD-FORMAT.md). The MOPP is EMPTY: finalize
            // with NifSkope "Update MOPP Code" before in-game use.
            cmsdBuf.raw(reinterpret_cast<const char*>(cmsdBytes.data()), cmsdBytes.size());

            cmsBuf.i32(0);                                   // target
            cmsBuf.u32(0);                                   // userData
            cmsBuf.f32(0.005f);                              // radius (corpus-invariant)
            cmsBuf.u32(0);                                   // per-mesh hash (unused by us)
            cmsBuf.f32(1); cmsBuf.f32(1); cmsBuf.f32(1); cmsBuf.f32(0);   // scale (1,1,1,0)
            cmsBuf.f32(0.005f);                              // radiusCopy
            cmsBuf.f32(1); cmsBuf.f32(1); cmsBuf.f32(1); cmsBuf.f32(0);   // scaleCopy
            cmsBuf.i32(cmsdIdx);                             // data -> CMSD

            moppBuf.i32(cmsIdx);                             // shape -> CMS
            moppBuf.u32(0); moppBuf.u32(0); moppBuf.u32(0);  // bvTree unknown ints
            moppBuf.f32(1.0f);                               // corpus-invariant 1.0
            moppBuf.u32(0);                                  // moppDataSize = 0 (EMPTY placeholder)
            moppBuf.f32(0); moppBuf.f32(0); moppBuf.f32(0);  // origin (NifSkope recomputes)
            moppBuf.f32(0);                                  // scale (NifSkope recomputes)
            moppBuf.u8(2);                                   // buildType = BUILD_NOT_SET
        }
        if (haveCollision) {

            // bhkRigidBody: byte template from a known-good layer-1 static
            // (deerskullstatic.nif; machine-extracted, fixed-motion body).
            static const std::uint8_t kRigidBodyTemplate[250] = {
                0x02,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x80,0x63,0x8B,0x1B,0x01,0x6F,0x6C,0x47,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x01,0x0A,0xFF,0xFF,
                0x6C,0x47,0x72,0x6F,0x01,0x00,0x00,0x00,0x80,0x63,0x8B,0x1B,0x00,0x00,0x00,0x00,
                0x01,0x6C,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x7F,0x3F,
                0x67,0x21,0xA2,0xB3,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xCC,0x3D,0x00,0x00,0x4C,0x3D,
                0x00,0x00,0x80,0x3F,0x00,0x00,0x80,0x3F,0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00,
                0xCD,0xCC,0xCC,0x3E,0xCD,0xCC,0xD0,0x42,0x5C,0x8F,0xFC,0x41,0x9A,0x99,0x19,0x3E,
                0x05,0x01,0x01,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
            };
            rigidBuf.raw(reinterpret_cast<const char*>(kRigidBodyTemplate), sizeof(kRigidBodyTemplate));
            const std::int32_t sref = meshColl ? moppIdx : (useList ? listIdx : convexBase);   // rigidbody.shape
            rigidBuf.v[0] = sref & 0xFF; rigidBuf.v[1] = (sref >> 8) & 0xFF;
            rigidBuf.v[2] = (sref >> 16) & 0xFF; rigidBuf.v[3] = (sref >> 24) & 0xFF;

            collObjBuf.i32(0);                               // target = root
            collObjBuf.u16(0x0081);                          // flags (observed on real statics)
            collObjBuf.i32(rigidIdx);                        // body -> bhkRigidBody

            bsxBuf.i32(2);                                   // BSXFlags name -> "BSX" (string 2)
            bsxBuf.u32(130);
        }

        // ── header (deduplicated block-type table) ───────────────────────
        std::vector<Buf*> blocks = { &b0, &b1, &b2, &b3 };
        std::vector<const char*> perBlockType = { treeMode ? "BSLeafAnimNode" : "BSFadeNode",
                                                  "BSTriShape", "BSLightingShaderProperty",
                                                  "BSShaderTextureSet" };
        if (meshColl) {
            blocks.push_back(&cmsdBuf); perBlockType.push_back("bhkCompressedMeshShapeData");
            blocks.push_back(&cmsBuf);  perBlockType.push_back("bhkCompressedMeshShape");
            blocks.push_back(&moppBuf); perBlockType.push_back("bhkMoppBvTreeShape");
        }
        for (auto& cb : convexBufs) { blocks.push_back(&cb); perBlockType.push_back("bhkConvexVerticesShape"); }
        if (useList) { blocks.push_back(&listBuf); perBlockType.push_back("bhkListShape"); }
        if (haveCollision) {
            blocks.push_back(&rigidBuf);   perBlockType.push_back("bhkRigidBody");
            blocks.push_back(&collObjBuf); perBlockType.push_back("bhkCollisionObject");
            blocks.push_back(&bsxBuf);     perBlockType.push_back("BSXFlags");
        }
        std::vector<std::string> typeList;
        std::vector<std::uint16_t> typeIndex;
        for (auto* t : perBlockType) {
            std::uint16_t ti = 0;
            for (; ti < typeList.size(); ++ti) if (typeList[ti] == t) break;
            if (ti == typeList.size()) typeList.push_back(t);
            typeIndex.push_back(ti);
        }

        Buf h;
        const std::string hdr = "Gamebryo File Format, Version 20.2.0.7";
        h.raw(hdr.data(), hdr.size()); h.u8(0x0A);
        h.u32(0x14020007);                                   // version
        h.u8(1);                                             // endian little
        h.u32(12);                                           // user version
        h.u32(static_cast<std::uint32_t>(blocks.size()));    // block count
        h.u32(100);                                          // BS version (SSE)
        h.shortstr(""); h.shortstr(""); h.shortstr("");      // author / process / export
        h.u16(static_cast<std::uint16_t>(typeList.size()));  // numBlockTypes
        for (auto& tn : typeList) h.sized(tn);
        for (auto ti : typeIndex) h.u16(ti);                 // per-block type index
        for (auto* bl : blocks) h.u32(static_cast<std::uint32_t>(bl->v.size()));   // block sizes
        std::vector<std::string> strs = { m.name.empty() ? "Scene Root" : (m.name + " Root"),
                                          m.name.empty() ? "Mesh" : m.name };
        if (haveCollision) strs.push_back("BSX");            // BSXFlags name (string index 2)
        std::uint32_t maxLen = 0;
        for (auto& s : strs) maxLen = std::max<std::uint32_t>(maxLen, static_cast<std::uint32_t>(s.size()));
        h.u32(static_cast<std::uint32_t>(strs.size()));      // numStrings
        h.u32(maxLen);
        for (auto& s : strs) h.sized(s);
        h.u32(0);                                            // numGroups

        std::vector<std::uint8_t> out = std::move(h.v);
        for (auto* bl : blocks) out.insert(out.end(), bl->v.begin(), bl->v.end());
        return out;
    }

    // ── OBJ front-end ────────────────────────────────────────────────────
    Mesh ParseOBJ(const std::string& text)
    {
        Mesh m;
        std::vector<Vec3> pos, nrm;
        std::vector<Vec2> uv;

        struct Key { int v, t, n; bool operator==(const Key& o) const { return v == o.v && t == o.t && n == o.n; } };
        struct KeyHash { std::size_t operator()(const Key& k) const { return (std::hash<int>()(k.v) * 73856093) ^ (std::hash<int>()(k.t) * 19349663) ^ (std::hash<int>()(k.n) * 83492791); } };
        std::unordered_map<Key, std::uint32_t, KeyHash> weld;

        auto resolve = [&](int idx, int count) { return idx > 0 ? idx - 1 : (idx < 0 ? count + idx : -1); };

        std::size_t i = 0;
        auto line = [&](std::string& out) {
            if (i >= text.size()) return false;
            std::size_t e = text.find('\n', i);
            if (e == std::string::npos) e = text.size();
            out.assign(text, i, e - i);
            if (!out.empty() && out.back() == '\r') out.pop_back();
            i = e + 1;
            return true;
        };

        std::string ln;
        while (line(ln)) {
            const char* s = ln.c_str();
            while (*s == ' ' || *s == '\t') ++s;
            if (s[0] == 'v' && s[1] == ' ') {
                Vec3 p; std::sscanf(s + 2, "%f %f %f", &p.x, &p.y, &p.z); pos.push_back(p);
            } else if (s[0] == 'v' && s[1] == 't') {
                Vec2 t{ 0, 0 }; std::sscanf(s + 3, "%f %f", &t.x, &t.y); t.y = 1.0f - t.y;   // OBJ->NIF V flip
                uv.push_back(t);
            } else if (s[0] == 'v' && s[1] == 'n') {
                Vec3 n; std::sscanf(s + 3, "%f %f %f", &n.x, &n.y, &n.z); nrm.push_back(n);
            } else if (s[0] == 'f' && (s[1] == ' ' || s[1] == '\t')) {
                std::vector<std::uint32_t> face;
                const char* p = s + 1;
                while (*p) {
                    while (*p == ' ' || *p == '\t') ++p;
                    if (!*p) break;
                    int vi = 0, ti = 0, ni = 0;
                    if (std::sscanf(p, "%d/%d/%d", &vi, &ti, &ni) == 3) {}
                    else if (std::sscanf(p, "%d//%d", &vi, &ni) == 2) { ti = 0; }
                    else if (std::sscanf(p, "%d/%d", &vi, &ti) == 2) { ni = 0; }
                    else if (std::sscanf(p, "%d", &vi) == 1) { ti = ni = 0; }
                    else break;
                    Key k{ resolve(vi, (int)pos.size()),
                           ti ? resolve(ti, (int)uv.size()) : -1,
                           ni ? resolve(ni, (int)nrm.size()) : -1 };
                    std::uint32_t out;
                    auto it = weld.find(k);
                    if (it != weld.end()) out = it->second;
                    else {
                        out = static_cast<std::uint32_t>(m.positions.size());
                        if (k.v < 0 || k.v >= (int)pos.size()) return m;   // malformed
                        m.positions.push_back(pos[k.v]);
                        m.uvs.push_back(k.t >= 0 && k.t < (int)uv.size() ? uv[k.t] : Vec2{ 0, 0 });
                        if (k.n >= 0 && k.n < (int)nrm.size()) m.normals.push_back(nrm[k.n]);
                        weld.emplace(k, out);
                    }
                    face.push_back(out);
                    while (*p && *p != ' ' && *p != '\t') ++p;
                }
                for (std::size_t f = 2; f < face.size(); ++f) {           // fan triangulation
                    m.indices.push_back(face[0]); m.indices.push_back(face[f - 1]); m.indices.push_back(face[f]);
                }
            }
        }
        if (m.normals.size() != m.positions.size()) m.normals.clear();    // partial normals -> recompute all
        m.valid = !m.positions.empty() && !m.indices.empty();
        return m;
    }

    // ── glTF front-end (minimal JSON + glTF/GLB) ─────────────────────────
    // Supports the common case: non-interleaved or strided float POSITION /
    // NORMAL / VEC2 TEXCOORD_0 and u16/u32 SCALAR indices, buffers from a GLB
    // BIN chunk, an external .bin, or a base64 data: URI. Honest nulls: Draco,
    // sparse accessors, multiple primitives (only the first is read).
    namespace
    {
        // Tiny JSON value (object/array/string/number/bool/null).
        struct JVal
        {
            enum T { Obj, Arr, Str, Num, Bool, Null } t = Null;
            std::unordered_map<std::string, JVal> obj;
            std::vector<JVal> arr;
            std::string str;
            double num = 0;
            bool b = false;
            const JVal* find(const std::string& k) const { auto it = obj.find(k); return it == obj.end() ? nullptr : &it->second; }
            int inum() const { return static_cast<int>(num); }
        };

        struct JParser
        {
            const char* p; const char* end; bool ok = true;
            void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
            JVal parse() { ws(); return value(); }
            JVal value()
            {
                ws();
                if (p >= end) { ok = false; return {}; }
                if (*p == '{') return object();
                if (*p == '[') return array();
                if (*p == '"') { JVal v; v.t = JVal::Str; v.str = string(); return v; }
                if (*p == 't') { p += 4; JVal v; v.t = JVal::Bool; v.b = true; return v; }
                if (*p == 'f') { p += 5; JVal v; v.t = JVal::Bool; v.b = false; return v; }
                if (*p == 'n') { p += 4; return {}; }
                JVal v; v.t = JVal::Num; v.num = number(); return v;
            }
            JVal object()
            {
                JVal v; v.t = JVal::Obj; ++p; ws();
                if (p < end && *p == '}') { ++p; return v; }
                while (p < end) {
                    ws(); std::string k = string(); ws();
                    if (p < end && *p == ':') ++p;
                    v.obj[k] = value(); ws();
                    if (p < end && *p == ',') { ++p; continue; }
                    if (p < end && *p == '}') { ++p; break; }
                    break;
                }
                return v;
            }
            JVal array()
            {
                JVal v; v.t = JVal::Arr; ++p; ws();
                if (p < end && *p == ']') { ++p; return v; }
                while (p < end) {
                    v.arr.push_back(value()); ws();
                    if (p < end && *p == ',') { ++p; continue; }
                    if (p < end && *p == ']') { ++p; break; }
                    break;
                }
                return v;
            }
            std::string string()
            {
                std::string s; ws();
                if (p >= end || *p != '"') { ok = false; return s; }
                ++p;
                while (p < end && *p != '"') {
                    if (*p == '\\' && p + 1 < end) {
                        ++p;
                        switch (*p) { case 'n': s += '\n'; break; case 't': s += '\t'; break;
                            case '/': s += '/'; break; case '\\': s += '\\'; break; case '"': s += '"'; break;
                            case 'u': { if (p + 4 < end) { int c = std::stoi(std::string(p + 1, p + 5), nullptr, 16); if (c < 128) s += static_cast<char>(c); p += 4; } break; }
                            default: s += *p; }
                        ++p;
                    } else s += *p++;
                }
                if (p < end) ++p;
                return s;
            }
            double number()
            {
                char* e; double d = std::strtod(p, &e);
                if (e == p) { ok = false; return 0; }
                p = e; return d;
            }
        };

        std::vector<std::uint8_t> B64Decode(const std::string& in)
        {
            static const std::string T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::vector<std::uint8_t> out; int val = 0, bits = -8;
            for (char c : in) {
                if (c == '=' || c == '\n' || c == '\r') continue;
                auto pos = T.find(c);
                if (pos == std::string::npos) continue;
                val = (val << 6) | static_cast<int>(pos); bits += 6;
                if (bits >= 0) { out.push_back(static_cast<std::uint8_t>((val >> bits) & 0xFF)); bits -= 8; }
            }
            return out;
        }

        float ReadComponent(const std::uint8_t* p, int compType)
        {
            switch (compType) {
            case 5126: { float f; std::memcpy(&f, p, 4); return f; }            // FLOAT
            case 5123: { std::uint16_t u; std::memcpy(&u, p, 2); return u; }    // UNSIGNED_SHORT
            case 5125: { std::uint32_t u; std::memcpy(&u, p, 4); return static_cast<float>(u); }  // UNSIGNED_INT
            case 5121: return *p;                                               // UNSIGNED_BYTE
            }
            return 0;
        }
        int CompSize(int t) { return t == 5126 || t == 5125 ? 4 : (t == 5123 ? 2 : 1); }
        int TypeCount(const std::string& t) { return t == "VEC3" ? 3 : t == "VEC2" ? 2 : t == "VEC4" ? 4 : 1; }
    }

    Mesh ParseGLTF(const std::uint8_t* data, std::size_t len, const std::filesystem::path& baseDir)
    {
        Mesh m;
        std::string json;
        std::vector<std::uint8_t> glbBin;
        if (len >= 12 && data[0] == 'g' && data[1] == 'l' && data[2] == 'T' && data[3] == 'F') {
            std::uint32_t total; std::memcpy(&total, data + 8, 4);
            std::size_t off = 12;
            while (off + 8 <= len) {
                std::uint32_t clen, ctype;
                std::memcpy(&clen, data + off, 4); std::memcpy(&ctype, data + off + 4, 4);
                const std::uint8_t* body = data + off + 8;
                if (off + 8 + clen > len) break;
                if (ctype == 0x4E4F534Au) json.assign(reinterpret_cast<const char*>(body), clen);   // "JSON"
                else if (ctype == 0x004E4942u) glbBin.assign(body, body + clen);                     // "BIN\0"
                off += 8 + clen;
            }
        } else {
            json.assign(reinterpret_cast<const char*>(data), len);
        }
        if (json.empty()) return m;

        JParser jp{ json.data(), json.data() + json.size() };
        JVal root = jp.parse();
        if (!jp.ok || root.t != JVal::Obj) return m;

        auto* meshes = root.find("meshes"); auto* accessors = root.find("accessors");
        auto* views = root.find("bufferViews"); auto* buffers = root.find("buffers");
        if (!meshes || !accessors || !views || !buffers || meshes->arr.empty()) return m;

        // Resolve buffer bytes (GLB bin, external file, or data: URI).
        std::vector<std::vector<std::uint8_t>> bufData;
        for (auto& bf : buffers->arr) {
            std::vector<std::uint8_t> bytes;
            auto* uri = bf.find("uri");
            if (!uri) bytes = glbBin;
            else if (uri->str.rfind("data:", 0) == 0) {
                auto comma = uri->str.find(',');
                if (comma != std::string::npos) bytes = B64Decode(uri->str.substr(comma + 1));
            } else {
                std::ifstream in(baseDir / uri->str, std::ios::binary);
                if (in) bytes.assign(std::istreambuf_iterator<char>(in), {});
            }
            bufData.push_back(std::move(bytes));
        }

        auto readAccessor = [&](int accIdx, std::vector<float>& out, int& comps) -> bool {
            if (accIdx < 0 || accIdx >= (int)accessors->arr.size()) return false;
            const JVal& acc = accessors->arr[accIdx];
            if (acc.find("sparse")) return false;                       // honest null
            int count = acc.find("count") ? acc.find("count")->inum() : 0;
            int compType = acc.find("componentType") ? acc.find("componentType")->inum() : 5126;
            std::string type = acc.find("type") ? acc.find("type")->str : "SCALAR";
            comps = TypeCount(type);
            int viewIdx = acc.find("bufferView") ? acc.find("bufferView")->inum() : -1;
            int accOff = acc.find("byteOffset") ? acc.find("byteOffset")->inum() : 0;
            if (viewIdx < 0 || viewIdx >= (int)views->arr.size()) return false;
            const JVal& bv = views->arr[viewIdx];
            int bufIdx = bv.find("buffer") ? bv.find("buffer")->inum() : -1;
            int bvOff = bv.find("byteOffset") ? bv.find("byteOffset")->inum() : 0;
            int stride = bv.find("byteStride") ? bv.find("byteStride")->inum() : 0;
            if (bufIdx < 0 || bufIdx >= (int)bufData.size()) return false;
            const auto& buf = bufData[bufIdx];
            int cs = CompSize(compType);
            int elemSize = stride ? stride : cs * comps;
            out.resize(static_cast<std::size_t>(count) * comps);
            for (int e = 0; e < count; ++e) {
                std::size_t base = static_cast<std::size_t>(bvOff) + accOff + static_cast<std::size_t>(e) * elemSize;
                for (int c = 0; c < comps; ++c) {
                    std::size_t o = base + static_cast<std::size_t>(c) * cs;
                    if (o + cs > buf.size()) return false;
                    out[static_cast<std::size_t>(e) * comps + c] = ReadComponent(buf.data() + o, compType);
                }
            }
            return true;
        };

        const JVal& prim = meshes->arr[0].find("primitives") && !meshes->arr[0].find("primitives")->arr.empty()
                         ? meshes->arr[0].find("primitives")->arr[0] : JVal{};
        auto* attrs = prim.find("attributes");
        if (!attrs) return m;
        auto accOf = [&](const char* k) { auto* v = attrs->find(k); return v ? v->inum() : -1; };

        std::vector<float> P, N, T; int cp = 0, cn = 0, ct = 0;
        if (!readAccessor(accOf("POSITION"), P, cp) || cp != 3) return m;
        readAccessor(accOf("NORMAL"), N, cn);
        readAccessor(accOf("TEXCOORD_0"), T, ct);

        std::size_t vcount = P.size() / 3;
        m.positions.resize(vcount);
        for (std::size_t i = 0; i < vcount; ++i) m.positions[i] = { P[i * 3], P[i * 3 + 1], P[i * 3 + 2] };
        if (N.size() == vcount * 3) { m.normals.resize(vcount); for (std::size_t i = 0; i < vcount; ++i) m.normals[i] = { N[i * 3], N[i * 3 + 1], N[i * 3 + 2] }; }
        if (ct == 2 && T.size() == vcount * 2) { m.uvs.resize(vcount); for (std::size_t i = 0; i < vcount; ++i) m.uvs[i] = { T[i * 2], T[i * 2 + 1] }; }   // glTF UV already top-left

        int idxAcc = prim.find("indices") ? prim.find("indices")->inum() : -1;
        if (idxAcc >= 0) {
            std::vector<float> I; int ci = 0;
            if (readAccessor(idxAcc, I, ci)) { m.indices.resize(I.size()); for (std::size_t i = 0; i < I.size(); ++i) m.indices[i] = static_cast<std::uint32_t>(I[i]); }
        } else {
            m.indices.resize(vcount);                                   // non-indexed
            for (std::uint32_t i = 0; i < vcount; ++i) m.indices[i] = i;
        }

        // Material -> diffuse / normal texture path (best-effort).
        if (auto* mats = root.find("materials"); mats && prim.find("material")) {
            int mi = prim.find("material")->inum();
            if (mi >= 0 && mi < (int)mats->arr.size()) {
                const JVal& mat = mats->arr[mi];
                auto* images = root.find("images"); auto* texs = root.find("textures");
                auto texURI = [&](int texIdx) -> std::string {
                    if (!texs || !images || texIdx < 0 || texIdx >= (int)texs->arr.size()) return {};
                    auto* src = texs->arr[texIdx].find("source"); if (!src) return {};
                    int si = src->inum();
                    if (si < 0 || si >= (int)images->arr.size()) return {};
                    auto* u = images->arr[si].find("uri"); return u ? u->str : std::string{};
                };
                if (auto* pbr = mat.find("pbrMetallicRoughness"))
                    if (auto* bc = pbr->find("baseColorTexture"); bc && bc->find("index")) m.diffuse = texURI(bc->find("index")->inum());
                if (auto* nt = mat.find("normalTexture"); nt && nt->find("index")) m.normalMap = texURI(nt->find("index")->inum());
            }
        }

        if (auto* nm = meshes->arr[0].find("name")) m.name = nm->str;
        m.valid = !m.positions.empty() && !m.indices.empty();
        return m;
    }

    Mesh LoadFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return {};
        std::vector<std::uint8_t> buf((std::istreambuf_iterator<char>(in)), {});
        if (buf.empty()) return {};
        auto ext = path.extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        Mesh m;
        if (ext == ".obj") m = ParseOBJ(std::string(buf.begin(), buf.end()));
        else if (ext == ".gltf" || ext == ".glb") m = ParseGLTF(buf.data(), buf.size(), path.parent_path());
        else return {};
        if (m.valid && m.name == "Mesh") m.name = path.stem().string();
        return m;
    }

    bool ConvertToNIF(const std::filesystem::path& in, const std::filesystem::path& out,
                      bool treeMode, bool collision, int collisionPieces,
                      std::uint32_t collisionMaterial, bool meshCollision)
    {
        Mesh m = LoadFile(in);
        if (!m.valid) return false;
        auto bytes = WriteNIF(m, treeMode, collision, collisionPieces, collisionMaterial, meshCollision);
        if (bytes.empty()) return false;
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(f);
    }
}
