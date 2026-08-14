#!/usr/bin/env python3
"""Offline validation of the exact mesh-collision chain assembly
(ModelCodec meshCollision mode: root/BSTriShape/shader/textureset +
bhkCompressedMeshShapeData -> bhkCompressedMeshShape -> bhkMoppBvTreeShape
(empty MOPP) -> bhkRigidBody -> bhkCollisionObject -> BSXFlags).

The CMS and MOPP block layouts were recovered byte-exact from real files
(sovspit01, dlc2telmithryn): CMS 56 bytes consumed, MOPP 41-byte header +
moppDataSize bytes, buildType before the bytecode. A ported writer mirrors
the C++ meshCollision emission; the F18/CMSD parsers walk the chain back and
confirm every ref, that the CMSD decodes to the input mesh, and that the
MOPP is the empty placeholder awaiting NifSkope finalize.

Also checks source consistency: the shipped ModelCodec.cpp carries the CMS
constants and the empty-MOPP placeholder.

Game-bound: NifSkope "Update MOPP Code" then walk on it (protocol sec 13).
"""

import os
import re
import struct
import sys

from nif_test_support import btype, parse_cmsd, parse_nif


def parse_cms(d, off, sz):
    o = [off]
    def i32(): v, = struct.unpack_from("<i", d, o[0]); o[0] += 4; return v
    def u32(): v, = struct.unpack_from("<I", d, o[0]); o[0] += 4; return v
    def f32(): v, = struct.unpack_from("<f", d, o[0]); o[0] += 4; return v
    def vec4(): v = struct.unpack_from("<4f", d, o[0]); o[0] += 16; return v
    r = {"target": i32(), "userData": u32(), "radius": f32(), "unkFloat": u32(),
         "scale": vec4(), "radiusCopy": f32(), "scaleCopy": vec4(), "data": i32()}
    r["_consumed"] = o[0] - off
    return r


def parse_mopp(d, off, sz):
    o = [off]
    def i32(): v, = struct.unpack_from("<i", d, o[0]); o[0] += 4; return v
    def u32(): v, = struct.unpack_from("<I", d, o[0]); o[0] += 4; return v
    def f32(): v, = struct.unpack_from("<f", d, o[0]); o[0] += 4; return v
    r = {"shape": i32(), "unk1": u32(), "unk2": u32(), "unk3": u32(), "unk4": f32(),
         "moppDataSize": u32(), "origin": (f32(), f32(), f32()), "scale": f32()}
    r["buildType"] = d[o[0]]; o[0] += 1
    o[0] += r["moppDataSize"]
    r["_consumed"] = o[0] - off
    return r

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MC = os.path.join(ROOT, "src", "core", "ModelCodec.cpp")
QUANT = 0.001
SNOW = 0x17C77AAF

_passed = _failed = 0


def check(label, ok, detail=""):
    global _passed, _failed
    if ok:
        _passed += 1
        print(f"  PASS  {label}")
    else:
        _failed += 1
        print(f"  FAIL  {label}  {detail}")


class Buf:
    def __init__(s): s.b = bytearray()
    def u8(s, x): s.b.append(x & 0xFF)
    def u16(s, x): s.b += struct.pack("<H", x & 0xFFFF)
    def u32(s, x): s.b += struct.pack("<I", x & 0xFFFFFFFF)
    def i32(s, x): s.b += struct.pack("<i", x)
    def u64(s, x): s.b += struct.pack("<Q", x)
    def f32(s, x): s.b += struct.pack("<f", x)
    def raw(s, d): s.b += d
    def sized(s, t): s.u32(len(t)); s.b += t.encode("latin1")
    def shortstr(s, t): s.u8(len(t)); s.b += t.encode("latin1")


def build_cmsd(positions, indices, material):
    lo = [min(p[k] for p in positions) for k in range(3)]
    hi = [max(p[k] for p in positions) for k in range(3)]

    def q(w, base):
        return max(0, min(0xFFFF, round((w - base) / QUANT)))
    b = bytearray()
    P = lambda f, *v: b.extend(struct.pack(f, *v))
    P("<IIII", 17, 18, 0x3FFFF, 0x1FFFF); P("<f", QUANT)
    P("<4f", lo[0], lo[1], lo[2], 0.0); P("<4f", hi[0], hi[1], hi[2], 0.0)
    P("<BB", 0, 1); P("<III", 0, 0, 0)
    P("<I", 1); P("<II", material, 0); P("<I", 0)
    P("<I", 1); P("<4f", 0, 0, 0, 1); P("<4f", 0, 0, 0, 1)
    P("<I", 0); P("<I", 0); P("<I", 1)
    P("<4f", lo[0], lo[1], lo[2], 0.0); P("<I", 0); P("<HH", 0xFFFF, 0)
    P("<I", len(positions) * 3)
    for p in positions:
        P("<HHH", q(p[0], lo[0]), q(p[1], lo[1]), q(p[2], lo[2]))
    P("<I", len(indices))
    for i in indices:
        P("<H", i)
    P("<I", 0); P("<I", 0); P("<I", 0)
    return bytes(b), lo


TEMPLATE = bytes(int(x, 16) for x in re.findall(
    r"0x([0-9A-Fa-f]{2})", re.search(r"kRigidBodyTemplate\[250\] = \{(.*?)\};",
    open(MC, encoding="utf-8").read(), re.S).group(1)))


def emit_chain(positions, indices, material):
    cmsd, lo = build_cmsd(positions, indices, material)
    cmsd_i, cms_i, mopp_i, rigid_i, coll_i, bsx_i = 4, 5, 6, 7, 8, 9
    b0 = Buf(); b0.i32(0); b0.u32(1); b0.i32(bsx_i); b0.i32(-1); b0.u32(0x0E)
    for v in (0, 0, 0): b0.f32(v)
    for v in (1, 0, 0, 0, 1, 0, 0, 0, 1): b0.f32(v)
    b0.f32(1.0); b0.i32(coll_i); b0.u32(1); b0.i32(1); b0.u32(0)
    # minimal BSTriShape (1 tri, geometry not under test)
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
    b4 = Buf(); b4.raw(cmsd)
    b5 = Buf()
    b5.i32(0); b5.u32(0); b5.f32(0.005); b5.u32(0)
    b5.f32(1); b5.f32(1); b5.f32(1); b5.f32(0)
    b5.f32(0.005); b5.f32(1); b5.f32(1); b5.f32(1); b5.f32(0)
    b5.i32(cmsd_i)
    b6 = Buf()
    b6.i32(cms_i); b6.u32(0); b6.u32(0); b6.u32(0); b6.f32(1.0)
    b6.u32(0); b6.f32(0); b6.f32(0); b6.f32(0); b6.f32(0); b6.u8(2)
    b7 = Buf(); b7.raw(bytearray(TEMPLATE)); b7.b[0:4] = struct.pack("<i", mopp_i)
    b8 = Buf(); b8.i32(0); b8.u16(0x0081); b8.i32(rigid_i)
    b9 = Buf(); b9.i32(2); b9.u32(130)
    blocks = [b0, b1, b2, b3, b4, b5, b6, b7, b8, b9]
    per = ["BSFadeNode", "BSTriShape", "BSLightingShaderProperty", "BSShaderTextureSet",
           "bhkCompressedMeshShapeData", "bhkCompressedMeshShape", "bhkMoppBvTreeShape",
           "bhkRigidBody", "bhkCollisionObject", "BSXFlags"]
    types, tindex = [], []
    for t in per:
        if t not in types:
            types.append(t)
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
    out = bytes(h.b)
    for bl in blocks: out += bytes(bl.b)
    return out, (cmsd_i, cms_i, mopp_i, rigid_i, coll_i)


# cube mesh
s = 10.0
verts = [(x*s, y*s, z*s) for x in (0, 1) for y in (0, 1) for z in (0, 1)]
faces = [(0,1,3),(0,3,2),(4,6,7),(4,7,5),(0,4,5),(0,5,1),
         (2,3,7),(2,7,6),(0,2,6),(0,6,4),(1,5,7),(1,7,3)]
idx = [i for f in faces for i in f]

print("[mesh-collision chain assembly]")
data, (cmsd_i, cms_i, mopp_i, rigid_i, coll_i) = emit_chain(verts, idx, SNOW)
tmp = os.path.join(os.environ.get("TEMP", "."), "sb_meshcoll.nif")
open(tmp, "wb").write(data)
try:
    nif = parse_nif(tmp)
    d = nif["data"]
    types = [btype(nif, i) for i in range(nif["nblocks"])]
    check("10 blocks with the exact-mesh-collision chain types",
          types[4:] == ["bhkCompressedMeshShapeData", "bhkCompressedMeshShape",
                        "bhkMoppBvTreeShape", "bhkRigidBody", "bhkCollisionObject", "BSXFlags"],
          str(types[4:]))
    cms = parse_cms(d, nif["starts"][cms_i], nif["sizes"][cms_i])
    check("bhkCompressedMeshShape consumes exact, radius 0.005, data->CMSD",
          cms["_consumed"] == nif["sizes"][cms_i] and abs(cms["radius"]-0.005) < 1e-6
          and cms["data"] == cmsd_i)
    mopp = parse_mopp(d, nif["starts"][mopp_i], nif["sizes"][mopp_i])
    check("bhkMoppBvTreeShape consumes exact, shape->CMS, EMPTY MOPP, buildType=2",
          mopp["_consumed"] == nif["sizes"][mopp_i] and mopp["shape"] == cms_i
          and mopp["moppDataSize"] == 0 and mopp["buildType"] == 2,
          f"consumed={mopp['_consumed']}/{nif['sizes'][mopp_i]} size={mopp['moppDataSize']} bt={mopp['buildType']}")
    out = parse_cmsd(d, nif["starts"][cmsd_i], nif["sizes"][cmsd_i])
    check("CMSD consumes exact, one chunk, chosen material",
          out["_consumed"] == nif["sizes"][cmsd_i] and len(out["chunks"]) == 1
          and out["materials"][0][0] == SNOW)
    # decode chunk verts and check they match input within quant
    c = out["chunks"][0]; t = c["translation"]; vs = c["vertices"]
    dec = [(t[0]+vs[k]*QUANT, t[1]+vs[k+1]*QUANT, t[2]+vs[k+2]*QUANT) for k in range(0, len(vs), 3)]
    maxerr = max(max(abs(dec[i][k]-verts[i][k]) for k in range(3)) for i in range(len(verts)))
    check(f"CMSD geometry decodes to the input mesh within {QUANT} ({maxerr:.5f})", maxerr <= QUANT + 1e-6)
    rshape, = struct.unpack_from("<i", d, nif["starts"][rigid_i])
    ctarget, cflags, cbody = struct.unpack_from("<iHi", d, nif["starts"][coll_i])
    check("chain refs: rigidBody.shape->MOPP, collObj target=root/flags 0x0081/body->rigidBody",
          rshape == mopp_i and (ctarget, cflags, cbody) == (0, 0x0081, rigid_i))
    check("root: collisionRef -> collisionObject",
          struct.unpack_from("<i", d, nif["starts"][0] + 72)[0] == coll_i)
finally:
    os.unlink(tmp)

print("[source consistency]")
src = open(MC, encoding="utf-8").read()
check("shipped meshCollision emits CMS radius + empty MOPP placeholder",
      "cmsBuf.f32(0.005f)" in src and "moppBuf.u32(0);" in src
      and "buildType = BUILD_NOT_SET" in src.replace("//", "") or "moppBuf.u8(2)" in src)
check("shipped meshCollision wires rigidbody.shape -> moppIdx",
      "meshColl ? moppIdx" in src)

print(f"\n{_passed} passed, {_failed} failed")
sys.exit(1 if _failed else 0)
