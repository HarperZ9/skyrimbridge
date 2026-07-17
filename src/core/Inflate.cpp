//=============================================================================
//  Inflate.cpp — DEFLATE + zlib decompression, CRC-32, Adler-32
//
//  Canonical-Huffman decoder using the per-length count/offset walk from
//  RFC 1951 §3.2.2 (the same structure as Mark Adler's reference "puff").
//  Validated offline against Python's zlib on real ENB PNG streams.
//=============================================================================

#include "Inflate.h"

namespace SB::Inflate
{
    // ── checksums ────────────────────────────────────────────────────────
    std::uint32_t Crc32(const std::uint8_t* data, std::size_t len)
    {
        static std::uint32_t table[256];
        static bool init = false;
        if (!init) {
            for (std::uint32_t n = 0; n < 256; ++n) {
                std::uint32_t c = n;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
                table[n] = c;
            }
            init = true;
        }
        std::uint32_t c = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < len; ++i)
            c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    }

    std::uint32_t Adler32(const std::uint8_t* data, std::size_t len)
    {
        std::uint32_t a = 1, b = 0;
        std::size_t i = 0;
        while (i < len) {
            // 5552 = largest n with no 32-bit overflow before the mod (zlib).
            std::size_t chunk = len - i < 5552 ? len - i : 5552;
            for (std::size_t e = i + chunk; i < e; ++i) { a += data[i]; b += a; }
            a %= 65521; b %= 65521;
        }
        return (b << 16) | a;
    }

    // ── bit reader (LSB-first, per RFC 1951) ─────────────────────────────
    namespace
    {
        struct BitReader
        {
            const std::uint8_t* d;
            std::size_t         len;
            std::size_t         pos = 0;
            std::uint32_t       bitbuf = 0;
            int                 bitcnt = 0;
            bool                bad = false;

            int Bits(int need)
            {
                while (bitcnt < need) {
                    if (pos >= len) { bad = true; return 0; }
                    bitbuf |= static_cast<std::uint32_t>(d[pos++]) << bitcnt;
                    bitcnt += 8;
                }
                int v = static_cast<int>(bitbuf & ((1u << need) - 1));
                bitbuf >>= need;
                bitcnt -= need;
                return v;
            }

            void AlignByte() { bitbuf = 0; bitcnt = 0; }
        };

        // Canonical Huffman: symbol counts per code length + sorted symbols.
        struct Huff
        {
            std::uint16_t count[16] = {};
            std::uint16_t symbol[288] = {};

            bool Build(const std::uint8_t* lengths, int n)
            {
                for (int i = 0; i < 16; ++i) count[i] = 0;
                for (int i = 0; i < n; ++i) ++count[lengths[i]];
                if (count[0] == n) return false;         // no codes at all
                int left = 1;                            // over-subscription check
                for (int l = 1; l < 16; ++l) {
                    left <<= 1;
                    left -= count[l];
                    if (left < 0) return false;
                }
                std::uint16_t offs[16] = {};
                for (int l = 1; l < 15; ++l) offs[l + 1] = static_cast<std::uint16_t>(offs[l] + count[l]);
                for (int i = 0; i < n; ++i)
                    if (lengths[i]) symbol[offs[lengths[i]]++] = static_cast<std::uint16_t>(i);
                return true;
            }
        };

        int Decode(BitReader& br, const Huff& h)
        {
            int code = 0, first = 0, index = 0;
            for (int len = 1; len <= 15; ++len) {
                code |= br.Bits(1);
                if (br.bad) return -1;
                int cnt = h.count[len];
                if (code - first < cnt) return h.symbol[index + (code - first)];
                index += cnt;
                first = (first + cnt) << 1;
                code <<= 1;
            }
            return -1;
        }

        // Length / distance code tables (RFC 1951 §3.2.5).
        const std::uint16_t LBASE[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
                                          35,43,51,59,67,83,99,115,131,163,195,227,258 };
        const std::uint8_t  LEXT[29]  = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
                                          3,3,3,3,4,4,4,4,5,5,5,5,0 };
        const std::uint16_t DBASE[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
                                          257,385,513,769,1025,1537,2049,3073,4097,6145,
                                          8193,12289,16385,24577 };
        const std::uint8_t  DEXT[30]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
                                          7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

        bool InflateBlock(BitReader& br, const Huff& lit, const Huff& dist,
                          std::vector<std::uint8_t>& out)
        {
            for (;;) {
                int sym = Decode(br, lit);
                if (sym < 0) return false;
                if (sym < 256) { out.push_back(static_cast<std::uint8_t>(sym)); continue; }
                if (sym == 256) return true;             // end of block
                sym -= 257;
                if (sym >= 29) return false;
                std::size_t length = LBASE[sym] + static_cast<std::size_t>(br.Bits(LEXT[sym]));
                int dsym = Decode(br, dist);
                if (dsym < 0 || dsym >= 30) return false;
                std::size_t distance = DBASE[dsym] + static_cast<std::size_t>(br.Bits(DEXT[dsym]));
                if (br.bad || distance > out.size()) return false;
                std::size_t from = out.size() - distance;
                for (std::size_t i = 0; i < length; ++i)   // may overlap; byte-wise
                    out.push_back(out[from + i]);
            }
        }

        bool FixedTables(Huff& lit, Huff& dist)
        {
            std::uint8_t ll[288];
            for (int i = 0; i < 144; ++i) ll[i] = 8;
            for (int i = 144; i < 256; ++i) ll[i] = 9;
            for (int i = 256; i < 280; ++i) ll[i] = 7;
            for (int i = 280; i < 288; ++i) ll[i] = 8;
            std::uint8_t dl[30];
            for (int i = 0; i < 30; ++i) dl[i] = 5;
            return lit.Build(ll, 288) && dist.Build(dl, 30);
        }

        bool DynamicTables(BitReader& br, Huff& lit, Huff& dist)
        {
            static const std::uint8_t ORDER[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
            int hlit = br.Bits(5) + 257;
            int hdist = br.Bits(5) + 1;
            int hclen = br.Bits(4) + 4;
            if (br.bad || hlit > 286 || hdist > 30) return false;

            std::uint8_t clLen[19] = {};
            for (int i = 0; i < hclen; ++i) clLen[ORDER[i]] = static_cast<std::uint8_t>(br.Bits(3));
            if (br.bad) return false;
            Huff cl;
            if (!cl.Build(clLen, 19)) return false;

            std::uint8_t lens[286 + 30] = {};
            int n = 0;
            while (n < hlit + hdist) {
                int sym = Decode(br, cl);
                if (sym < 0) return false;
                if (sym < 16) { lens[n++] = static_cast<std::uint8_t>(sym); continue; }
                int repeat, value = 0;
                if (sym == 16) {
                    if (n == 0) return false;
                    value = lens[n - 1];
                    repeat = 3 + br.Bits(2);
                } else if (sym == 17) {
                    repeat = 3 + br.Bits(3);
                } else {
                    repeat = 11 + br.Bits(7);
                }
                if (br.bad || n + repeat > hlit + hdist) return false;
                while (repeat--) lens[n++] = static_cast<std::uint8_t>(value);
            }
            if (lens[256] == 0) return false;            // must have an end-of-block code
            return lit.Build(lens, hlit) && dist.Build(lens + hlit, hdist);
        }
    }

    // ── entry points ─────────────────────────────────────────────────────
    bool InflateRaw(const std::uint8_t* data, std::size_t len,
                    std::vector<std::uint8_t>& out, std::size_t sizeHint)
    {
        if (sizeHint) out.reserve(out.size() + sizeHint);
        BitReader br{ data, len };
        for (;;) {
            int bfinal = br.Bits(1);
            int btype = br.Bits(2);
            if (br.bad) return false;
            if (btype == 0) {                            // stored
                br.AlignByte();
                if (br.pos + 4 > len) return false;
                std::uint32_t l = static_cast<std::uint32_t>(data[br.pos]) | (static_cast<std::uint32_t>(data[br.pos + 1]) << 8);
                std::uint32_t nl = static_cast<std::uint32_t>(data[br.pos + 2]) | (static_cast<std::uint32_t>(data[br.pos + 3]) << 8);
                br.pos += 4;
                if ((l ^ 0xFFFFu) != nl || br.pos + l > len) return false;
                out.insert(out.end(), data + br.pos, data + br.pos + l);
                br.pos += l;
            } else if (btype == 1 || btype == 2) {
                Huff lit, dist;
                bool ok = (btype == 1) ? FixedTables(lit, dist) : DynamicTables(br, lit, dist);
                if (!ok || !InflateBlock(br, lit, dist, out)) return false;
            } else {
                return false;                            // btype 3 is reserved
            }
            if (bfinal) return true;
        }
    }

    bool InflateZlib(const std::uint8_t* data, std::size_t len,
                     std::vector<std::uint8_t>& out, std::size_t sizeHint)
    {
        if (len < 6) return false;
        std::uint8_t cmf = data[0], flg = data[1];
        if ((cmf & 0x0F) != 8) return false;             // CM must be deflate
        if (((cmf << 8) | flg) % 31 != 0) return false;  // FCHECK
        if (flg & 0x20) return false;                    // FDICT unsupported
        std::size_t start = out.size();
        if (!InflateRaw(data + 2, len - 6, out, sizeHint)) return false;
        std::uint32_t want = (static_cast<std::uint32_t>(data[len - 4]) << 24) |
                             (static_cast<std::uint32_t>(data[len - 3]) << 16) |
                             (static_cast<std::uint32_t>(data[len - 2]) << 8) |
                              static_cast<std::uint32_t>(data[len - 1]);
        return Adler32(out.data() + start, out.size() - start) == want;
    }
}
