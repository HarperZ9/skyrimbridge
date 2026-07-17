//=============================================================================
//  ConvexHull.cpp — quickhull (global-scan variant)
//
//  Standard quickhull with one simplification suited to collision-scale
//  inputs: the visible-face set for each inserted point is found by scanning
//  all live faces rather than walking adjacency. For a convex hull the
//  visibility region of an outside point is exactly the positive-distance
//  set, so the scan is equivalent to the DFS and removes the entire class of
//  adjacency-bookkeeping bugs. Cost is O(faces) per insertion; collision
//  hulls are tens of vertices, so this is nowhere near a bottleneck.
//  Doubles internally for robustness; epsilon scales with the input extent.
//=============================================================================

#include "ConvexHull.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

namespace SB::Hull
{
    namespace
    {
        struct D3 { double x, y, z; };
        D3 sub(const D3& a, const D3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        D3 cross(const D3& a, const D3& b)
        {
            return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
        }
        double dot(const D3& a, const D3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

        struct Face
        {
            int a, b, c;          // point indices, counter-clockwise from outside
            D3 n;                 // unit outward normal
            double d;             // plane offset: n.p + d = 0 on the face
            std::vector<int> outside;
            bool alive = true;
        };

        Face MakeFace(const std::vector<D3>& P, int a, int b, int c, const D3& interior)
        {
            Face f{ a, b, c };
            D3 n = cross(sub(P[b], P[a]), sub(P[c], P[a]));
            double len = std::sqrt(dot(n, n));
            if (len < 1e-30) len = 1.0;
            f.n = { n.x / len, n.y / len, n.z / len };
            f.d = -dot(f.n, P[a]);
            if (dot(f.n, interior) + f.d > 0) {           // flip outward
                std::swap(f.b, f.c);
                f.n = { -f.n.x, -f.n.y, -f.n.z };
                f.d = -f.d;
            }
            return f;
        }
    }

    bool ConvexHull(const std::vector<ModelCodec::Vec3>& points,
                    std::vector<ModelCodec::Vec3>& hullVerts,
                    std::vector<std::array<float, 4>>& planes,
                    double* volumeOut)
    {
        hullVerts.clear();
        planes.clear();
        if (volumeOut) *volumeOut = 0.0;
        if (points.size() < 4) return false;

        std::vector<D3> P;
        P.reserve(points.size());
        for (auto& p : points) P.push_back({ p.x, p.y, p.z });

        // Extent-relative epsilon.
        D3 lo = P[0], hi = P[0];
        for (auto& p : P) {
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
        }
        const double diag = std::sqrt(dot(sub(hi, lo), sub(hi, lo)));
        if (diag <= 0) return false;
        const double eps = 1e-7 * diag;

        // Initial simplex: extreme pair, then farthest from the line, then
        // farthest from the plane. Refuse degenerate input at each step.
        int i0 = 0, i1 = 0;
        for (std::size_t i = 1; i < P.size(); ++i) {
            if (P[i].x < P[i0].x) i0 = static_cast<int>(i);
            if (P[i].x > P[i1].x) i1 = static_cast<int>(i);
        }
        if (i0 == i1) { for (std::size_t i = 1; i < P.size(); ++i) if (P[i].y > P[i1].y) i1 = static_cast<int>(i); }
        if (i0 == i1) return false;

        int i2 = -1; double best = eps;
        for (std::size_t i = 0; i < P.size(); ++i) {
            D3 c = cross(sub(P[i1], P[i0]), sub(P[i], P[i0]));
            double a = std::sqrt(dot(c, c));
            if (a > best) { best = a; i2 = static_cast<int>(i); }
        }
        if (i2 < 0) return false;

        Face f0 = MakeFace(P, i0, i1, i2, P[i0]);   // orientation fixed below
        int i3 = -1; best = eps;
        for (std::size_t i = 0; i < P.size(); ++i) {
            double h = std::abs(dot(f0.n, P[i]) + f0.d);
            if (h > best) { best = h; i3 = static_cast<int>(i); }
        }
        if (i3 < 0) return false;

        D3 interior{ (P[i0].x + P[i1].x + P[i2].x + P[i3].x) / 4,
                     (P[i0].y + P[i1].y + P[i2].y + P[i3].y) / 4,
                     (P[i0].z + P[i1].z + P[i2].z + P[i3].z) / 4 };

        std::vector<Face> faces;
        faces.push_back(MakeFace(P, i0, i1, i2, interior));
        faces.push_back(MakeFace(P, i0, i1, i3, interior));
        faces.push_back(MakeFace(P, i0, i2, i3, interior));
        faces.push_back(MakeFace(P, i1, i2, i3, interior));

        // Assign every point to the first face it lies outside of.
        for (int i = 0; i < static_cast<int>(P.size()); ++i) {
            for (auto& f : faces)
                if (dot(f.n, P[i]) + f.d > eps) { f.outside.push_back(i); break; }
        }

        // Insert farthest outside points until every face is clean. The
        // iteration cap is a backstop far above any real hull.
        for (int guard = 0; guard < 100000; ++guard) {
            int fi = -1, pi = -1; double far = eps;
            for (int k = 0; k < static_cast<int>(faces.size()); ++k) {
                if (!faces[k].alive) continue;
                for (int idx : faces[k].outside) {
                    double h = dot(faces[k].n, P[idx]) + faces[k].d;
                    if (h > far) { far = h; fi = k; pi = idx; }
                }
            }
            if (fi < 0) break;                                    // done

            std::vector<int> visible;
            std::vector<int> orphans;
            for (int k = 0; k < static_cast<int>(faces.size()); ++k) {
                if (!faces[k].alive) continue;
                if (dot(faces[k].n, P[pi]) + faces[k].d > eps) {
                    visible.push_back(k);
                    orphans.insert(orphans.end(), faces[k].outside.begin(), faces[k].outside.end());
                    faces[k].alive = false;
                    faces[k].outside.clear();
                }
            }

            // Horizon = edges used by exactly one visible face.
            std::map<std::pair<int, int>, std::pair<int, int>> edges;   // undirected -> directed
            for (int k : visible) {
                const Face& f = faces[k];
                const int e[3][2] = { { f.a, f.b }, { f.b, f.c }, { f.c, f.a } };
                for (auto& ed : e) {
                    auto key = std::minmax(ed[0], ed[1]);
                    auto it = edges.find({ key.first, key.second });
                    if (it == edges.end()) edges[{ key.first, key.second }] = { ed[0], ed[1] };
                    else it->second = { -1, -1 };                       // interior edge
                }
            }
            std::vector<int> fresh;
            for (auto& [k, dir] : edges) {
                if (dir.first < 0) continue;
                faces.push_back(MakeFace(P, dir.first, dir.second, pi, interior));
                fresh.push_back(static_cast<int>(faces.size()) - 1);
            }
            for (int idx : orphans) {
                if (idx == pi) continue;
                for (int k : fresh)
                    if (dot(faces[k].n, P[idx]) + faces[k].d > eps) { faces[k].outside.push_back(idx); break; }
            }
        }

        // Collect unique hull vertices and deduplicated face planes.
        std::vector<int> used;
        std::map<std::array<long long, 4>, std::array<float, 4>> uniq;
        double volume = 0;
        for (auto& f : faces) {
            if (!f.alive) continue;
            used.push_back(f.a); used.push_back(f.b); used.push_back(f.c);
            std::array<long long, 4> key{
                static_cast<long long>(std::llround(f.n.x * 10000.0)),
                static_cast<long long>(std::llround(f.n.y * 10000.0)),
                static_cast<long long>(std::llround(f.n.z * 10000.0)),
                static_cast<long long>(std::llround(f.d / diag * 100000.0)) };
            uniq.emplace(key, std::array<float, 4>{
                static_cast<float>(f.n.x), static_cast<float>(f.n.y),
                static_cast<float>(f.n.z), static_cast<float>(f.d) });
            // (a,b,c) is outward-wound after MakeFace, so the signed
            // tetra volumes to the origin sum to the hull volume.
            volume += dot(P[f.a], cross(P[f.b], P[f.c])) / 6.0;
        }
        std::sort(used.begin(), used.end());
        used.erase(std::unique(used.begin(), used.end()), used.end());
        if (used.size() < 4 || uniq.size() < 4 || volume <= 0) return false;

        hullVerts.reserve(used.size());
        for (int idx : used) hullVerts.push_back(points[idx]);
        planes.reserve(uniq.size());
        for (auto& [k, pl] : uniq) planes.push_back(pl);
        if (volumeOut) *volumeOut = volume;
        return true;
    }

    // ── Approximate convex decomposition ─────────────────────────────────
    namespace
    {
        using Vec3 = ModelCodec::Vec3;

        std::vector<Vec3> Subset(const std::vector<Vec3>& pts, const std::vector<int>& idx)
        {
            std::vector<Vec3> s; s.reserve(idx.size());
            for (int i : idx) s.push_back(pts[i]);
            return s;
        }

        double VolumeOf(const std::vector<Vec3>& pts)
        {
            std::vector<Vec3> hv; std::vector<std::array<float, 4>> hp; double v = 0;
            return ConvexHull(pts, hv, hp, &v) ? v : 0.0;
        }

        struct Part
        {
            std::vector<int> idx;
            double vol = 0;
            double reduction = -1;   // parent vol - best summed child vol
            std::vector<int> childA, childB;
        };

        // Best split of a part: over each axis, try several split positions
        // (not just the median) and keep the (axis, position) that most
        // reduces the summed child hull volume. The position search is what
        // lets a split align with a concave feature; a fixed median cut
        // misses it when the shape is denser on one side (e.g. an L-solid).
        // Leaves reduction < 0 if unsplittable.
        void BestSplit(const std::vector<Vec3>& pts, Part& part)
        {
            part.reduction = -1;
            const std::size_t n = part.idx.size();
            if (n < 8) return;
            static const double kFrac[] = { 0.20, 0.35, 0.50, 0.65, 0.80 };
            for (int axis = 0; axis < 3; ++axis) {
                std::vector<int> sorted = part.idx;
                std::sort(sorted.begin(), sorted.end(), [&](int a, int b) {
                    const Vec3& pa = pts[a]; const Vec3& pb = pts[b];
                    return (axis == 0 ? pa.x : axis == 1 ? pa.y : pa.z) <
                           (axis == 0 ? pb.x : axis == 1 ? pb.y : pb.z);
                });
                for (double frac : kFrac) {
                    const std::size_t cut = static_cast<std::size_t>(n * frac);
                    if (cut < 4 || n - cut < 4) continue;
                    std::vector<int> a(sorted.begin(), sorted.begin() + cut);
                    std::vector<int> b(sorted.begin() + cut, sorted.end());
                    double va = VolumeOf(Subset(pts, a));
                    double vb = VolumeOf(Subset(pts, b));
                    if (va <= 0 || vb <= 0) continue;      // a child could not be hulled
                    double red = part.vol - (va + vb);
                    if (red > part.reduction) {
                        part.reduction = red;
                        part.childA = std::move(a);
                        part.childB = std::move(b);
                    }
                }
            }
        }
    }

    bool ConvexDecompose(const std::vector<ModelCodec::Vec3>& points,
                         int maxPieces, double concavityFrac,
                         std::vector<std::vector<ModelCodec::Vec3>>& pieces)
    {
        pieces.clear();
        if (points.size() < 4) return false;
        if (maxPieces < 2 || points.size() < 8) {          // single-piece fall back
            if (VolumeOf(points) <= 0) return false;
            pieces.push_back(points);
            return true;
        }

        std::vector<Part> parts(1);
        parts[0].idx.resize(points.size());
        for (std::size_t i = 0; i < points.size(); ++i) parts[0].idx[i] = static_cast<int>(i);
        parts[0].vol = VolumeOf(points);
        if (parts[0].vol <= 0) return false;
        BestSplit(points, parts[0]);

        while (static_cast<int>(parts.size()) < maxPieces) {
            // Pick the part whose split reduces the most, gated by concavity.
            int pick = -1; double bestRed = 0;
            for (int i = 0; i < static_cast<int>(parts.size()); ++i) {
                if (parts[i].reduction <= 0) continue;
                if (parts[i].reduction < concavityFrac * parts[i].vol) continue;
                if (parts[i].reduction > bestRed) { bestRed = parts[i].reduction; pick = i; }
            }
            if (pick < 0) break;                            // every part is convex enough

            Part a, b;
            a.idx = std::move(parts[pick].childA);
            b.idx = std::move(parts[pick].childB);
            a.vol = VolumeOf(Subset(points, a.idx));
            b.vol = VolumeOf(Subset(points, b.idx));
            BestSplit(points, a);
            BestSplit(points, b);
            parts[pick] = std::move(a);
            parts.push_back(std::move(b));
        }

        for (auto& part : parts) pieces.push_back(Subset(points, part.idx));
        return true;
    }
}
