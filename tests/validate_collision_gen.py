#!/usr/bin/env python3
"""Offline validation of convex-hull collision generation (F18 follow-on:
src/core/ConvexHull.cpp + the collision emission in ModelCodec.cpp).

Three layers:

1. QUICKHULL, held to the hull properties directly (these falsify a wrong
   hull without needing a second hull implementation): every input point
   inside every plane, hull vertices a subset of the input, unit outward
   normals, each plane supported by >= 3 hull vertices, positive volume,
   exact known answers on cube/octahedron, deterministic, and degenerate
   input refused.
2. TEMPLATE/SOURCE CONSISTENCY: the 250-byte bhkRigidBody template embedded
   in ModelCodec.cpp is re-derived from its donor (deerskullstatic.nif) and
   compared byte-for-byte; the F18 constants (stone material, Havok scale,
   cinfo capacity flag, BSXFlags value, collision-object flags) are present
   in the shipped source.
3. CONTAINER: a faithful Python port of the writer emits a cube NIF with
   collision; the F18 parser walks it back: root -> bhkCollisionObject ->
   bhkRigidBody(template, shape ref patched) -> bhkConvexVerticesShape
   (consumed == blockSize, Havok scale round-trips), BSXFlags on the root's
   extra list, "BSX" in the string table.

Game-bound (protocol section 7): walking against a generated hull.
"""

import os
import re
import struct
import sys

MODS = r"E:\Modlists\SkyGroundChronicles\mods"
DONOR = os.path.join(MODS, r"2K Deer Skull and Antlers\meshes\clutter\bones\deerskullstatic.nif")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPP = os.path.join(ROOT, "src", "core", "ModelCodec.cpp")

_passed = _failed = 0


def check(label, ok, detail=""):
    global _passed, _failed
    if ok:
        _passed += 1
        print(f"  PASS  {label}")
    else:
        _failed += 1
        print(f"  FAIL  {label}  {detail}")


# ── quickhull port (mirror of ConvexHull.cpp) ──────────────────────────────
def _sub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def _cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def _dot(a, b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]


def convex_hull(points):
    if len(points) < 4:
        return None
    P = [tuple(map(float, p)) for p in points]
    lo = [min(p[i] for p in P) for i in range(3)]
    hi = [max(p[i] for p in P) for i in range(3)]
    diag = _dot(_sub(hi, lo), _sub(hi, lo)) ** 0.5
    if diag <= 0:
        return None
    eps = 1e-7 * diag

    def make_face(a, b, c, interior):
        n = _cross(_sub(P[b], P[a]), _sub(P[c], P[a]))
        ln = _dot(n, n) ** 0.5 or 1.0
        n = (n[0]/ln, n[1]/ln, n[2]/ln)
        d = -_dot(n, P[a])
        if _dot(n, interior) + d > 0:
            b, c = c, b
            n = (-n[0], -n[1], -n[2])
            d = -d
        return dict(a=a, b=b, c=c, n=n, d=d, outside=[], alive=True)

    i0 = min(range(len(P)), key=lambda i: P[i][0])
    i1 = max(range(len(P)), key=lambda i: P[i][0])
    if i0 == i1:
        i1 = max(range(len(P)), key=lambda i: P[i][1])
    if i0 == i1:
        return None
    best, i2 = eps, -1
    for i in range(len(P)):
        c = _cross(_sub(P[i1], P[i0]), _sub(P[i], P[i0]))
        a = _dot(c, c) ** 0.5
        if a > best:
            best, i2 = a, i
    if i2 < 0:
        return None
    f0 = make_face(i0, i1, i2, P[i0])
    best, i3 = eps, -1
    for i in range(len(P)):
        h = abs(_dot(f0["n"], P[i]) + f0["d"])
        if h > best:
            best, i3 = h, i
    if i3 < 0:
        return None
    interior = tuple(sum(P[k][j] for k in (i0, i1, i2, i3)) / 4 for j in range(3))
    faces = [make_face(i0, i1, i2, interior), make_face(i0, i1, i3, interior),
             make_face(i0, i2, i3, interior), make_face(i1, i2, i3, interior)]
    for i in range(len(P)):
        for f in faces:
            if _dot(f["n"], P[i]) + f["d"] > eps:
                f["outside"].append(i)
                break
    for _ in range(100000):
        fi = pi = -1
        far = eps
        for f in faces:
            if not f["alive"]:
                continue
            for idx in f["outside"]:
                h = _dot(f["n"], P[idx]) + f["d"]
                if h > far:
                    far, pi = h, idx
        if pi < 0:
            break
        orphans, edges = [], {}
        for f in faces:
            if not f["alive"]:
                continue
            if _dot(f["n"], P[pi]) + f["d"] > eps:
                orphans += f["outside"]
                f["alive"] = False
                f["outside"] = []
                for e in ((f["a"], f["b"]), (f["b"], f["c"]), (f["c"], f["a"])):
                    key = tuple(sorted(e))
                    edges[key] = None if key in edges else e
        fresh = []
        for e in edges.values():
            if e is None:
                continue
            faces.append(make_face(e[0], e[1], pi, interior))
            fresh.append(faces[-1])
        for idx in orphans:
            if idx == pi:
                continue
            for f in fresh:
                if _dot(f["n"], P[idx]) + f["d"] > eps:
                    f["outside"].append(idx)
                    break
    used, uniq, volume = set(), {}, 0.0
    for f in faces:
        if not f["alive"]:
            continue
        used |= {f["a"], f["b"], f["c"]}
        key = (round(f["n"][0]*10000), round(f["n"][1]*10000),
               round(f["n"][2]*10000), round(f["d"]/diag*100000))
        uniq[key] = (f["n"][0], f["n"][1], f["n"][2], f["d"])
        volume += _dot(P[f["a"]], _cross(P[f["b"]], P[f["c"]])) / 6.0
    if len(used) < 4 or len(uniq) < 4 or volume <= 0:
        return None
    hv = [points[i] for i in sorted(used)]
    return hv, sorted(uniq.values()), volume, diag


def hull_properties_ok(points, res, label):
    hv, planes, volume, diag = res
    eps = 1e-4 * diag
    inside = all(_dot(pl[:3], p) + pl[3] <= eps for pl in planes for p in points)
    subset = all(any(v == p for p in points) for v in hv)
    unit = all(abs(_dot(pl[:3], pl[:3]) - 1.0) < 1e-6 for pl in planes)
    support = all(sum(1 for v in hv if abs(_dot(pl[:3], v) + pl[3]) < eps) >= 3
                  for pl in planes)
    check(f"{label}: all points inside, verts subset, unit planes, "
          f">=3-vert support, volume>0",
          inside and subset and unit and support and volume > 0)


print("[quickhull properties]")
import random
random.seed(70)
cube = [(x, y, z) for x in (0, 10) for y in (0, 10) for z in (0, 10)]
noise = [(random.uniform(1, 9), random.uniform(1, 9), random.uniform(1, 9)) for _ in range(50)]
res = convex_hull(cube + noise)
check("cube+noise: exactly 8 hull verts, 6 planes",
      res and len(res[0]) == 8 and len(res[1]) == 6,
      f"{len(res[0])} verts {len(res[1])} planes" if res else "refused")
check("cube volume = 1000", res and abs(res[2] - 1000) < 1e-6, f"{res[2]}")
hull_properties_ok(cube + noise, res, "cube+noise")

octa = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
res = convex_hull(octa)
check("octahedron: 6 verts, 8 planes", res and len(res[0]) == 6 and len(res[1]) == 8)

cloud = [(random.gauss(0, 5), random.gauss(0, 3), random.gauss(0, 8)) for _ in range(200)]
res = convex_hull(cloud)
check("random cloud: hull found", res is not None)
hull_properties_ok(cloud, res, "random cloud")
res2 = convex_hull(cloud)
check("deterministic", res == res2)

check("coplanar grid refused",
      convex_hull([(x, y, 0.0) for x in range(5) for y in range(5)]) is None)
check("collinear refused", convex_hull([(i, 2.0*i, -i) for i in range(10)]) is None)
check("fewer than 4 points refused", convex_hull([(0, 0, 0), (1, 1, 1), (2, 0, 1)]) is None)

print("[template and source consistency]")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
src = open(CPP, encoding="utf-8").read()
m = re.search(r"kRigidBodyTemplate\[250\] = \{(.*?)\};", src, re.S)
shipped = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1))) if m else b""
check("template present in shipped source, 250 bytes", len(shipped) == 250)

donor = None
if os.path.exists(DONOR):
    d = open(DONOR, "rb").read()
    idx = d.find(shipped[:16]) if shipped else -1
    donor = d[idx:idx + 250] if idx >= 0 else None
check("template byte-identical to the donor static's rigid body",
      donor is not None and donor == shipped)
check("F18 constants in shipped source",
      all(s in src for s in ("0x1DD9C611", "69.99125", "0x80000000u", "b7.u32(130)", "0x0081")))

print("[container round-trip (ported writer)]")
INV = 1.0 / 69.99125


def f2h(f):
    s = struct.unpack("<I", struct.pack("<f", f))[0]
    sign = (s >> 16) & 0x8000
    exp = ((s >> 23) & 0xFF) - 127 + 15
    man = s & 0x7FFFFF
    if exp <= 0 or exp >= 0x1F:
        return sign
    return sign | (exp << 10) | (man >> 13)


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


def write_cube_nif_with_collision():
    verts = cube
    tris = [(0, 1, 2)]                      # geometry content is not under test
    hv, planes, _, _ = convex_hull(verts)

    b0 = Buf()                              # root with extra + collision refs
    b0.i32(0); b0.u32(1); b0.i32(7); b0.i32(-1)
    b0.u32(0x0E)
    for v in (0, 0, 0): b0.f32(v)
    for v in (1, 0, 0, 0, 1, 0, 0, 0, 1): b0.f32(v)
    b0.f32(1.0); b0.i32(6)
    b0.u32(1); b0.i32(1); b0.u32(0)

    b1 = Buf()                              # BSTriShape (minimal faithful)
    b1.i32(1); b1.u32(0); b1.i32(-1)
    b1.u32(0x0E)
    for v in (0, 0, 0): b1.f32(v)
    for v in (1, 0, 0, 0, 1, 0, 0, 0, 1): b1.f32(v)
    b1.f32(1.0); b1.i32(-1)
    for v in (5, 5, 5, 9): b1.f32(v)
    b1.i32(-1); b1.i32(2); b1.i32(-1)
    b1.u64(0x0003B00007650408)
    b1.u16(len(tris)); b1.u16(len(verts))
    b1.u32(len(verts) * 32 + len(tris) * 6)
    for v in verts:
        b1.f32(v[0]); b1.f32(v[1]); b1.f32(v[2]); b1.f32(0)
        b1.u16(f2h(0)); b1.u16(f2h(0))
        for _ in range(8): b1.u8(128)
        for _ in range(4): b1.u8(255)
    for t in tris:
        for i in t: b1.u16(i)
    b1.u32(0)

    b2 = Buf()                              # shader
    b2.u32(0); b2.i32(-1); b2.u32(0); b2.i32(-1)
    b2.u32(0x82400301); b2.u32(0x08008071)
    for v in (0, 0, 1, 1): b2.f32(v)
    b2.i32(3)
    for v in (0, 0, 0, 1): b2.f32(v)
    b2.u32(3); b2.f32(1); b2.f32(0); b2.f32(30)
    for v in (1, 1, 1, 1, 0.3, 2.0): b2.f32(v)

    b3 = Buf()
    b3.u32(9)
    for _ in range(9): b3.sized("")

    b4 = Buf()                              # bhkConvexVerticesShape
    b4.u32(0x1DD9C611); b4.f32(0.05)
    for _ in range(2): b4.u32(0); b4.u32(0); b4.u32(0x80000000)
    b4.u32(len(hv))
    for v in hv:
        b4.f32(v[0]*INV); b4.f32(v[1]*INV); b4.f32(v[2]*INV); b4.f32(0)
    b4.u32(len(planes))
    for p in planes:
        b4.f32(p[0]); b4.f32(p[1]); b4.f32(p[2]); b4.f32(p[3]*INV)

    b5 = Buf()
    b5.b = bytearray(shipped)
    b5.b[0:4] = struct.pack("<i", 4)

    b6 = Buf()
    b6.i32(0); b6.u16(0x0081); b6.i32(5)

    b7 = Buf()
    b7.i32(2); b7.u32(130)

    blocks = [b0, b1, b2, b3, b4, b5, b6, b7]
    types = ["BSFadeNode", "BSTriShape", "BSLightingShaderProperty", "BSShaderTextureSet",
             "bhkConvexVerticesShape", "bhkRigidBody", "bhkCollisionObject", "BSXFlags"]
    h = Buf()
    h.b += b"Gamebryo File Format, Version 20.2.0.7\n"
    h.u32(0x14020007); h.u8(1); h.u32(12)
    h.u32(len(blocks)); h.u32(100)
    h.shortstr(""); h.shortstr(""); h.shortstr("")
    h.u16(len(types))
    for t in types: h.sized(t)
    for i in range(len(blocks)): h.u16(i)
    for bl in blocks: h.u32(len(bl.b))
    strs = ["Cube Root", "Cube", "BSX"]
    h.u32(len(strs)); h.u32(max(len(s) for s in strs))
    for s in strs: h.sized(s)
    h.u32(0)
    out = bytes(h.b)
    for bl in blocks: out += bytes(bl.b)
    return out


sys.path.insert(0, r"C:\Users\Zain\AppData\Local\Temp\claude"
                r"\C--\d8c04c17-40f0-4f6f-bc85-54dd1ce6b32c\scratchpad")
nif_bytes = write_cube_nif_with_collision()
tmp = os.path.join(os.environ.get("TEMP", "."), "sb_collision_cube.nif")
open(tmp, "wb").write(nif_bytes)
try:
    from parse_convex_collision import parse_nif, btype
    nif = parse_nif(tmp)
    types = [btype(nif, i) for i in range(nif["nblocks"])]
    check("8 blocks with the expected types",
          types == ["BSFadeNode", "BSTriShape", "BSLightingShaderProperty",
                    "BSShaderTextureSet", "bhkConvexVerticesShape", "bhkRigidBody",
                    "bhkCollisionObject", "BSXFlags"], str(types))
    d = nif["data"]
    off = nif["starts"][6]
    target, = struct.unpack_from("<i", d, off)
    flags, = struct.unpack_from("<H", d, off + 4)
    body, = struct.unpack_from("<i", d, off + 6)
    check("bhkCollisionObject chain: target=root, flags 0x0081, body -> 5",
          (target, flags, body) == (0, 0x0081, 5))
    off = nif["starts"][5]
    shape_ref, = struct.unpack_from("<i", d, off)
    body_bytes = bytes(d[off:off + nif["sizes"][5]])
    check("rigid body = donor template with shape ref patched to 4",
          shape_ref == 4 and body_bytes[4:] == shipped[4:])
    off = nif["starts"][4]
    mat, = struct.unpack_from("<I", d, off)
    nv, = struct.unpack_from("<I", d, off + 32)
    verts = [struct.unpack_from("<4f", d, off + 36 + k * 16) for k in range(nv)]
    nno = off + 36 + nv * 16
    nn, = struct.unpack_from("<I", d, nno)
    consumed = 8 + 24 + 4 + nv * 16 + 4 + nn * 16
    check("shape block: consumed == blockSize, stone material",
          consumed == nif["sizes"][4] and mat == 0x1DD9C611)
    hext = max(max(abs(v[k]) for k in range(3)) for v in verts)
    check(f"Havok scale round-trips (10 units -> {hext*69.99125:.4f})",
          abs(hext * 69.99125 - 10.0) < 1e-3)
    check("BSXFlags on root extra list with value 130 and 'BSX' string",
          struct.unpack_from("<iI", d, nif["starts"][7]) == (2, 130)
          and "BSX" in nif["strings"])
finally:
    os.unlink(tmp)

print(f"\n{_passed} passed, {_failed} failed")
sys.exit(1 if _failed else 0)
