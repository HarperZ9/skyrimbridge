//=============================================================================
//  TextureCodec.cpp — foreign-format decode + uncompressed DDS transcode
//=============================================================================

#include "TextureCodec.h"
#include "Inflate.h"

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

    // ── PNG (W3C spec; zlib/DEFLATE via SB::Inflate) ─────────────────────
    namespace
    {
        struct PNGInfo
        {
            std::uint32_t w = 0, h = 0;
            std::uint8_t  depth = 0, colorType = 0, interlace = 0;
            std::vector<std::uint8_t> palette;    // RGB triples (PLTE)
            std::vector<std::uint8_t> palAlpha;   // per-index alpha (tRNS)
            std::int32_t trnsGray = -1;           // colorkey sample, -1 = none
            std::int32_t trnsR = -1, trnsG = -1, trnsB = -1;

            int Channels() const
            {
                switch (colorType) {
                case 0: return 1; case 2: return 3; case 3: return 1;
                case 4: return 2; case 6: return 4;
                }
                return 0;
            }
        };

        std::uint32_t be32(const std::uint8_t* p)
        {
            return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
                   (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
        }

        // idx-th sample of a packed row, MSB-first within each byte (depth <= 8).
        std::uint32_t GetBits(const std::uint8_t* raw, std::uint32_t idx, int depth)
        {
            std::uint32_t bit = idx * static_cast<std::uint32_t>(depth);
            int shift = 8 - depth - static_cast<int>(bit & 7);
            return (raw[bit >> 3] >> shift) & ((1u << depth) - 1);
        }

        std::uint8_t Scale8(std::uint32_t v, int depth)   // exact for 1/2/4/8
        {
            switch (depth) { case 1: return static_cast<std::uint8_t>(v * 255);
                             case 2: return static_cast<std::uint8_t>(v * 85);
                             case 4: return static_cast<std::uint8_t>(v * 17); }
            return static_cast<std::uint8_t>(v);
        }

        int Paeth(int a, int b, int c)
        {
            int p = a + b - c;
            int pa = p > a ? p - a : a - p;
            int pb = p > b ? p - b : b - p;
            int pc = p > c ? p - c : c - p;
            if (pa <= pb && pa <= pc) return a;
            if (pb <= pc) return b;
            return c;
        }

        bool UnfilterRow(std::uint8_t f, std::uint8_t* r, const std::uint8_t* prior,
                         std::size_t n, int bpp)
        {
            switch (f) {
            case 0: return true;
            case 1:
                for (std::size_t i = bpp; i < n; ++i) r[i] = static_cast<std::uint8_t>(r[i] + r[i - bpp]);
                return true;
            case 2:
                if (prior) for (std::size_t i = 0; i < n; ++i) r[i] = static_cast<std::uint8_t>(r[i] + prior[i]);
                return true;
            case 3:
                for (std::size_t i = 0; i < n; ++i) {
                    int a = i >= static_cast<std::size_t>(bpp) ? r[i - bpp] : 0;
                    int b = prior ? prior[i] : 0;
                    r[i] = static_cast<std::uint8_t>(r[i] + ((a + b) >> 1));
                }
                return true;
            case 4:
                for (std::size_t i = 0; i < n; ++i) {
                    int a = i >= static_cast<std::size_t>(bpp) ? r[i - bpp] : 0;
                    int b = prior ? prior[i] : 0;
                    int c = (prior && i >= static_cast<std::size_t>(bpp)) ? prior[i - bpp] : 0;
                    r[i] = static_cast<std::uint8_t>(r[i] + Paeth(a, b, c));
                }
                return true;
            }
            return false;
        }

        // Defiltered raw row (pw pixels) -> RGBA at output row y, columns
        // x0, x0+dx, ... (dx=1 for non-interlaced; Adam7 pass geometry else).
        void ExpandRow(const PNGInfo& in, const std::uint8_t* raw, std::uint32_t pw,
                       std::uint8_t* out, std::uint32_t W, std::uint32_t y,
                       std::uint32_t x0, std::uint32_t dx)
        {
            const int d = in.depth;
            for (std::uint32_t i = 0; i < pw; ++i) {
                std::uint8_t R = 0, G = 0, B = 0, A = 255;
                switch (in.colorType) {
                case 0: {   // grayscale (1/2/4/8/16)
                    std::uint32_t v = (d == 16) ? ((raw[i * 2] << 8) | raw[i * 2 + 1]) : GetBits(raw, i, d);
                    R = G = B = (d == 16) ? raw[i * 2] : Scale8(v, d);
                    if (in.trnsGray >= 0 && v == static_cast<std::uint32_t>(in.trnsGray)) A = 0;
                    break; }
                case 2: {   // RGB (8/16)
                    std::uint32_t r, g, b;
                    if (d == 16) { r = (raw[i*6] << 8) | raw[i*6+1]; g = (raw[i*6+2] << 8) | raw[i*6+3]; b = (raw[i*6+4] << 8) | raw[i*6+5];
                                   R = raw[i*6]; G = raw[i*6+2]; B = raw[i*6+4]; }
                    else         { r = raw[i*3]; g = raw[i*3+1]; b = raw[i*3+2]; R = static_cast<std::uint8_t>(r); G = static_cast<std::uint8_t>(g); B = static_cast<std::uint8_t>(b); }
                    if (in.trnsR >= 0 && r == static_cast<std::uint32_t>(in.trnsR) &&
                        g == static_cast<std::uint32_t>(in.trnsG) && b == static_cast<std::uint32_t>(in.trnsB)) A = 0;
                    break; }
                case 3: {   // palette (1/2/4/8)
                    std::uint32_t idx = GetBits(raw, i, d);
                    if (idx * 3 + 2 < in.palette.size()) {
                        R = in.palette[idx * 3]; G = in.palette[idx * 3 + 1]; B = in.palette[idx * 3 + 2];
                    }
                    if (idx < in.palAlpha.size()) A = in.palAlpha[idx];
                    break; }
                case 4: {   // gray + alpha (8/16)
                    if (d == 16) { R = G = B = raw[i * 4]; A = raw[i * 4 + 2]; }
                    else         { R = G = B = raw[i * 2]; A = raw[i * 2 + 1]; }
                    break; }
                case 6: {   // RGBA (8/16)
                    if (d == 16) { R = raw[i*8]; G = raw[i*8+2]; B = raw[i*8+4]; A = raw[i*8+6]; }
                    else         { R = raw[i*4]; G = raw[i*4+1]; B = raw[i*4+2]; A = raw[i*4+3]; }
                    break; }
                }
                std::size_t o = (static_cast<std::size_t>(y) * W + x0 + static_cast<std::size_t>(i) * dx) * 4;
                out[o] = R; out[o + 1] = G; out[o + 2] = B; out[o + 3] = A;
            }
        }

        bool ValidDepth(std::uint8_t ct, std::uint8_t d)
        {
            switch (ct) {
            case 0: return d == 1 || d == 2 || d == 4 || d == 8 || d == 16;
            case 3: return d == 1 || d == 2 || d == 4 || d == 8;
            case 2: case 4: case 6: return d == 8 || d == 16;
            }
            return false;
        }
    }

    Image DecodePNG(const std::uint8_t* d, std::size_t len)
    {
        Image img;
        static const std::uint8_t SIG[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        if (len < 8 + 12 || std::memcmp(d, SIG, 8) != 0) return img;

        // Chunk walk. Every CRC is verified; unknown CRITICAL chunks fail
        // loudly (ancillary ones are skipped, per spec).
        PNGInfo info;
        std::vector<std::uint8_t> idat;
        bool haveIHDR = false, haveIEND = false;
        std::size_t pos = 8;
        while (pos + 12 <= len && !haveIEND) {
            std::uint32_t clen = be32(d + pos);
            if (clen > len || pos + 12 + clen > len) return img;
            const std::uint8_t* type = d + pos + 4;
            const std::uint8_t* body = d + pos + 8;
            if (Inflate::Crc32(type, 4 + clen) != be32(body + clen)) return img;

            if (!std::memcmp(type, "IHDR", 4)) {
                if (haveIHDR || clen != 13) return img;
                info.w = be32(body); info.h = be32(body + 4);
                info.depth = body[8]; info.colorType = body[9];
                info.interlace = body[12];
                if (body[10] != 0 || body[11] != 0 || info.interlace > 1) return img;
                if (!info.w || !info.h || !ValidDepth(info.colorType, info.depth)) return img;
                if (static_cast<std::uint64_t>(info.w) * info.h > (1ull << 26)) return img;   // 67M px cap
                haveIHDR = true;
            } else if (!std::memcmp(type, "PLTE", 4)) {
                if (!haveIHDR || clen % 3 || clen > 768) return img;
                info.palette.assign(body, body + clen);
            } else if (!std::memcmp(type, "tRNS", 4)) {
                if (!haveIHDR) return img;
                if (info.colorType == 3)      info.palAlpha.assign(body, body + clen);
                else if (info.colorType == 0 && clen >= 2) info.trnsGray = (body[0] << 8) | body[1];
                else if (info.colorType == 2 && clen >= 6) {
                    info.trnsR = (body[0] << 8) | body[1];
                    info.trnsG = (body[2] << 8) | body[3];
                    info.trnsB = (body[4] << 8) | body[5];
                }
            } else if (!std::memcmp(type, "IDAT", 4)) {
                if (!haveIHDR) return img;
                idat.insert(idat.end(), body, body + clen);
            } else if (!std::memcmp(type, "IEND", 4)) {
                haveIEND = true;
            } else if (!(type[0] & 0x20)) {
                return img;                        // unknown critical chunk
            }
            pos += 12 + clen;
        }
        if (!haveIHDR || !haveIEND || idat.empty()) return img;
        if (info.colorType == 3 && info.palette.empty()) return img;

        // Pass geometry: one full pass, or the seven Adam7 sub-images.
        struct Pass { std::uint32_t x0, y0, dx, dy; };
        static const Pass ADAM7[7] = { {0,0,8,8}, {4,0,8,8}, {0,4,4,8}, {2,0,4,4},
                                       {0,2,2,4}, {1,0,2,2}, {0,1,1,2} };
        const Pass single = { 0, 0, 1, 1 };
        const Pass* passes = info.interlace ? ADAM7 : &single;
        const int nPasses = info.interlace ? 7 : 1;
        const int ch = info.Channels();
        const int bpp = (ch * info.depth + 7) / 8;

        // Exact expected size of the decompressed scanline stream.
        std::size_t expect = 0;
        for (int p = 0; p < nPasses; ++p) {
            std::uint32_t pw = info.w > passes[p].x0 ? (info.w - passes[p].x0 + passes[p].dx - 1) / passes[p].dx : 0;
            std::uint32_t ph = info.h > passes[p].y0 ? (info.h - passes[p].y0 + passes[p].dy - 1) / passes[p].dy : 0;
            if (pw && ph)
                expect += static_cast<std::size_t>(ph) * (1 + (static_cast<std::size_t>(pw) * ch * info.depth + 7) / 8);
        }

        std::vector<std::uint8_t> raw;
        if (!Inflate::InflateZlib(idat.data(), idat.size(), raw, expect)) return img;
        if (raw.size() != expect) return img;

        img.rgba.assign(static_cast<std::size_t>(info.w) * info.h * 4, 0);
        std::size_t off = 0;
        for (int p = 0; p < nPasses; ++p) {
            std::uint32_t pw = info.w > passes[p].x0 ? (info.w - passes[p].x0 + passes[p].dx - 1) / passes[p].dx : 0;
            std::uint32_t ph = info.h > passes[p].y0 ? (info.h - passes[p].y0 + passes[p].dy - 1) / passes[p].dy : 0;
            if (!pw || !ph) continue;
            std::size_t rowBytes = (static_cast<std::size_t>(pw) * ch * info.depth + 7) / 8;
            std::uint8_t* prior = nullptr;
            for (std::uint32_t y = 0; y < ph; ++y) {
                std::uint8_t filter = raw[off++];
                std::uint8_t* row = raw.data() + off;
                off += rowBytes;
                if (!UnfilterRow(filter, row, prior, rowBytes, bpp)) return img;
                ExpandRow(info, row, pw, img.rgba.data(), info.w,
                          passes[p].y0 + y * passes[p].dy, passes[p].x0, passes[p].dx);
                prior = row;
            }
        }
        img.width = info.w; img.height = info.h; img.valid = true;
        return img;
    }

    // Block-codec helpers defined with the BC section below.
    namespace
    {
        void DecodeBlockBC1(const std::uint8_t* in, bool separateAlpha, std::uint8_t px[16][4]);
        void DecodeBlockAlpha(const std::uint8_t* in, std::uint8_t alpha[16]);
    }

    // DDS -> RGBA (top mip): uncompressed 32-bit masked formats and DXT1/DXT5.
    Image DecodeDDSImage(const std::uint8_t* d, std::size_t len)
    {
        Image img;
        if (len < 128 || d[0] != 'D' || d[1] != 'D' || d[2] != 'S' || d[3] != ' ') return img;
        if (rd32(d + 4) != 124) return img;
        const std::uint32_t H = rd32(d + 12), W = rd32(d + 16);
        if (!W || !H || static_cast<std::uint64_t>(W) * H > (1ull << 26)) return img;
        const std::uint32_t pfFlags = rd32(d + 80), fourCC = rd32(d + 84);
        const std::uint8_t* data = d + 128;
        const std::size_t   avail = len - 128;

        if (pfFlags & 0x4) {                                       // FOURCC path
            const bool dxt1 = fourCC == 0x31545844;                // "DXT1"
            const bool dxt5 = fourCC == 0x35545844;                // "DXT5"
            if (!dxt1 && !dxt5) return img;                        // BC7/DX10/...: honest null
            const std::uint32_t bw = (W + 3) / 4, bh = (H + 3) / 4;
            const std::size_t blockBytes = dxt5 ? 16 : 8;
            if (static_cast<std::size_t>(bw) * bh * blockBytes > avail) return img;
            img.rgba.resize(static_cast<std::size_t>(W) * H * 4);
            std::uint8_t px[16][4]; std::uint8_t alpha[16];
            for (std::uint32_t by = 0; by < bh; ++by)
                for (std::uint32_t bx = 0; bx < bw; ++bx) {
                    const std::uint8_t* b = data + (static_cast<std::size_t>(by) * bw + bx) * blockBytes;
                    if (dxt5) { DecodeBlockAlpha(b, alpha); DecodeBlockBC1(b + 8, true, px); }
                    else      { DecodeBlockBC1(b, false, px); }
                    for (int r = 0; r < 4; ++r)
                        for (int c = 0; c < 4; ++c) {
                            std::uint32_t x = bx * 4 + c, y = by * 4 + r;
                            if (x >= W || y >= H) continue;
                            std::uint8_t* o = img.rgba.data() + (static_cast<std::size_t>(y) * W + x) * 4;
                            o[0] = px[r*4+c][0]; o[1] = px[r*4+c][1]; o[2] = px[r*4+c][2];
                            o[3] = dxt5 ? alpha[r*4+c] : px[r*4+c][3];
                        }
                }
        } else if (pfFlags & 0x40) {                               // uncompressed RGB(A)
            if (rd32(d + 88) != 32) return img;                    // 32bpp only
            const std::uint32_t mr = rd32(d + 92), mg = rd32(d + 96), mb = rd32(d + 100), ma = rd32(d + 104);
            auto shiftOf = [](std::uint32_t mask) -> int {
                for (int s = 0; s < 32; s += 8) if (mask == (0xFFu << s)) return s;
                return -1;
            };
            const int sr = shiftOf(mr), sg = shiftOf(mg), sb = shiftOf(mb);
            const int sa = ma ? shiftOf(ma) : -2;                  // -2 = no alpha channel
            if (sr < 0 || sg < 0 || sb < 0 || sa == -1) return img;   // non-byte-aligned masks: honest null
            if (static_cast<std::size_t>(W) * H * 4 > avail) return img;
            img.rgba.resize(static_cast<std::size_t>(W) * H * 4);
            for (std::size_t i = 0; i < static_cast<std::size_t>(W) * H; ++i) {
                std::uint32_t v = rd32(data + i * 4);
                img.rgba[i*4+0] = static_cast<std::uint8_t>(v >> sr);
                img.rgba[i*4+1] = static_cast<std::uint8_t>(v >> sg);
                img.rgba[i*4+2] = static_cast<std::uint8_t>(v >> sb);
                img.rgba[i*4+3] = sa >= 0 ? static_cast<std::uint8_t>(v >> sa) : 255;
            }
        } else {
            return img;
        }
        img.width = W; img.height = H; img.valid = true;
        return img;
    }

    Image Decode(const std::uint8_t* d, std::size_t len)
    {
        switch (DetectFormat(d, len)) {
        case Format::BMP: return DecodeBMP(d, len);
        case Format::PNG: return DecodePNG(d, len);
        case Format::DDS: return DecodeDDSImage(d, len);
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

    // ── BC1 / BC3 block codec ────────────────────────────────────────────
    // Interpolation arithmetic matches the D3D reference decoders (verified
    // empirically against Pillow's native BCn decoder): truncating (2a+b)/3
    // and (a+b)/2 for color, truncating /7 and /5 ramps for BC3 alpha, and
    // DXT5 color blocks are ALWAYS 4-color regardless of endpoint order.
    namespace
    {
        std::uint16_t To565(const std::uint8_t* c)
        {
            return static_cast<std::uint16_t>(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3));
        }

        void From565(std::uint16_t v, int* out)   // bit-replicated expansion
        {
            int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
            out[0] = (r << 3) | (r >> 2);
            out[1] = (g << 2) | (g >> 4);
            out[2] = (b << 3) | (b >> 2);
        }

        // 4x4 block of RGBA source pixels, edge-clamped.
        void FetchBlock(const Image& img, std::uint32_t bx, std::uint32_t by, std::uint8_t px[16][4])
        {
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) {
                    std::uint32_t x = bx * 4 + c; if (x >= img.width)  x = img.width - 1;
                    std::uint32_t y = by * 4 + r; if (y >= img.height) y = img.height - 1;
                    const std::uint8_t* s = img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
                    px[r * 4 + c][0] = s[0]; px[r * 4 + c][1] = s[1];
                    px[r * 4 + c][2] = s[2]; px[r * 4 + c][3] = s[3];
                }
        }

        // Color palette a decoder will reconstruct from two 565 endpoints.
        void BCPalette(std::uint16_t c0, std::uint16_t c1, bool fourColor, int p[4][4])
        {
            From565(c0, p[0]); From565(c1, p[1]);
            p[0][3] = p[1][3] = p[2][3] = p[3][3] = 255;
            if (fourColor) {
                for (int k = 0; k < 3; ++k) {
                    p[2][k] = (2 * p[0][k] + p[1][k]) / 3;
                    p[3][k] = (p[0][k] + 2 * p[1][k]) / 3;
                }
            } else {
                for (int k = 0; k < 3; ++k) {
                    p[2][k] = (p[0][k] + p[1][k]) / 2;
                    p[3][k] = 0;
                }
                p[3][3] = 0;                     // transparent black
            }
        }

        // Baseline endpoint pick: the two most distant colors in the block.
        void PickEndpoints(const std::uint8_t px[16][4], std::uint16_t& c0, std::uint16_t& c1)
        {
            int bi = 0, bj = 0; long best = -1;
            for (int i = 0; i < 16; ++i)
                for (int j = i + 1; j < 16; ++j) {
                    long d = 0;
                    for (int k = 0; k < 3; ++k) { long e = px[i][k] - px[j][k]; d += e * e; }
                    if (d > best) { best = d; bi = i; bj = j; }
                }
            c0 = To565(px[bi]); c1 = To565(px[bj]);
            if (c0 < c1) { std::uint16_t t = c0; c0 = c1; c1 = t; }
        }

        void EncodeBlockBC1(const std::uint8_t px[16][4], std::uint8_t* out)
        {
            std::uint16_t c0, c1;
            PickEndpoints(px, c0, c1);
            int p[4][4];
            BCPalette(c0, c1, true, p);
            std::uint32_t idx = 0;
            const int n = (c0 == c1) ? 1 : 4;    // degenerate: only index 0 is safe
            for (int i = 0; i < 16; ++i) {
                int bk = 0; long bd = 1L << 30;
                for (int k = 0; k < n; ++k) {
                    long d = 0;
                    for (int c = 0; c < 3; ++c) { long e = px[i][c] - p[k][c]; d += e * e; }
                    if (d < bd) { bd = d; bk = k; }
                }
                idx |= static_cast<std::uint32_t>(bk) << (2 * i);
            }
            out[0] = c0 & 0xFF; out[1] = c0 >> 8;
            out[2] = c1 & 0xFF; out[3] = c1 >> 8;
            out[4] = idx & 0xFF; out[5] = (idx >> 8) & 0xFF;
            out[6] = (idx >> 16) & 0xFF; out[7] = (idx >> 24) & 0xFF;
        }

        void EncodeBlockAlpha(const std::uint8_t px[16][4], std::uint8_t* out)
        {
            std::uint8_t a0 = 0, a1 = 255;
            for (int i = 0; i < 16; ++i) {
                if (px[i][3] > a0) a0 = px[i][3];
                if (px[i][3] < a1) a1 = px[i][3];
            }
            int ramp[8];
            int count;
            if (a0 > a1) {                       // 8-value mode
                ramp[0] = a0; ramp[1] = a1;
                for (int i = 1; i <= 6; ++i) ramp[1 + i] = ((7 - i) * a0 + i * a1) / 7;
                count = 8;
            } else {                             // flat block: index 0 = a0
                ramp[0] = a0; count = 1;
            }
            std::uint64_t bits = 0;
            for (int i = 0; i < 16; ++i) {
                int bk = 0, bd = 1 << 30;
                for (int k = 0; k < count; ++k) {
                    int d = px[i][3] - ramp[k]; if (d < 0) d = -d;
                    if (d < bd) { bd = d; bk = k; }
                }
                bits |= static_cast<std::uint64_t>(bk) << (3 * i);
            }
            out[0] = a0; out[1] = a1;
            for (int i = 0; i < 6; ++i) out[2 + i] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF);
        }

        std::vector<std::uint8_t> CompressBC(const Image& img, bool bc3)
        {
            std::uint32_t bw = (img.width + 3) / 4, bh = (img.height + 3) / 4;
            std::vector<std::uint8_t> out(static_cast<std::size_t>(bw) * bh * (bc3 ? 16 : 8));
            std::uint8_t px[16][4];
            std::size_t o = 0;
            for (std::uint32_t by = 0; by < bh; ++by)
                for (std::uint32_t bx = 0; bx < bw; ++bx) {
                    FetchBlock(img, bx, by, px);
                    if (bc3) { EncodeBlockAlpha(px, out.data() + o); o += 8; }
                    EncodeBlockBC1(px, out.data() + o); o += 8;
                }
            return out;
        }

        void DecodeBlockBC1(const std::uint8_t* in, bool separateAlpha, std::uint8_t px[16][4])
        {
            std::uint16_t c0 = static_cast<std::uint16_t>(in[0] | (in[1] << 8));
            std::uint16_t c1 = static_cast<std::uint16_t>(in[2] | (in[3] << 8));
            std::uint32_t idx = static_cast<std::uint32_t>(in[4]) | (static_cast<std::uint32_t>(in[5]) << 8) |
                                (static_cast<std::uint32_t>(in[6]) << 16) | (static_cast<std::uint32_t>(in[7]) << 24);
            int p[4][4];
            BCPalette(c0, c1, separateAlpha || c0 > c1, p);
            for (int i = 0; i < 16; ++i) {
                int k = (idx >> (2 * i)) & 3;
                px[i][0] = static_cast<std::uint8_t>(p[k][0]); px[i][1] = static_cast<std::uint8_t>(p[k][1]);
                px[i][2] = static_cast<std::uint8_t>(p[k][2]); px[i][3] = static_cast<std::uint8_t>(p[k][3]);
            }
        }

        void DecodeBlockAlpha(const std::uint8_t* in, std::uint8_t alpha[16])
        {
            int a0 = in[0], a1 = in[1];
            int ramp[8] = { a0, a1 };
            if (a0 > a1)
                for (int i = 1; i <= 6; ++i) ramp[1 + i] = ((7 - i) * a0 + i * a1) / 7;
            else {
                for (int i = 1; i <= 4; ++i) ramp[1 + i] = ((5 - i) * a0 + i * a1) / 5;
                ramp[6] = 0; ramp[7] = 255;
            }
            std::uint64_t bits = 0;
            for (int i = 0; i < 6; ++i) bits |= static_cast<std::uint64_t>(in[2 + i]) << (8 * i);
            for (int i = 0; i < 16; ++i)
                alpha[i] = static_cast<std::uint8_t>(ramp[(bits >> (3 * i)) & 7]);
        }
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

    // Clamp-edge 2x2 box filter, one mip level down (round to nearest).
    static Image HalveBox(const Image& src)
    {
        Image m;
        m.width = src.width > 1 ? src.width / 2 : 1;
        m.height = src.height > 1 ? src.height / 2 : 1;
        m.rgba.resize(static_cast<std::size_t>(m.width) * m.height * 4);
        for (std::uint32_t y = 0; y < m.height; ++y) {
            std::uint32_t y0 = 2 * y, y1 = y0 + 1 < src.height ? y0 + 1 : src.height - 1;
            for (std::uint32_t x = 0; x < m.width; ++x) {
                std::uint32_t x0 = 2 * x, x1 = x0 + 1 < src.width ? x0 + 1 : src.width - 1;
                for (int c = 0; c < 4; ++c) {
                    unsigned s = src.rgba[(static_cast<std::size_t>(y0) * src.width + x0) * 4 + c] +
                                 src.rgba[(static_cast<std::size_t>(y0) * src.width + x1) * 4 + c] +
                                 src.rgba[(static_cast<std::size_t>(y1) * src.width + x0) * 4 + c] +
                                 src.rgba[(static_cast<std::size_t>(y1) * src.width + x1) * 4 + c];
                    m.rgba[(static_cast<std::size_t>(y) * m.width + x) * 4 + c] = static_cast<std::uint8_t>((s + 2) >> 2);
                }
            }
        }
        m.valid = true;
        return m;
    }

    std::vector<std::uint8_t> EncodeDDS(const Image& img, DDSFormat fmt, bool mipmaps)
    {
        std::vector<std::uint8_t> out;
        if (!img.valid || img.rgba.size() != static_cast<std::size_t>(img.width) * img.height * 4) return out;

        std::uint32_t levels = 1;
        if (mipmaps)
            for (std::uint32_t w = img.width, h = img.height; w > 1 || h > 1;
                 w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1) ++levels;

        const bool bc = fmt != DDSFormat::RGBA8;
        const bool bc3 = fmt == DDSFormat::BC3;
        const std::uint32_t blockBytes = bc3 ? 16 : 8;

        out.reserve(128 + img.rgba.size() * (mipmaps ? 4 : 3) / 3 / (bc ? 4 : 1));
        out.push_back('D'); out.push_back('D'); out.push_back('S'); out.push_back(' ');
        wr32(out, 124);                                  // dwSize
        std::uint32_t flags = 0x1 | 0x2 | 0x4 | 0x1000;  // CAPS|HEIGHT|WIDTH|PIXELFORMAT
        flags |= bc ? 0x80000 : 0x8;                     // LINEARSIZE : PITCH
        if (mipmaps) flags |= 0x20000;                   // DDSD_MIPMAPCOUNT
        wr32(out, flags);
        wr32(out, img.height);
        wr32(out, img.width);
        wr32(out, bc ? ((img.width + 3) / 4) * ((img.height + 3) / 4) * blockBytes
                     : img.width * 4);                   // linear size / pitch
        wr32(out, 0);                                    // depth
        wr32(out, mipmaps ? levels : 0);                 // mipMapCount
        for (int i = 0; i < 11; ++i) wr32(out, 0);       // reserved1
        wr32(out, 32);                                   // pixelformat size
        if (bc) {
            wr32(out, 0x4);                              // DDPF_FOURCC
            wr32(out, bc3 ? 0x35545844u : 0x31545844u);  // "DXT5" / "DXT1"
            wr32(out, 0); wr32(out, 0); wr32(out, 0); wr32(out, 0); wr32(out, 0);
        } else {
            wr32(out, 0x1 | 0x40);                       // DDPF_ALPHAPIXELS|DDPF_RGB
            wr32(out, 0);                                // fourCC
            wr32(out, 32);                               // RGBBitCount
            wr32(out, 0x000000FF);                       // R mask (RGBA byte order)
            wr32(out, 0x0000FF00);                       // G mask
            wr32(out, 0x00FF0000);                       // B mask
            wr32(out, 0xFF000000);                       // A mask
        }
        std::uint32_t caps = 0x1000;                     // DDSCAPS_TEXTURE
        if (mipmaps) caps |= 0x8 | 0x400000;             // COMPLEX | MIPMAP
        wr32(out, caps);
        wr32(out, 0); wr32(out, 0); wr32(out, 0);        // caps2..4
        wr32(out, 0);                                    // reserved2

        Image level = img;
        for (std::uint32_t i = 0; ; ++i) {
            if (bc) {
                auto blocks = CompressBC(level, bc3);
                out.insert(out.end(), blocks.begin(), blocks.end());
            } else {
                out.insert(out.end(), level.rgba.begin(), level.rgba.end());
            }
            if (i + 1 >= levels) break;
            level = HalveBox(level);
        }
        return out;
    }

    std::vector<std::uint8_t> EncodeDDS_RGBA(const Image& img, bool mipmaps)
    {
        return EncodeDDS(img, DDSFormat::RGBA8, mipmaps);
    }

    bool WriteDDS(const std::filesystem::path& out, const Image& img, bool mipmaps, DDSFormat fmt)
    {
        auto bytes = EncodeDDS(img, fmt, mipmaps);
        if (bytes.empty()) return false;
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(f);
    }

    bool WriteTGA(const std::filesystem::path& out, const Image& img)
    {
        if (!img.valid || img.width > 0xFFFF || img.height > 0xFFFF) return false;
        std::vector<std::uint8_t> bytes;
        bytes.reserve(18 + img.rgba.size());
        std::uint8_t hdr[18] = {};
        hdr[2] = 2;                                       // uncompressed truecolor
        hdr[12] = img.width & 0xFF;  hdr[13] = (img.width >> 8) & 0xFF;
        hdr[14] = img.height & 0xFF; hdr[15] = (img.height >> 8) & 0xFF;
        hdr[16] = 32;
        hdr[17] = 0x28;                                   // top-origin, 8 alpha bits
        bytes.insert(bytes.end(), hdr, hdr + 18);
        for (std::size_t i = 0; i < img.rgba.size(); i += 4) {   // RGBA -> BGRA
            bytes.push_back(img.rgba[i + 2]); bytes.push_back(img.rgba[i + 1]);
            bytes.push_back(img.rgba[i + 0]); bytes.push_back(img.rgba[i + 3]);
        }
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(f);
    }

    bool ConvertToDDS(const std::filesystem::path& in, const std::filesystem::path& out)
    {
        return Convert(in, out, DDSFormat::RGBA8, true);
    }

    bool Convert(const std::filesystem::path& in, const std::filesystem::path& out,
                 DDSFormat fmt, bool mipmaps)
    {
        Image img = DecodeFile(in);
        if (!img.valid) return false;
        Format target = DetectFromPath(out.string());
        if (target == Format::TGA) return WriteTGA(out, img);
        if (target == Format::DDS) return WriteDDS(out, img, mipmaps, fmt);
        return false;                                     // PNG/BMP write: honest null
    }
}
