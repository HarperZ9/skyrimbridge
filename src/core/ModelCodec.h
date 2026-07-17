#pragma once
//=============================================================================
//  ModelCodec.h — native foreign static-mesh import to Skyrim SE NIF
//
//  The offline half of native non-.nif asset integration (the pragmatic 80% of
//  Lane C): parse a foreign static mesh (OBJ or glTF/GLB) and emit a valid
//  Skyrim SE NIF (version 20.2.0.7, user 12, BS 100) that the engine loads
//  natively — a BSFadeNode root over one BSTriShape with a
//  BSLightingShaderProperty + BSShaderTextureSet. The byte layout reproduces a
//  real shipping SSE static mesh exactly (32-byte full-precision vertex:
//  position float3, bitangent split, UV half2, packed normal/tangent, RGBA),
//  so it is engine-loadable by construction.
//
//  Pure, zero-dep, no engine or SKSE access: fully offline-testable. Normals
//  and tangents are computed when the source omits them.
//
//  Honest nulls (not faked): skinned/animated meshes, multiple sub-meshes per
//  file (only the first primitive/group is emitted), Draco-compressed or
//  sparse glTF accessors are NOT done. Collision is available as an optional
//  convex hull (F18 recipe); concave/MOPP mesh collision is NOT done.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace SB::ModelCodec
{
    struct Vec2 { float x = 0, y = 0; };
    struct Vec3 { float x = 0, y = 0, z = 0; };

    // A welded vertex stream + triangle list: the common target both the OBJ
    // and glTF front-ends produce and the NIF writer consumes.
    struct Mesh
    {
        std::vector<Vec3>          positions;
        std::vector<Vec3>          normals;    // empty => computed
        std::vector<Vec2>          uvs;        // empty => zero
        std::vector<std::uint32_t> indices;    // triangle list (multiple of 3)
        std::string                name = "Mesh";
        std::string                diffuse;    // texture path (may be empty)
        std::string                normalMap;  // texture path (may be empty)
        bool valid = false;
    };

    // Front-ends. valid=false on parse failure / unsupported feature.
    Mesh ParseOBJ(const std::string& text);
    Mesh ParseGLTF(const std::uint8_t* data, std::size_t len,
                   const std::filesystem::path& baseDir);   // .gltf or .glb
    Mesh LoadFile(const std::filesystem::path& path);

    // Fill missing normals (area-weighted) and always compute tangents.
    void FinalizeMesh(Mesh& m);

    // Serialize a finalized mesh to SSE NIF bytes. Empty on failure.
    //
    // treeMode emits a wind-animated tree: procedural sway weights painted
    // into the vertex colors (grayscale, 127 at the trunk base rising to 255
    // at canopy extremities), SLSF2_Tree_Anim set on the shader, and a
    // BSLeafAnimNode root. The mapping is derived empirically from real
    // animated tree assets; receipt: tests/validate_tree_wind.py.
    //
    // collision emits MOPP-free convex-hull collision (the F18 recipe:
    // bhkConvexVerticesShape + a known-good static bhkRigidBody template +
    // bhkCollisionObject + BSXFlags; docs/COLLISION-INVESTIGATION-F18.md).
    // A degenerate hull falls back to no collision. Convex cannot be
    // concave: an archway's opening fills in; walk-testing is the oracle.
    std::vector<std::uint8_t> WriteNIF(const Mesh& m, bool treeMode = false,
                                       bool collision = false);

    // Convenience: any supported input -> .nif on disk.
    bool ConvertToNIF(const std::filesystem::path& in, const std::filesystem::path& out,
                      bool treeMode = false, bool collision = false);

    // Half-float codec (exposed for the offline validator).
    std::uint16_t FloatToHalf(float f);
    float         HalfToFloat(std::uint16_t h);
}
