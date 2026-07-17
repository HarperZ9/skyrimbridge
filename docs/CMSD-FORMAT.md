# bhkCompressedMeshShapeData: recovered format (2026-07-17)

Reversed the offline half of exact concave mesh collision (the geometry the
engine narrowphase walks; the MOPP tree that indexes it is the separate
Havok-tool step, `docs/MOPP-INVESTIGATION.md`). Method: field-exact parser
proven against the whole corpus.

## Correctness of the reversal (receipt)

`tests/validate_cmsd.py` and the scratch corpus pass:
**29,725 blocks across 28,474 NIFs, 100% exact-consume and 100% byte-exact
re-serialize.** Sizes 204 B .. 1.4 MB. No block in the modlist is
unaccounted for.

## Layout (little-endian; all counts are u32 array prefixes)

```
bitsPerIndex        u32     invariant 17 in the corpus
bitsPerWIndex       u32     invariant 18
maskWIndex          u32     invariant 0x3FFFF  (2^18 - 1)
maskIndex           u32     invariant 0x1FFFF  (2^17 - 1)
error               f32     invariant 0.001 = the vertex quantization step
boundsMin           Vector4 (xyz + unused w)
boundsMax           Vector4
weldingType         u8      0 dominant (19 of 29725 non-zero)
materialType        u8      invariant 1
mat32[]             u32 count + u32 each   (empty in corpus)
mat16[]             u32 count + u32 each   (empty)
mat8[]              u32 count + u32 each   (empty)
chunkMaterials[]    u32 count + {u32 hash, u32 filter} each   (SkyrimHavokMaterial)
numNamedMaterials   u32     0 in corpus
transforms[]        u32 count + {Vector4 translation, Vector4 quat} each
                            observed identity: translation.w=1, quat=(0,0,0,1)
bigVerts[]          u32 count + Vector4 each   (uncompressed float verts)
bigTris[]           u32 count + {u16 v1,v2,v3; u32 material; u16 welding} each
chunks[]            u32 count + per chunk:
    translation     Vector4      chunk origin (xyz)
    materialIndex   u32          index into chunkMaterials
    reference       u16          0xFFFF (no transform) common
    transformIndex  u16
    vertices[]      u32 count + u16 each   (3 per vertex: x,y,z)
    indices[]       u32 count + u16 each
    strips[]        u32 count + u16 each   (per-strip length)
    welding[]       u32 count + u16 each
numConvexPieces     u32     (bhkCMSBigTris "convex piece A" tail; 0 common)
```

## Semantics (verified on 400 blocks, geometry inside bounds)

- **Vertex decode:** `world = chunk.translation + u16_triplet * error`. The
  `error` field is the quantization step (0.001 game units); `span / maxU16`
  clusters at 0.001 across the sample.
- **Index decode:** the `strips[]` lengths partition the front of
  `indices[]` into triangle strips (a strip of length L is L-2 triangles);
  any indices past `sum(strips)` are a flat list (3 per triangle). 400/400
  blocks: no leftover, no out-of-range index.
- **Materials:** per-chunk `materialIndex` selects a `chunkMaterials` entry
  whose hash is a `SkyrimHavokMaterial` (the same table
  `CollisionMaterial.cpp` now carries), so footstep/impact feel is per-chunk.

## What generation needs (the builder)

`CompressedMesh::Build` emits multi-chunk when needed. Triangles group by
centroid on a 48-unit grid; each chunk quantizes its own vertices to u16
around its own translation (step 0.001), emits list triangles (`strips[]`
empty, all in `indices[]` with a per-chunk deduplicated vertex table), one
`chunkMaterials` entry with the chosen material at index 0, identity
transform list, invariant head. A triangle a chunk cannot absorb without
exceeding the 65.535-unit span, or one larger than the span itself, escapes
to `bigVerts`/`bigTris` as exact float4 vertices, which is what the wild
blocks use them for. Verified against the corpus: `bigTris.material` and the
chunk `materialIndex` are TABLE INDICES (0/1/2 in the wild), not hashes.
Coverage is exact: every input triangle lands in a chunk or bigTris, and
each chunk's decoded verts stay within the u16 span. The remaining
game-bound step is the MOPP tree over this data via the free niftools tool.

Honest bound: the builder is offline-provable to byte-round-trip and to
decode back to the input mesh within the 0.001 quantization; the assembled
`bhkCompressedMeshShape` + MOPP + rigid-body chain accepting in-engine is
game-bound and needs the MOPP finalize step (below).

## The chain (built) and the finalize workflow

`ConvertModelMeshCollision` / `model.meshcollision` emit the full file:
root -> `bhkCollisionObject` -> `bhkRigidBody` -> `bhkMoppBvTreeShape` ->
`bhkCompressedMeshShape` -> `bhkCompressedMeshShapeData`, alongside the
visual `BSTriShape`. The `bhkMoppBvTreeShape` ships with an **empty MOPP**
(`moppDataSize = 0`, `buildType = 2` BUILD_NOT_SET). Both wrapper layouts
were recovered byte-exact from real files (CMS 56 bytes; MOPP 41-byte header
= shape ref + 3 unknown ints + f32 1.0 + moppDataSize + origin + scale +
buildType byte, then the bytecode).

**This file does not collide until the MOPP is finalized.** Open it in
NifSkope and run Spells -> Havok -> "Update MOPP Code" (which invokes the
free niftools Havok tool to generate the MOPP over our geometry), then save.
That is the one proprietary step, the same one every collision tool relies
on; we produce the hard geometry it needs. File output only, never live
spawn (a live spawn of an empty-MOPP chain would crash on load).

Receipt: `tests/validate_mesh_collision_chain.py` (9 checks) assembles the
chain with a ported writer and walks it back: every block type and ref, CMS
radius/data ref, the empty-MOPP placeholder, and the CMSD decoding to the
input mesh within 0.001.
