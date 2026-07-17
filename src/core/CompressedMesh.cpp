//=============================================================================
//  CompressedMesh.cpp — bhkCompressedMeshShapeData builder
//
//  Layout and invariants recovered from the modlist corpus (29,725 blocks,
//  100% byte-exact re-serialize; docs/CMSD-FORMAT.md).
//=============================================================================

#include "CompressedMesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
        if (hi.x - lo.x > kMaxChunkSpan || hi.y - lo.y > kMaxChunkSpan ||
            hi.z - lo.z > kMaxChunkSpan)
            return {};                                  // needs multi-chunk splitting

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
        b.u32(0);                                       // bigVerts (empty)
        b.u32(0);                                       // bigTris (empty)

        b.u32(1);                                       // chunks count
        b.f32(lo.x); b.f32(lo.y); b.f32(lo.z); b.f32(0.0f);   // chunk translation (= boundsMin)
        b.u32(0);                                       // materialIndex -> chunkMaterials[0]
        b.u16(0xFFFF);                                  // reference (no per-chunk transform)
        b.u16(0);                                       // transformIndex -> transforms[0]
        b.u32(static_cast<std::uint32_t>(pos.size() * 3));   // vertices (u16 x3 per vertex)
        for (auto& p : pos) { b.u16(q(p.x, lo.x)); b.u16(q(p.y, lo.y)); b.u16(q(p.z, lo.z)); }
        b.u32(static_cast<std::uint32_t>(idx.size())); // indices (list triangles)
        for (auto i : idx) b.u16(static_cast<std::uint16_t>(i));
        b.u32(0);                                       // strips (none: all list triangles)
        b.u32(0);                                       // welding (none)

        b.u32(0);                                       // numConvexPieces
        return b.v;
    }
}
