#!/usr/bin/env python3
"""Offline validation of the BC7 codec (src/core/TextureBC7.cpp) and the
DX10-header DDS paths (src/core/TextureCodec.cpp).

Method, same as the other codec receipts: a faithful Python port of the C++
is cross-checked against Pillow's independent native BCn decoder. The
partition/anchor tables are NOT duplicated here: they are parsed out of
TextureBC7.cpp at run time, so the exact table data compiled into the DLL is
what Pillow verifies. Coverage:

- per-mode random-block fuzz (all 8 modes; random partitions, rotations,
  p-bits, indices) decoded by our model vs Pillow, byte-exact
- real modlist BC7 (DX10 dxgiFormat 98/99) textures vs Pillow, byte-exact
- DX10-wrapped BC1/BC3 (rewrapped from real legacy DXT1/DXT5 files) and
  DX10 RGBA8/BGRA8 vs Pillow
- refusal paths: reserved mode -> transparent black (spec), cubemap/array
  DX10 files -> honest decline
- mode-6 encoder: lossless on exactly-representable blocks, anchor-swap
  path, PIL-agreed decode of encoded output, PSNR floor (value reported)

Scope, stated honestly: encode is mode 6 only (baseline tier); BC4/BC5/BC6H
and cubemaps/volumes stay undecoded; sRGB payloads pass through unconverted.
"""

import io
import math
import os
import random
import re
import struct
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPP = os.path.join(ROOT, "src", "core", "TextureBC7.cpp")
MODS = r"E:\Modlists\SkyGroundChronicles\mods"

# ── tables parsed from the shipped C++ ─────────────────────────────────────
_src = open(CPP, encoding="utf-8").read()


def _parse_2d(name, rows, cols):
    m = re.search(re.escape(name) + r"\[\d+\]\[\d+\]\s*=\s*\{(.*?)\n\s*\};", _src, re.S)
    assert m, name
    vals = re.findall(r"\{\s*([0-9,\s]+?)\s*\}", m.group(1))
    table = [list(map(int, v.replace(" ", "").replace("\n", "").split(","))) for v in vals]
    assert len(table) == rows and all(len(r) == cols for r in table), name
    return table


def _parse_1d(name):
    m = re.search(re.escape(name) + r"\[64\]\s*=\s*\{\s*([0-9,\s]+?)\s*\};", _src)
    assert m, name
    v = list(map(int, m.group(1).replace(" ", "").split(",")))
    assert len(v) == 64, name
    return v


PARTITION2 = _parse_2d("kPartition2", 64, 16)
PARTITION3 = _parse_2d("kPartition3", 64, 16)
ANCHOR2_1 = _parse_1d("kAnchor2_1")
ANCHOR3_1 = _parse_1d("kAnchor3_1")
ANCHOR3_2 = _parse_1d("kAnchor3_2")

# subsets, partBits, pBits, rotBits, idxModeBits, idxPrec, idxPrec2, prec, precP
MODES = [
    (3, 4, 6, 0, 0, 3, 0, (4, 4, 4, 0), (5, 5, 5, 0)),
    (2, 6, 2, 0, 0, 3, 0, (6, 6, 6, 0), (7, 7, 7, 0)),
    (3, 6, 0, 0, 0, 2, 0, (5, 5, 5, 0), (5, 5, 5, 0)),
    (2, 6, 4, 0, 0, 2, 0, (7, 7, 7, 0), (8, 8, 8, 0)),
    (1, 0, 0, 2, 1, 2, 3, (5, 5, 5, 6), (5, 5, 5, 6)),
    (1, 0, 0, 2, 0, 2, 2, (7, 7, 7, 8), (7, 7, 7, 8)),
    (1, 0, 2, 0, 0, 4, 0, (7, 7, 7, 7), (8, 8, 8, 8)),
    (2, 6, 4, 0, 0, 2, 0, (5, 5, 5, 5), (6, 6, 6, 6)),
]
W2 = [0, 21, 43, 64]
W3 = [0, 9, 18, 27, 37, 46, 55, 64]
W4 = [0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64]


def weights(prec):
    return W2 if prec == 2 else W3 if prec == 3 else W4


def interp(a, b, w):
    return (a * (64 - w) + b * w + 32) >> 6


def unquant(v, prec):
    v <<= (8 - prec)
    return (v | (v >> prec)) & 0xFF


def subset_of(subsets, shape, i):
    if subsets == 2:
        return PARTITION2[shape][i]
    if subsets == 3:
        return PARTITION3[shape][i]
    return 0


def is_anchor(subsets, shape, i):
    if i == 0:
        return True
    if subsets == 2:
        return i == ANCHOR2_1[shape]
    if subsets == 3:
        return i == ANCHOR3_1[shape] or i == ANCHOR3_2[shape]
    return False


class BitReader:
    def __init__(self, data, pos):
        self.d, self.pos = data, pos

    def bits(self, n):
        v = 0
        for i in range(n):
            v |= ((self.d[self.pos >> 3] >> (self.pos & 7)) & 1) << i
            self.pos += 1
        return v


def decode_block(blk):
    """Port of BC7::DecodeBlock -> [16][4]."""
    if blk[0] == 0:
        return [[0, 0, 0, 0] for _ in range(16)]
    mode = 0
    while not (blk[0] >> mode) & 1:
        mode += 1
    subsets, partBits, pBits, rotBits, idxModeBits, idxPrec, idxPrec2, prec, precP = MODES[mode]
    br = BitReader(blk, mode + 1)
    nEp = subsets * 2
    shape = br.bits(partBits)
    rot = br.bits(rotBits)
    idxMode = br.bits(idxModeBits)

    e = [[0, 0, 0, 0] for _ in range(nEp)]
    for ch in range(4):
        for i in range(nEp):
            if prec[ch]:
                e[i][ch] = br.bits(prec[ch])
    if pBits:
        P = [br.bits(1) for _ in range(pBits)]
        for i in range(nEp):
            pi = i * pBits // nEp
            for ch in range(4):
                if prec[ch] != precP[ch]:
                    e[i][ch] = (e[i][ch] << 1) | P[pi]
    for i in range(nEp):
        for ch in range(3):
            e[i][ch] = unquant(e[i][ch], precP[ch])
        e[i][3] = unquant(e[i][3], precP[3]) if precP[3] else 255

    w1 = [br.bits(idxPrec - 1 if is_anchor(subsets, shape, i) else idxPrec) for i in range(16)]
    w2 = [0] * 16
    if idxPrec2:
        w2 = [br.bits(idxPrec2 if i else idxPrec2 - 1) for i in range(16)]

    out = []
    for i in range(16):
        s = subset_of(subsets, shape, i)
        a, b = e[s * 2], e[s * 2 + 1]
        if not idxPrec2:
            wc = wa = w1[i]; pc = pa = idxPrec
        elif idxMode == 0:
            wc, pc, wa, pa = w1[i], idxPrec, w2[i], idxPrec2
        else:
            wc, pc, wa, pa = w2[i], idxPrec2, w1[i], idxPrec
        cw, aw = weights(pc), weights(pa)
        o = [interp(a[ch], b[ch], cw[wc]) for ch in range(3)] + [interp(a[3], b[3], aw[wa])]
        if rot == 1:
            o[0], o[3] = o[3], o[0]
        elif rot == 2:
            o[1], o[3] = o[3], o[1]
        elif rot == 3:
            o[2], o[3] = o[3], o[2]
        out.append(o)
    return out


def encode_block_mode6(px):
    """Port of BC7::EncodeBlockMode6 -> 16 bytes."""
    bi, bj, best = 0, 0, -1
    for i in range(16):
        for j in range(i + 1, 16):
            d = sum((px[i][c] - px[j][c]) ** 2 for c in range(4))
            if d > best:
                best, bi, bj = d, i, j
    q = [[0] * 4, [0] * 4]
    pb = [0, 0]
    for epi, s in enumerate((px[bi], px[bj])):
        bestErr = None
        for p in (0, 1):
            cand, err = [], 0
            for c in range(4):
                v = min(127, max(0, (s[c] - p + 1) >> 1))
                cand.append(v)
                err += (s[c] - ((v << 1) | p)) ** 2
            if bestErr is None or err < bestErr:
                bestErr, pb[epi], q[epi] = err, p, cand
    r0 = [(q[0][c] << 1) | pb[0] for c in range(4)]
    r1 = [(q[1][c] << 1) | pb[1] for c in range(4)]
    pal = [[interp(r0[c], r1[c], W4[k]) for c in range(4)] for k in range(16)]
    idx = []
    for i in range(16):
        bd, bk = None, 0
        for k in range(16):
            d = sum((px[i][c] - pal[k][c]) ** 2 for c in range(4))
            if bd is None or d < bd:
                bd, bk = d, k
        idx.append(bk)
    if idx[0] & 8:
        q[0], q[1], pb[0], pb[1] = q[1], q[0], pb[1], pb[0]
        idx = [15 - v for v in idx]
    out = bytearray(16)
    pos = 0

    def put(v, n):
        nonlocal pos
        for i in range(n):
            if (v >> i) & 1:
                out[pos >> 3] |= 1 << (pos & 7)
            pos += 1

    put(1 << 6, 7)
    for c in range(4):
        put(q[0][c], 7)
        put(q[1][c], 7)
    put(pb[0], 1)
    put(pb[1], 1)
    put(idx[0], 3)
    for i in range(1, 16):
        put(idx[i], 4)
    return bytes(out)


# ── DDS container helpers (mirror the C++ reader/writer contracts) ────────
def dds_dx10(w, h, dxgi, payload, mips=0):
    hdr = bytearray(b"DDS ")
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000
    if mips:
        flags |= 0x20000
    hdr += struct.pack("<IIIII", 124, flags, h, w, ((w + 3) // 4) * ((h + 3) // 4) * 16)
    hdr += struct.pack("<II", 0, mips)
    hdr += b"\0" * 44
    hdr += struct.pack("<II4s", 32, 0x4, b"DX10") + b"\0" * 20
    caps = 0x1000 | (0x8 | 0x400000 if mips else 0)
    hdr += struct.pack("<IIIII", caps, 0, 0, 0, 0)
    hdr += struct.pack("<IIIII", dxgi, 3, 0, 1, 0)
    return bytes(hdr) + payload


def decode_dds_ours(data):
    """Port of the C++ DecodeDDSImage DX10/legacy-BC paths. Returns
    (w, h, rgba bytes) or None where the C++ declines."""
    if data[:4] != b"DDS " or struct.unpack_from("<I", data, 4)[0] != 124:
        return None
    h, w = struct.unpack_from("<II", data, 12)
    pfFlags, fourCC = struct.unpack_from("<I4s", data, 80)
    off = 128
    kind = None
    if (pfFlags & 4) and fourCC == b"DX10":
        dxgi, dim, misc, arr = struct.unpack_from("<IIII", data, 128)
        if dim != 3 or arr > 1 or (misc & 4):
            return None
        off = 148
        kind = {71: "BC1", 72: "BC1", 77: "BC3", 78: "BC3", 98: "BC7", 99: "BC7",
                28: "RGBA8", 29: "RGBA8", 87: "BGRA8", 91: "BGRA8"}.get(dxgi)
        if kind is None:
            return None
    elif pfFlags & 4:
        kind = {b"DXT1": "BC1", b"DXT5": "BC3"}.get(fourCC)
        if kind is None:
            return None
    else:
        return None  # masked path covered by the earlier receipts

    out = bytearray(w * h * 4)
    if kind in ("RGBA8", "BGRA8"):
        for i in range(w * h):
            p = data[off + i * 4: off + i * 4 + 4]
            if kind == "BGRA8":
                out[i * 4: i * 4 + 4] = bytes((p[2], p[1], p[0], p[3]))
            else:
                out[i * 4: i * 4 + 4] = p
        return w, h, bytes(out)

    bw, bh = (w + 3) // 4, (h + 3) // 4
    bb = 8 if kind == "BC1" else 16
    for by in range(bh):
        for bx in range(bw):
            b = data[off + (by * bw + bx) * bb: off + (by * bw + bx) * bb + bb]
            if kind == "BC7":
                px = decode_block(b)
            elif kind == "BC3":
                alpha = _bc3_alpha(b)
                px = _bc1_colors(b[8:], True)
                px = [[p[0], p[1], p[2], alpha[i]] for i, p in enumerate(px)]
            else:
                px = _bc1_colors(b, False)
            for r in range(4):
                for c in range(4):
                    x, y = bx * 4 + c, by * 4 + r
                    if x >= w or y >= h:
                        continue
                    out[(y * w + x) * 4: (y * w + x) * 4 + 4] = bytes(px[r * 4 + c])
    return w, h, bytes(out)


def _from565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]


def _bc1_colors(blk, always4):
    c0, c1, idx = struct.unpack("<HHI", blk)
    p = [_from565(c0) + [255], _from565(c1) + [255]]
    if always4 or c0 > c1:
        p.append([(2 * p[0][k] + p[1][k]) // 3 for k in range(3)] + [255])
        p.append([(p[0][k] + 2 * p[1][k]) // 3 for k in range(3)] + [255])
    else:
        p.append([(p[0][k] + p[1][k]) // 2 for k in range(3)] + [255])
        p.append([0, 0, 0, 0])
    return [list(p[(idx >> (2 * i)) & 3]) for i in range(16)]


def _bc3_alpha(blk):
    a0, a1 = blk[0], blk[1]
    ramp = [a0, a1]
    if a0 > a1:
        ramp += [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        ramp += [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    bits = int.from_bytes(blk[2:8], "little")
    return [ramp[(bits >> (3 * i)) & 7] for i in range(16)]


def pil_rgba(dds_bytes):
    im = Image.open(io.BytesIO(dds_bytes))
    return im.convert("RGBA").tobytes()


_passed = _failed = 0


def check(label, ok, detail=""):
    global _passed, _failed
    if ok:
        _passed += 1
        print(f"  PASS  {label}")
    else:
        _failed += 1
        print(f"  FAIL  {label}  {detail}")


random.seed(1170)

print("[decode model vs Pillow: per-mode random-block fuzz]")
for mode in range(8):
    blocks = []
    for _ in range(64):
        b = bytearray(random.getrandbits(8) for _ in range(16))
        b[0] = (1 << mode) | ((random.getrandbits(8) << (mode + 1)) & 0xFF)
        blocks.append(bytes(b))
    payload = b"".join(blocks)          # 8x8 blocks = 32x32 px
    dds = dds_dx10(32, 32, 98, payload)
    ours = decode_dds_ours(dds)[2]
    pil = pil_rgba(dds)
    check(f"mode {mode}: 64 random blocks byte-exact vs PIL", ours == pil)

print("[spec edge]")
zero = decode_block(bytes(16))
check("reserved mode decodes to transparent black", all(v == [0, 0, 0, 0] for v in zero))

print("[real modlist BC7 files]")
real = []
cube = None
for dirpath, _, files in os.walk(MODS):
    for fn in files:
        if len(real) >= 4 and cube:
            break
        if not fn.lower().endswith(".dds"):
            continue
        p = os.path.join(dirpath, fn)
        try:
            with open(p, "rb") as f:
                hdr = f.read(148)
        except OSError:
            continue
        if len(hdr) < 148 or hdr[:4] != b"DDS " or hdr[84:88] != b"DX10":
            continue
        dxgi, dim, misc, arr = struct.unpack_from("<IIII", hdr, 128)
        h, w = struct.unpack_from("<II", hdr, 12)
        if dxgi in (98, 99) and ((misc & 4) or arr > 1) and cube is None:
            cube = p
        elif dxgi in (98, 99) and w * h <= 512 * 512 and not (misc & 4) and arr <= 1:
            real.append(p)
    if len(real) >= 4 and cube:
        break
for p in real:
    data = open(p, "rb").read()
    ours = decode_dds_ours(data)
    pil = pil_rgba(data)
    ok = ours is not None and ours[2] == pil
    check(f"{os.path.basename(p)} ({ours[0]}x{ours[1]}) byte-exact vs PIL" if ours
          else os.path.basename(p), ok)
if not real:
    check("real BC7 sample files found", False, "no small BC7 files under mods/")
if cube:
    check("cubemap/array DX10 file declined (honest null)",
          decode_dds_ours(open(cube, "rb").read()) is None, cube)
else:
    print("  note: no BC7 cubemap/array found to exercise the refusal path")

print("[DX10-wrapped BC1/BC3 and byte-order formats]")
legacy = {"DXT1": None, "DXT5": None}
for dirpath, _, files in os.walk(MODS):
    if all(legacy.values()):
        break
    for fn in files:
        if not fn.lower().endswith(".dds"):
            continue
        p = os.path.join(dirpath, fn)
        try:
            with open(p, "rb") as f:
                hdr = f.read(128)
        except OSError:
            continue
        if len(hdr) < 128 or hdr[:4] != b"DDS ":
            continue
        fc = hdr[84:88].decode("ascii", "replace")
        h, w = struct.unpack_from("<II", hdr, 12)
        if fc in legacy and legacy[fc] is None and w * h <= 512 * 512 and w % 4 == 0 and h % 4 == 0:
            legacy[fc] = p
for fc, dxgi, bb in (("DXT1", 71, 8), ("DXT5", 77, 16)):
    p = legacy[fc]
    if not p:
        check(f"legacy {fc} sample found", False)
        continue
    data = open(p, "rb").read()
    h, w = struct.unpack_from("<II", data, 12)
    payload = data[128:128 + ((w + 3) // 4) * ((h + 3) // 4) * bb]
    wrapped = dds_dx10(w, h, dxgi, payload)
    ours = decode_dds_ours(wrapped)
    check(f"{fc} rewrapped as DX10 dxgi {dxgi}: ours == PIL",
          ours is not None and ours[2] == pil_rgba(wrapped))

grad = bytes((x * 8) % 256 for x in range(16 * 16 * 4))
dds = dds_dx10(16, 16, 28, grad)
ours = decode_dds_ours(dds)
check("DX10 RGBA8 byte-order: ours == PIL", ours is not None and ours[2] == pil_rgba(dds))
# Pillow has no DXGI 87 decoder, so BGRA8 is checked against by-construction
# ground truth: the format IS the byte order (B,G,R,A per the DXGI spec).
bgra = bytearray(grad)
for i in range(0, len(bgra), 4):
    bgra[i], bgra[i + 2] = bgra[i + 2], bgra[i]
ours = decode_dds_ours(dds_dx10(16, 16, 87, bytes(bgra)))
check("DX10 BGRA8 byte-order: ours == swizzle ground truth (PIL lacks dxgi 87)",
      ours is not None and ours[2] == grad)

print("[mode-6 encoder]")
flat = [[10, 20, 30, 254]] * 16
enc = encode_block_mode6(flat)
check("flat all-even block lossless", decode_block(enc) == flat)

two = [[10, 20, 30, 254] if i % 3 else [200, 100, 50, 2] for i in range(16)]
enc = encode_block_mode6(two)
check("two-color all-even block lossless", decode_block(enc) == two)

anchor = [[200, 100, 50, 2]] + [[10, 20, 30, 254]] * 15   # pixel 0 nearest far endpoint
enc = encode_block_mode6(anchor)
dec = decode_block(enc)
check("anchor-swap path exact", dec == anchor)

if real:
    src_img = decode_dds_ours(open(real[0], "rb").read())
    w0, h0 = src_img[0], src_img[1]
    crop = []
    for y in range(64):
        row = src_img[2][(y * w0) * 4:(y * w0 + 64) * 4]
        crop.append(row)
    content = b"".join(crop)
    blocks = []
    px = [[0, 0, 0, 0]] * 16
    for by in range(16):
        for bx in range(16):
            px = []
            for r in range(4):
                for c in range(4):
                    o = ((by * 4 + r) * 64 + bx * 4 + c) * 4
                    px.append(list(content[o:o + 4]))
            blocks.append(encode_block_mode6(px))
    dds = dds_dx10(64, 64, 98, b"".join(blocks))
    ours = decode_dds_ours(dds)[2]
    pil = pil_rgba(dds)
    check("encoded 64x64 real content: our decode == PIL decode", ours == pil)
    mse = sum((a - b) ** 2 for a, b in zip(ours, content)) / len(content)
    psnr = 10 * math.log10(255 * 255 / mse) if mse else float("inf")
    check(f"PSNR vs source {psnr:.1f} dB (floor 33; measured, not claimed)", psnr >= 33.0)

print("[DX10 writer contract (mirrors EncodeDDS)]")
mips_levels = 7                                  # 64x64 -> 1x1
payload = []
imgw = imgh = 64
level = content if real else bytes(64 * 64 * 4)
lw = lh = 64
for _ in range(mips_levels):
    blocks = []
    for by in range((lh + 3) // 4):
        for bx in range((lw + 3) // 4):
            px = []
            for r in range(4):
                for c in range(4):
                    x, y = min(bx * 4 + c, lw - 1), min(by * 4 + r, lh - 1)
                    o = (y * lw + x) * 4
                    px.append(list(level[o:o + 4]))
            blocks.append(encode_block_mode6(px))
    payload.append(b"".join(blocks))
    nw, nh = max(1, lw // 2), max(1, lh // 2)
    nxt = bytearray(nw * nh * 4)
    for y in range(nh):
        y0, y1 = 2 * y, min(2 * y + 1, lh - 1)
        for x in range(nw):
            x0, x1 = 2 * x, min(2 * x + 1, lw - 1)
            for ch in range(4):
                s = (level[(y0 * lw + x0) * 4 + ch] + level[(y0 * lw + x1) * 4 + ch] +
                     level[(y1 * lw + x0) * 4 + ch] + level[(y1 * lw + x1) * 4 + ch])
                nxt[(y * nw + x) * 4 + ch] = (s + 2) >> 2
    level, lw, lh = bytes(nxt), nw, nh
full = dds_dx10(64, 64, 98, b"".join(payload), mips=mips_levels)
d = full
ok = (d[:4] == b"DDS " and struct.unpack_from("<I", d, 4)[0] == 124 and
      d[84:88] == b"DX10" and struct.unpack_from("<IIIII", d, 128) == (98, 3, 0, 1, 0) and
      struct.unpack_from("<I", d, 28)[0] == mips_levels)
check("header fields (magic/size/DX10/dxgi 98/dim/array/mips)", ok)
im = Image.open(io.BytesIO(full))
check("PIL reads mipmapped BC7 file (top mip size/pixels)",
      im.size == (64, 64) and pil_rgba(full) == decode_dds_ours(full)[2])

print(f"\n{_passed} passed, {_failed} failed")
sys.exit(1 if _failed else 0)
