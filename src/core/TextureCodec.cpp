//=============================================================================
//  TextureCodec.cpp — foreign-format decode + uncompressed DDS transcode
//=============================================================================

#include "TextureCodec.h"

#include <cstring>
#include <fstream>

namespace SB::TexCodec
{
    // ── little-endian readers / writers ──────────────────────────────────
    static std::uint16_t rd16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
    static std::uint32_t rd32(const std::uint8_t* p)
    {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
               (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    }
    static void wr32(std::vector<std::uint8_t>& v, std::uint32_t x)
    {
        v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
        v.push_back((x >> 16) & 0xFF); v.push_back((x >> 24) & 0xFF);
    }

    // ── format detection ─────────────────────────────────────────────────
    Format DetectFormat(const std::uint8_t* d, std::size_t len)
    {
        if (len >= 4 && d[0] == 'D' && d[1] == 'D' && d[2] == 'S' && d[3] == ' ') return Format::DDS;
        if (len >= 2 && d[0] == 'B' && d[1] == 'M') return Format::BMP;
        if (len >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return Format::PNG;
        return Format::Unknown;   // TGA is headerless; resolved by extension/fallback
    }

    Format DetectFromPath(std::string_view path)
    {
        auto dot = path.find_last_of('.');
        if (dot == std::string_view::npos) return Format::Unknown;
        std::string ext;
        for (auto c : path.substr(dot + 1)) ext.push_back(static_cast<char>(std::tolower(c)));
        if (ext == "tga") return Format::TGA;
        if (ext == "bmp") return Format::BMP;
        if (ext == "png") return Format::PNG;
        if (ext == "dds") return Format::DDS;
        return Format::Unknown;
    }

    // ── TGA (uncompressed + RLE truecolor, 24/32-bit) ────────────────────
    Image DecodeTGA(const std::uint8_t* d, std::size_t len)
    {
        Image img;
        if (len < 18) return img;
        std::uint8_t idLen = d[0], cmapType = d[1], imgType = d[2], bpp = d[16], desc = d[17];
        std::uint16_t w = rd16(d + 12), h = rd16(d + 14);
        if (cmapType != 0 || (imgType != 2 && imgType != 10)) return img;
        if ((bpp != 24 && bpp != 32) || w == 0 || h == 0) return img;

        const int ch = bpp / 8;
        const std::size_t total = static_cast<std::size_t>(w) * h;
        std::vector<std::uint8_t> px(total * ch);   // stored BGRA/BGR
        std::size_t off = 18 + idLen;

        if (imgType == 2) {
            if (off + px.size() > len) return img;
            std::memcpy(px.data(), d + off, px.size());
        } else {                                     // RLE
            std::size_t o = off, di = 0, done = 0;
            while (done < total && o < len) {
                std::uint8_t hdr = d[o++];
                int count = (hdr & 0x7F) + 1;
                if (hdr & 0x80) {                    // run packet
                    if (o + ch > len) break;
                    for (int i = 0; i < count && done < total; ++i, ++done)
                        for (int c = 0; c < ch; ++c) px[di++] = d[o + c];
                    o += ch;
                } else {                             // raw packet
                    for (int i = 0; i < count && done < total; ++i, ++done) {
                        if (o + ch > len) break;
                        for (int c = 0; c < ch; ++c) px[di++] = d[o++];
                    }
                }
            }
        }

        const bool topOrigin = (desc & 0x20) != 0;
        img.rgba.resize(total * 4);
        for (std::uint32_t y = 0; y < h; ++y) {
            std::uint32_t sy = topOrigin ? y : (h - 1 - y);
            for (std::uint32_t x = 0; x < w; ++x) {
                std::size_t s = (static_cast<std::size_t>(sy) * w + x) * ch;
                std::size_t di = (static_cast<std::size_t>(y) * w + x) * 4;
                img.rgba[di + 0] = px[s + 2];        // R <- B
                img.rgba[di + 1] = px[s + 1];        // G
                img.rgba[di + 2] = px[s + 0];        // B <- R
                img.rgba[di + 3] = (ch == 4) ? px[s + 3] : 255;
            }
        }
        img.width = w; img.height = h; img.valid = true;
        return img;
    }

    // ── BMP (uncompressed BI_RGB, 24/32-bit) ─────────────────────────────
    Image DecodeBMP(const std::uint8_t* d, std::size_t len)
    {
        Image img;
        if (len < 54 || d[0] != 'B' || d[1] != 'M') return img;
        std::uint32_t dataOff = rd32(d + 10), hdrSize = rd32(d + 14);
        if (hdrSize < 40) return img;
        std::int32_t w = static_cast<std::int32_t>(rd32(d + 18));
        std::int32_t h = static_cast<std::int32_t>(rd32(d + 22));
        std::uint16_t planes = rd16(d + 26), bpp = rd16(d + 28);
        std::uint32_t compression = rd32(d + 30);
        if (planes != 1 || compression != 0 || (bpp != 24 && bpp != 32)) return img;

        const bool topDown = h < 0;
        const std::uint32_t W = static_cast<std::uint32_t>(w < 0 ? -w : w);
        const std::uint32_t H = static_cast<std::uint32_t>(h < 0 ? -h : h);
        if (W == 0 || H == 0) return img;
        const int ch = bpp / 8;
        const std::uint32_t rowSize = ((W * ch + 3) / 4) * 4;   // 4-byte aligned rows
        if (dataOff + static_cast<std::size_t>(rowSize) * H > len) return img;

        img.rgba.resize(static_cast<std::size_t>(W) * H * 4);
        for (std::uint32_t y = 0; y < H; ++y) {
            std::uint32_t sr = topDown ? y : (H - 1 - y);
            const std::uint8_t* row = d + dataOff + static_cast<std::size_t>(sr) * rowSize;
            for (std::uint32_t x = 0; x < W; ++x) {
                const std::uint8_t* p = row + static_cast<std::size_t>(x) * ch;   // BGR(A)
                std::size_t di = (static_cast<std::size_t>(y) * W + x) * 4;
                img.rgba[di + 0] = p[2]; img.rgba[di + 1] = p[1]; img.rgba[di + 2] = p[0];
                img.rgba[di + 3] = (ch == 4) ? p[3] : 255;
            }
        }
        img.width = W; img.height = H; img.valid = true;
        return img;
    }

    Image Decode(const std::uint8_t* d, std::size_t len)
    {
        switch (DetectFormat(d, len)) {
        case Format::BMP: return DecodeBMP(d, len);
        case Format::PNG: return {};   // needs a DEFLATE stage (deferred)
        case Format::DDS: return {};   // already engine-native
        default:          return DecodeTGA(d, len);   // headerless fallback
        }
    }

    Image DecodeFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return {};
        std::vector<std::uint8_t> buf((std::istreambuf_iterator<char>(in)), {});
        if (buf.empty()) return {};
        if (DetectFromPath(path.string()) == Format::TGA)
            return DecodeTGA(buf.data(), buf.size());
        return Decode(buf.data(), buf.size());
    }

    // ── DDS ──────────────────────────────────────────────────────────────
    DDSInfo ReadDDSHeader(const std::uint8_t* d, std::size_t len)
    {
        DDSInfo info;
        if (len < 128 || d[0] != 'D' || d[1] != 'D' || d[2] != 'S' || d[3] != ' ') return info;
        if (rd32(d + 4) != 124) return info;
        info.height = rd32(d + 12);
        info.width = rd32(d + 16);
        info.mipCount = rd32(d + 28);
        std::uint32_t pfFlags = rd32(d + 80), fourCC = rd32(d + 84);
        if (pfFlags & 0x4) {   // DDPF_FOURCC
            char fc[5] = { static_cast<char>(fourCC & 0xFF), static_cast<char>((fourCC >> 8) & 0xFF),
                           static_cast<char>((fourCC >> 16) & 0xFF), static_cast<char>((fourCC >> 24) & 0xFF), 0 };
            info.format = fc;
            info.compressed = info.format == "DXT1" || info.format == "DXT2" || info.format == "DXT3" ||
                              info.format == "DXT4" || info.format == "DXT5" || info.format == "DX10" ||
                              info.format == "ATI1" || info.format == "ATI2" || info.format == "BC4U" || info.format == "BC5U";
        } else {
            info.format = "RGB" + std::to_string(rd32(d + 88));
            info.compressed = false;
        }
        info.valid = true;
        return info;
    }

    std::vector<std::uint8_t> EncodeDDS_RGBA(const Image& img)
    {
        std::vector<std::uint8_t> out;
        if (!img.valid || img.rgba.size() != static_cast<std::size_t>(img.width) * img.height * 4) return out;
        out.reserve(128 + img.rgba.size());
        out.push_back('D'); out.push_back('D'); out.push_back('S'); out.push_back(' ');
        wr32(out, 124);                                  // dwSize
        wr32(out, 0x1 | 0x2 | 0x4 | 0x1000 | 0x8);       // CAPS|HEIGHT|WIDTH|PIXELFORMAT|PITCH
        wr32(out, img.height);
        wr32(out, img.width);
        wr32(out, img.width * 4);                        // pitch
        wr32(out, 0);                                    // depth
        wr32(out, 0);                                    // mipMapCount (single)
        for (int i = 0; i < 11; ++i) wr32(out, 0);       // reserved1
        wr32(out, 32);                                   // pixelformat size
        wr32(out, 0x1 | 0x40);                           // DDPF_ALPHAPIXELS|DDPF_RGB
        wr32(out, 0);                                    // fourCC
        wr32(out, 32);                                   // RGBBitCount
        wr32(out, 0x000000FF);                           // R mask (RGBA byte order)
        wr32(out, 0x0000FF00);                           // G mask
        wr32(out, 0x00FF0000);                           // B mask
        wr32(out, 0xFF000000);                           // A mask
        wr32(out, 0x1000);                               // caps = DDSCAPS_TEXTURE
        wr32(out, 0); wr32(out, 0); wr32(out, 0);        // caps2..4
        wr32(out, 0);                                    // reserved2
        out.insert(out.end(), img.rgba.begin(), img.rgba.end());
        return out;
    }

    bool WriteDDS(const std::filesystem::path& out, const Image& img)
    {
        auto bytes = EncodeDDS_RGBA(img);
        if (bytes.empty()) return false;
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(f);
    }

    bool ConvertToDDS(const std::filesystem::path& in, const std::filesystem::path& out)
    {
        Image img = DecodeFile(in);
        if (!img.valid) return false;
        return WriteDDS(out, img);
    }
}
