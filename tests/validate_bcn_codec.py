"""Offline validation of SkyrimBridge's BC1/BC3 codec + DDS decode/encode.

Faithful Python port of the BC section of src/core/TextureCodec.cpp,
cross-checked against ground truth:
  - block decode vs PIL's native BCn decoder (exact), incl. real modlist DDS
  - encode: our file decoded by PIL == decoded by our port (exact), PSNR
    floor vs source, and exactly-representable blocks round-trip losslessly
  - DDS structure: fourCC, LINEARSIZE, mip count, per-level sizes
Interpolation model (verified empirically against PIL first): truncating
(2a+b)/3 and (a+b)/2 for color, truncating /7 and /5 alpha ramps, DXT5 color
always 4-color.
"""
import io
import math
import os
import random
import struct
import sys

from test_support import require_mods_root

PASS = FAIL = 0
def check(name, ok, detail=""):
    global PASS, FAIL
    if ok: PASS += 1; print("  PASS  %s" % name)
    else:  FAIL += 1; print("  FAIL  %s  %s" % (name, detail))

# ---------------------------------------------------------------- port
def to565(c):
    return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)

def from565(v):
    r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]

def bc_palette(c0, c1, four_color):
    p = [from565(c0) + [255], from565(c1) + [255], [0, 0, 0, 255], [0, 0, 0, 255]]
    if four_color:
        for k in range(3):
            p[2][k] = (2 * p[0][k] + p[1][k]) // 3
            p[3][k] = (p[0][k] + 2 * p[1][k]) // 3
    else:
        for k in range(3):
            p[2][k] = (p[0][k] + p[1][k]) // 2
            p[3][k] = 0
        p[3][3] = 0
    return p

def fetch_block(w, h, rgba, bx, by):
    px = []
    for r in range(4):
        for c in range(4):
            x = min(bx * 4 + c, w - 1); y = min(by * 4 + r, h - 1)
            o = (y * w + x) * 4
            px.append([rgba[o], rgba[o+1], rgba[o+2], rgba[o+3]])
    return px

def pick_endpoints(px):
    bi = bj = 0; best = -1
    for i in range(16):
        for j in range(i + 1, 16):
            d = sum((px[i][k] - px[j][k]) ** 2 for k in range(3))
            if d > best: best = d; bi, bj = i, j
    c0, c1 = to565(px[bi]), to565(px[bj])
    if c0 < c1: c0, c1 = c1, c0
    return c0, c1

def encode_block_bc1(px):
    c0, c1 = pick_endpoints(px)
    p = bc_palette(c0, c1, True)
    n = 1 if c0 == c1 else 4
    idx = 0
    for i in range(16):
        bk, bd = 0, 1 << 30
        for k in range(n):
            d = sum((px[i][c] - p[k][c]) ** 2 for c in range(3))
            if d < bd: bd, bk = d, k
        idx |= bk << (2 * i)
    return struct.pack("<HHI", c0, c1, idx)

def encode_block_alpha(px):
    a0 = max(p[3] for p in px); a1 = min(p[3] for p in px)
    if a0 > a1:
        ramp = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
        count = 8
    else:
        ramp = [a0]; count = 1
    bits = 0
    for i in range(16):
        bk, bd = 0, 1 << 30
        for k in range(count):
            d = abs(px[i][3] - ramp[k])
            if d < bd: bd, bk = d, k
        bits |= bk << (3 * i)
    return struct.pack("<BB", a0, a1) + bits.to_bytes(6, "little")

def compress_bc(w, h, rgba, bc3):
    out = bytearray()
    for by in range((h + 3) // 4):
        for bx in range((w + 3) // 4):
            px = fetch_block(w, h, rgba, bx, by)
            if bc3: out += encode_block_alpha(px)
            out += encode_block_bc1(px)
    return bytes(out)

def decode_block_bc1(b, separate_alpha):
    c0, c1, idx = struct.unpack("<HHI", b)
    p = bc_palette(c0, c1, separate_alpha or c0 > c1)
    return [p[(idx >> (2 * i)) & 3] for i in range(16)]

def decode_block_alpha(b):
    a0, a1 = b[0], b[1]
    if a0 > a1:
        ramp = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        ramp = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    bits = int.from_bytes(b[2:8], "little")
    return [ramp[(bits >> (3 * i)) & 7] for i in range(16)]

def decode_dds(d):
    if d[:4] != b"DDS " or struct.unpack("<I", d[4:8])[0] != 124: raise ValueError("hdr")
    H, W = struct.unpack("<II", d[12:20])
    pf_flags, fourcc = struct.unpack("<II", d[80:88])
    data = d[128:]
    out = bytearray(W * H * 4)
    if pf_flags & 0x4:
        dxt1 = fourcc == 0x31545844; dxt5 = fourcc == 0x35545844
        if not (dxt1 or dxt5): raise ValueError("fourcc")
        bw, bh = (W + 3) // 4, (H + 3) // 4
        bb = 16 if dxt5 else 8
        for by in range(bh):
            for bx in range(bw):
                b = data[(by * bw + bx) * bb:(by * bw + bx) * bb + bb]
                if dxt5:
                    alpha = decode_block_alpha(b[:8]); px = decode_block_bc1(b[8:], True)
                else:
                    px = decode_block_bc1(b, False); alpha = None
                for r in range(4):
                    for c in range(4):
                        x, y = bx * 4 + c, by * 4 + r
                        if x >= W or y >= H: continue
                        o = (y * W + x) * 4
                        out[o:o+3] = bytes(px[r*4+c][:3])
                        out[o+3] = alpha[r*4+c] if dxt5 else px[r*4+c][3]
    elif pf_flags & 0x40:
        if struct.unpack("<I", d[88:92])[0] != 32: raise ValueError("bpp")
        mr, mg, mb, ma = struct.unpack("<4I", d[92:108])
        def shift_of(m):
            for s in (0, 8, 16, 24):
                if m == 0xFF << s: return s
            return -1
        sr, sg, sb = shift_of(mr), shift_of(mg), shift_of(mb)
        sa = shift_of(ma) if ma else -2
        if -1 in (sr, sg, sb) or sa == -1: raise ValueError("masks")
        for i in range(W * H):
            v = struct.unpack("<I", data[i*4:i*4+4])[0]
            out[i*4+0] = (v >> sr) & 0xFF
            out[i*4+1] = (v >> sg) & 0xFF
            out[i*4+2] = (v >> sb) & 0xFF
            out[i*4+3] = (v >> sa) & 0xFF if sa >= 0 else 255
    else:
        raise ValueError("format")
    return W, H, bytes(out)

def halve_box(w, h, rgba):
    mw = w // 2 if w > 1 else 1
    mh = h // 2 if h > 1 else 1
    out = bytearray(mw * mh * 4)
    for y in range(mh):
        y0 = 2*y; y1 = min(y0 + 1, h - 1)
        for x in range(mw):
            x0 = 2*x; x1 = min(x0 + 1, w - 1)
            for c in range(4):
                s = (rgba[(y0*w + x0)*4 + c] + rgba[(y0*w + x1)*4 + c] +
                     rgba[(y1*w + x0)*4 + c] + rgba[(y1*w + x1)*4 + c])
                out[(y*mw + x)*4 + c] = (s + 2) >> 2
    return mw, mh, bytes(out)

def encode_dds(w, h, rgba, fmt, mipmaps):
    levels = 1
    if mipmaps:
        ww, hh = w, h
        while ww > 1 or hh > 1:
            ww = max(1, ww // 2); hh = max(1, hh // 2); levels += 1
    bc = fmt != "RGBA8"; bc3 = fmt == "BC3"
    bb = 16 if bc3 else 8
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | (0x80000 if bc else 0x8) | (0x20000 if mipmaps else 0)
    pitch = ((w + 3) // 4) * ((h + 3) // 4) * bb if bc else w * 4
    hdr = b"DDS " + struct.pack("<7I", 124, flags, h, w, pitch, 0, levels if mipmaps else 0)
    hdr += b"\0" * 44
    if bc:
        hdr += struct.pack("<II4s5I", 32, 0x4, b"DXT5" if bc3 else b"DXT1", 0, 0, 0, 0, 0)
    else:
        hdr += struct.pack("<8I", 32, 0x41, 0, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000)
    caps = 0x1000 | ((0x8 | 0x400000) if mipmaps else 0)
    hdr += struct.pack("<5I", caps, 0, 0, 0, 0)
    body = bytearray()
    lw, lh, lv = w, h, rgba
    for i in range(levels):
        body += compress_bc(lw, lh, lv, bc3) if bc else lv
        if i + 1 < levels:
            lw, lh, lv = halve_box(lw, lh, lv)
    return hdr + bytes(body), levels

# ---------------------------------------------------------------- tests
def dds_header_for_probe(w, h, fourcc):
    n = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * (8 if fourcc == b"DXT1" else 16)
    hdr = b"DDS " + struct.pack("<7I", 124, 0x1007, h, w, n, 0, 0) + b"\0" * 44
    hdr += struct.pack("<II4s5I", 32, 0x4, fourcc, 0, 0, 0, 0, 0) + struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    return hdr

def pil_rgba(img):
    return img.convert("RGBA").tobytes()

def test_handbuilt_blocks():
    print("[hand-built block decode vs PIL]")
    from PIL import Image
    rng = random.Random(42)
    cases = []
    for mode in ("4color", "3color"):
        while True:
            c0 = rng.randrange(65536); c1 = rng.randrange(65536)
            if c0 != c1: break
        if (mode == "4color") != (c0 > c1): c0, c1 = c1, c0
        idx = rng.randrange(1 << 32)
        cases.append(("DXT1 " + mode, b"DXT1", struct.pack("<HHI", c0, c1, idx)))
    for mode in ("a0>a1", "a0<=a1", "colorswap"):
        a0, a1 = (200, 20) if mode == "a0>a1" else (20, 200)
        abits = rng.randrange(1 << 48)
        c0, c1 = rng.randrange(65536), rng.randrange(65536)
        if mode == "colorswap" and c0 > c1: c0, c1 = c1, c0   # force c0<=c1: must still be 4-color
        blk = struct.pack("<BB", a0, a1) + abits.to_bytes(6, "little") + struct.pack("<HHI", c0, c1, rng.randrange(1 << 32))
        cases.append(("DXT5 " + mode, b"DXT5", blk))
    for name, fourcc, block in cases:
        blob = dds_header_for_probe(4, 4, fourcc) + block
        w, h, ours = decode_dds(blob)
        ref = pil_rgba(Image.open(io.BytesIO(blob)))
        check(name, ours == ref)

def find_real_dds(limit_each=4, max_dim=1024):
    root = require_mods_root()
    dxt1, dxt5 = [], []
    for dirpath, _dirs, files in os.walk(root):
        for f in files:
            if not f.lower().endswith(".dds"): continue
            path = os.path.join(dirpath, f)
            try:
                with open(path, "rb") as fh:
                    hdr = fh.read(128)
                if len(hdr) < 128 or hdr[:4] != b"DDS ": continue
                H, W = struct.unpack("<II", hdr[12:20])
                caps2 = struct.unpack("<I", hdr[112:116])[0]
                if caps2 & 0x200 or W > max_dim or H > max_dim or W < 4 or H < 4: continue
                fourcc = hdr[84:88]
                if fourcc == b"DXT1" and len(dxt1) < limit_each: dxt1.append(path)
                if fourcc == b"DXT5" and len(dxt5) < limit_each: dxt5.append(path)
            except OSError:
                continue
        if len(dxt1) >= limit_each and len(dxt5) >= limit_each: break
    return dxt1 + dxt5

def test_real_dds():
    print("[real modlist DDS decode vs PIL]")
    from PIL import Image
    files = find_real_dds()
    if not files:
        check("real DXT files found", False); return
    for path in files:
        try:
            with open(path, "rb") as fh:
                blob = fh.read()
            w, h, ours = decode_dds(blob)
            ref = Image.open(io.BytesIO(blob))
            ok = ref.size == (w, h) and pil_rgba(ref) == ours
            check("%s %dx%d %s" % (os.path.basename(path), w, h, blob[84:88].decode()), ok)
        except Exception as e:  # noqa: BLE001
            check(os.path.basename(path), False, repr(e))

def psnr(a, b):
    se = sum((x - y) ** 2 for x, y in zip(a, b))
    if se == 0: return float("inf")
    return 10 * math.log10((255.0 ** 2) / (se / len(a)))

def test_encode():
    print("[BC encode: PIL-agreement + PSNR floor + exact-representable]")
    from PIL import Image
    src_path = None
    for cand in find_real_pngs_for_encode():
        src_path = cand; break
    if src_path:
        img = Image.open(src_path).convert("RGBA")
        img = img.crop((0, 0, min(256, img.width) // 4 * 4, min(256, img.height) // 4 * 4))
        w, h = img.size
        rgba = img.tobytes()
    else:
        rng = random.Random(1); w = h = 64
        rgba = bytes(rng.randrange(256) for _ in range(w * h * 4))
    for fmt in ("BC1", "BC3"):
        blob, _levels = encode_dds(w, h, rgba, fmt, False)
        _w, _h, ours = decode_dds(blob)
        ref = pil_rgba(Image.open(io.BytesIO(blob)))
        check("%s: our decode == PIL decode of our file" % fmt, ours == ref)
        chans = 4 if fmt == "BC3" else 3
        flat_o = [ours[i] for i in range(len(ours)) if i % 4 < chans]
        flat_s = [rgba[i] for i in range(len(rgba)) if i % 4 < chans]
        p = psnr(flat_o, flat_s)
        check("%s: PSNR %.1f dB > 25 (source %s)" % (fmt, p, os.path.basename(src_path or "random")), p > 25)
    # exactly representable: every 4x4 block has two 565-lattice colors
    rng = random.Random(5)
    w = h = 16
    px = bytearray(w * h * 4)
    for by in range(4):
        for bx in range(4):
            ca = from565(rng.randrange(65536)); cb = from565(rng.randrange(65536))
            aa, ab = rng.randrange(256), rng.randrange(256)
            for r in range(4):
                for c in range(4):
                    o = ((by*4 + r) * w + bx*4 + c) * 4
                    src = ca if (r + c) % 2 else cb
                    px[o:o+3] = bytes(src)
                    px[o+3] = aa if (r + c) % 2 else ab
    blob, _ = encode_dds(w, h, bytes(px), "BC3", False)
    _w, _h, ours = decode_dds(blob)
    rgb_ok = all(ours[i] == px[i] for i in range(len(px)) if i % 4 != 3)
    a_ok = all(ours[i] == px[i] for i in range(3, len(px), 4))
    check("BC3: 2-color 565-lattice blocks round-trip color-exact", rgb_ok)
    check("BC3: 2-value alpha blocks round-trip alpha-exact", a_ok)

def find_real_pngs_for_encode():
    root = require_mods_root()
    for dirpath, _dirs, files in os.walk(root):
        if "enbseries" not in dirpath.lower(): continue
        for f in files:
            if f.lower() == "frost.png":
                yield os.path.join(dirpath, f)

def test_structure():
    print("[DDS structure: header + level sizes]")
    rng = random.Random(2)
    for (w, h, fmt) in ((64, 32, "BC1"), (23, 17, "BC3"), (16, 16, "BC1")):
        rgba = bytes(rng.randrange(256) for _ in range(w * h * 4))
        blob, levels = encode_dds(w, h, rgba, fmt, True)
        bb = 16 if fmt == "BC3" else 8
        total = 0
        ww, hh = w, h
        for _ in range(levels):
            total += ((ww + 3) // 4) * ((hh + 3) // 4) * bb
            ww = max(1, ww // 2); hh = max(1, hh // 2)
        flags, = struct.unpack("<I", blob[8:12])
        mipc, = struct.unpack("<I", blob[28:32])
        fourcc = blob[84:88].decode()
        ok = (len(blob) == 128 + total and (flags & 0x80000) and not (flags & 0x8)
              and mipc == levels and fourcc == ("DXT5" if fmt == "BC3" else "DXT1"))
        check("%s %dx%d: %d levels, linear-size flags, size" % (fmt, w, h, levels), bool(ok))

def test_rgba8_dds_decode_roundtrip():
    print("[uncompressed DDS decode round-trip + PIL-written DDS]")
    from PIL import Image
    rng = random.Random(9)
    w, h = 20, 12
    rgba = bytes(rng.randrange(256) for _ in range(w * h * 4))
    blob, _ = encode_dds(w, h, rgba, "RGBA8", True)
    dw, dh, back = decode_dds(blob)
    check("our RGBA8 DDS -> our decode == source", (dw, dh) == (w, h) and back == rgba)
    img = Image.frombytes("RGBA", (w, h), rgba)
    buf = io.BytesIO(); img.save(buf, "DDS")
    try:
        dw, dh, ours = decode_dds(buf.getvalue())
        check("PIL-written DDS (BGRA masks) -> our decode", (dw, dh) == (w, h) and ours == rgba)
    except ValueError as e:
        check("PIL-written DDS (BGRA masks) -> our decode", False, str(e))

def test_tga_write():
    print("[TGA write vs PIL read]")
    from PIL import Image
    rng = random.Random(11)
    w, h = 19, 7
    rgba = bytes(rng.randrange(256) for _ in range(w * h * 4))
    hdr = bytearray(18)
    hdr[2] = 2
    hdr[12] = w & 0xFF; hdr[13] = w >> 8
    hdr[14] = h & 0xFF; hdr[15] = h >> 8
    hdr[16] = 32; hdr[17] = 0x28
    body = bytearray()
    for i in range(0, len(rgba), 4):
        body += bytes((rgba[i+2], rgba[i+1], rgba[i], rgba[i+3]))
    blob = bytes(hdr) + bytes(body)
    ref = Image.open(io.BytesIO(blob))
    check("WriteTGA layout: PIL reads back exactly",
          ref.size == (w, h) and pil_rgba(ref) == rgba)

if __name__ == "__main__":
    test_handbuilt_blocks()
    test_real_dds()
    test_encode()
    test_structure()
    test_rgba8_dds_decode_roundtrip()
    test_tga_write()
    print("\n%d passed, %d failed" % (PASS, FAIL))
    sys.exit(1 if FAIL else 0)
