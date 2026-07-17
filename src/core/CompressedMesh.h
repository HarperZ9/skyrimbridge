#pragma once
//=============================================================================
//  CompressedMesh.h — bhkCompressedMeshShapeData builder (lane: exact mesh
//  collision, offline half)
//
//  Emits the geometry block the engine narrowphase walks for concave mesh
//  collision: a single chunk of u16-quantized vertices (step 0.001 game
//  units) and list triangles, one SkyrimHavokMaterial, the invariant head
//  recovered from the whole modlist corpus (docs/CMSD-FORMAT.md, 29,725
//  blocks byte-exact). Pure and zero-dep: fully offline-testable.
//
//  This is the OFFLINE-PROVABLE half only. A CMSD alone does not collide;
//  the engine needs a MOPP bounding-volume tree over it, which is generated
//  by the free niftools Havok tool (docs/MOPP-INVESTIGATION.md), a separate
//  game-bound step. So this builder is a library foundation, not yet a live
//  spawn path.
//
//  Single-chunk limit: a chunk spans at most 65535 * 0.001 = 65.535 game
//  units per axis (~0.9 m). Larger meshes need multi-chunk splitting (a
//  documented extension); Build() returns empty rather than overflow.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <cstdint>
#include <vector>

#include "ModelCodec.h"   // Mesh

namespace SB::CompressedMesh
{
    // Serialize a mesh to bhkCompressedMeshShapeData bytes. Empty on failure
    // (degenerate mesh, or per-axis span > 65.535 units: needs chunking).
    // material is a SkyrimHavokMaterial hash (see CollisionMaterial.h).
    std::vector<std::uint8_t> Build(const ModelCodec::Mesh& mesh, std::uint32_t material);

    // The quantization step (game units per u16 unit), invariant in the corpus.
    constexpr float kQuantStep = 0.001f;
    constexpr float kMaxChunkSpan = 65535.0f * kQuantStep;
}
