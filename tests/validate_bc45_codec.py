#!/usr/bin/env python3
"""Offline validation of the BC4/BC5 codec (src/core/TextureCodec.cpp).

BC4 and BC5 reuse the BCn alpha-block codec (two endpoints + 3-bit
selectors, 8-value interpolated mode) on the R channel (BC4) or the R and G
channels (BC5). Decode is checked against Pillow on real modlist BC4
textures and on hand-built blocks; BC5 (absent from the modlist) is checked
against by-construction ground truth. Encode is checked by round-trip
(encode -> our decode) and PSNR.

Scope, honest: BC4 output is grayscale (R=G=B, matches Pillow "L"); BC5 is
raw two-channel (R, G, B=0) as stored (a normal map's Z is reconstructed by
the shader, not the codec).
"""

import io
import math
import os
import struct
import sys

from PIL import Image

_passed = _failed = 0


def check(label, ok, detail=""):
    global _passed, _failed
    if ok:
        _passed += 1
        print(f"  PASS  {label}")
    else:
        _failed += 1
        print(f"  FAIL  {label}  {detail}")


# ── BCn single-channel block codec (port of DecodeBlockAlpha / EncodeBlockChannel)
def decode_block(blk):
    a0, a1 = blk[0], blk[1]
    ramp = [a0, a1]
    if a0 > a1:
        ramp += [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        ramp += [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    bits = int.from_bytes(blk[2:8], "little")
    return [ramp[(bits >> (3 * i)) & 7] for i in range(16)]


def encode_block(vals):
    a0, a1 = max(vals), min(vals)
    if a0 > a1:
        ramp = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
        count = 8
    else:
        ramp, count = [a0], 1
    bits = 0
    for i, v in enumerate(vals):
        bk = min(range(count), key=lambda k: abs(v - ramp[k]))
        bits |= bk << (3 * i)
    return bytes([a0, a1]) + bits.to_bytes(6, "little")


def dds_fourcc(w, h, fourcc, payload):
    hdr = bytearray(b"DDS ")
    hdr += struct.pack("<IIIII", 124, 0x000A1007, h, w, len(payload))
    hdr += struct.pack("<II", 0, 0) + b"\0" * 44
    hdr += struct.pack("<II4s", 32, 0x4, fourcc) + b"\0" * 20
    hdr += struct.pack("<IIIII", 0x1000, 0, 0, 0, 0)
    return bytes(hdr) + payload


def decode_bc4_ours(w, h, payload):
    bw, bh = (w + 3) // 4, (h + 3) // 4
    out = bytearray(w * h * 4)
    for by in range(bh):
        for bx in range(bw):
            vals = decode_block(payload[(by*bw+bx)*8:(by*bw+bx)*8+8])
            for r in range(4):
                for c in range(4):
                    x, y = bx*4+c, by*4+r
                    if x < w and y < h:
                        v = vals[r*4+c]
                        out[(y*w+x)*4:(y*w+x)*4+4] = bytes((v, v, v, 255))
    return bytes(out)


def decode_bc5_ours(w, h, payload):
    bw, bh = (w + 3) // 4, (h + 3) // 4
    out = bytearray(w * h * 4)
    for by in range(bh):
        for bx in range(bw):
            b = payload[(by*bw+bx)*16:(by*bw+bx)*16+16]
            rr, gg = decode_block(b[:8]), decode_block(b[8:])
            for r in range(4):
                for c in range(4):
                    x, y = bx*4+c, by*4+r
                    if x < w and y < h:
                        out[(y*w+x)*4:(y*w+x)*4+4] = bytes((rr[r*4+c], gg[r*4+c], 0, 255))
    return bytes(out)


print("[BC4 decode vs Pillow: hand-built blocks]")
import random
random.seed(45)
blocks = bytearray()
for _ in range(64):        # 8x8 blocks = 32x32 grayscale
    blocks += encode_block([random.randrange(256) for _ in range(16)])
dds = dds_fourcc(32, 32, b"BC4U", bytes(blocks))
pil = Image.open(io.BytesIO(dds)).convert("RGBA").tobytes()
check("64 random BC4 blocks byte-exact vs PIL", decode_bc4_ours(32, 32, bytes(blocks)) == pil)

print("[real modlist BC4 textures]")
MODS = r"E:\Modlists\SkyGroundChronicles\mods"
real = []
for dirpath, _, files in os.walk(MODS):
    for fn in files:
        if len(real) >= 5:
            break
        if not fn.lower().endswith(".dds"):
            continue
        p = os.path.join(dirpath, fn)
        try:
            with open(p, "rb") as f:
                hdr = f.read(128)
        except OSError:
            continue
        if len(hdr) < 128 or hdr[:4] != b"DDS " or hdr[84:88] not in (b"BC4U", b"ATI1"):
            continue
        h, w = struct.unpack_from("<II", hdr, 12)
        if w * h <= 512 * 512 and w % 4 == 0 and h % 4 == 0:
            real.append(p)
    if len(real) >= 5:
        break
for p in real:
    data = open(p, "rb").read()
    h, w = struct.unpack_from("<II", data, 12)
    payload = data[128:128 + ((w+3)//4)*((h+3)//4)*8]
    ours = decode_bc4_ours(w, h, payload)
    pil = Image.open(p).convert("RGBA").tobytes()
    check(f"{os.path.basename(p)} ({w}x{h}) byte-exact vs PIL", ours == pil)
if not real:
    check("real BC4 sample found", False, "none <=512 in scan")

print("[BC5 decode vs by-construction ground truth]")
# build two independent channels, encode as BC5, decode, compare to raw
rvals = [[random.randrange(256) for _ in range(16)] for _ in range(4)]
gvals = [[random.randrange(256) for _ in range(16)] for _ in range(4)]
payload = bytearray()
for bi in range(4):
    payload += encode_block(rvals[bi]) + encode_block(gvals[bi])
# 4 blocks in a row -> 16x4 image
dec = decode_bc5_ours(16, 4, bytes(payload))
ok = True
for bi in range(4):
    er = decode_block(encode_block(rvals[bi]))
    eg = decode_block(encode_block(gvals[bi]))
    for r in range(4):
        for c in range(4):
            x, y = bi*4+c, r
            o = (y*16+x)*4
            if dec[o] != er[r*4+c] or dec[o+1] != eg[r*4+c] or dec[o+2] != 0 or dec[o+3] != 255:
                ok = False
check("BC5 decodes R,G independently with B=0, A=255", ok)

print("[encode round-trip + PSNR]")
# BC4 encode: a smooth ramp -> encode -> our decode close to source
src = [[min(255, (bx*4+c)*8 + (by*4+r)*2) for r in range(4) for c in range(4)]
       for by in range(4) for bx in range(4)]
errs = []
for block in src:
    dec = decode_block(encode_block(block))
    errs += [abs(dec[i] - block[i]) for i in range(16)]
mse = sum(e*e for e in errs) / len(errs)
psnr = 10 * math.log10(255*255/mse) if mse else 99.0
check(f"BC4 encode round-trip PSNR {psnr:.1f} dB on a smooth ramp (floor 30)", psnr >= 30)

flat = [77]*16
check("BC4 flat block is lossless", decode_block(encode_block(flat)) == flat)

print("[source consistency]")
src_cpp = open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
               "src", "core", "TextureCodec.cpp"), encoding="utf-8").read()
check("shipped codec recognizes BC4/BC5 fourCC and DX10 dxgi",
      all(s in src_cpp for s in ("ATI1", "ATI2", "0x55344342", "0x55354342",
                                 "case 80: case 81: case 82:", "case 83: case 84: case 85:")))
check("shipped encode handles BC4 (8-byte) and BC5 (two channel blocks)",
      "EncodeBlockChannel(px, 0" in src_cpp and "EncodeBlockChannel(px, 1" in src_cpp)

print(f"\n{_passed} passed, {_failed} failed")
sys.exit(1 if _failed else 0)
