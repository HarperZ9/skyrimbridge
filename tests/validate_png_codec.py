"""Offline validation of SkyrimBridge's Inflate + PNG decode + DDS mip chain.

Faithful Python port of src/core/Inflate.cpp and the PNG/mip parts of
src/core/TextureCodec.cpp, cross-checked against ground truth:
  - inflate  vs Python zlib (stored / fixed / dynamic blocks, real IDAT)
  - CRC-32 / Adler-32 vs binascii
  - PNG decode vs PIL, pixel-exact, on the real ENB PNGs in the modlist
  - synthetic PNGs (palette+tRNS, gray, LA, 1-bit, 16-bit, Adam7 interlace)
  - DDS mip chain: header fields + level sizes + independent box-filter check
"""
import binascii
import os
import random
import struct
import sys
import zlib

# ---------------------------------------------------------------- inflate port
LBASE = [3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258]
LEXT  = [0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0]
DBASE = [1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577]
DEXT  = [0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13]
ORDER = [16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15]

class Bad(Exception):
    pass

class BitReader:
    def __init__(self, d):
        self.d = d; self.pos = 0; self.bitbuf = 0; self.bitcnt = 0
    def bits(self, need):
        while self.bitcnt < need:
            if self.pos >= len(self.d):
                raise Bad("out of input")
            self.bitbuf |= self.d[self.pos] << self.bitcnt
            self.pos += 1; self.bitcnt += 8
        v = self.bitbuf & ((1 << need) - 1)
        self.bitbuf >>= need; self.bitcnt -= need
        return v
    def align(self):
        self.bitbuf = 0; self.bitcnt = 0

class Huff:
    def __init__(self, lengths, n):
        self.count = [0]*16
        for i in range(n): self.count[lengths[i]] += 1
        if self.count[0] == n: raise Bad("no codes")
        left = 1
        for l in range(1, 16):
            left = (left << 1) - self.count[l]
            if left < 0: raise Bad("oversubscribed")
        offs = [0]*16
        for l in range(1, 15): offs[l+1] = offs[l] + self.count[l]
        self.symbol = [0]*n
        for i in range(n):
            if lengths[i]: self.symbol[offs[lengths[i]]] = i; offs[lengths[i]] += 1

def decode(br, h):
    code = first = index = 0
    for ln in range(1, 16):
        code |= br.bits(1)
        cnt = h.count[ln]
        if code - first < cnt:
            return h.symbol[index + (code - first)]
        index += cnt
        first = (first + cnt) << 1
        code <<= 1
    raise Bad("ran out of codes")

def fixed_tables():
    ll = [8]*144 + [9]*112 + [7]*24 + [8]*8
    return Huff(ll, 288), Huff([5]*30, 30)

def dynamic_tables(br):
    hlit = br.bits(5) + 257; hdist = br.bits(5) + 1; hclen = br.bits(4) + 4
    if hlit > 286 or hdist > 30: raise Bad("bad counts")
    cl = [0]*19
    for i in range(hclen): cl[ORDER[i]] = br.bits(3)
    clh = Huff(cl, 19)
    lens = [0]*(hlit + hdist); n = 0
    while n < hlit + hdist:
        sym = decode(br, clh)
        if sym < 16:
            lens[n] = sym; n += 1; continue
        if sym == 16:
            if n == 0: raise Bad("no prev")
            value = lens[n-1]; repeat = 3 + br.bits(2)
        elif sym == 17:
            value = 0; repeat = 3 + br.bits(3)
        else:
            value = 0; repeat = 11 + br.bits(7)
        if n + repeat > hlit + hdist: raise Bad("overflow")
        for _ in range(repeat): lens[n] = value; n += 1
    if lens[256] == 0: raise Bad("no EOB code")
    return Huff(lens[:hlit], hlit), Huff(lens[hlit:], hdist)

def inflate_block(br, lit, dist, out):
    while True:
        sym = decode(br, lit)
        if sym < 256:
            out.append(sym); continue
        if sym == 256:
            return
        sym -= 257
        if sym >= 29: raise Bad("bad length sym")
        length = LBASE[sym] + br.bits(LEXT[sym])
        dsym = decode(br, dist)
        if dsym >= 30: raise Bad("bad dist sym")
        distance = DBASE[dsym] + br.bits(DEXT[dsym])
        if distance > len(out): raise Bad("dist too far")
        frm = len(out) - distance
        for i in range(length):
            out.append(out[frm + i])

def inflate_raw(data):
    out = bytearray(); br = BitReader(data)
    while True:
        bfinal = br.bits(1); btype = br.bits(2)
        if btype == 0:
            br.align()
            if br.pos + 4 > len(data): raise Bad("stored trunc")
            l = data[br.pos] | (data[br.pos+1] << 8)
            nl = data[br.pos+2] | (data[br.pos+3] << 8)
            br.pos += 4
            if (l ^ 0xFFFF) != nl or br.pos + l > len(data): raise Bad("stored len")
            out += data[br.pos:br.pos+l]; br.pos += l
        elif btype in (1, 2):
            lit, dist = fixed_tables() if btype == 1 else dynamic_tables(br)
            inflate_block(br, lit, dist, out)
        else:
            raise Bad("btype 3")
        if bfinal:
            return bytes(out)

def adler32_port(data):
    a, b, i = 1, 0, 0
    while i < len(data):
        chunk = min(len(data) - i, 5552)
        for j in range(i, i + chunk):
            a += data[j]; b += a
        a %= 65521; b %= 65521
        i += chunk
    return (b << 16) | a

def crc32_port(data):
    table = []
    for n in range(256):
        c = n
        for _ in range(8):
            c = (0xEDB88320 ^ (c >> 1)) if (c & 1) else (c >> 1)
        table.append(c)
    c = 0xFFFFFFFF
    for byte in data:
        c = table[(c ^ byte) & 0xFF] ^ (c >> 8)
    return c ^ 0xFFFFFFFF

def inflate_zlib(data):
    if len(data) < 6: raise Bad("short")
    cmf, flg = data[0], data[1]
    if (cmf & 0x0F) != 8: raise Bad("CM")
    if ((cmf << 8) | flg) % 31 != 0: raise Bad("FCHECK")
    if flg & 0x20: raise Bad("FDICT")
    out = inflate_raw(data[2:len(data)-4])
    want = struct.unpack(">I", data[-4:])[0]
    if adler32_port(out) != want: raise Bad("adler mismatch")
    return out

# ---------------------------------------------------------------- PNG port
ADAM7 = [(0,0,8,8),(4,0,8,8),(0,4,4,8),(2,0,4,4),(0,2,2,4),(1,0,2,2),(0,1,1,2)]

def get_bits(raw, idx, depth):
    bit = idx * depth
    shift = 8 - depth - (bit & 7)
    return (raw[bit >> 3] >> shift) & ((1 << depth) - 1)

def scale8(v, depth):
    return {1: v*255, 2: v*85, 4: v*17}.get(depth, v) & 0xFF

def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
    if pa <= pb and pa <= pc: return a
    if pb <= pc: return b
    return c

def unfilter_row(f, r, prior, bpp):
    n = len(r)
    if f == 0: return
    if f == 1:
        for i in range(bpp, n): r[i] = (r[i] + r[i-bpp]) & 0xFF
    elif f == 2:
        if prior is not None:
            for i in range(n): r[i] = (r[i] + prior[i]) & 0xFF
    elif f == 3:
        for i in range(n):
            a = r[i-bpp] if i >= bpp else 0
            b = prior[i] if prior is not None else 0
            r[i] = (r[i] + ((a + b) >> 1)) & 0xFF
    elif f == 4:
        for i in range(n):
            a = r[i-bpp] if i >= bpp else 0
            b = prior[i] if prior is not None else 0
            c = prior[i-bpp] if (prior is not None and i >= bpp) else 0
            r[i] = (r[i] + paeth(a, b, c)) & 0xFF
    else:
        raise Bad("bad filter %d" % f)

def expand_row(info, raw, pw, out, W, y, x0, dx):
    d = info["depth"]; ct = info["colorType"]
    pal, pal_a = info["palette"], info["palAlpha"]
    for i in range(pw):
        R = G = B = 0; A = 255
        if ct == 0:
            v = ((raw[i*2] << 8) | raw[i*2+1]) if d == 16 else get_bits(raw, i, d)
            R = G = B = raw[i*2] if d == 16 else scale8(v, d)
            if info["trnsGray"] >= 0 and v == info["trnsGray"]: A = 0
        elif ct == 2:
            if d == 16:
                r = (raw[i*6] << 8) | raw[i*6+1]; g = (raw[i*6+2] << 8) | raw[i*6+3]; b = (raw[i*6+4] << 8) | raw[i*6+5]
                R, G, B = raw[i*6], raw[i*6+2], raw[i*6+4]
            else:
                r, g, b = raw[i*3], raw[i*3+1], raw[i*3+2]
                R, G, B = r, g, b
            if info["trnsR"] >= 0 and (r, g, b) == (info["trnsR"], info["trnsG"], info["trnsB"]): A = 0
        elif ct == 3:
            idx = get_bits(raw, i, d)
            if idx*3 + 2 < len(pal):
                R, G, B = pal[idx*3], pal[idx*3+1], pal[idx*3+2]
            if idx < len(pal_a): A = pal_a[idx]
        elif ct == 4:
            if d == 16: R = G = B = raw[i*4]; A = raw[i*4+2]
            else:       R = G = B = raw[i*2]; A = raw[i*2+1]
        elif ct == 6:
            if d == 16: R, G, B, A = raw[i*8], raw[i*8+2], raw[i*8+4], raw[i*8+6]
            else:       R, G, B, A = raw[i*4], raw[i*4+1], raw[i*4+2], raw[i*4+3]
        o = (y * W + x0 + i * dx) * 4
        out[o], out[o+1], out[o+2], out[o+3] = R, G, B, A

def valid_depth(ct, d):
    return {0: (1,2,4,8,16), 3: (1,2,4,8), 2: (8,16), 4: (8,16), 6: (8,16)}.get(ct, ()).__contains__(d)

def channels(ct):
    return {0:1, 2:3, 3:1, 4:2, 6:4}[ct]

def decode_png(d):
    SIG = bytes([0x89]) + b"PNG\r\n\x1a\n"
    if len(d) < 20 or d[:8] != SIG: raise Bad("sig")
    info = {"palette": b"", "palAlpha": b"", "trnsGray": -1, "trnsR": -1, "trnsG": -1, "trnsB": -1}
    idat = bytearray(); have_ihdr = have_iend = False
    pos = 8
    while pos + 12 <= len(d) and not have_iend:
        clen = struct.unpack(">I", d[pos:pos+4])[0]
        if pos + 12 + clen > len(d): raise Bad("chunk len")
        typ = d[pos+4:pos+8]; body = d[pos+8:pos+8+clen]
        if crc32_port(d[pos+4:pos+8+clen]) != struct.unpack(">I", d[pos+8+clen:pos+12+clen])[0]:
            raise Bad("chunk crc")
        if typ == b"IHDR":
            if have_ihdr or clen != 13: raise Bad("ihdr")
            info["w"], info["h"] = struct.unpack(">II", body[:8])
            info["depth"], info["colorType"] = body[8], body[9]
            info["interlace"] = body[12]
            if body[10] or body[11] or info["interlace"] > 1: raise Bad("ihdr fields")
            if not info["w"] or not info["h"] or not valid_depth(info["colorType"], info["depth"]): raise Bad("ihdr combo")
            have_ihdr = True
        elif typ == b"PLTE":
            if not have_ihdr or clen % 3 or clen > 768: raise Bad("plte")
            info["palette"] = body
        elif typ == b"tRNS":
            ct = info["colorType"]
            if ct == 3: info["palAlpha"] = body
            elif ct == 0 and clen >= 2: info["trnsGray"] = (body[0] << 8) | body[1]
            elif ct == 2 and clen >= 6:
                info["trnsR"] = (body[0] << 8) | body[1]
                info["trnsG"] = (body[2] << 8) | body[3]
                info["trnsB"] = (body[4] << 8) | body[5]
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            have_iend = True
        elif not (typ[0] & 0x20):
            raise Bad("unknown critical " + typ.decode("latin1"))
        pos += 12 + clen
    if not have_ihdr or not have_iend or not idat: raise Bad("structure")
    if info["colorType"] == 3 and not info["palette"]: raise Bad("no plte")

    passes = ADAM7 if info["interlace"] else [(0, 0, 1, 1)]
    ch = channels(info["colorType"]); W, H = info["w"], info["h"]
    bpp = (ch * info["depth"] + 7) // 8

    expect = 0
    for (x0, y0, dx, dy) in passes:
        pw = (W - x0 + dx - 1) // dx if W > x0 else 0
        ph = (H - y0 + dy - 1) // dy if H > y0 else 0
        if pw and ph:
            expect += ph * (1 + (pw * ch * info["depth"] + 7) // 8)

    raw = bytearray(inflate_zlib(bytes(idat)))
    if len(raw) != expect: raise Bad("size %d != %d" % (len(raw), expect))

    out = bytearray(W * H * 4)
    off = 0
    for (x0, y0, dx, dy) in passes:
        pw = (W - x0 + dx - 1) // dx if W > x0 else 0
        ph = (H - y0 + dy - 1) // dy if H > y0 else 0
        if not pw or not ph: continue
        row_bytes = (pw * ch * info["depth"] + 7) // 8
        prior = None
        for y in range(ph):
            f = raw[off]; off += 1
            row = raw[off:off+row_bytes]; off += row_bytes
            unfilter_row(f, row, prior, bpp)
            expand_row(info, row, pw, out, W, y0 + y * dy, x0, dx)
            prior = row
    return W, H, bytes(out)

# ---------------------------------------------------------------- DDS mip port
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

def encode_dds(w, h, rgba, mipmaps):
    levels = 1
    if mipmaps:
        ww, hh = w, h
        while ww > 1 or hh > 1:
            ww = max(1, ww // 2); hh = max(1, hh // 2); levels += 1
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x8 | (0x20000 if mipmaps else 0)
    caps = 0x1000 | ((0x8 | 0x400000) if mipmaps else 0)
    hdr = b"DDS " + struct.pack("<7I", 124, flags, h, w, w*4, 0, levels if mipmaps else 0)
    hdr += b"\0" * 44
    hdr += struct.pack("<8I", 32, 0x41, 0, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000)
    hdr += struct.pack("<5I", caps, 0, 0, 0, 0)
    data = bytearray(rgba)
    lw, lh, lv = w, h, rgba
    if mipmaps:
        for _ in range(levels - 1):
            lw, lh, lv = halve_box(lw, lh, lv)
            data += lv
    return hdr + bytes(data), levels

# ---------------------------------------------------------------- test drivers
PASS = FAIL = 0
def check(name, ok, detail=""):
    global PASS, FAIL
    if ok: PASS += 1; print("  PASS  %s" % name)
    else:  FAIL += 1; print("  FAIL  %s  %s" % (name, detail))

def test_checksums():
    print("[checksums vs binascii]")
    for blob in (b"", b"hello", os.urandom(70000)):
        check("crc32 len=%d" % len(blob), crc32_port(blob) == (binascii.crc32(blob) & 0xFFFFFFFF))
        check("adler32 len=%d" % len(blob), adler32_port(blob) == (zlib.adler32(blob) & 0xFFFFFFFF))

def test_inflate_synthetic():
    print("[inflate vs zlib, synthetic]")
    rng = random.Random(20260716)
    cases = {
        "stored(level0)": (zlib.compress(os.urandom(5000), 0), None),
        "dynamic(level9,repetitive)": (None, bytes(rng.randrange(4) for _ in range(40000))),
        "dynamic(random)": (None, os.urandom(30000)),
        "tiny": (None, b"a"),
        "empty": (zlib.compress(b"", 6), b""),
    }
    for name, (comp, plain) in cases.items():
        if comp is None:
            comp = zlib.compress(plain, 9)
        want = zlib.decompress(comp)
        try:
            got = inflate_zlib(comp)
            check(name, got == want, "len %d vs %d" % (len(got), len(want)))
        except Bad as e:
            check(name, False, str(e))
    co = zlib.compressobj(6, zlib.DEFLATED, 15, 8, zlib.Z_FIXED)
    fixed = co.compress(b"fixed huffman block content 12345" * 30) + co.flush()
    try:
        check("fixed(Z_FIXED)", inflate_zlib(fixed) == zlib.decompress(fixed))
    except Bad as e:
        check("fixed(Z_FIXED)", False, str(e))

def find_real_pngs(limit=40):
    roots = [r"E:\Modlists\SkyGroundChronicles\mods"]
    hits = []
    for root in roots:
        for dirpath, _dirs, files in os.walk(root):
            if "enbseries" not in dirpath.lower() and "enb" not in dirpath.lower():
                continue
            for f in files:
                if f.lower().endswith(".png"):
                    hits.append(os.path.join(dirpath, f))
            if len(hits) > limit * 3:
                break
    seen, out = set(), []
    for h in hits:
        key = (os.path.basename(h).lower(), os.path.getsize(h))
        if key in seen: continue
        seen.add(key); out.append(h)
    return out[:limit]

def pil_rgba(img):
    from PIL import Image
    if img.mode in ("I", "I;16", "I;16B"):
        vals = list(img.getdata())
        w, h = img.size
        out = bytearray(w * h * 4)
        for i, v in enumerate(vals):
            g = (v >> 8) & 0xFF
            out[i*4:i*4+4] = bytes((g, g, g, 255))
        return bytes(out)
    return img.convert("RGBA").tobytes()

def test_real_pngs():
    print("[PNG decode vs PIL, real ENB files]")
    try:
        from PIL import Image
    except ImportError:
        check("PIL available", False, "Pillow not installed"); return
    files = find_real_pngs()
    if not files:
        check("real PNGs found", False); return
    for path in files:
        name = os.path.basename(path)
        try:
            with open(path, "rb") as fh:
                blob = fh.read()
            w, h, ours = decode_png(blob)
            ref = Image.open(path)
            if ref.size != (w, h):
                check(name, False, "size"); continue
            check("%s %dx%d mode=%s" % (name, w, h, ref.mode), pil_rgba(ref) == ours)
        except Bad as e:
            check(name, False, "decode: " + str(e))
        except Exception as e:  # noqa: BLE001 - report any oracle failure as a test failure
            check(name, False, repr(e))

def test_synthetic_pngs():
    print("[PNG decode vs PIL, synthetic modes]")
    try:
        from PIL import Image
    except ImportError:
        check("PIL available", False); return
    import io
    rng = random.Random(7)
    w, h = 23, 17   # odd sizes exercise sub-byte row padding

    def roundtrip(name, img):
        buf = io.BytesIO(); img.save(buf, "PNG")
        blob = buf.getvalue()
        dw, dh, ours = decode_png(blob)
        ref = pil_rgba(Image.open(io.BytesIO(blob)))
        check(name, (dw, dh) == img.size and ours == ref)

    rgba = Image.frombytes("RGBA", (w, h), bytes(rng.randrange(256) for _ in range(w*h*4)))
    roundtrip("RGBA8", rgba)
    roundtrip("RGB8", rgba.convert("RGB"))
    roundtrip("L8", rgba.convert("L"))
    roundtrip("LA8", rgba.convert("LA"))
    roundtrip("P8", rgba.convert("RGB").convert("P", palette=Image.ADAPTIVE))
    p = rgba.convert("RGB").convert("P", palette=Image.ADAPTIVE)
    p.info["transparency"] = bytes(range(0, 256, 2))[:128]
    import io as _io
    buf = _io.BytesIO(); p.save(buf, "PNG", transparency=bytes(min(i*2, 255) for i in range(128)))
    dw, dh, ours = decode_png(buf.getvalue())
    ref = pil_rgba(Image.open(_io.BytesIO(buf.getvalue())))
    check("P8+tRNS", (dw, dh) == (w, h) and ours == ref)
    roundtrip("1bit", rgba.convert("1"))
    g16 = Image.frombytes("I;16", (w, h), bytes(rng.randrange(256) for _ in range(w*h*2)))
    roundtrip("gray16(highbyte)", g16)

def build_png(w, h, ct, depth, pixels, interlace=0, palette=b"", trns=b""):
    """Independent PNG writer for cases PIL cannot author (Adam7, 16-bit RGB)."""
    ch = channels(ct)
    def be(v): return struct.pack(">I", v)
    def chunk(typ, body):
        return be(len(body)) + typ + body + be(binascii.crc32(typ + body) & 0xFFFFFFFF)
    def sample_bytes(x, y):
        px = pixels[y][x]
        out = b""
        for c in range(ch):
            v = px[c] if isinstance(px, tuple) else px
            out += struct.pack(">H", v) if depth == 16 else bytes((v,))
        return out
    passes = ADAM7 if interlace else [(0, 0, 1, 1)]
    raw = bytearray()
    for (x0, y0, dx, dy) in passes:
        pw = (w - x0 + dx - 1) // dx if w > x0 else 0
        ph = (h - y0 + dy - 1) // dy if h > y0 else 0
        if not pw or not ph: continue
        for yy in range(ph):
            raw.append(0)   # filter None
            row = bytearray()
            if depth >= 8:
                for xx in range(pw):
                    row += sample_bytes(x0 + xx*dx, y0 + yy*dy)
            else:
                acc = nbits = 0
                for xx in range(pw):
                    acc = (acc << depth) | pixels[y0 + yy*dy][x0 + xx*dx]
                    nbits += depth
                    if nbits == 8:
                        row.append(acc); acc = nbits = 0
                if nbits:
                    row.append(acc << (8 - nbits))
            raw += row
    ihdr = be(w) + be(h) + bytes((depth, ct, 0, 0, interlace))
    out = bytes([0x89]) + b"PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
    if palette: out += chunk(b"PLTE", palette)
    if trns: out += chunk(b"tRNS", trns)
    out += chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    return out

def test_handbuilt_pngs():
    print("[PNG decode, hand-built (Adam7 + 16-bit RGB/RGBA + colorkey)]")
    rng = random.Random(99)
    w, h = 13, 9
    px = [[(rng.randrange(256), rng.randrange(256), rng.randrange(256)) for _ in range(w)] for _ in range(h)]
    blob = build_png(w, h, 2, 8, px, interlace=1)
    dw, dh, ours = decode_png(blob)
    want = bytearray()
    for row in px:
        for (r, g, b) in row: want += bytes((r, g, b, 255))
    check("Adam7 RGB8 vs authored pixels", (dw, dh) == (w, h) and ours == bytes(want))
    try:
        from PIL import Image
        import io
        ref = pil_rgba(Image.open(io.BytesIO(blob)))
        check("Adam7 RGB8 vs PIL", ours == ref)
    except ImportError:
        check("Adam7 RGB8 vs PIL", False, "no PIL")

    px16 = [[(rng.randrange(65536), rng.randrange(65536), rng.randrange(65536), rng.randrange(65536))
             for _ in range(w)] for _ in range(h)]
    blob = build_png(w, h, 6, 16, px16)
    dw, dh, ours = decode_png(blob)
    want = bytearray()
    for row in px16:
        for (r, g, b, a) in row: want += bytes((r >> 8, g >> 8, b >> 8, a >> 8))
    check("RGBA16 high-byte narrowing", (dw, dh) == (w, h) and ours == bytes(want))

    key = px[0][0]
    trns = struct.pack(">HHH", key[0], key[1], key[2])
    blob = build_png(w, h, 2, 8, px, trns=trns)
    _, _, ours = decode_png(blob)
    ok = True
    for y in range(h):
        for x in range(w):
            expect_a = 0 if px[y][x] == key else 255
            if ours[(y*w + x)*4 + 3] != expect_a: ok = False
    check("RGB8 tRNS colorkey", ok)

    pal = bytes(rng.randrange(256) for _ in range(48))       # 16 entries
    pix2 = [[rng.randrange(16) for _ in range(w)] for _ in range(h)]
    blob = build_png(w, h, 3, 4, pix2, palette=pal, trns=bytes(range(16)))
    dw, dh, ours = decode_png(blob)
    ok = (dw, dh) == (w, h)
    for y in range(h):
        for x in range(w):
            i = pix2[y][x]; o = (y*w + x)*4
            if (ours[o], ours[o+1], ours[o+2], ours[o+3]) != (pal[i*3], pal[i*3+1], pal[i*3+2], i):
                ok = False
    check("palette4+tRNS vs authored", ok)

def test_dds_mips():
    print("[DDS mip chain]")
    rng = random.Random(3)
    for (w, h) in ((64, 64), (23, 17), (1, 8), (5, 1)):
        rgba = bytes(rng.randrange(256) for _ in range(w*h*4))
        blob, levels = encode_dds(w, h, rgba, True)
        expect_levels = 1
        ww, hh = w, h
        total = w*h*4
        while ww > 1 or hh > 1:
            ww = max(1, ww // 2); hh = max(1, hh // 2)
            total += ww*hh*4; expect_levels += 1
        ok = levels == expect_levels and len(blob) == 128 + total
        flags = struct.unpack("<I", blob[8:12])[0]
        caps = struct.unpack("<I", blob[108:112])[0]
        mipc = struct.unpack("<I", blob[28:32])[0]
        ok = ok and (flags & 0x20000) and (caps & 0x400000) and (caps & 0x8) and mipc == expect_levels
        check("%dx%d: %d levels, size+header flags" % (w, h, expect_levels), bool(ok))
    w, h = 4, 4
    rgba = bytes(range(0, 64))
    mw, mh, mip = halve_box(w, h, rgba)
    ok = (mw, mh) == (2, 2)
    for y in range(2):
        for x in range(2):
            for c in range(4):
                vals = [rgba[((2*y+dy)*4 + (2*x+dx))*4 + c] for dy in (0, 1) for dx in (0, 1)]
                if mip[(y*2 + x)*4 + c] != (sum(vals) + 2) >> 2: ok = False
    check("box filter = mean of 2x2 (round nearest)", ok)
    blob1, _ = encode_dds(3, 3, bytes(36), False)
    check("single-mip layout unchanged (mipcount=0, no MIPMAP caps)",
          len(blob1) == 128 + 36 and struct.unpack("<I", blob1[28:32])[0] == 0 and
          not (struct.unpack("<I", blob1[108:112])[0] & 0x400000))

def test_real_idat_streams():
    print("[inflate on real IDAT streams vs zlib]")
    files = find_real_pngs(limit=10)
    tested = 0
    for path in files:
        with open(path, "rb") as fh:
            d = fh.read()
        if d[:8] != bytes([0x89]) + b"PNG\r\n\x1a\n": continue
        idat = bytearray(); pos = 8
        while pos + 12 <= len(d):
            clen = struct.unpack(">I", d[pos:pos+4])[0]
            typ = d[pos+4:pos+8]
            if typ == b"IDAT": idat += d[pos+8:pos+8+clen]
            if typ == b"IEND": break
            pos += 12 + clen
        if not idat: continue
        try:
            ours = inflate_zlib(bytes(idat))
            ref = zlib.decompress(bytes(idat))
            check(os.path.basename(path) + " (%d -> %d bytes)" % (len(idat), len(ref)), ours == ref)
            tested += 1
        except Bad as e:
            check(os.path.basename(path), False, str(e))
        if tested >= 6: break

if __name__ == "__main__":
    test_checksums()
    test_inflate_synthetic()
    test_real_idat_streams()
    test_real_pngs()
    test_synthetic_pngs()
    test_handbuilt_pngs()
    test_dds_mips()
    print("\n%d passed, %d failed" % (PASS, FAIL))
    sys.exit(1 if FAIL else 0)
