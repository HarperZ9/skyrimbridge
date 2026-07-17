#pragma once
//=============================================================================
//  Inflate.h — pure DEFLATE (RFC 1951) + zlib (RFC 1950) decompression
//
//  Zero-dependency inflate for the texture codec's PNG path (and any other
//  compressed asset format later). No engine or SKSE access, fully
//  unit-testable offline. Checksums are VERIFIED, not skipped: zlib streams
//  must pass Adler-32, and the CRC-32 here backs PNG chunk validation.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <cstddef>
#include <cstdint>
#include <vector>

namespace SB::Inflate
{
    // Raw DEFLATE stream -> out (appended). False on malformed input.
    // sizeHint reserves the output buffer up front when the caller knows it.
    bool InflateRaw(const std::uint8_t* data, std::size_t len,
                    std::vector<std::uint8_t>& out, std::size_t sizeHint = 0);

    // zlib stream: 2-byte header + DEFLATE + Adler-32 trailer (verified).
    bool InflateZlib(const std::uint8_t* data, std::size_t len,
                     std::vector<std::uint8_t>& out, std::size_t sizeHint = 0);

    std::uint32_t Crc32(const std::uint8_t* data, std::size_t len);
    std::uint32_t Adler32(const std::uint8_t* data, std::size_t len);
}
