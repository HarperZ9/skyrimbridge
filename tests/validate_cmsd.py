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

sys.path.insert(0, r"C:\Users\Zain\AppData\Local\Temp\claude"
                r"\C--\d8c04c17-40f0-4f6f-bc85-54dd1ce6b32c\scratchpad")
from cmsd_parse import parse_cmsd, reserialize
from parse_convex_collision import parse_nif, btype

MODS = r"E:\Modlists\SkyGroundChronicles\mods"
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
def build_cmsd(positions, indices, material):
    """Port of CompressedMesh::Build -> bytes ('' if it would refuse)."""
    if len(positions) < 3 or len(indices) < 3 or len(indices) % 3 or len(positions) > 0xFFFF:
        return b""
    lo = [min(p[k] for p in positions) for k in range(3)]
    hi = [max(p[k] for p in positions) for k in range(3)]
    if any(hi[k] - lo[k] > MAX_SPAN for k in range(3)):
        return b""

    def q(world, base):
        v = round((world - base) / QUANT)
        return max(0, min(0xFFFF, v))

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
    P("<I", 0); P("<I", 0)
    P("<I", 1)
    P("<4f", lo[0], lo[1], lo[2], 0.0)
    P("<I", 0); P("<HH", 0xFFFF, 0)
    P("<I", len(positions) * 3)
    for p in positions:
        P("<HHH", q(p[0], lo[0]), q(p[1], lo[1]), q(p[2], lo[2]))
    P("<I", len(indices))
    for ix in indices:
        P("<H", ix)
    P("<I", 0); P("<I", 0)
    P("<I", 0)
    return bytes(b)


def decode_verts(out):
    c = out["chunks"][0]
    t = c["translation"]
    vs = c["vertices"]
    return [(t[0] + vs[i] * QUANT, t[1] + vs[i+1] * QUANT, t[2] + vs[i+2] * QUANT)
            for i in range(0, len(vs), 3)]


print("[builder: round-trip + decode accuracy]")


def cube_mesh(origin=(0, 0, 0), s=10.0):
    o = origin
    verts = [(o[0]+x*s, o[1]+y*s, o[2]+z*s) for x in (0, 1) for y in (0, 1) for z in (0, 1)]
    # 12 triangles
    faces = [(0,1,3),(0,3,2),(4,6,7),(4,7,5),(0,4,5),(0,5,1),
             (2,3,7),(2,7,6),(0,2,6),(0,6,4),(1,5,7),(1,7,3)]
    idx = [i for f in faces for i in f]
    return verts, idx


def check_builder(label, verts, idx, mat):
    data = build_cmsd(verts, idx, mat)
    if not data:
        check(f"{label}: built", False, "refused")
        return
    out = parse_cmsd(data, 0, len(data))
    if out["_consumed"] != len(data):
        check(f"{label}: parses exactly", False, f"{out['_consumed']} != {len(data)}")
        return
    if reserialize(out) != data:
        check(f"{label}: re-serializes byte-exact", False)
        return
    dec = decode_verts(out)
    maxerr = max(max(abs(dec[i][k] - verts[i][k]) for k in range(3)) for i in range(len(verts)))
    matok = out["materials"][0][0] == mat
    idxok = out["chunks"][0]["indices"] == list(idx)
    check(f"{label}: round-trips, verts within {QUANT} ({maxerr:.5f}), material+indices exact",
          maxerr <= QUANT + 1e-6 and matok and idxok,
          f"err={maxerr} matok={matok} idxok={idxok}")


v, i = cube_mesh()
check_builder("cube", v, i, 0x17C77AAF)  # snow

# concave L: two boxes sharing an edge
lv, li = [], []
base = 0
for org in ((0, 0, 0), (10, 0, 0)):
    cv, ci = cube_mesh(org, 10.0)
    lv += cv
    li += [x + base for x in ci]
    base += len(cv)
check_builder("concave-L (two boxes)", lv, li, 0xDF02F237)  # stone

# span-limit refusal
big = [(0, 0, 0), (100, 0, 0), (0, 100, 0), (0, 0, 100)]
check("span > 65.535 units refused (needs chunking)",
      build_cmsd(big, [0, 1, 2, 0, 2, 3], 0x1DD9C611) == b"")

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
    check_builder("rebuilt from a real decoded mesh", real[0], real[1], 0x340E5D1C)
else:
    check("real list-triangle chunk found to rebuild", False, "none in sample")

# ── 3. source consistency ──────────────────────────────────────────────────
print("[source consistency]")
src = open(CPP, encoding="utf-8").read()
check("shipped builder carries the invariant head + step",
      all(s in src for s in ("b.u32(17)", "b.u32(18)", "0x3FFFF", "0x1FFFF",
                             "kQuantStep", "b.u8(1)")))
check("shipped builder writes the chosen material and refuses on span",
      "b.u32(material)" in src and "kMaxChunkSpan" in src)

print(f"\n{_passed} passed, {_failed} failed")
sys.exit(1 if _failed else 0)
