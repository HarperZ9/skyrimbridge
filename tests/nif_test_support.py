"""Portable NIF/CMSD parsing helpers shared by offline validators.

These functions were originally held in a local scratch directory. Keeping
the exact parsers beside the validators makes the validation suite
self-contained and prevents results from depending on a vanished session.
"""

from __future__ import annotations

from pathlib import Path
import struct


def parse_nif(path: str | Path) -> dict:
    data = Path(path).read_bytes()
    assert data.startswith(b"Gamebryo File Format, Version 20.2.0.7\n")
    offset = data.index(b"\n") + 1
    offset += 4 + 1 + 4
    block_count = struct.unpack_from("<I", data, offset)[0]
    offset += 4
    offset += 4
    for _ in range(3):
        length = data[offset]
        offset += 1 + length
    type_count = struct.unpack_from("<H", data, offset)[0]
    offset += 2
    types = []
    for _ in range(type_count):
        length = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        types.append(data[offset : offset + length].decode("latin1"))
        offset += length
    type_indexes = list(
        struct.unpack_from(f"<{block_count}H", data, offset)
    )
    offset += 2 * block_count
    sizes = list(struct.unpack_from(f"<{block_count}I", data, offset))
    offset += 4 * block_count
    string_count = struct.unpack_from("<I", data, offset)[0]
    offset += 8
    strings = []
    for _ in range(string_count):
        length = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        strings.append(data[offset : offset + length].decode("latin1"))
        offset += length
    group_count = struct.unpack_from("<I", data, offset)[0]
    offset += 4 + 4 * group_count
    starts = []
    for size in sizes:
        starts.append(offset)
        offset += size
    return {
        "data": data,
        "types": types,
        "tidx": type_indexes,
        "starts": starts,
        "sizes": sizes,
        "strings": strings,
        "nblocks": block_count,
    }


def btype(nif: dict, index: int) -> str:
    return nif["types"][nif["tidx"][index]]


class Reader:
    def __init__(self, data: bytes, offset: int, end: int):
        self.data = data
        self.offset = offset
        self.end = end

    def u8(self) -> int:
        value = self.data[self.offset]
        self.offset += 1
        return value

    def u16(self) -> int:
        value = struct.unpack_from("<H", self.data, self.offset)[0]
        self.offset += 2
        return value

    def u32(self) -> int:
        value = struct.unpack_from("<I", self.data, self.offset)[0]
        self.offset += 4
        return value

    def f32(self) -> float:
        value = struct.unpack_from("<f", self.data, self.offset)[0]
        self.offset += 4
        return value

    def vec4(self) -> tuple[float, float, float, float]:
        value = struct.unpack_from("<4f", self.data, self.offset)
        self.offset += 16
        return value


def parse_cmsd(data: bytes, offset: int, size: int) -> dict:
    reader = Reader(data, offset, offset + size)
    out = {
        "bitsPerIndex": reader.u32(),
        "bitsPerWIndex": reader.u32(),
        "maskWIndex": reader.u32(),
        "maskIndex": reader.u32(),
        "error": reader.f32(),
        "boundsMin": reader.vec4(),
        "boundsMax": reader.vec4(),
        "weldingType": reader.u8(),
        "materialType": reader.u8(),
    }
    out["mat32"] = [reader.u32() for _ in range(reader.u32())]
    out["mat16"] = [reader.u32() for _ in range(reader.u32())]
    out["mat8"] = [reader.u32() for _ in range(reader.u32())]
    out["materials"] = [
        (reader.u32(), reader.u32()) for _ in range(reader.u32())
    ]
    out["numNamedMat"] = reader.u32()
    out["transforms"] = [
        (reader.vec4(), reader.vec4()) for _ in range(reader.u32())
    ]
    out["bigVerts"] = [reader.vec4() for _ in range(reader.u32())]
    out["bigTris"] = []
    for _ in range(reader.u32()):
        triangle = (reader.u16(), reader.u16(), reader.u16())
        out["bigTris"].append((*triangle, reader.u32(), reader.u16()))
    out["chunks"] = []
    for _ in range(reader.u32()):
        chunk = {
            "translation": reader.vec4(),
            "materialIndex": reader.u32(),
            "reference": reader.u16(),
            "transformIndex": reader.u16(),
        }
        for key in ("vertices", "indices", "strips", "welding"):
            chunk[key] = [reader.u16() for _ in range(reader.u32())]
        out["chunks"].append(chunk)
    out["numConvexPieceA"] = reader.u32()
    out["_consumed"] = reader.offset - offset
    return out


def reserialize_cmsd(out: dict) -> bytes:
    data = bytearray()

    def pack(fmt: str, *values: object) -> None:
        data.extend(struct.pack(fmt, *values))

    pack(
        "<IIII",
        out["bitsPerIndex"],
        out["bitsPerWIndex"],
        out["maskWIndex"],
        out["maskIndex"],
    )
    pack("<f", out["error"])
    pack("<4f", *out["boundsMin"])
    pack("<4f", *out["boundsMax"])
    pack("<BB", out["weldingType"], out["materialType"])
    for key in ("mat32", "mat16", "mat8"):
        pack("<I", len(out[key]))
        for value in out[key]:
            pack("<I", value)
    pack("<I", len(out["materials"]))
    for material, collision_filter in out["materials"]:
        pack("<II", material, collision_filter)
    pack("<I", out["numNamedMat"])
    pack("<I", len(out["transforms"]))
    for translation, rotation in out["transforms"]:
        pack("<4f", *translation)
        pack("<4f", *rotation)
    pack("<I", len(out["bigVerts"]))
    for vertex in out["bigVerts"]:
        pack("<4f", *vertex)
    pack("<I", len(out["bigTris"]))
    for v1, v2, v3, material, welding in out["bigTris"]:
        pack("<HHH", v1, v2, v3)
        pack("<I", material)
        pack("<H", welding)
    pack("<I", len(out["chunks"]))
    for chunk in out["chunks"]:
        pack("<4f", *chunk["translation"])
        pack("<I", chunk["materialIndex"])
        pack("<HH", chunk["reference"], chunk["transformIndex"])
        for key in ("vertices", "indices", "strips", "welding"):
            pack("<I", len(chunk[key]))
            for value in chunk[key]:
                pack("<H", value)
    pack("<I", out["numConvexPieceA"])
    return bytes(data)
