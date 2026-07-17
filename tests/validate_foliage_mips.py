#!/usr/bin/env python3
"""Offline validation of alpha-coverage-preserving mipmaps (lane F15,
src/core/TextureCodec.cpp: AlphaCoverage / ScaleAlphaToCoverage wired into
EncodeDDS).

Method: a faithful Python port of the C++ (box filter, coverage bisection,
mip-chain integration), checked on controlled synthetic foliage AND real
modlist foliage textures (decoded independently by Pillow). The harness first
DEMONSTRATES the defect being fixed (box-filter mips dilute alpha-test
coverage), then proves the preservation property, then proves it survives
BC3 alpha quantization (the format foliage actually ships in).

Scope, stated honestly: the visual claim (distant grass and leaves keep their
density in-game) is aesthetic and game-bound; what is proven here is the
measurable property the visual effect rides on.
"""

import math
import os
import random
import struct
import sys

from PIL import Image

MODS = r"E:\Modlists\SkyGroundChronicles\mods"

# ── port of the C++ ─────────────────────────────────────────────────────────


def halve_box(px, w, h):
    """Clamp-edge 2x2 box filter, round to nearest (port of HalveBox)."""
    nw, nh = max(1, w // 2), max(1, h // 2)
    out = bytearray(nw * nh * 4)
    for y in range(nh):
        y0, y1 = 2 * y, min(2 * y + 1, h - 1)
        for x in range(nw):
            x0, x1 = 2 * x, min(2 * x + 1, w - 1)
            for c in range(4):
                s = (px[(y0 * w + x0) * 4 + c] + px[(y0 * w + x1) * 4 + c] +
                     px[(y1 * w + x0) * 4 + c] + px[(y1 * w + x1) * 4 + c])
                out[(y * nw + x) * 4 + c] = (s + 2) >> 2
    return bytes(out), nw, nh


def coverage(px, threshold):
    n = len(px) // 4
    hit = sum(1 for i in range(n) if px[i * 4 + 3] >= threshold)
    return hit / n if n else 0.0


def scale_alpha_to_coverage(px, target, threshold):
    """Port of ScaleAlphaToCoverage. Returns new bytes."""
    n = len(px) // 4
    if not n or target <= 0.0:
        return bytes(px)

    def cov_at(s):
        hit = 0
        for i in range(n):
            a = int(px[i * 4 + 3] * s + 0.5)
            if min(255, a) >= threshold:
                hit += 1
        return hit / n

    lo, hi = 0.0, 8.0
    if cov_at(hi) < target:
        lo = hi
    i = 0
    while i < 24 and lo < hi:
        mid = 0.5 * (lo + hi)
        if cov_at(mid) >= target:
            hi = mid
        else:
            lo = mid
        i += 1
    s = hi
    out = bytearray(px)
    for i in range(n):
        a = int(px[i * 4 + 3] * s + 0.5)
        out[i * 4 + 3] = min(255, a)
    return bytes(out)


def mip_chain(px, w, h, threshold=None):
    """All mip levels below the top; coverage-preserving when threshold set
    (mirrors the EncodeDDS loop)."""
    target = coverage(px, threshold) if threshold else 0.0
    levels = []
    while w > 1 or h > 1:
        px, w, h = halve_box(px, w, h)
        if threshold:
            px = scale_alpha_to_coverage(px, target, threshold)
        levels.append((px, w, h))
    return levels


# BC3 alpha block codec (same model the earlier receipts locked against PIL).
def _bc3_alpha_encode(alphas):
    a0, a1 = max(alphas), min(alphas)
    if a0 > a1:
        ramp = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
        count = 8
    else:
        ramp, count = [a0], 1
    bits = 0
    for i, a in enumerate(alphas):
        bk, bd = 0, 1 << 30
        for k in range(count):
            d = abs(a - ramp[k])
            if d < bd:
                bd, bk = d, k
        bits |= bk << (3 * i)
    return bytes([a0, a1]) + bits.to_bytes(6, "little")


def _bc3_alpha_decode(blk):
    a0, a1 = blk[0], blk[1]
    ramp = [a0, a1]
    if a0 > a1:
        ramp += [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        ramp += [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    bits = int.from_bytes(blk[2:8], "little")
    return [ramp[(bits >> (3 * i)) & 7] for i in range(16)]


def bc3_roundtrip_alpha(px, w, h):
    """Alpha channel after BC3 encode+decode (edge-clamped blocks)."""
    out = bytearray(px)
    for by in range((h + 3) // 4):
        for bx in range((w + 3) // 4):
            alphas = []
            for r in range(4):
                for c in range(4):
                    x, y = min(bx * 4 + c, w - 1), min(by * 4 + r, h - 1)
                    alphas.append(px[(y * w + x) * 4 + 3])
            dec = _bc3_alpha_decode(_bc3_alpha_encode(alphas))
            for r in range(4):
                for c in range(4):
                    x, y = bx * 4 + c, by * 4 + r
                    if x < w and y < h:
                        out[(y * w + x) * 4 + 3] = dec[r * 4 + c]
    return bytes(out)


_passed = _failed = 0


def check(label, ok, detail=""):
    global _passed, _failed
    if ok:
        _passed += 1
        print(f"  PASS  {label}")
    else:
        _failed += 1
        print(f"  FAIL  {label}  {detail}")


T = 128
random.seed(31170)

# Synthetic foliage: sparse discs of full alpha on a transparent ground.
W = H = 256
syn = bytearray(W * H * 4)
for _ in range(220):
    cx, cy, r = random.randrange(W), random.randrange(H), random.randint(1, 4)
    for y in range(max(0, cy - r), min(H, cy + r + 1)):
        for x in range(max(0, cx - r), min(W, cx + r + 1)):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                o = (y * W + x) * 4
                syn[o:o + 4] = bytes((40, 120, 30, 255))
syn = bytes(syn)
c0 = coverage(syn, T)

print("[the defect, demonstrated (controlled synthetic)]")
check(f"synthetic foliage coverage in a realistic band (c0={c0:.3f})", 0.05 < c0 < 0.5)
naive = mip_chain(syn, W, H)
decayed = [cov for px, w, h in naive if w * h >= 64
           for cov in [coverage(px, T)]]
worst = min(decayed)
check(f"box-filter mips lose coverage (worst mip {worst:.3f} vs c0 {c0:.3f})",
      worst < 0.7 * c0, f"{decayed}")

print("[the fix (synthetic)]")
fixed = mip_chain(syn, W, H, threshold=T)
ok = True
detail = []
for px, w, h in fixed:
    if w * h < 64:
        continue
    cov = coverage(px, T)
    tol = 0.05 if w * h >= 1024 else 0.10
    detail.append(f"{w}x{h}:{cov:.3f}")
    if not (c0 - 1e-9 <= cov <= c0 + tol):
        ok = False
check(f"every mip >= 64 px holds c0 within tolerance, never thinner ({' '.join(detail)})", ok)

again = mip_chain(syn, W, H, threshold=T)
check("deterministic (two runs byte-identical)",
      all(a[0] == b[0] for a, b in zip(fixed, again)))

flat = bytes([10, 20, 30, 255]) * (64 * 64)
ok = all(coverage(px, T) == 1.0 for px, w, h in mip_chain(flat, 64, 64, threshold=T))
check("fully opaque image passes through (coverage stays 1)", ok)

clear = bytes([10, 20, 30, 0]) * (64 * 64)
ok = all(px == npx for (px, _, _), (npx, _, _) in
         zip(mip_chain(clear, 64, 64, threshold=T), mip_chain(clear, 64, 64)))
check("fully transparent image: target 0 disables scaling (no-op)", ok)

print("[BC3 survival (the shipping format)]")
ok = True
detail = []
for px, w, h in fixed:
    if w * h < 256:
        continue
    cov = coverage(bc3_roundtrip_alpha(px, w, h), T)
    detail.append(f"{w}x{h}:{cov:.3f}")
    if abs(cov - c0) > 0.08:
        ok = False
check(f"coverage survives BC3 alpha quantization within 0.08 ({' '.join(detail)})", ok)

print("[real modlist foliage]")
FOLIAGE_HINTS = ("grass", "plant", "flora", "leaf", "leaves", "fern", "bush")
real = []
for dirpath, _, files in os.walk(MODS):
    low = dirpath.lower()
    if not any(hint in low for hint in FOLIAGE_HINTS):
        continue
    for fn in files:
        if len(real) >= 3:
            break
        if not fn.lower().endswith(".dds") or fn.lower().endswith("_n.dds"):
            continue
        p = os.path.join(dirpath, fn)
        try:
            im = Image.open(p)
            if im.size[0] * im.size[1] > 512 * 512 or "A" not in im.mode:
                continue
            im = im.convert("RGBA")
        except Exception:
            continue
        px = im.tobytes()
        cov = coverage(px, T)
        if 0.15 <= cov <= 0.85:
            real.append((p, px, im.size[0], im.size[1], cov))
    if len(real) >= 3:
        break

if not real:
    check("real foliage sample files found", False, "no alpha-tested foliage located")
for p, px, w, h, cov0 in real:
    naive_last = None
    for m, (mp, mw, mh) in enumerate(mip_chain(px, w, h)):
        if mw * mh >= 64:
            naive_last = coverage(mp, T)
    ok = True
    fixed_covs = []
    for mp, mw, mh in mip_chain(px, w, h, threshold=T):
        if mw * mh < 64:
            continue
        cv = coverage(mp, T)
        fixed_covs.append(cv)
        tol = 0.05 if mw * mh >= 1024 else 0.10
        if not (cov0 - 1e-9 <= cv <= cov0 + tol):
            ok = False
    check(f"{os.path.basename(p)} c0={cov0:.3f} naive-min={naive_last:.3f} "
          f"fixed holds c0 on every mip", ok, f"{fixed_covs}")

print(f"\n{_passed} passed, {_failed} failed")
sys.exit(1 if _failed else 0)
