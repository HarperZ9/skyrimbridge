#pragma once
//=============================================================================
//  ConvexHull.h — 3D convex hull for collision generation
//
//  Quickhull over a point cloud, producing the two things a
//  bhkConvexVerticesShape needs: the hull's unique vertices and its
//  deduplicated outward unit face planes (n.x, n.y, n.z, d) with
//  n . p + d = 0 for points p on the face. Pure and zero-dep so the offline
//  receipt (tests/validate_collision_gen.py) can hold it to the hull
//  properties directly: every input point inside every plane, hull vertices
//  a subset of the input, unit normals, positive volume, deterministic.
//
//  Degenerate input (fewer than four non-coplanar points) is refused, not
//  approximated.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <array>
#include <vector>

#include "ModelCodec.h"   // Vec3

namespace SB::Hull
{
    // volumeOut (optional) receives the hull volume in the input's own units.
    bool ConvexHull(const std::vector<ModelCodec::Vec3>& points,
                    std::vector<ModelCodec::Vec3>& hullVerts,
                    std::vector<std::array<float, 4>>& planes,
                    double* volumeOut = nullptr);

    // Approximate convex decomposition: partition a point cloud into up to
    // maxPieces groups by greedy volume-reducing binary splits (each split
    // chooses the axis whose median cut most reduces the summed child hull
    // volume; a piece stops splitting once the reduction falls below
    // concavityFrac of its own volume). The union of the pieces' hulls
    // contains every input point by construction, so hulling each group and
    // collecting them under a bhkListShape gives concave-approximating
    // collision that a single hull cannot (F19's honest null). The caller
    // hulls each returned group; groups that degenerate are simply dropped.
    bool ConvexDecompose(const std::vector<ModelCodec::Vec3>& points,
                         int maxPieces, double concavityFrac,
                         std::vector<std::vector<ModelCodec::Vec3>>& pieces);
}
