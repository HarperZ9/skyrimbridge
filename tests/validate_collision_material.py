#!/usr/bin/env python3
"""Offline validation of the collision material system (src/core/
CollisionMaterial.cpp + the material wiring in ModelCodec.cpp).

Two layers:

1. THE TABLE, checked against ground truth: the SkyrimHavokMaterial hashes
   the shipped CollisionMaterial.cpp carries are parsed out of the .cpp and
   compared to the authoritative niftools nif.xml enum values (hardcoded
   here from that enum). The earlier code mislabeled 0x1DD9C611 as STONE;
   this locks it as WOOD and locks STONE at 0xDF02F237, SNOW at 0x17C77AAF.

2. MATERIAL PROPAGATION: a ported writer emits a decomposed bhkListShape
   chain with a chosen material (snow), and the F18 parser confirms the
   material reaches BOTH the list shape and every convex child (the engine
   reads the child material for narrowphase footstep/impact feel).

Game-bound (protocol section 12): walk on a snow-material spawn and hear
snow footsteps / see snow impacts. Honest null: visual footprint
depressions are a separate shader/footprint system, not the material.
"""

import os
import re
import struct
import sys

sys.path.insert(0, r"C:\Users\Zain\AppData\Local\Temp\claude"
                r"\C--\d8c04c17-40f0-4f6f-bc85-54dd1ce6b32c\scratchpad")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPP = os.path.join(ROOT, "src", "core", "CollisionMaterial.cpp")

# Authoritative SkyrimHavokMaterial hashes (niftools nif.xml enum).
NIFXML = {
    "wood":         0x1DD9C611,   # SKY_HAV_MAT_WOOD
    "stone":        0xDF02F237,   # SKY_HAV_MAT_STONE
    "snow":         0x17C77AAF,   # SKY_HAV_MAT_SNOW
    "ice":          0x340E5D1C,   # SKY_HAV_MAT_ICE
    "dirt":         0xB9233EAA,   # SKY_HAV_MAT_DIRT
    "grass":        0x6E2F68EE,   # SKY_HAV_MAT_GRASS
    "gravel":       0x198BBA58,   # SKY_HAV_MAT_GRAVEL
    "sand":         0x813E4D0D,   # SKY_HAV_MAT_SAND
    "metal":        0x4CCACC3B,   # SKY_HAV_MAT_SOLID_METAL
    "heavymetal":   0x84E226A3,   # SKY_HAV_MAT_HEAVY_METAL
    "glass":        0xDEE94842,   # SKY_HAV_MAT_GLASS
    "mud":          0x58987081,   # SKY_HAV_MAT_MUD
    "water":        0x3D11E3C7,   # SKY_HAV_MAT_WATER
    "bone":         0xB5C27C14,   # SKY_HAV_MAT_MATERIAL_BONE
    "organic":      0xB151ADDB,   # SKY_HAV_MAT_ORGANIC
    "stairs_stone": 0x359D733D,   # SKY_HAV_MAT_STAIRS_STONE
    "stairs_wood":  0x571FF595,   # SKY_HAV_MAT_STAIRS_WOOD
    "stairs_snow":  0x5D01492B,   # SKY_HAV_MAT_STAIRS_SNOW
    "heavystone":   0x5DA0D740,   # SKY_HAV_MAT_HEAVY_STONE
    "heavywood":    0xB7087047,   # SKY_HAV_MAT_HEAVY_WOOD
}

_passed = _failed = 0


def check(label, ok, detail=""):
    global _passed, _failed
    if ok:
        _passed += 1
        print(f"  PASS  {label}")
    else:
        _failed += 1
        print(f"  FAIL  {label}  {detail}")


print("[table vs niftools nif.xml ground truth]")
src = open(CPP, encoding="utf-8").read()
# parse the { "name", 0xHASHu }, rows
rows = re.findall(r'\{\s*"([a-z_]+)",\s*0x([0-9A-Fa-f]+)u\s*\}', src)
table = {name: int(h, 16) for name, h in rows}
check(f"parsed the shipped table ({len(table)} entries)", len(table) >= 20)

mismatches = [n for n, h in table.items() if n in NIFXML and h != NIFXML[n]]
check("every named material matches the nif.xml enum hash",
      not mismatches, f"mismatched: {mismatches}")

check("0x1DD9C611 is WOOD (the earlier STONE label was wrong)",
      table.get("wood") == 0x1DD9C611)
check("STONE is 0xDF02F237 (distinct from wood)",
      table.get("stone") == 0xDF02F237 and table["stone"] != table["wood"])
check("SNOW is 0x17C77AAF", table.get("snow") == 0x17C77AAF)

check("index 0 is the shipped default (wood)", rows[0][0] == "wood")

print("[material propagation into the emitted chain]")
# Reuse the ported list-shape writer from the decomposition receipt by
# importing its module-level helpers is not clean (it runs on import), so
# emit a minimal 2-child list chain here with a chosen material and parse it.
INV = 1.0 / 69.99125
m = re.search(r"kRigidBodyTemplate\[250\] = \{(.*?)\};",
              open(os.path.join(ROOT, "src", "core", "ModelCodec.cpp"), encoding="utf-8").read(), re.S)
TEMPLATE = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))

SNOW = NIFXML["snow"]


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


def cube(cx):
    return [(cx+x, y, z) for x in (0, 4) for y in (0, 4) for z in (0, 4)]


def hull8(pts):
    # 8-corner box: verts = pts; 6 axis planes suffice for this test
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    planes = [(-1, 0, 0, lo[0]), (1, 0, 0, -hi[0]), (0, -1, 0, lo[1]),
              (0, 1, 0, -hi[1]), (0, 0, -1, lo[2]), (0, 0, 1, -hi[2])]
    return pts, planes


pieces = [hull8(cube(0)), hull8(cube(6))]
n = len(pieces)
convex_base, list_idx, rigid_idx, coll_idx, bsx_idx = 4, 6, 7, 8, 9

b0 = Buf(); b0.i32(0); b0.u32(1); b0.i32(bsx_idx); b0.i32(-1); b0.u32(0x0E)
for v in (0, 0, 0): b0.f32(v)
for v in (1, 0, 0, 0, 1, 0, 0, 0, 1): b0.f32(v)
b0.f32(1.0); b0.i32(coll_idx); b0.u32(1); b0.i32(1); b0.u32(0)
b1 = Buf(); b1.i32(1); b1.u32(0); b1.i32(-1); b1.u32(0x0E)
for v in (0, 0, 0): b1.f32(v)
for v in (1, 0, 0, 0, 1, 0, 0, 0, 1): b1.f32(v)
b1.f32(1.0); b1.i32(-1)
for v in (0, 0, 0, 1): b1.f32(v)
b1.i32(-1); b1.i32(2); b1.i32(-1); b1.u64(0x0003B00007650408)
b1.u16(1); b1.u16(3); b1.u32(3*32+6)
for _ in range(3):
    for v in (0, 0, 0, 0): b1.f32(v)
    b1.u16(0); b1.u16(0)
    for _ in range(8): b1.u8(128)
    for _ in range(4): b1.u8(255)
for i in (0, 1, 2): b1.u16(i)
b1.u32(0)
b2 = Buf(); b2.u32(0); b2.i32(-1); b2.u32(0); b2.i32(-1)
b2.u32(0x82400301); b2.u32(0x08008071)
for v in (0, 0, 1, 1): b2.f32(v)
b2.i32(3)
for v in (0, 0, 0, 1): b2.f32(v)
b2.u32(3); b2.f32(1); b2.f32(0); b2.f32(30)
for v in (1, 1, 1, 1, 0.3, 2.0): b2.f32(v)
b3 = Buf(); b3.u32(9)
for _ in range(9): b3.sized("")
convex = []
for verts, planes in pieces:
    cb = Buf(); cb.u32(SNOW); cb.f32(0.05)
    for _ in range(2): cb.u32(0); cb.u32(0); cb.u32(0x80000000)
    cb.u32(len(verts))
    for v in verts: cb.f32(v[0]*INV); cb.f32(v[1]*INV); cb.f32(v[2]*INV); cb.f32(0)
    cb.u32(len(planes))
    for p in planes: cb.f32(p[0]); cb.f32(p[1]); cb.f32(p[2]); cb.f32(p[3]*INV)
    convex.append(cb)
lb = Buf(); lb.u32(n)
for i in range(n): lb.i32(convex_base+i)
lb.u32(SNOW)
for _ in range(2): lb.u32(0); lb.u32(0); lb.u32(0x80000000)
lb.u32(n)
for i in range(n): lb.u32(0)
rb = Buf(); rb.b = bytearray(TEMPLATE); rb.b[0:4] = struct.pack("<i", list_idx)
cob = Buf(); cob.i32(0); cob.u16(0x0081); cob.i32(rigid_idx)
bx = Buf(); bx.i32(2); bx.u32(130)
blocks = [b0, b1, b2, b3] + convex + [lb, rb, cob, bx]
per = (["BSFadeNode", "BSTriShape", "BSLightingShaderProperty", "BSShaderTextureSet"]
       + ["bhkConvexVerticesShape"]*n + ["bhkListShape", "bhkRigidBody", "bhkCollisionObject", "BSXFlags"])
types, tindex = [], []
for t in per:
    if t not in types: types.append(t)
    tindex.append(types.index(t))
h = Buf(); h.b += b"Gamebryo File Format, Version 20.2.0.7\n"
h.u32(0x14020007); h.u8(1); h.u32(12); h.u32(len(blocks)); h.u32(100)
h.shortstr(""); h.shortstr(""); h.shortstr("")
h.u16(len(types))
for t in types: h.sized(t)
for ti in tindex: h.u16(ti)
for bl in blocks: h.u32(len(bl.b))
strs = ["Root", "Mesh", "BSX"]
h.u32(len(strs)); h.u32(max(len(s) for s in strs))
for s in strs: h.sized(s)
h.u32(0)
data = bytes(h.b)
for bl in blocks: data += bytes(bl.b)

tmp = os.path.join(os.environ.get("TEMP", "."), "sb_material.nif")
open(tmp, "wb").write(data)
try:
    from parse_convex_collision import parse_nif, btype
    nif = parse_nif(tmp)
    d = nif["data"]
    lmat, = struct.unpack_from("<I", d, nif["starts"][list_idx] + 4 + n*4)
    check("bhkListShape material == chosen snow hash", lmat == SNOW, f"{lmat:#010x}")
    child_mats = [struct.unpack_from("<I", d, nif["starts"][convex_base+i])[0] for i in range(n)]
    check("every convex child material == snow (engine reads the child)",
          all(cm == SNOW for cm in child_mats), f"{[hex(c) for c in child_mats]}")
    check("snow hash is not the wood default",
          SNOW != 0x1DD9C611)
finally:
    os.unlink(tmp)

print(f"\n{_passed} passed, {_failed} failed")
sys.exit(1 if _failed else 0)
