#!/usr/bin/env python3
"""Offline validation of tree wind auto-painting (lane F16, ModelCodec tree
mode).

Two halves:

1. THE EMPIRICAL MAPPING, asserted against real animated tree assets in the
   modlist (Aspens Ablaze, a vanilla Dawnguard tree). These assertions are
   the evidence the generator's design rests on: animated shapes carry
   SLSF2 bit 29 (Tree_Anim); their vertex color is a GRAYSCALE sway weight
   (R=G=B on every vertex), sitting near 127 on rigid geometry and rising
   toward 255 at canopy extremities (positive correlation with distance from
   the trunk base); vertex alpha is constant per animated shape; tree roots
   are BSLeafAnimNode.

2. THE GENERATOR, checked as a port (weight function properties, and source
   consistency: the shipped ModelCodec.cpp carries exactly the empirically
   derived flag and root type). The NIF container bytes themselves are
   locked by the existing C9 receipt (tests/validate_model_codec.py); tree
   mode changes three values inside that locked layout.

Honest nulls: the aspens ship vertex alpha 68 on animated shapes and its
semantics are unrecovered (we emit the vanilla-accepted 255); whether a
STAT-placed reference sways in-game (vs needing a TREE form) is game-bound,
protocol section 7.
"""

import os
import re
import struct
import sys

MODS = r"E:\Modlists\SkyGroundChronicles\mods"
ASPEN1 = os.path.join(MODS, r"Aspens Ablaze\meshes\landscape\trees\treeaspen01.nif")
ASPEN6 = os.path.join(MODS, r"Aspens Ablaze\meshes\landscape\trees\treeaspen06.nif")
GLADE = os.path.join(MODS, r"A Canticle Tree\meshes\dlc01\plants\dlc01ancestorgladetree01.nif")
CPP = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "src", "core", "ModelCodec.cpp")

TREE_ANIM = 0x20000000


# ── minimal SSE NIF reader (BSTriShape + BSLightingShaderProperty) ─────────
def parse_nif(path):
    d = open(path, "rb").read()
    assert d.startswith(b"Gamebryo File Format, Version 20.2.0.7\n")
    off = d.index(b"\n") + 1
    off += 4 + 1 + 4                       # version, endian, user version
    nblocks, = struct.unpack_from("<I", d, off); off += 4
    off += 4                               # BS stream version
    for _ in range(3):
        n = d[off]; off += 1 + n           # export strings
    ntypes, = struct.unpack_from("<H", d, off); off += 2
    types = []
    for _ in range(ntypes):
        n, = struct.unpack_from("<I", d, off); off += 4
        types.append(d[off:off + n].decode("latin1")); off += n
    tidx = list(struct.unpack_from(f"<{nblocks}H", d, off)); off += 2 * nblocks
    sizes = list(struct.unpack_from(f"<{nblocks}I", d, off)); off += 4 * nblocks
    nstr, = struct.unpack_from("<I", d, off); off += 8
    strings = []
    for _ in range(nstr):
        n, = struct.unpack_from("<I", d, off); off += 4
        strings.append(d[off:off + n].decode("latin1")); off += n
    ngroups, = struct.unpack_from("<I", d, off); off += 4 + 4 * ngroups
    starts = []
    for s in sizes:
        starts.append(off); off += s
    return dict(data=d, types=types, tidx=tidx, starts=starts, sizes=sizes,
                strings=strings, nblocks=nblocks)


def btype(nif, i):
    return nif["types"][nif["tidx"][i]]


def tri_shape(nif, i):
    d = nif["data"]; off = nif["starts"][i]
    nameref, = struct.unpack_from("<i", d, off); off += 4
    nex, = struct.unpack_from("<I", d, off); off += 8 + 4 * nex   # extra + controller
    off += 4 + 12 + 36 + 4 + 4             # flags, translate, rotate, scale, collision
    off += 16 + 4                          # bounding sphere, skin
    shader, = struct.unpack_from("<i", d, off); off += 8          # shader + alpha
    vdesc, = struct.unpack_from("<Q", d, off); off += 8
    ntri, nvert = struct.unpack_from("<HH", d, off); off += 4
    dsize, = struct.unpack_from("<I", d, off); off += 4
    name = nif["strings"][nameref] if 0 <= nameref < len(nif["strings"]) else "?"
    if not nvert or not dsize:
        return None
    stride = (dsize - ntri * 6) // nvert
    has_color = bool((vdesc >> 44) & 0x20)
    verts = []
    for v in range(nvert):
        b = off + v * stride
        x, y, z = struct.unpack_from("<3f", d, b)     # these assets are full precision
        col = tuple(d[b + stride - 4:b + stride]) if has_color else None
        verts.append(((x, y, z), col))
    return dict(name=name, shader=shader, stride=stride, verts=verts, has_color=has_color)


def slsf2(nif, ref):
    if ref < 0 or btype(nif, ref) != "BSLightingShaderProperty":
        return None
    d = nif["data"]; off = nif["starts"][ref]
    nex, = struct.unpack_from("<I", d, off + 8)
    return struct.unpack_from("<I", d, off + 12 + 4 * nex + 8)[0]


def corr(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sx = (sum((x - mx) ** 2 for x in xs)) ** 0.5
    sy = (sum((y - my) ** 2 for y in ys)) ** 0.5
    if sx == 0 or sy == 0:
        return 0.0
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / (sx * sy)


_passed = _failed = 0


def check(label, ok, detail=""):
    global _passed, _failed
    if ok:
        _passed += 1
        print(f"  PASS  {label}")
    else:
        _failed += 1
        print(f"  FAIL  {label}  {detail}")


print("[the empirical mapping (real animated trees)]")
for path in (ASPEN1, ASPEN6, GLADE):
    if not os.path.exists(path):
        check(f"asset present: {os.path.basename(path)}", False)
        continue
    nif = parse_nif(path)
    animated, rigid = [], []
    for i in range(nif["nblocks"]):
        if btype(nif, i) != "BSTriShape":
            continue
        sh = tri_shape(nif, i)
        if not sh or not sh["has_color"]:
            continue
        f2 = slsf2(nif, sh["shader"])
        (animated if (f2 and f2 & TREE_ANIM) else rigid).append((sh, f2))
    base = os.path.basename(path)
    check(f"{base}: has Tree_Anim shapes", len(animated) > 0, f"{len(animated)}")
    gray = all(c[0] == c[1] == c[2] for sh, _ in animated for p, c in sh["verts"])
    check(f"{base}: animated wind weight is grayscale (R=G=B every vertex)", gray)
    amin = min(c[0] for sh, _ in animated for p, c in sh["verts"])
    amax = max(c[0] for sh, _ in animated for p, c in sh["verts"])
    check(f"{base}: weights span [{amin},{amax}] within [127,255]",
          amin >= 127 and amax <= 255)
    aconst = all(len({c[3] for p, c in sh["verts"]}) == 1 for sh, _ in animated)
    check(f"{base}: vertex alpha constant per animated shape", aconst)

# distance-from-base correlation on the aspens' leaf shapes
for path in (ASPEN1, ASPEN6):
    nif = parse_nif(path)
    ok, vals = True, []
    for i in range(nif["nblocks"]):
        if btype(nif, i) != "BSTriShape":
            continue
        sh = tri_shape(nif, i)
        if not sh or not sh["has_color"] or "Leaves" not in sh["name"]:
            continue
        zs = [p[2] for p, c in sh["verts"]]
        base_z = min(zs)
        ds = [((p[0]) ** 2 + (p[1]) ** 2 + (p[2] - base_z) ** 2) ** 0.5
              for p, c in sh["verts"]]
        ws = [c[0] for p, c in sh["verts"]]
        r = corr(ds, ws)
        vals.append(f"{sh['name']}:{r:+.2f}")
        if r < 0.2:
            ok = False
    check(f"{os.path.basename(path)}: leaf weight rises with distance from base "
          f"({' '.join(vals)})", ok)

nif = parse_nif(ASPEN1)
check("aspen root block is BSLeafAnimNode", btype(nif, 0) == "BSLeafAnimNode")

nif = parse_nif(GLADE)
all255 = True
for i in range(nif["nblocks"]):
    if btype(nif, i) != "BSTriShape":
        continue
    sh = tri_shape(nif, i)
    if not sh or not sh["has_color"]:
        continue
    f2 = slsf2(nif, sh["shader"])
    if f2 and f2 & TREE_ANIM:
        if any(c != (255, 255, 255, 255) for p, c in sh["verts"]):
            all255 = False
check("vanilla glade tree: animated shapes are all-255 (minimal accepted config)", all255)

print("[the generator (port + source consistency)]")


def wind_weight(p, base, maxd):
    """Port of the C++ tree-mode weight."""
    d = ((p[0] - base[0]) ** 2 + (p[1] - base[1]) ** 2 + (p[2] - base[2]) ** 2) ** 0.5
    e = d / maxd
    w = 0.5 + 0.5 * e ** 1.5
    return min(255, int(w * 255.0 + 0.5))


base = (0.0, 0.0, 0.0)
pts = [(0, 0, z) for z in range(0, 101, 10)]
maxd = 100.0
ws = [wind_weight(p, base, maxd) for p in pts]
check("weight monotone non-decreasing from base to tip",
      all(a <= b for a, b in zip(ws, ws[1:])), f"{ws}")
check("weight range: base 128 (parity with the real 127 baseline), tip 255",
      ws[0] == 128 and ws[-1] == 255, f"{ws[0]}..{ws[-1]}")
check("deterministic", ws == [wind_weight(p, base, maxd) for p in pts])

src = open(CPP, encoding="utf-8").read()
m = re.search(r"treeMode \? 0x([0-9A-Fa-f]+)u : 0x([0-9A-Fa-f]+)u", src)
check("shipped SLSF2: tree value = base | Tree_Anim bit 29",
      m and int(m.group(1), 16) == int(m.group(2), 16) | TREE_ANIM,
      m.group(0) if m else "pattern not found")
check("shipped root type: BSLeafAnimNode in tree mode",
      'treeMode ? "BSLeafAnimNode" : "BSFadeNode"' in src)
check("shipped weight formula matches the port (0.5 + 0.5 * e^1.5)",
      "0.5f + 0.5f * std::pow(e, 1.5f)" in src)

print(f"\n{_passed} passed, {_failed} failed")
sys.exit(1 if _failed else 0)
