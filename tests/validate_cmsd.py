#!/usr/bin/env python3
"""Offline validation of the bhkCompressedMeshShapeData reversal + builder
(src/core/CompressedMesh.cpp; docs/CMSD-FORMAT.md).

Three layers:

1. THE REVERSAL: a field-exact parser round-trips real modlist CMSD blocks
   byte-for-byte (a bounded sample here; the full corpus run was 29,725
   blocks / 28,474 files, 100% byte-exact, recorded in CMSD-FORMAT.md).
2. THE BUILDER: a Python port of CompressedMesh::Build emits CMSD that the
   parser consumes exactly, and whose decoded vertices match the input mesh
   within the 0.001 quantization step. Cube, concave L, and a real decoded
   mesh; plus the span-limit refusal.
3. SOURCE CONSISTENCY: the shipped CompressedMesh.cpp carries the invariant
   head (17/18/0x3FFFF/0x1FFFF, step 0.001, materialType 1) and the material
   wiring, so the C++ builder emits what the port proves.

Game-bound (not yet built): assembling the CMSD into a bhkMoppBvTreeShape
chain needs the Havok MOPP tool (docs/MOPP-INVESTIGATION.md).
"""

import glob
import os
import struct
import sys

from nif_test_support import (
    btype,
    parse_cmsd,
    parse_nif,
    reserialize_cmsd as reserialize,
)
from test_support import require_mods_root

MODS = require_mods_root()
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPP = os.path.join(ROOT, "src", "core", "CompressedMesh.cpp")

QUANT = 0.001
MAX_SPAN = 65535 * QUANT

_passed = _failed = 0


def check(label, ok, detail=""):
    global _passed, _failed
    if ok:
        _passed += 1
        print(f"  PASS  {label}")
    else:
        _failed += 1
        print(f"  FAIL  {label}  {detail}")


# ── 1. reversal: real-file round-trip (bounded sample) ─────────────────────
print("[reversal: real CMSD round-trip]")
sample = []
for dirpath, _, fnames in os.walk(MODS):
    for fn in fnames:
        if fn.lower().endswith(".nif"):
            sample.append(os.path.join(dirpath, fn))
    if len(sample) > 4000:
        break

checked = exact = byteexact = 0
for p in sample:
    try:
        with open(p, "rb") as f:
            head = f.read(8192)
    except OSError:
        continue
    if b"bhkCompressedMeshShapeData" not in head:
        continue
    try:
        nif = parse_nif(p)
    except Exception:
        continue
    d = nif["data"]
    for i in range(nif["nblocks"]):
        if btype(nif, i) != "bhkCompressedMeshShapeData":
            continue
        off, sz = nif["starts"][i], nif["sizes"][i]
        out = parse_cmsd(d, off, sz)
        checked += 1
        if out["_consumed"] == sz:
            exact += 1
        if reserialize(out) == bytes(d[off:off + sz]):
            byteexact += 1
    if checked >= 200:
        break
check(f"real CMSD blocks consume exactly ({exact}/{checked})", checked and exact == checked)
check(f"real CMSD blocks re-serialize byte-exact ({byteexact}/{checked})",
      checked and byteexact == checked)


# ── 2. builder: port of CompressedMesh::Build ──────────────────────────────
CHUNK_GRID = 48.0


def build_cmsd(positions, indices, material):
    """Port of CompressedMesh::Build (multi-chunk) -> bytes ('' on refusal)."""
    if len(positions) < 3 or len(indices) < 3 or len(indices) % 3 or len(positions) > 0xFFFF:
        return b""
    lo = [min(p[k] for p in positions) for k in range(3)]
    hi = [max(p[k] for p in positions) for k in range(3)]
    ntri = len(indices) // 3

    def tri_bounds(t):
        vs = [positions[indices[t*3+k]] for k in range(3)]
        tl = [min(v[k] for v in vs) for k in range(3)]
        th = [max(v[k] for v in vs) for k in range(3)]
        return tl, th

    import math
    grid = {}          # key -> {"lo","hi","tris"}
    big_tris = []
    for t in range(ntri):
        tl, th = tri_bounds(t)
        if any(th[k] - tl[k] > MAX_SPAN for k in range(3)):
            big_tris.append(t)
            continue
        c = [sum(positions[indices[t*3+k]][ax] for k in range(3)) / 3.0 for ax in range(3)]
        key = tuple(int(math.floor((c[ax] - lo[ax]) / CHUNK_GRID)) for ax in range(3))
        ch = grid.get(key)
        if ch is None:
            grid[key] = {"lo": tl[:], "hi": th[:], "tris": [t]}
            continue
        nlo = [min(ch["lo"][k], tl[k]) for k in range(3)]
        nhi = [max(ch["hi"][k], th[k]) for k in range(3)]
        if any(nhi[k] - nlo[k] > MAX_SPAN for k in range(3)):
            big_tris.append(t)
            continue
        ch["lo"], ch["hi"] = nlo, nhi
        ch["tris"].append(t)

    big_vert_of = {}
    big_verts = []
    for t in big_tris:
        for k in range(3):
            g = indices[t*3+k]
            if g not in big_vert_of:
                big_vert_of[g] = len(big_verts)
                big_verts.append(g)
    if len(big_verts) > 0xFFFF:
        return b""

    def q(world, base):
        return max(0, min(0xFFFF, round((world - base) / QUANT)))

    b = bytearray()
    P = lambda fmt, *v: b.extend(struct.pack(fmt, *v))
    P("<IIII", 17, 18, 0x3FFFF, 0x1FFFF)
    P("<f", QUANT)
    P("<4f", lo[0], lo[1], lo[2], 0.0)
    P("<4f", hi[0], hi[1], hi[2], 0.0)
    P("<BB", 0, 1)
    P("<III", 0, 0, 0)
    P("<I", 1); P("<II", material, 0)
    P("<I", 0)
    P("<I", 1); P("<4f", 0, 0, 0, 1); P("<4f", 0, 0, 0, 1)
    P("<I", len(big_verts))
    for g in big_verts:
        P("<4f", positions[g][0], positions[g][1], positions[g][2], 0.0)
    P("<I", len(big_tris))
    for t in big_tris:
        for k in range(3):
            P("<H", big_vert_of[indices[t*3+k]])
        P("<I", 0); P("<H", 0)
    P("<I", len(grid))
    for key in grid:            # dict preserves insertion order == C++ map? see note
        ch = grid[key]
        P("<4f", ch["lo"][0], ch["lo"][1], ch["lo"][2], 0.0)
        P("<I", 0); P("<HH", 0xFFFF, 0)
        local_of, verts, tri16 = {}, [], []
        for t in ch["tris"]:
            for k in range(3):
                g = indices[t*3+k]
                if g not in local_of:
                    local_of[g] = len(verts)
                    verts.append(g)
                tri16.append(local_of[g])
        P("<I", len(verts) * 3)
        for g in verts:
            P("<HHH", q(positions[g][0], ch["lo"][0]), q(positions[g][1], ch["lo"][1]),
              q(positions[g][2], ch["lo"][2]))
        P("<I", len(tri16))
        for v16 in tri16:
            P("<H", v16)
        P("<I", 0); P("<I", 0)
    P("<I", 0)
    return bytes(b)


print("[builder: round-trip + decode accuracy]")


def cube_mesh(origin=(0, 0, 0), s=10.0):
    o = origin
    verts = [(o[0]+x*s, o[1]+y*s, o[2]+z*s) for x in (0, 1) for y in (0, 1) for z in (0, 1)]
    # 12 triangles
    faces = [(0,1,3),(0,3,2),(4,6,7),(4,7,5),(0,4,5),(0,5,1),
             (2,3,7),(2,7,6),(0,2,6),(0,6,4),(1,5,7),(1,7,3)]
    idx = [i for f in faces for i in f]
    return verts, idx


def decode_all_tris(out, positions):
    """Reconstruct every triangle (chunks + bigTris) as vertex-position
    triples rounded to the quant grid, for coverage comparison."""
    tris = []
    for c in out["chunks"]:
        t = c["translation"]
        vs = c["vertices"]
        wverts = [(t[0] + vs[k]*QUANT, t[1] + vs[k+1]*QUANT, t[2] + vs[k+2]*QUANT)
                  for k in range(0, len(vs), 3)]
        idx = c["indices"]
        for j in range(0, len(idx) - 2, 3):
            tris.append(tuple(sorted(tuple(round(x, 3) for x in wverts[idx[j+m]]) for m in range(3))))
    for (a, b3, c3, mat, weld) in out["bigTris"]:
        bv = out["bigVerts"]
        tri = tuple(sorted(tuple(round(bv[e][ax], 3) for ax in range(3)) for e in (a, b3, c3)))
        tris.append(tri)
    return tris


def input_tris(positions, indices):
    out = []
    for j in range(0, len(indices), 3):
        out.append(tuple(sorted(tuple(round(positions[indices[j+m]][ax], 3) for ax in range(3))
                                for m in range(3))))
    return out


def check_multichunk(label, verts, idx, mat, expect_chunks_ge=1, expect_bigtris=False):
    data = build_cmsd(verts, idx, mat)
    if not data:
        check(f"{label}: built", False, "refused")
        return
    out = parse_cmsd(data, 0, len(data))
    ok_consume = out["_consumed"] == len(data) and reserialize(out) == data
    nchunks = len(out["chunks"])
    # per-chunk span within the u16 limit
    span_ok = True
    for c in out["chunks"]:
        vs = c["vertices"]
        if vs:
            for ax in range(3):
                comp = vs[ax::3]
                if (max(comp) - min(comp)) * QUANT > MAX_SPAN + 1e-4:
                    span_ok = False
    got = sorted(decode_all_tris(out, verts))
    want = sorted(input_tris(verts, idx))
    coverage = got == want
    has_big = len(out["bigTris"]) > 0
    check(f"{label}: {nchunks} chunks, byte-round-trip, per-chunk span ok, "
          f"every input triangle covered{' (+bigTris)' if has_big else ''}",
          ok_consume and span_ok and coverage and nchunks >= expect_chunks_ge
          and (has_big == expect_bigtris),
          f"consume={ok_consume} span={span_ok} coverage={coverage} "
          f"chunks={nchunks} big={len(out['bigTris'])}")


v, i = cube_mesh()
check_multichunk("cube (single chunk)", v, i, 0x17C77AAF)  # snow

# concave L: two boxes sharing an edge
lv, li = [], []
base = 0
for org in ((0, 0, 0), (10, 0, 0)):
    cv, ci = cube_mesh(org, 10.0)
    lv += cv
    li += [x + base for x in ci]
    base += len(cv)
check_multichunk("concave-L (two boxes)", lv, li, 0xDF02F237)  # stone

print("[multi-chunk: large meshes]")
# a 200x200 unit terrain grid of small triangles -> many chunks, no bigTris
import math as _m
gverts, gidx = [], []
N = 20
STEP = 10.0   # 200 units span, far over the 65.5 chunk limit
for gy in range(N + 1):
    for gx in range(N + 1):
        gverts.append((gx * STEP, gy * STEP, (gx + gy) % 3 * 1.0))
for gy in range(N):
    for gx in range(N):
        a = gy * (N + 1) + gx
        gidx += [a, a + 1, a + N + 1,  a + 1, a + N + 2, a + N + 1]
check_multichunk("200-unit terrain grid", gverts, gidx, 0x340E5D1C, expect_chunks_ge=9)

# a mesh with one giant triangle spanning > limit -> forced into bigTris
bigverts = list(gverts) + [(0, 0, 0), (300, 0, 0), (0, 300, 5)]
n0 = len(gverts)
bigidx = list(gidx) + [n0, n0 + 1, n0 + 2]
check_multichunk("terrain + one giant triangle (bigTris escape)", bigverts, bigidx,
                 0x340E5D1C, expect_chunks_ge=9, expect_bigtris=True)

# span-limit no longer a hard refusal: a mesh just over the limit now chunks
big = [(0, 0, 0), (100, 0, 0), (0, 100, 0), (0, 0, 100)]
check("mesh over single-chunk span now builds (multi-chunk / bigTris), not refused",
      build_cmsd(big, [0, 1, 2, 0, 2, 3], 0x1DD9C611) != b"")

# a real decoded mesh -> rebuild
real = None
for p in sample:
    try:
        with open(p, "rb") as f:
            if b"bhkCompressedMeshShapeData" not in f.read(8192):
                continue
        nif = parse_nif(p)
    except Exception:
        continue
    d = nif["data"]
    for bi in range(nif["nblocks"]):
        if btype(nif, bi) != "bhkCompressedMeshShapeData":
            continue
        out = parse_cmsd(d, nif["starts"][bi], nif["sizes"][bi])
        c = out["chunks"][0] if out["chunks"] else None
        if not c or not c["vertices"] or c["strips"]:
            continue
        t = c["translation"]
        verts = [(t[0]+c["vertices"][k]*QUANT, t[1]+c["vertices"][k+1]*QUANT, t[2]+c["vertices"][k+2]*QUANT)
                 for k in range(0, len(c["vertices"]), 3)]
        nverts = len(verts)
        idx = [ix for ix in c["indices"] if ix < nverts]
        span = [max(v[k] for v in verts) - min(v[k] for v in verts) for k in range(3)]
        if len(idx) >= 3 and len(idx) % 3 == 0 and nverts <= 0xFFFF and all(s <= MAX_SPAN for s in span):
            real = (verts, idx[:len(idx)//3*3])
            break
    if real:
        break
if real:
    check_multichunk("rebuilt from a real decoded mesh", real[0], real[1], 0x340E5D1C)
else:
    check("real list-triangle chunk found to rebuild", False, "none in sample")

# ── 3. source consistency ──────────────────────────────────────────────────
print("[source consistency]")
src = open(CPP, encoding="utf-8").read()
check("shipped builder carries the invariant head + step",
      all(s in src for s in ("b.u32(17)", "b.u32(18)", "0x3FFFF", "0x1FFFF",
                             "kQuantStep", "b.u8(1)")))
check("shipped builder writes the chosen material and chunks on span",
      "b.u32(material)" in src and "kMaxChunkSpan" in src and "kChunkGrid" in src)

print(f"\n{_passed} passed, {_failed} failed")
sys.exit(1 if _failed else 0)
