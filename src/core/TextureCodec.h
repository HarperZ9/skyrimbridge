#pragma once
//=============================================================================
//  TextureCodec.h — native foreign-format texture decode / DDS transcode
//
//  Step one of native non-.dds asset integration: a pure, zero-dependency
//  codec that decodes foreign texture formats into RGBA32 and writes DDS the
//  engine natively accepts (R8G8B8A8_UNORM). No engine or SKSE access, so it is
//  fully unit-testable offline; the runtime D3D11 texture-substitution hook and
//  an offline batch converter both sit on top of this core.
//
//  Supported now: TGA (uncompressed + RLE, 24/32-bit), BMP (24/32-bit), DDS
//  header read + uncompressed DDS write.
//  Honest nulls (not faked): PNG decode needs a DEFLATE/inflate stage; BCn
//  block compression on write. Both are separate follow-ups.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace SB::TexCodec
{
    enum class Format { Unknown, TGA, BMP, PNG, DDS };

    // Decoded image: tightly packed RGBA (width*height*4), top-left origin.
    struct Image
    {
        std::uint32_t             width = 0;
        std::uint32_t             height = 0;
        std::vector<std::uint8_t> rgba;
        bool valid = false;
    };

    // DDS header facts (for inspection / passthrough decisions).
    struct DDSInfo
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t mipCount = 0;
        std::string   format;       // "DXT1" / "DXT5" / "DX10" / "RGBA8" / ...
        bool          compressed = false;
        bool          valid = false;
    };

    Format DetectFormat(const std::uint8_t* data, std::size_t len);   // by magic bytes
    Format DetectFromPath(std::string_view path);                     // by extension

    // Decoders. Return an Image with valid=false on failure/unsupported.
    Image DecodeTGA(const std::uint8_t* data, std::size_t len);
    Image DecodeBMP(const std::uint8_t* data, std::size_t len);
    Image Decode(const std::uint8_t* data, std::size_t len);          // dispatch by magic
    Image DecodeFile(const std::filesystem::path& path);

    DDSInfo ReadDDSHeader(const std::uint8_t* data, std::size_t len);

    // Encode an RGBA image as an uncompressed R8G8B8A8_UNORM DDS (single mip).
    std::vector<std::uint8_t> EncodeDDS_RGBA(const Image& img);
    bool WriteDDS(const std::filesystem::path& out, const Image& img);

    // Convenience: decode any supported file -> write a DDS. Returns false on
    // an unsupported/failed decode (PNG currently returns false).
    bool ConvertToDDS(const std::filesystem::path& in, const std::filesystem::path& out);
}
