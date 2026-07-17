#pragma once
//=============================================================================
//  TextureBC7.h — BC7 (BPTC) block codec
//
//  Decode: all eight BC7 modes per the D3D11 functional spec (partitions,
//  anchor indices, p-bits, rotation, index-selection), matching the reference
//  decoder bit-for-bit. Table data (partition shapes, anchor indices) is
//  machine-extracted from Microsoft DirectXTex (MIT), not hand-transcribed.
//
//  Encode: BASELINE tier, stated plainly: mode 6 only (one subset,
//  RGBA 7.7.7.7 + per-endpoint p-bit, 4-bit indices), endpoints = the block's
//  two most distant RGBA colors, nearest palette index per pixel. The same
//  tier as the BC1/BC3 encoder; a utility converter, not a partition-searching
//  art-pipeline encoder. Quality is measured, not claimed, in
//  tests/validate_bc7_codec.py.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <cstdint>

namespace SB::TexCodec::BC7
{
    // One 16-byte BC7 block -> 16 RGBA pixels (row-major 4x4).
    // A reserved/invalid mode decodes to transparent black, per spec.
    void DecodeBlock(const std::uint8_t* block, std::uint8_t px[16][4]);

    // 16 RGBA pixels -> one 16-byte mode-6 BC7 block.
    void EncodeBlockMode6(const std::uint8_t px[16][4], std::uint8_t* out);
}
