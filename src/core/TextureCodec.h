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
//  Supported now: TGA (uncompressed + RLE, 24/32-bit) read/write, BMP
//  (24/32-bit) read, PNG read (all five color types, 1/2/4/8/16-bit, Adam7
//  interlace, tRNS; 16-bit narrows to 8 via high byte), DDS read (uncompressed
//  32-bit masked, BC1/DXT1, BC3/DXT5; top mip) and DDS write (uncompressed
//  RGBA8 or BC1/BC3 block compression, optional box-filter mipmaps).
//  BC encode quality tier is BASELINE and documented: endpoints are the
//  block's two most distant colors, nearest palette index per pixel. Good
//  for utility conversion; not a cluster-fit art-pipeline encoder.
//  Honest nulls (not faked): BC7, DX10-header DDS, cubemaps/volumes, DDS
//  mip-chain read beyond the top level; the runtime texture-load hook.
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

    enum class DDSFormat { RGBA8, BC1, BC3 };

    Format DetectFormat(const std::uint8_t* data, std::size_t len);   // by magic bytes
    Format DetectFromPath(std::string_view path);                     // by extension

    // Decoders. Return an Image with valid=false on failure/unsupported.
    Image DecodeTGA(const std::uint8_t* data, std::size_t len);
    Image DecodeBMP(const std::uint8_t* data, std::size_t len);
    Image DecodePNG(const std::uint8_t* data, std::size_t len);
    Image DecodeDDSImage(const std::uint8_t* data, std::size_t len);  // RGBA8/BGRA8 masks, DXT1, DXT5; top mip
    Image Decode(const std::uint8_t* data, std::size_t len);          // dispatch by magic
    Image DecodeFile(const std::filesystem::path& path);

    DDSInfo ReadDDSHeader(const std::uint8_t* data, std::size_t len);

    // Encode an RGBA image as a DDS in the given format. With mipmaps, a
    // clamp-edge 2x2 box-filter chain down to 1x1 is appended and the header
    // carries the mip count. BC1 writes opaque color (no 1-bit alpha mode);
    // BC3 carries the alpha channel.
    std::vector<std::uint8_t> EncodeDDS(const Image& img, DDSFormat fmt, bool mipmaps);
    // Back-compat wrapper: uncompressed, default single-mip byte layout.
    std::vector<std::uint8_t> EncodeDDS_RGBA(const Image& img, bool mipmaps = false);
    bool WriteDDS(const std::filesystem::path& out, const Image& img,
                  bool mipmaps = true, DDSFormat fmt = DDSFormat::RGBA8);

    // 32-bit uncompressed top-origin TGA (the DDS -> editable-format lane).
    bool WriteTGA(const std::filesystem::path& out, const Image& img);

    // Convenience: decode any supported file -> write a DDS (mipmapped).
    bool ConvertToDDS(const std::filesystem::path& in, const std::filesystem::path& out);
    // Generic: decode any supported input (incl. DDS) and write by output
    // extension (.dds -> fmt+mips, .tga -> 32-bit TGA).
    bool Convert(const std::filesystem::path& in, const std::filesystem::path& out,
                 DDSFormat fmt = DDSFormat::RGBA8, bool mipmaps = true);
}
