#!/usr/bin/env python3
"""Offline validation of approximate convex decomposition + bhkListShape
collision (deep collision follow-on: ConvexHull.cpp ConvexDecompose + the
list-shape emission in ModelCodec.cpp).

Two layers:

1. THE DECOMPOSER, held to the properties that make it correct for
   collision, demonstrated on a genuinely concave shape (an L-solid whose
   single convex hull fills the notch): the union of the piece hulls
   contains every input point (no gaps), each piece is a valid hull, and
   decomposition strictly reduces "phantom" collision in the concave notch
   versus a single hull while keeping full coverage of the true solid.
   Deterministic; degenerate/small input falls back to one piece.

2. THE bhkListShape CONTAINER, layout recovered byte-exact from real NIFs
   (ms11unholyaltar n=5 -> 76 bytes, daedricwaraxe n=2 -> 52 bytes;
   36 + 8N). A ported writer emits an N-piece chain; the F18 parser walks
   it back: root -> collisionObject -> rigidBody(shape->list) ->
   listShape(numSub, refs, material, 2x cinfo, numInts==numSub, filters),
   each child a bhkConvexVerticesShape, consumed == blockSize.

Game-bound (protocol section 11): walking a spawned decomposed hull; a hull
list approximates concavity but cannot be exact (that is MOPP).
"""

import os
import struct
import sys

from nif_test_support import btype, parse_nif

_passed = _failed = 0


def check(label, ok, detail=""):
    global _passed, _failed
    if ok:
        _passed += 1
        print(f"  PASS  {label}")
    else:
        _failed += 1
        print(f"  FAIL  {label}  {detail}")


# ── compact quickhull port (verts + volume + planes), mirror of the C++ ────
def _sub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def _cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def _dot(a, b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]


def hull(points):
    """Returns (verts, planes, volume) or None (mirror of ConvexHull)."""
    P = [tuple(map(float, p)) for p in points]
    if len(P) < 4:
        return None
    lo = [min(p[i] for p in P) for i in range(3)]
    hi = [max(p[i] for p in P) for i in range(3)]
    diag = _dot(_sub(hi, lo), _sub(hi, lo)) ** 0.5
    if diag <= 0:
        return None
    eps = 1e-7 * diag

    def mk(a, b, c, interior):
        n = _cross(_sub(P[b], P[a]), _sub(P[c], P[a]))
        ln = _dot(n, n) ** 0.5 or 1.0
        n = (n[0]/ln, n[1]/ln, n[2]/ln)
        d = -_dot(n, P[a])
        if _dot(n, interior) + d > 0:
            b, c = c, b
            n = (-n[0], -n[1], -n[2]); d = -d
        return dict(a=a, b=b, c=c, n=n, d=d, outside=[], alive=True)

    i0 = min(range(len(P)), key=lambda i: P[i][0])
    i1 = max(range(len(P)), key=lambda i: P[i][0])
    if i0 == i1:
        i1 = max(range(len(P)), key=lambda i: P[i][1])
    if i0 == i1:
        return None
    best, i2 = eps, -1
    for i in range(len(P)):
        a = _dot(_cross(_sub(P[i1], P[i0]), _sub(P[i], P[i0])),
                 _cross(_sub(P[i1], P[i0]), _sub(P[i], P[i0]))) ** 0.5
        if a > best:
            best, i2 = a, i
    if i2 < 0:
        return None
    f0 = mk(i0, i1, i2, P[i0])
    best, i3 = eps, -1
    for i in range(len(P)):
        h = abs(_dot(f0["n"], P[i]) + f0["d"])
        if h > best:
            best, i3 = h, i
    if i3 < 0:
        return None
    interior = tuple(sum(P[k][j] for k in (i0, i1, i2, i3))/4 for j in range(3))
    faces = [mk(i0, i1, i2, interior), mk(i0, i1, i3, interior),
             mk(i0, i2, i3, interior), mk(i1, i2, i3, interior)]
    for i in range(len(P)):
        for f in faces:
            if _dot(f["n"], P[i]) + f["d"] > eps:
                f["outside"].append(i); break
    for _ in range(100000):
        pi, far = -1, eps
        for f in faces:
            if not f["alive"]:
                continue
            for idx in f["outside"]:
                hh = _dot(f["n"], P[idx]) + f["d"]
                if hh > far:
                    far, pi = hh, idx
        if pi < 0:
            break
        orphans, edges = [], {}
        for f in faces:
            if f["alive"] and _dot(f["n"], P[pi]) + f["d"] > eps:
                orphans += f["outside"]; f["alive"] = False; f["outside"] = []
                for e in ((f["a"], f["b"]), (f["b"], f["c"]), (f["c"], f["a"])):
                    k = tuple(sorted(e))
                    edges[k] = None if k in edges else e
        fresh = []
        for e in edges.values():
            if e is None:
                continue
            faces.append(mk(e[0], e[1], pi, interior)); fresh.append(faces[-1])
        for idx in orphans:
            if idx == pi:
                continue
            for f in fresh:
                if _dot(f["n"], P[idx]) + f["d"] > eps:
                    f["outside"].append(idx); break
    used, planes, vol = set(), [], 0.0
    for f in faces:
        if not f["alive"]:
            continue
        used |= {f["a"], f["b"], f["c"]}
        planes.append((f["n"][0], f["n"][1], f["n"][2], f["d"]))
        vol += _dot(P[f["a"]], _cross(P[f["b"]], P[f["c"]])) / 6.0
    if len(used) < 4 or vol <= 0:
        return None
    return [points[i] for i in sorted(used)], planes, vol


def vol_of(points):
    r = hull(points)
    return r[2] if r else 0.0


def best_split(pts, idx):
    """(reduction, childA, childB) mirror of BestSplit: axis x position search."""
    n = len(idx)
    if n < 8:
        return (-1, None, None)
    parent = vol_of([pts[i] for i in idx])
    best = (-1, None, None)
    for axis in range(3):
        s = sorted(idx, key=lambda i: pts[i][axis])
        for frac in (0.20, 0.35, 0.50, 0.65, 0.80):
            cut = int(n * frac)
            if cut < 4 or n - cut < 4:
                continue
            a, b = s[:cut], s[cut:]
            va, vb = vol_of([pts[i] for i in a]), vol_of([pts[i] for i in b])
            if va <= 0 or vb <= 0:
                continue
            red = parent - (va + vb)
            if red > best[0]:
                best = (red, a, b)
    return best


def decompose(points, max_pieces, concavity_frac):
    """Mirror of ConvexDecompose. Returns list of point-groups."""
    if len(points) < 4:
        return None
    if max_pieces < 2 or len(points) < 8:
        return [points] if vol_of(points) > 0 else None
    parts = [dict(idx=list(range(len(points))), vol=vol_of(points))]
    if parts[0]["vol"] <= 0:
        return None
    parts[0]["split"] = best_split(points, parts[0]["idx"])
    while len(parts) < max_pieces:
        pick, best_red = -1, 0
        for i, p in enumerate(parts):
            red = p["split"][0]
            if red > 0 and red >= concavity_frac * p["vol"] and red > best_red:
                best_red, pick = red, i
        if pick < 0:
            break
        _, a, b = parts[pick]["split"]
        pa = dict(idx=a, vol=vol_of([points[i] for i in a]))
        pb = dict(idx=b, vol=vol_of([points[i] for i in b]))
        pa["split"] = best_split(points, a)
        pb["split"] = best_split(points, b)
        parts[pick] = pa
        parts.append(pb)
    return [[points[i] for i in p["idx"]] for p in parts]


def inside_union(pt, hulls, tol):
    for verts, planes, _ in hulls:
        if all(_dot(pl[:3], pt) + pl[3] <= tol for pl in planes):
            return True
    return False


# ── an L-solid: single hull fills the notch, decomposition should not ──────
def in_L(x, y, z):
    if not (0 <= z <= 1):
        return False
    return (0 <= x <= 2 and 0 <= y <= 1) or (0 <= x <= 1 and 0 <= y <= 2)


cloud = []
g = 9
for xi in range(2*g + 1):
    for yi in range(2*g + 1):
        for zi in range(g + 1):
            x, y, z = xi/g, yi/g, zi/g
            if in_L(x, y, z):
                cloud.append((x, y, z))

print("[decomposer properties on a concave L-solid]")
single = decompose(cloud, 1, 0.05)
multi = decompose(cloud, 8, 0.05)
check("single-piece fallback returns one group", len(single) == 1)
check("decomposition produced multiple pieces", len(multi) >= 2, f"{len(multi)}")

single_h = [hull(g) for g in single]
multi_h = [hull(g) for g in multi]
check("every single-piece hull valid", all(h for h in single_h))
check("every decomposed piece hull valid", all(h for h in multi_h))

diag = 3.0
tol = 1e-4 * diag
check("union of decomposed hulls contains every input point (no gaps)",
      all(inside_union(p, multi_h, tol) for p in cloud))

# phantom collision = notch samples (in bbox, NOT in the L) caught by hulls
notch = [(x/g, y/g, z/g)
         for x in range(2*g+1) for y in range(2*g+1) for z in range(g+1)
         if not in_L(x/g, y/g, z/g)]
single_phantom = sum(1 for p in notch if inside_union(p, single_h, tol)) / len(notch)
multi_phantom = sum(1 for p in notch if inside_union(p, multi_h, tol)) / len(notch)
check(f"single hull fills the notch (phantom={single_phantom:.2f})", single_phantom > 0.15)
check(f"decomposition cuts phantom collision ({single_phantom:.2f} -> {multi_phantom:.2f})",
      multi_phantom < single_phantom * 0.6)
true_pts = [p for p in cloud]
check("decomposition keeps full coverage of the true solid",
      all(inside_union(p, multi_h, tol) for p in true_pts))

again = decompose(cloud, 8, 0.05)
check("deterministic (two runs same piece sizes)",
      [len(g) for g in multi] == [len(g) for g in again])

tiny = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 1, 1)]
check("small input falls back to one piece", len(decompose(tiny, 8, 0.05)) == 1)

# ── bhkListShape container round-trip (ported writer) ──────────────────────
print("[bhkListShape container: layout + round-trip]")
INV = 1.0 / 69.99125
TEMPLATE = None
import re
CPP = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "src", "core", "ModelCodec.cpp")
m = re.search(r"kRigidBodyTemplate\[250\] = \{(.*?)\};", open(CPP, encoding="utf-8").read(), re.S)
TEMPLATE = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))
assert len(TEMPLATE) == 250


class Buf:
    def __init__(s): s.b = bytearray()
    def u8(s, x): s.b.append(x & 0xFF)
    def u16(s, x): s.b += struct.pack("<H", x & 0xFFFF)
    def u32(s, x): s.b += struct.pack("<I", x & 0xFFFFFFFF)
    def i32(s, x): s.b += struct.pack("<i", x)
    def u64(s, x): s.b += struct.pack("<Q", x)
    def f32(s, x): s.b += struct.pack("<f", x)
    def sized(s, t): s.u32(len(t)); s.b += t.encode("latin1")
    def shortstr(s, t): s.u8(len(t)); s.b += t.encode("latin1")


def emit_listshape_nif(pieces_hulls):
    n = len(pieces_hulls)
    convex_base = 4
    list_idx = convex_base + n
    rigid_idx = list_idx + 1
    coll_idx = rigid_idx + 1
    bsx_idx = coll_idx + 1

    b0 = Buf()
    b0.i32(0); b0.u32(1); b0.i32(bsx_idx); b0.i32(-1); b0.u32(0x0E)
    for v in (0, 0, 0): b0.f32(v)
    for v in (1, 0, 0, 0, 1, 0, 0, 0, 1): b0.f32(v)
    b0.f32(1.0); b0.i32(coll_idx); b0.u32(1); b0.i32(1); b0.u32(0)

    b1 = Buf()   # minimal BSTriShape (geometry not under test here)
    b1.i32(1); b1.u32(0); b1.i32(-1); b1.u32(0x0E)
    for v in (0, 0, 0): b1.f32(v)
    for v in (1, 0, 0, 0, 1, 0, 0, 0, 1): b1.f32(v)
    b1.f32(1.0); b1.i32(-1)
    for v in (0, 0, 0, 1): b1.f32(v)
    b1.i32(-1); b1.i32(2); b1.i32(-1); b1.u64(0x0003B00007650408)
    b1.u16(1); b1.u16(3); b1.u32(3*32 + 6)
    for _ in range(3):
        for v in (0, 0, 0, 0): b1.f32(v)
        b1.u16(0); b1.u16(0)
        for _ in range(8): b1.u8(128)
        for _ in range(4): b1.u8(255)
    for i in (0, 1, 2): b1.u16(i)
    b1.u32(0)

    b2 = Buf()
    b2.u32(0); b2.i32(-1); b2.u32(0); b2.i32(-1)
    b2.u32(0x82400301); b2.u32(0x08008071)
    for v in (0, 0, 1, 1): b2.f32(v)
    b2.i32(3)
    for v in (0, 0, 0, 1): b2.f32(v)
    b2.u32(3); b2.f32(1); b2.f32(0); b2.f32(30)
    for v in (1, 1, 1, 1, 0.3, 2.0): b2.f32(v)

    b3 = Buf(); b3.u32(9)
    for _ in range(9): b3.sized("")

    convex = []
    for verts, planes, _ in pieces_hulls:
        cb = Buf()
        cb.u32(0x1DD9C611); cb.f32(0.05)
        for _ in range(2): cb.u32(0); cb.u32(0); cb.u32(0x80000000)
        cb.u32(len(verts))
        for v in verts:
            cb.f32(v[0]*INV); cb.f32(v[1]*INV); cb.f32(v[2]*INV); cb.f32(0)
        cb.u32(len(planes))
        for p in planes:
            cb.f32(p[0]); cb.f32(p[1]); cb.f32(p[2]); cb.f32(p[3]*INV)
        convex.append(cb)

    lb = Buf()
    lb.u32(n)
    for i in range(n): lb.i32(convex_base + i)
    lb.u32(0x1DD9C611)
    for _ in range(2): lb.u32(0); lb.u32(0); lb.u32(0x80000000)
    lb.u32(n)
    for i in range(n): lb.u32(0)

    rb = Buf(); rb.b = bytearray(TEMPLATE); rb.b[0:4] = struct.pack("<i", list_idx)
    cob = Buf(); cob.i32(0); cob.u16(0x0081); cob.i32(rigid_idx)
    bx = Buf(); bx.i32(2); bx.u32(130)

    blocks = [b0, b1, b2, b3] + convex + [lb, rb, cob, bx]
    per_type = (["BSFadeNode", "BSTriShape", "BSLightingShaderProperty", "BSShaderTextureSet"]
                + ["bhkConvexVerticesShape"]*n
                + ["bhkListShape", "bhkRigidBody", "bhkCollisionObject", "BSXFlags"])
    types, tindex = [], []
    for t in per_type:
        if t not in types:
            types.append(t)
        tindex.append(types.index(t))

    h = Buf()
    h.b += b"Gamebryo File Format, Version 20.2.0.7\n"
    h.u32(0x14020007); h.u8(1); h.u32(12); h.u32(len(blocks)); h.u32(100)
    h.shortstr(""); h.shortstr(""); h.shortstr("")
    h.u16(len(types))
    for t in types: h.sized(t)
    for ti in tindex: h.u16(ti)
    for bl in blocks: h.u32(len(bl.b))
    strs = ["Cube Root", "Cube", "BSX"]
    h.u32(len(strs)); h.u32(max(len(s) for s in strs))
    for s in strs: h.sized(s)
    h.u32(0)
    out = bytes(h.b)
    for bl in blocks: out += bytes(bl.b)
    return out, list_idx, rigid_idx, coll_idx


nif_bytes, list_idx, rigid_idx, coll_idx = emit_listshape_nif(multi_h)
tmp = os.path.join(os.environ.get("TEMP", "."), "sb_listshape.nif")
open(tmp, "wb").write(nif_bytes)
try:
    nif = parse_nif(tmp)
    d = nif["data"]
    n = len(multi_h)
    # list shape layout
    off = nif["starts"][list_idx]; sz = nif["sizes"][list_idx]
    nsub, = struct.unpack_from("<I", d, off)
    refs = struct.unpack_from(f"<{nsub}i", d, off + 4)
    o = off + 4 + nsub*4
    mat, = struct.unpack_from("<I", d, o); o += 4
    c1 = struct.unpack_from("<III", d, o); o += 12
    c2 = struct.unpack_from("<III", d, o); o += 12
    nints, = struct.unpack_from("<I", d, o); o += 4
    ints = struct.unpack_from(f"<{nints}I", d, o); o += nints*4
    check("bhkListShape numSubShapes matches piece count", nsub == n, f"{nsub} vs {n}")
    check("child refs all point to bhkConvexVerticesShape",
          all(btype(nif, r) == "bhkConvexVerticesShape" for r in refs))
    check("material set, two cinfo blocks (0,0,0x80000000)",
          mat == 0x1DD9C611 and c1 == (0, 0, 0x80000000) and c2 == (0, 0, 0x80000000),
          f"{hex(mat)} {c1} {c2}")
    check("numInts == numSubShapes, filters all zero",
          nints == n and all(v == 0 for v in ints))
    check(f"consumed == blockSize and 36+8N formula ({o-off} == {sz} == {36+8*n})",
          (o - off) == sz == 36 + 8*n)
    # chain
    rshape, = struct.unpack_from("<i", d, nif["starts"][rigid_idx])
    ctarget, cflags, cbody = struct.unpack_from("<iHi", d, nif["starts"][coll_idx])
    check("rigidBody.shape -> listShape", rshape == list_idx)
    check("collisionObject: target=root, flags 0x0081, body -> rigidBody",
          (ctarget, cflags, cbody) == (0, 0x0081, rigid_idx))
    # root node: name(4)+numExtra(4)+extraRef(4)+ctrl(4)+flags(4)
    # +translation(12)+rotation(36)+scale(4) -> collisionRef @+72
    check("root: collisionRef -> collisionObject",
          struct.unpack_from("<i", d, nif["starts"][0] + 72)[0] == coll_idx)
finally:
    os.unlink(tmp)

print(f"\n{_passed} passed, {_failed} failed")
sys.exit(1 if _failed else 0)
