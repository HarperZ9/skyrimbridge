#!/usr/bin/env python3
"""Offline validation of TexCodec::Describe (header-only texture inspection
behind the TextureInfo native and texture.info verb).

Python port checked against files with known-by-construction headers: a PIL
PNG (independent writer), a legacy DXT5 DDS and a DX10 BC7 DDS built to the
container layouts the earlier receipts locked, a TGA and BMP. FormChain and
the LandTexture schema in the same utility pass are engine-bound reads
(compile-verified; in-game per protocol).
"""

import io
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


def rd16(b, o): return struct.unpack_from("<H", b, o)[0]
def rd32(b, o): return struct.unpack_from("<I", b, o)[0]


def dds_format_name(h, size):
    pf_flags, fourcc = rd32(h, 80), h[84:88]
    if pf_flags & 4:
        name = fourcc.decode("latin1")
        if name == "DX10" and size >= 148:
            dxgi = rd32(h, 128)
            name = {71: "DX10/BC1", 77: "DX10/BC3", 98: "DX10/BC7",
                    28: "DX10/RGBA8", 87: "DX10/BGRA8"}.get(dxgi, f"DX10/{dxgi}")
        return name
    return f"RGB{rd32(h, 88)}"


def describe(path):
    """Port of TexCodec::Describe."""
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        h = f.read(256)
    if h[:4] == b"DDS " and rd32(h, 4) == 124:
        mips = rd32(h, 28) or 1
        return (f"DDS {dds_format_name(h, len(h))} {rd32(h, 16)}x{rd32(h, 12)} "
                f"mips={mips} ({size} bytes)")
    if h[:4] == b"\x89PNG" and len(h) >= 33:
        w = struct.unpack_from(">I", h, 16)[0]
        hh = struct.unpack_from(">I", h, 20)[0]
        return (f"PNG {w}x{hh} depth={h[24]} colorType={h[25]} "
                f"interlace={h[28]} ({size} bytes)")
    if h[:2] == b"BM" and len(h) >= 30:
        w = struct.unpack_from("<i", h, 18)[0]
        hh = struct.unpack_from("<i", h, 22)[0]
        return f"BMP {w}x{hh} {rd16(h, 28)}bpp ({size} bytes)"
    if path.lower().endswith(".tga") and len(h) >= 18:
        t, bpp = h[2], h[16]
        if t in (2, 10) and bpp in (24, 32):
            return f"TGA type={t} {bpp}bpp {rd16(h, 12)}x{rd16(h, 14)} ({size} bytes)"
    return ""


TMP = os.path.join(os.environ.get("TEMP", "."), "sb_texinfo")
os.makedirs(TMP, exist_ok=True)

print("[known-by-construction headers]")
p = os.path.join(TMP, "t.png")
Image.new("RGBA", (200, 100)).save(p)
check("PNG dims/type from an independent writer",
      describe(p) == f"PNG 200x100 depth=8 colorType=6 interlace=0 ({os.path.getsize(p)} bytes)",
      describe(p))

p = os.path.join(TMP, "t.bmp")
Image.new("RGB", (64, 32)).save(p)
d = describe(p)
check("BMP dims/bpp", d.startswith("BMP 64x32 24bpp"), d)

p = os.path.join(TMP, "t.tga")
hdr = bytearray(18)
hdr[2] = 2; hdr[12:14] = struct.pack("<H", 512); hdr[14:16] = struct.pack("<H", 256); hdr[16] = 32
open(p, "wb").write(bytes(hdr) + b"\0" * 64)
d = describe(p)
check("TGA type/bpp/dims (extension-trusted, field-sanity-checked)",
      d.startswith("TGA type=2 32bpp 512x256"), d)

p = os.path.join(TMP, "legacy.dds")
h = bytearray(b"DDS ") + struct.pack("<IIIII", 124, 0x000A1007, 128, 256, 0)
h += struct.pack("<II", 0, 7) + b"\0" * 44
h += struct.pack("<II4s", 32, 4, b"DXT5") + b"\0" * 20
h += struct.pack("<IIIII", 0x401008, 0, 0, 0, 0)
open(p, "wb").write(bytes(h) + b"\0" * 64)
d = describe(p)
check("legacy DXT5 DDS with mip count", d.startswith("DDS DXT5 256x128 mips=7"), d)

p = os.path.join(TMP, "bc7.dds")
h = bytearray(b"DDS ") + struct.pack("<IIIII", 124, 0x000A1007, 32, 32, 0)
h += struct.pack("<II", 0, 0) + b"\0" * 44
h += struct.pack("<II4s", 32, 4, b"DX10") + b"\0" * 20
h += struct.pack("<IIIII", 0x401008, 0, 0, 0, 0)
h += struct.pack("<IIIII", 98, 3, 0, 1, 0)
open(p, "wb").write(bytes(h) + b"\0" * 64)
d = describe(p)
check("DX10 BC7 DDS named, zero mip count reported as 1",
      d.startswith("DDS DX10/BC7 32x32 mips=1"), d)

p = os.path.join(TMP, "junk.bin")
open(p, "wb").write(b"not a texture at all" * 4)
check("unknown content refused (empty string)", describe(p) == "")

for fn in os.listdir(TMP):
    os.unlink(os.path.join(TMP, fn))
os.rmdir(TMP)

print(f"\n{_passed} passed, {_failed} failed")
sys.exit(1 if _failed else 0)
