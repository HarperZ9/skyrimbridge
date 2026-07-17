//=============================================================================
//  CompressedMesh.cpp — bhkCompressedMeshShapeData builder
//
//  Layout and invariants recovered from the modlist corpus (29,725 blocks,
//  100% byte-exact re-serialize; docs/CMSD-FORMAT.md).
//=============================================================================

#include "CompressedMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>

namespace SB::CompressedMesh
{
    namespace
    {
        struct Buf
        {
            std::vector<std::uint8_t> v;
            void u8(std::uint8_t x) { v.push_back(x); }
            void u16(std::uint16_t x) { v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF); }
            void u32(std::uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xFF); }
            void f32(float f) { std::uint32_t x; std::memcpy(&x, &f, 4); u32(x); }
        };
    }

    std::vector<std::uint8_t> Build(const ModelCodec::Mesh& mesh, std::uint32_t material)
    {
        const auto& pos = mesh.positions;
        const auto& idx = mesh.indices;
        if (pos.size() < 3 || idx.size() < 3 || idx.size() % 3 != 0 || pos.size() > 0xFFFF)
            return {};

        ModelCodec::Vec3 lo = pos[0], hi = pos[0];
        for (auto& p : pos) {
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
        }

        // ── partition triangles into quantizable chunks ──────────────────
        // A chunk's own vertex span must stay within the u16 range
        // (65.535 units/axis). Triangles are grouped by centroid on a grid
        // coarser than half the limit; a triangle the grid chunk cannot
        // absorb without breaking its span, and any triangle bigger than
        // the limit itself, escapes to bigVerts/bigTris (float4, exact),
        // which is precisely what the wild blocks use them for. bigTris
        // material is a TABLE INDEX (0/1/2 in the wild), as is the chunk's
        // materialIndex; we carry one material entry at index 0.
        struct Chunk
        {
            ModelCodec::Vec3 lo{}, hi{};
            bool init = false;
            std::vector<std::uint32_t> tris;
        };
        const std::size_t ntri = idx.size() / 3;
        std::map<std::array<int, 3>, Chunk> grid;
        std::vector<std::uint32_t> bigTriIdx;

        for (std::uint32_t t = 0; t < ntri; ++t) {
            const ModelCodec::Vec3* v[3] = { &pos[idx[t*3]], &pos[idx[t*3+1]], &pos[idx[t*3+2]] };
            ModelCodec::Vec3 tlo = *v[0], thi = *v[0];
            for (int k = 1; k < 3; ++k) {
                tlo.x = std::min(tlo.x, v[k]->x); tlo.y = std::min(tlo.y, v[k]->y); tlo.z = std::min(tlo.z, v[k]->z);
                thi.x = std::max(thi.x, v[k]->x); thi.y = std::max(thi.y, v[k]->y); thi.z = std::max(thi.z, v[k]->z);
            }
            if (thi.x - tlo.x > kMaxChunkSpan || thi.y - tlo.y > kMaxChunkSpan ||
                thi.z - tlo.z > kMaxChunkSpan) {
                bigTriIdx.push_back(t);                 // giant triangle: exact floats
                continue;
            }
            const float cx = (v[0]->x + v[1]->x + v[2]->x) / 3.0f;
            const float cy = (v[0]->y + v[1]->y + v[2]->y) / 3.0f;
            const float cz = (v[0]->z + v[1]->z + v[2]->z) / 3.0f;
            const std::array<int, 3> key = {
                static_cast<int>(std::floor((cx - lo.x) / kChunkGrid)),
                static_cast<int>(std::floor((cy - lo.y) / kChunkGrid)),
                static_cast<int>(std::floor((cz - lo.z) / kChunkGrid)) };
            Chunk& c = grid[key];
            ModelCodec::Vec3 nlo = c.init ? ModelCodec::Vec3{ std::min(c.lo.x, tlo.x), std::min(c.lo.y, tlo.y), std::min(c.lo.z, tlo.z) } : tlo;
            ModelCodec::Vec3 nhi = c.init ? ModelCodec::Vec3{ std::max(c.hi.x, thi.x), std::max(c.hi.y, thi.y), std::max(c.hi.z, thi.z) } : thi;
            if (nhi.x - nlo.x > kMaxChunkSpan || nhi.y - nlo.y > kMaxChunkSpan ||
                nhi.z - nlo.z > kMaxChunkSpan) {
                bigTriIdx.push_back(t);                 // would break the chunk's span
                continue;
            }
            c.lo = nlo; c.hi = nhi; c.init = true;
            c.tris.push_back(t);
        }

        // bigVerts: unique verts used by escaped triangles, first-use order.
        std::vector<std::uint32_t> bigVertOf(pos.size(), 0xFFFFFFFFu);
        std::vector<std::uint32_t> bigVerts;
        for (auto t : bigTriIdx)
            for (int k = 0; k < 3; ++k) {
                const std::uint32_t g = idx[t*3+k];
                if (bigVertOf[g] == 0xFFFFFFFFu) {
                    bigVertOf[g] = static_cast<std::uint32_t>(bigVerts.size());
                    bigVerts.push_back(g);
                }
            }
        if (bigVerts.size() > 0xFFFF) return {};        // bigTris indices are u16

        auto q = [](float world, float base) -> std::uint16_t {
            long v = std::lround((world - base) / kQuantStep);
            return static_cast<std::uint16_t>(std::clamp<long>(v, 0, 0xFFFF));
        };

        Buf b;
        b.u32(17);                                      // bitsPerIndex
        b.u32(18);                                      // bitsPerWIndex
        b.u32(0x3FFFF);                                 // maskWIndex
        b.u32(0x1FFFF);                                 // maskIndex
        b.f32(kQuantStep);                              // error (quantization step)
        b.f32(lo.x); b.f32(lo.y); b.f32(lo.z); b.f32(0.0f);   // boundsMin (+ unused w)
        b.f32(hi.x); b.f32(hi.y); b.f32(hi.z); b.f32(0.0f);   // boundsMax
        b.u8(0);                                        // weldingType
        b.u8(1);                                        // materialType
        b.u32(0); b.u32(0); b.u32(0);                   // mat32 / mat16 / mat8 (empty)
        b.u32(1);                                       // chunkMaterials count
        b.u32(material); b.u32(0);                      // { hash, filter }
        b.u32(0);                                       // numNamedMaterials
        b.u32(1);                                       // transforms count
        b.f32(0); b.f32(0); b.f32(0); b.f32(1);         // identity translation (w=1)
        b.f32(0); b.f32(0); b.f32(0); b.f32(1);         // identity quaternion

        b.u32(static_cast<std::uint32_t>(bigVerts.size()));
        for (auto g : bigVerts) {
            b.f32(pos[g].x); b.f32(pos[g].y); b.f32(pos[g].z); b.f32(0.0f);
        }
        b.u32(static_cast<std::uint32_t>(bigTriIdx.size()));
        for (auto t : bigTriIdx) {
            for (int k = 0; k < 3; ++k) b.u16(static_cast<std::uint16_t>(bigVertOf[idx[t*3+k]]));
            b.u32(0);                                   // material INDEX -> chunkMaterials[0]
            b.u16(0);                                   // welding
        }

        b.u32(static_cast<std::uint32_t>(grid.size())); // chunks count
        for (auto& [key, c] : grid) {
            b.f32(c.lo.x); b.f32(c.lo.y); b.f32(c.lo.z); b.f32(0.0f);   // chunk translation
            b.u32(0);                                   // materialIndex -> chunkMaterials[0]
            b.u16(0xFFFF);                              // reference (no per-chunk transform)
            b.u16(0);                                   // transformIndex -> transforms[0]
            // per-chunk vertex table (dedup, first-use order)
            std::vector<std::uint32_t> localOf(pos.size(), 0xFFFFFFFFu);
            std::vector<std::uint32_t> verts;
            std::vector<std::uint16_t> tri16;
            for (auto t : c.tris)
                for (int k = 0; k < 3; ++k) {
                    const std::uint32_t g = idx[t*3+k];
                    if (localOf[g] == 0xFFFFFFFFu) {
                        localOf[g] = static_cast<std::uint32_t>(verts.size());
                        verts.push_back(g);
                    }
                    tri16.push_back(static_cast<std::uint16_t>(localOf[g]));
                }
            b.u32(static_cast<std::uint32_t>(verts.size() * 3));
            for (auto g : verts) { b.u16(q(pos[g].x, c.lo.x)); b.u16(q(pos[g].y, c.lo.y)); b.u16(q(pos[g].z, c.lo.z)); }
            b.u32(static_cast<std::uint32_t>(tri16.size()));   // list triangles
            for (auto v16 : tri16) b.u16(v16);
            b.u32(0);                                   // strips (none)
            b.u32(0);                                   // welding (none)
        }

        b.u32(0);                                       // numConvexPieces
        return b.v;
    }
}
