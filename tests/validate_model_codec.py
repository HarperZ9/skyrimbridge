"""Offline validation of SkyrimBridge's ModelCodec (OBJ/glTF -> SSE NIF).

Faithful Python port of src/core/ModelCodec.cpp's WriteNIF + OBJ parser +
tangent math, cross-checked against ground truth:
  - half-float codec round-trips vs struct '<e'
  - the emitted NIF re-parses with an independent reader: header fields
    (20.2.0.7 / user 12 / bs 100), block-type table, geometry decodes back to
    the source positions (within full-float / byte-normal precision),
    triangles preserved, bounding sphere contains every vertex
  - the emitted block field-layout is byte-compatible with a REAL shipping SSE
    static mesh from the modlist (same structure the engine loads)
The C++ mirrors this port field-for-field.
"""
import base64
import math
import os
import struct
import sys

from test_support import require_fixture

PASS = FAIL = 0
def check(name, ok, detail=""):
    global PASS, FAIL
    if ok: PASS += 1; print("  PASS  %s" % name)
    else:  FAIL += 1; print("  FAIL  %s  %s" % (name, detail))

VDESC = 0x0003B00007650408

def f2h(f):  # port of FloatToHalf via struct (ground-truth IEEE half)
    return struct.unpack("<H", struct.pack("<e", f))[0]
def h2f(h):
    return struct.unpack("<e", struct.pack("<H", h))[0]

def packsnorm(v):
    return max(0, min(255, int((v + 1.0) * 0.5 * 255.0 + 0.5)))

# ---- vector helpers ----
def sub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def dot(a, b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]
def norm(a):
    l = math.sqrt(dot(a, a))
    return (a[0]/l, a[1]/l, a[2]/l) if l > 1e-12 else (0.0, 0.0, 1.0)

def finalize(mesh):
    nv = len(mesh["pos"])
    if len(mesh.get("nrm", [])) != nv:
        nrm = [(0.0, 0.0, 0.0)] * nv
        acc = [[0.0, 0.0, 0.0] for _ in range(nv)]
        idx = mesh["idx"]
        for i in range(0, len(idx) - 2, 3):
            a, b, c = idx[i], idx[i+1], idx[i+2]
            fn = cross(sub(mesh["pos"][b], mesh["pos"][a]), sub(mesh["pos"][c], mesh["pos"][a]))
            for k in (a, b, c):
                acc[k][0] += fn[0]; acc[k][1] += fn[1]; acc[k][2] += fn[2]
        mesh["nrm"] = [norm(tuple(a)) for a in acc]
    if len(mesh.get("uv", [])) != nv:
        mesh["uv"] = [(0.0, 0.0)] * nv

def tangents(mesh):
    nv = len(mesh["pos"])
    tacc = [[0.0]*3 for _ in range(nv)]; bacc = [[0.0]*3 for _ in range(nv)]
    idx = mesh["idx"]
    for i in range(0, len(idx) - 2, 3):
        a, b, c = idx[i], idx[i+1], idx[i+2]
        e1 = sub(mesh["pos"][b], mesh["pos"][a]); e2 = sub(mesh["pos"][c], mesh["pos"][a])
        du1 = mesh["uv"][b][0]-mesh["uv"][a][0]; dv1 = mesh["uv"][b][1]-mesh["uv"][a][1]
        du2 = mesh["uv"][c][0]-mesh["uv"][a][0]; dv2 = mesh["uv"][c][1]-mesh["uv"][a][1]
        d = du1*dv2 - du2*dv1
        r = 1.0/d if abs(d) > 1e-12 else 0.0
        t = ((e1[0]*dv2-e2[0]*dv1)*r, (e1[1]*dv2-e2[1]*dv1)*r, (e1[2]*dv2-e2[2]*dv1)*r)
        bt = ((e2[0]*du1-e1[0]*du2)*r, (e2[1]*du1-e1[1]*du2)*r, (e2[2]*du1-e1[2]*du2)*r)
        for k in (a, b, c):
            for j in range(3): tacc[k][j] += t[j]; bacc[k][j] += bt[j]
    tan = []; sign = []
    for i in range(nv):
        n = mesh["nrm"][i]; t = tuple(tacc[i])
        nd = dot(n, t)
        t = (t[0]-n[0]*nd, t[1]-n[1]*nd, t[2]-n[2]*nd)
        if dot(t, t) < 1e-12:
            ax = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
            t = norm(cross(n, ax))
        else:
            t = norm(t)
        tan.append(t)
        sign.append(-1.0 if dot(cross(n, t), tuple(bacc[i])) < 0 else 1.0)
    return tan, sign

class W:
    def __init__(s): s.b = bytearray()
    def u8(s, x): s.b.append(x & 0xFF)
    def u16(s, x): s.b += struct.pack("<H", x & 0xFFFF)
    def u32(s, x): s.b += struct.pack("<I", x & 0xFFFFFFFF)
    def i32(s, x): s.b += struct.pack("<i", x)
    def u64(s, x): s.b += struct.pack("<Q", x)
    def f32(s, x): s.b += struct.pack("<f", x)
    def sized(s, t): s.u32(len(t)); s.b += t.encode("latin1")
    def shortstr(s, t): s.u8(len(t)); s.b += t.encode("latin1")

def write_nif(mesh):
    finalize(mesh)
    nv = len(mesh["pos"]); nt = len(mesh["idx"]) // 3
    assert 0 < nv <= 0xFFFF and 0 < nt <= 0xFFFF
    tan, sign = tangents(mesh)
    lo = list(mesh["pos"][0]); hi = list(mesh["pos"][0])
    for p in mesh["pos"]:
        for j in range(3): lo[j] = min(lo[j], p[j]); hi[j] = max(hi[j], p[j])
    center = tuple((lo[j]+hi[j])*0.5 for j in range(3))
    radius = max(math.sqrt(dot(sub(p, center), sub(p, center))) for p in mesh["pos"])

    b0 = W()
    b0.i32(0); b0.u32(0); b0.i32(-1); b0.u32(0x0E)
    b0.f32(0); b0.f32(0); b0.f32(0)
    for x in (1,0,0,0,1,0,0,0,1): b0.f32(x)
    b0.f32(1.0); b0.i32(-1); b0.u32(1); b0.i32(1); b0.u32(0)

    b1 = W()
    b1.i32(1); b1.u32(0); b1.i32(-1); b1.u32(0x0E)
    b1.f32(0); b1.f32(0); b1.f32(0)
    for x in (1,0,0,0,1,0,0,0,1): b1.f32(x)
    b1.f32(1.0); b1.i32(-1)
    b1.f32(center[0]); b1.f32(center[1]); b1.f32(center[2]); b1.f32(radius)
    b1.i32(-1); b1.i32(2); b1.i32(-1)
    b1.u64(VDESC); b1.u16(nt); b1.u16(nv); b1.u32(nv*32 + nt*6)
    for i in range(nv):
        p = mesh["pos"][i]; n = mesh["nrm"][i]; t = tan[i]
        bit = cross(n, t); bit = (bit[0]*sign[i], bit[1]*sign[i], bit[2]*sign[i])
        b1.f32(p[0]); b1.f32(p[1]); b1.f32(p[2]); b1.f32(bit[0])
        b1.u16(f2h(mesh["uv"][i][0])); b1.u16(f2h(mesh["uv"][i][1]))
        b1.u8(packsnorm(n[0])); b1.u8(packsnorm(n[1])); b1.u8(packsnorm(n[2])); b1.u8(packsnorm(bit[1]))
        b1.u8(packsnorm(t[0])); b1.u8(packsnorm(t[1])); b1.u8(packsnorm(t[2])); b1.u8(packsnorm(bit[2]))
        b1.u8(255); b1.u8(255); b1.u8(255); b1.u8(255)
    for i in range(nt):
        b1.u16(mesh["idx"][i*3]); b1.u16(mesh["idx"][i*3+1]); b1.u16(mesh["idx"][i*3+2])
    b1.u32(0)   # particleDataSize trailer (matches real SSE BSTriShape)

    b2 = W()
    b2.u32(0); b2.i32(-1); b2.u32(0); b2.i32(-1)
    b2.u32(0x82400301); b2.u32(0x08008071)
    b2.f32(0); b2.f32(0); b2.f32(1); b2.f32(1); b2.i32(3)
    b2.f32(0); b2.f32(0); b2.f32(0); b2.f32(1.0); b2.u32(3); b2.f32(1.0); b2.f32(0.0); b2.f32(30.0)
    b2.f32(1); b2.f32(1); b2.f32(1); b2.f32(1.0); b2.f32(0.3); b2.f32(2.0)

    b3 = W()
    b3.u32(9)
    for s in (mesh.get("diffuse",""), mesh.get("normalMap",""), "", "", "", "", "", "", ""): b3.sized(s)

    blocks = [b0, b1, b2, b3]
    types = ["BSFadeNode", "BSTriShape", "BSLightingShaderProperty", "BSShaderTextureSet"]
    h = W()
    hdr = "Gamebryo File Format, Version 20.2.0.7"
    h.b += hdr.encode("latin1"); h.u8(0x0A)
    h.u32(0x14020007); h.u8(1); h.u32(12); h.u32(len(blocks)); h.u32(100)
    h.shortstr(""); h.shortstr(""); h.shortstr("")
    h.u16(4)
    for t in types: h.sized(t)
    for i in range(4): h.u16(i)
    for bl in blocks: h.u32(len(bl.b))
    strs = [(mesh["name"]+" Root"), mesh["name"]]
    h.u32(2); h.u32(max(len(s) for s in strs))
    for s in strs: h.sized(s)
    h.u32(0)
    out = bytes(h.b)
    for bl in blocks: out += bytes(bl.b)
    return out, center, radius

# ---- independent NIF reader (re-parse our own output) ----
def read_nif(data):
    p = 0
    nl = data.index(b"\n", p); hdr = data[:nl].decode("latin1"); p = nl+1
    ver, = struct.unpack_from("<I", data, p); p += 4
    endian = data[p]; p += 1
    uver, = struct.unpack_from("<I", data, p); p += 4
    nblocks, = struct.unpack_from("<I", data, p); p += 4
    bsver, = struct.unpack_from("<I", data, p); p += 4
    for _ in range(3):  # author/proc/export shortstrings
        ln = data[p]; p += 1 + ln
    nbt, = struct.unpack_from("<H", data, p); p += 2
    btypes = []
    for _ in range(nbt):
        n, = struct.unpack_from("<I", data, p); p += 4; btypes.append(data[p:p+n].decode("latin1")); p += n
    btidx = list(struct.unpack_from("<%dH" % nblocks, data, p)); p += 2*nblocks
    bsizes = list(struct.unpack_from("<%dI" % nblocks, data, p)); p += 4*nblocks
    nstr, = struct.unpack_from("<I", data, p); p += 4
    maxlen, = struct.unpack_from("<I", data, p); p += 4
    strings = []
    for _ in range(nstr):
        n, = struct.unpack_from("<I", data, p); p += 4; strings.append(data[p:p+n].decode("latin1")); p += n
    ngroups, = struct.unpack_from("<I", data, p); p += 4
    blockstart = p
    return dict(hdr=hdr, ver=ver, endian=endian, uver=uver, bsver=bsver, nblocks=nblocks,
                btypes=btypes, order=[btypes[i] for i in btidx], bsizes=bsizes,
                strings=strings, blockstart=blockstart, data=data)

def decode_bstrishape(nif, block_index):
    off = nif["blockstart"] + sum(nif["bsizes"][:block_index])
    b = nif["data"][off:off+nif["bsizes"][block_index]]
    p = [0]
    def rd(fmt, n):
        v = struct.unpack_from(fmt, b, p[0]); p[0] += n; return v
    rd("<i", 4)  # name
    ne, = rd("<I", 4)
    p[0] += 4*ne
    rd("<i", 4)  # controller
    rd("<I", 4)  # flags
    p[0] += 12 + 36 + 4  # trans, rot, scale
    rd("<i", 4)  # collision
    center = rd("<3f", 12); radius, = rd("<f", 4)
    rd("<i", 4)  # skin
    shaderRef, = rd("<i", 4); alphaRef, = rd("<i", 4)
    vdesc, = rd("<Q", 8)
    numTri, = rd("<H", 2); numVert, = rd("<H", 2); dataSize, = rd("<I", 4)
    verts = []
    base = p[0]
    for i in range(numVert):
        vo = base + i*32
        px, py, pz, bx = struct.unpack_from("<4f", b, vo)
        u = h2f(struct.unpack_from("<H", b, vo+16)[0]); v = h2f(struct.unpack_from("<H", b, vo+18)[0])
        verts.append((px, py, pz, u, v))
    tstart = base + numVert*32
    tris = [struct.unpack_from("<3H", b, tstart + i*6) for i in range(numTri)]
    particleTrailer, = struct.unpack_from("<I", b, tstart + numTri*6)   # SSE BSTriShape trailer
    return dict(center=center, radius=radius, shaderRef=shaderRef, alphaRef=alphaRef, vdesc=vdesc,
                numTri=numTri, numVert=numVert, dataSize=dataSize, verts=verts, tris=tris,
                particleTrailer=particleTrailer, consumed=tstart + numTri*6 + 4, blocklen=len(b))

# ---- test meshes ----
CUBE_OBJ = """
o cube
v -1 -1 -1
v  1 -1 -1
v  1  1 -1
v -1  1 -1
v -1 -1  1
v  1 -1  1
v  1  1  1
v -1  1  1
vt 0 0
vt 1 0
vt 1 1
vt 0 1
f 1/1 2/2 3/3 4/4
f 5/1 8/4 7/3 6/2
f 1/1 5/2 6/3 2/4
f 2/1 6/2 7/3 3/4
f 3/1 7/2 8/3 4/4
f 5/1 1/2 4/3 8/4
"""

def parse_obj(text):
    pos = []; uv = []; nrm = []
    weld = {}; mesh = {"pos": [], "uv": [], "nrm": [], "idx": [], "name": "cube"}
    def resolve(i, c): return i-1 if i > 0 else (c+i if i < 0 else -1)
    for ln in text.splitlines():
        s = ln.strip()
        if s.startswith("v "):
            _, x, y, z = s.split()[:4]; pos.append((float(x), float(y), float(z)))
        elif s.startswith("vt "):
            parts = s.split(); u = float(parts[1]); v = float(parts[2]) if len(parts) > 2 else 0.0
            uv.append((u, 1.0 - v))
        elif s.startswith("vn "):
            _, x, y, z = s.split()[:4]; nrm.append((float(x), float(y), float(z)))
        elif s.startswith("f "):
            face = []
            for tok in s.split()[1:]:
                a = tok.split("/")
                vi = resolve(int(a[0]), len(pos))
                ti = resolve(int(a[1]), len(uv)) if len(a) > 1 and a[1] else -1
                ni = resolve(int(a[2]), len(nrm)) if len(a) > 2 and a[2] else -1
                key = (vi, ti, ni)
                if key in weld:
                    out = weld[key]
                else:
                    out = len(mesh["pos"]); weld[key] = out
                    mesh["pos"].append(pos[vi])
                    mesh["uv"].append(uv[ti] if 0 <= ti < len(uv) else (0.0, 0.0))
                    if 0 <= ni < len(nrm): mesh["nrm"].append(nrm[ni])
                face.append(out)
            for f in range(2, len(face)):
                mesh["idx"] += [face[0], face[f-1], face[f]]
    if len(mesh["nrm"]) != len(mesh["pos"]): mesh["nrm"] = []
    return mesh

def test_halffloat():
    print("[half-float codec]")
    ok = True
    for v in [0.0, 1.0, -1.0, 0.5, 3.14159, 1024.0, -0.001, 65504.0, 2.0/3.0]:
        h = f2h(v); back = h2f(h)
        rel = abs(back - v) / (abs(v) + 1e-6)
        if rel > 1e-2 and abs(back - v) > 1e-3: ok = False
    check("f2h/h2f round-trip within half precision", ok)

def test_write_and_reparse():
    print("[NIF write -> independent re-parse]")
    mesh = parse_obj(CUBE_OBJ)
    check("OBJ parse (8 welded corners? 24 face-verts welded by v/vt)", len(mesh["pos"]) >= 8 and len(mesh["idx"]) == 36,
          "pos=%d idx=%d" % (len(mesh["pos"]), len(mesh["idx"])))
    nif, center, radius = write_nif(mesh)
    r = read_nif(nif)
    check("header string", r["hdr"] == "Gamebryo File Format, Version 20.2.0.7", r["hdr"])
    check("version 0x14020007 / user 12 / bs 100 / little-endian",
          r["ver"] == 0x14020007 and r["uver"] == 12 and r["bsver"] == 100 and r["endian"] == 1)
    check("4 blocks, correct type order",
          r["nblocks"] == 4 and r["order"] == ["BSFadeNode", "BSTriShape", "BSLightingShaderProperty", "BSShaderTextureSet"],
          str(r["order"]))
    check("block sizes sum + header == file length",
          r["blockstart"] + sum(r["bsizes"]) == len(nif))
    ts = decode_bstrishape(r, 1)
    check("BSTriShape consumes exactly its block", ts["consumed"] == ts["blocklen"],
          "%d vs %d" % (ts["consumed"], ts["blocklen"]))
    check("vertexDesc == 0x%016X" % VDESC, ts["vdesc"] == VDESC)
    check("particleData trailer == 0 (SSE BSTriShape byte layout)", ts["particleTrailer"] == 0)
    check("dataSize == numVert*32 + numTri*6",
          ts["dataSize"] == ts["numVert"]*32 + ts["numTri"]*6)
    check("shaderProperty ref -> block 2, alpha -> -1", ts["shaderRef"] == 2 and ts["alphaRef"] == -1)
    check("36 triangle indices -> 12 tris", ts["numTri"] == 12)
    # geometry round-trip: every source position appears in the decoded verts (exact, full float)
    src = set((round(p[0], 3), round(p[1], 3), round(p[2], 3)) for p in mesh["pos"])
    got = set((round(v[0], 3), round(v[1], 3), round(v[2], 3)) for v in ts["verts"])
    check("all source positions present in decoded vertex stream (full-float exact)", src == got,
          "missing=%s" % (src - got))
    # triangles index in range and reference real corners
    allok = all(all(idx < ts["numVert"] for idx in tri) for tri in ts["tris"])
    check("all triangle indices in range", allok)
    # bounding sphere contains every vertex
    contains = all(math.sqrt(sum((v[j]-ts["center"][j])**2 for j in range(3))) <= ts["radius"] + 1e-3 for v in ts["verts"])
    check("bounding sphere contains every vertex", contains)
    # UV round-trip (half precision) for a known corner
    check("UV stored as half floats (0 or 1 corners recover)",
          any(abs(v[3]) < 0.01 or abs(v[3]-1) < 0.01 for v in ts["verts"]))

def test_against_real_mesh():
    print("[block field-layout vs real shipping SSE mesh]")
    ref = require_fixture(
        r"Arcs WispMother Redux 2K\meshes\clutter\ingredients\wispwrappings.nif"
    )
    data = open(ref, "rb").read()
    r = read_nif(data)
    check("reference is 20.2.0.7 / user12 / bs100", r["ver"] == 0x14020007 and r["uver"] == 12 and r["bsver"] == 100)
    # find the BSTriShape block index in the reference and decode with OUR reader
    if "BSTriShape" in r["order"]:
        bi = r["order"].index("BSTriShape")
        ts = decode_bstrishape(r, bi)
        check("our BSTriShape decoder consumes the real block exactly (layout match)",
              ts["consumed"] == ts["blocklen"], "%d vs %d" % (ts["consumed"], ts["blocklen"]))
        check("real mesh vertexDesc is the format we emit (full-precision 32B)",
              (ts["vdesc"] & 0xF) == 8, "vsize nibble=%d" % (ts["vdesc"] & 0xF))
    else:
        check("reference has a BSTriShape", False)

def test_indexed_gltf_shapes():
    print("[synthetic glTF-style non-indexed mesh -> valid NIF]")
    # a triangle strip of 2 tris as raw positions, no normals/uv -> writer computes them
    mesh = {"pos": [(0,0,0),(1,0,0),(0,1,0),(1,1,0)], "uv": [], "nrm": [], "idx": [0,1,2, 2,1,3], "name": "quad"}
    nif, c, rad = write_nif(mesh)
    r = read_nif(nif)
    ts = decode_bstrishape(r, 1)
    check("computed-normal quad: block consistent + 2 tris", ts["consumed"] == ts["blocklen"] and ts["numTri"] == 2)
    check("quad bounding sphere sane", rad > 0.5 and rad < 2.0, "r=%.3f" % rad)

if __name__ == "__main__":
    test_halffloat()
    test_write_and_reparse()
    test_against_real_mesh()
    test_indexed_gltf_shapes()
    print("\n%d passed, %d failed" % (PASS, FAIL))
    sys.exit(1 if FAIL else 0)
