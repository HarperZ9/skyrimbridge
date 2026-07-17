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
//  Chunking: a chunk's vertex span is limited to 65535 * 0.001 = 65.535
//  game units per axis by the u16 quantization. Larger meshes are split
//  automatically: triangles group by centroid on a grid, any triangle a
//  chunk cannot absorb without breaking its span (and any triangle larger
//  than the limit itself) escapes to bigVerts/bigTris as exact floats,
//  which is what the wild blocks use them for.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <cstdint>
#include <vector>

#include "ModelCodec.h"   // Mesh

namespace SB::CompressedMesh
{
    // Serialize a mesh to bhkCompressedMeshShapeData bytes (multi-chunk when
    // needed). Empty on failure (degenerate mesh, or more escaped big-tri
    // vertices than u16 indexing allows).
    // material is a SkyrimHavokMaterial hash (see CollisionMaterial.h).
    std::vector<std::uint8_t> Build(const ModelCodec::Mesh& mesh, std::uint32_t material);

    // The quantization step (game units per u16 unit), invariant in the corpus.
    constexpr float kQuantStep = 0.001f;
    constexpr float kMaxChunkSpan = 65535.0f * kQuantStep;
    constexpr float kChunkGrid = 48.0f;    // centroid grid cell (< span/1.36)
}
