//=============================================================================
//  TextureBC7.cpp — BC7 (BPTC) block codec
//
//  Algorithm and tables follow the D3D11 BC7 functional specification, as
//  implemented by the Microsoft DirectXTex reference decoder (MIT). The
//  partition and anchor tables below were machine-extracted from
//  DirectXTex BC6HBC7.cpp (g_aPartitionTable / g_aFixUp), not typed from
//  memory. Offline receipt: tests/validate_bc7_codec.py locks this decode
//  model against Pillow on hand-built blocks for every mode and on real
//  modlist BC7 textures.
//=============================================================================

#include "TextureBC7.h"

#include <cstring>
#include <utility>

namespace SB::TexCodec::BC7
{
    namespace
    {
        // 4x4 subset assignment per shape (from the BC7 spec tables).
        const std::uint8_t kPartition2[64][16] = {
            { 0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1 },
            { 0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1 },
            { 0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1 },
            { 0,0,0,1,0,0,1,1,0,0,1,1,0,1,1,1 },
            { 0,0,0,0,0,0,0,1,0,0,0,1,0,0,1,1 },
            { 0,0,1,1,0,1,1,1,0,1,1,1,1,1,1,1 },
            { 0,0,0,1,0,0,1,1,0,1,1,1,1,1,1,1 },
            { 0,0,0,0,0,0,0,1,0,0,1,1,0,1,1,1 },
            { 0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,1 },
            { 0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1 },
            { 0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1 },
            { 0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1 },
            { 0,0,0,1,0,1,1,1,1,1,1,1,1,1,1,1 },
            { 0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1 },
            { 0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1 },
            { 0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1 },
            { 0,0,0,0,1,0,0,0,1,1,1,0,1,1,1,1 },
            { 0,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0 },
            { 0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,0 },
            { 0,1,1,1,0,0,1,1,0,0,0,1,0,0,0,0 },
            { 0,0,1,1,0,0,0,1,0,0,0,0,0,0,0,0 },
            { 0,0,0,0,1,0,0,0,1,1,0,0,1,1,1,0 },
            { 0,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0 },
            { 0,1,1,1,0,0,1,1,0,0,1,1,0,0,0,1 },
            { 0,0,1,1,0,0,0,1,0,0,0,1,0,0,0,0 },
            { 0,0,0,0,1,0,0,0,1,0,0,0,1,1,0,0 },
            { 0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0 },
            { 0,0,1,1,0,1,1,0,0,1,1,0,1,1,0,0 },
            { 0,0,0,1,0,1,1,1,1,1,1,0,1,0,0,0 },
            { 0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0 },
            { 0,1,1,1,0,0,0,1,1,0,0,0,1,1,1,0 },
            { 0,0,1,1,1,0,0,1,1,0,0,1,1,1,0,0 },
            { 0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1 },
            { 0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1 },
            { 0,1,0,1,1,0,1,0,0,1,0,1,1,0,1,0 },
            { 0,0,1,1,0,0,1,1,1,1,0,0,1,1,0,0 },
            { 0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0 },
            { 0,1,0,1,0,1,0,1,1,0,1,0,1,0,1,0 },
            { 0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1 },
            { 0,1,0,1,1,0,1,0,1,0,1,0,0,1,0,1 },
            { 0,1,1,1,0,0,1,1,1,1,0,0,1,1,1,0 },
            { 0,0,0,1,0,0,1,1,1,1,0,0,1,0,0,0 },
            { 0,0,1,1,0,0,1,0,0,1,0,0,1,1,0,0 },
            { 0,0,1,1,1,0,1,1,1,1,0,1,1,1,0,0 },
            { 0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0 },
            { 0,0,1,1,1,1,0,0,1,1,0,0,0,0,1,1 },
            { 0,1,1,0,0,1,1,0,1,0,0,1,1,0,0,1 },
            { 0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0 },
            { 0,1,0,0,1,1,1,0,0,1,0,0,0,0,0,0 },
            { 0,0,1,0,0,1,1,1,0,0,1,0,0,0,0,0 },
            { 0,0,0,0,0,0,1,0,0,1,1,1,0,0,1,0 },
            { 0,0,0,0,0,1,0,0,1,1,1,0,0,1,0,0 },
            { 0,1,1,0,1,1,0,0,1,0,0,1,0,0,1,1 },
            { 0,0,1,1,0,1,1,0,1,1,0,0,1,0,0,1 },
            { 0,1,1,0,0,0,1,1,1,0,0,1,1,1,0,0 },
            { 0,0,1,1,1,0,0,1,1,1,0,0,0,1,1,0 },
            { 0,1,1,0,1,1,0,0,1,1,0,0,1,0,0,1 },
            { 0,1,1,0,0,0,1,1,0,0,1,1,1,0,0,1 },
            { 0,1,1,1,1,1,1,0,1,0,0,0,0,0,0,1 },
            { 0,0,0,1,1,0,0,0,1,1,1,0,0,1,1,1 },
            { 0,0,0,0,1,1,1,1,0,0,1,1,0,0,1,1 },
            { 0,0,1,1,0,0,1,1,1,1,1,1,0,0,0,0 },
            { 0,0,1,0,0,0,1,0,1,1,1,0,1,1,1,0 },
            { 0,1,0,0,0,1,0,0,0,1,1,1,0,1,1,1 },
        };
        const std::uint8_t kPartition3[64][16] = {
            { 0,0,1,1,0,0,1,1,0,2,2,1,2,2,2,2 },
            { 0,0,0,1,0,0,1,1,2,2,1,1,2,2,2,1 },
            { 0,0,0,0,2,0,0,1,2,2,1,1,2,2,1,1 },
            { 0,2,2,2,0,0,2,2,0,0,1,1,0,1,1,1 },
            { 0,0,0,0,0,0,0,0,1,1,2,2,1,1,2,2 },
            { 0,0,1,1,0,0,1,1,0,0,2,2,0,0,2,2 },
            { 0,0,2,2,0,0,2,2,1,1,1,1,1,1,1,1 },
            { 0,0,1,1,0,0,1,1,2,2,1,1,2,2,1,1 },
            { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2 },
            { 0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2 },
            { 0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2 },
            { 0,0,1,2,0,0,1,2,0,0,1,2,0,0,1,2 },
            { 0,1,1,2,0,1,1,2,0,1,1,2,0,1,1,2 },
            { 0,1,2,2,0,1,2,2,0,1,2,2,0,1,2,2 },
            { 0,0,1,1,0,1,1,2,1,1,2,2,1,2,2,2 },
            { 0,0,1,1,2,0,0,1,2,2,0,0,2,2,2,0 },
            { 0,0,0,1,0,0,1,1,0,1,1,2,1,1,2,2 },
            { 0,1,1,1,0,0,1,1,2,0,0,1,2,2,0,0 },
            { 0,0,0,0,1,1,2,2,1,1,2,2,1,1,2,2 },
            { 0,0,2,2,0,0,2,2,0,0,2,2,1,1,1,1 },
            { 0,1,1,1,0,1,1,1,0,2,2,2,0,2,2,2 },
            { 0,0,0,1,0,0,0,1,2,2,2,1,2,2,2,1 },
            { 0,0,0,0,0,0,1,1,0,1,2,2,0,1,2,2 },
            { 0,0,0,0,1,1,0,0,2,2,1,0,2,2,1,0 },
            { 0,1,2,2,0,1,2,2,0,0,1,1,0,0,0,0 },
            { 0,0,1,2,0,0,1,2,1,1,2,2,2,2,2,2 },
            { 0,1,1,0,1,2,2,1,1,2,2,1,0,1,1,0 },
            { 0,0,0,0,0,1,1,0,1,2,2,1,1,2,2,1 },
            { 0,0,2,2,1,1,0,2,1,1,0,2,0,0,2,2 },
            { 0,1,1,0,0,1,1,0,2,0,0,2,2,2,2,2 },
            { 0,0,1,1,0,1,2,2,0,1,2,2,0,0,1,1 },
            { 0,0,0,0,2,0,0,0,2,2,1,1,2,2,2,1 },
            { 0,0,0,0,0,0,0,2,1,1,2,2,1,2,2,2 },
            { 0,2,2,2,0,0,2,2,0,0,1,2,0,0,1,1 },
            { 0,0,1,1,0,0,1,2,0,0,2,2,0,2,2,2 },
            { 0,1,2,0,0,1,2,0,0,1,2,0,0,1,2,0 },
            { 0,0,0,0,1,1,1,1,2,2,2,2,0,0,0,0 },
            { 0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0 },
            { 0,1,2,0,2,0,1,2,1,2,0,1,0,1,2,0 },
            { 0,0,1,1,2,2,0,0,1,1,2,2,0,0,1,1 },
            { 0,0,1,1,1,1,2,2,2,2,0,0,0,0,1,1 },
            { 0,1,0,1,0,1,0,1,2,2,2,2,2,2,2,2 },
            { 0,0,0,0,0,0,0,0,2,1,2,1,2,1,2,1 },
            { 0,0,2,2,1,1,2,2,0,0,2,2,1,1,2,2 },
            { 0,0,2,2,0,0,1,1,0,0,2,2,0,0,1,1 },
            { 0,2,2,0,1,2,2,1,0,2,2,0,1,2,2,1 },
            { 0,1,0,1,2,2,2,2,2,2,2,2,0,1,0,1 },
            { 0,0,0,0,2,1,2,1,2,1,2,1,2,1,2,1 },
            { 0,1,0,1,0,1,0,1,0,1,0,1,2,2,2,2 },
            { 0,2,2,2,0,1,1,1,0,2,2,2,0,1,1,1 },
            { 0,0,0,2,1,1,1,2,0,0,0,2,1,1,1,2 },
            { 0,0,0,0,2,1,1,2,2,1,1,2,2,1,1,2 },
            { 0,2,2,2,0,1,1,1,0,1,1,1,0,2,2,2 },
            { 0,0,0,2,1,1,1,2,1,1,1,2,0,0,0,2 },
            { 0,1,1,0,0,1,1,0,0,1,1,0,2,2,2,2 },
            { 0,0,0,0,0,0,0,0,2,1,1,2,2,1,1,2 },
            { 0,1,1,0,0,1,1,0,2,2,2,2,2,2,2,2 },
            { 0,0,2,2,0,0,1,1,0,0,1,1,0,0,2,2 },
            { 0,0,2,2,1,1,2,2,1,1,2,2,0,0,2,2 },
            { 0,0,0,0,0,0,0,0,0,0,0,0,2,1,1,2 },
            { 0,0,0,2,0,0,0,1,0,0,0,2,0,0,0,1 },
            { 0,2,2,2,1,2,2,2,0,2,2,2,1,2,2,2 },
            { 0,1,0,1,2,2,2,2,2,2,2,2,2,2,2,2 },
            { 0,1,1,1,2,0,1,1,2,2,0,1,2,2,2,0 },
        };
        // Anchor (fix-up) texel index of subsets 1 and 2 per shape.
        const std::uint8_t kAnchor2_1[64] = { 15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,2,8,2,2,8,8,15,2,8,2,2,8,8,2,2,15,15,6,8,2,8,15,15,2,8,2,2,2,15,15,6,6,2,6,8,15,15,2,2,15,15,15,15,15,2,2,15 };
        const std::uint8_t kAnchor3_1[64] = { 3,3,15,15,8,3,15,15,8,8,6,6,6,5,3,3,3,3,8,15,3,3,6,10,5,8,8,6,8,5,15,15,8,15,3,5,6,10,8,15,15,3,15,5,15,15,15,15,3,15,5,5,5,8,5,10,5,10,8,13,15,12,3,3 };
        const std::uint8_t kAnchor3_2[64] = { 15,8,8,3,15,15,3,8,15,15,15,15,15,15,15,8,15,8,15,3,15,8,15,8,3,15,6,10,15,15,10,8,15,3,15,10,10,8,9,10,6,15,8,15,3,6,6,8,15,3,15,15,15,15,15,15,15,15,15,15,3,15,15,8 };

        // Per-mode layout: subsets, partition/p/rotation/index-selection bit
        // counts, index precisions, endpoint precision without/with p-bit.
        struct ModeInfo
        {
            std::uint8_t subsets, partBits, pBits, rotBits, idxModeBits;
            std::uint8_t idxPrec, idxPrec2;
            std::uint8_t prec[4], precP[4];
        };
        const ModeInfo kModes[8] = {
            { 3, 4, 6, 0, 0, 3, 0, { 4,4,4,0 }, { 5,5,5,0 } },
            { 2, 6, 2, 0, 0, 3, 0, { 6,6,6,0 }, { 7,7,7,0 } },
            { 3, 6, 0, 0, 0, 2, 0, { 5,5,5,0 }, { 5,5,5,0 } },
            { 2, 6, 4, 0, 0, 2, 0, { 7,7,7,0 }, { 8,8,8,0 } },
            { 1, 0, 0, 2, 1, 2, 3, { 5,5,5,6 }, { 5,5,5,6 } },
            { 1, 0, 0, 2, 0, 2, 2, { 7,7,7,8 }, { 7,7,7,8 } },
            { 1, 0, 2, 0, 0, 4, 0, { 7,7,7,7 }, { 8,8,8,8 } },
            { 2, 6, 4, 0, 0, 2, 0, { 5,5,5,5 }, { 6,6,6,6 } },
        };

        const int kWeights2[4]  = { 0, 21, 43, 64 };
        const int kWeights3[8]  = { 0, 9, 18, 27, 37, 46, 55, 64 };
        const int kWeights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };
        const int* WeightsFor(int prec) { return prec == 2 ? kWeights2 : prec == 3 ? kWeights3 : kWeights4; }

        struct BitReader
        {
            const std::uint8_t* d;
            unsigned pos;
            unsigned GetBit() { unsigned b = (d[pos >> 3] >> (pos & 7)) & 1u; ++pos; return b; }
            unsigned GetBits(int n) { unsigned v = 0; for (int i = 0; i < n; ++i) v |= GetBit() << i; return v; }
        };

        struct BitWriter
        {
            std::uint8_t* d;
            unsigned pos = 0;
            void Put(unsigned v, int n)
            {
                for (int i = 0; i < n; ++i, ++pos)
                    if ((v >> i) & 1u) d[pos >> 3] |= static_cast<std::uint8_t>(1u << (pos & 7));
            }
        };

        std::uint8_t Unquant(unsigned v, int prec)
        {
            v <<= (8 - prec);
            return static_cast<std::uint8_t>(v | (v >> prec));
        }

        int Interp(int a, int b, int w) { return (a * (64 - w) + b * w + 32) >> 6; }

        int SubsetOf(int subsets, int shape, int i)
        {
            if (subsets == 2) return kPartition2[shape][i];
            if (subsets == 3) return kPartition3[shape][i];
            return 0;
        }

        bool IsAnchor(int subsets, int shape, int i)
        {
            if (i == 0) return true;
            if (subsets == 2) return i == kAnchor2_1[shape];
            if (subsets == 3) return i == kAnchor3_1[shape] || i == kAnchor3_2[shape];
            return false;
        }
    }

    void DecodeBlock(const std::uint8_t* blk, std::uint8_t px[16][4])
    {
        if (blk[0] == 0) {                      // reserved mode: transparent black
            std::memset(px, 0, 64);
            return;
        }
        int mode = 0;
        while (!((blk[0] >> mode) & 1)) ++mode;
        const ModeInfo& mi = kModes[mode];
        BitReader br{ blk, static_cast<unsigned>(mode + 1) };

        const int nEp = mi.subsets * 2;
        const int shape = static_cast<int>(br.GetBits(mi.partBits));
        const int rot = static_cast<int>(br.GetBits(mi.rotBits));
        const int idxMode = static_cast<int>(br.GetBits(mi.idxModeBits));

        // Endpoints, channel-major (all R, all G, all B, all A).
        std::uint8_t e[6][4] = {};
        for (int ch = 0; ch < 4; ++ch)
            for (int i = 0; i < nEp; ++i)
                if (mi.prec[ch]) e[i][ch] = static_cast<std::uint8_t>(br.GetBits(mi.prec[ch]));

        // P-bits: appended as the endpoint LSB on every stored channel.
        if (mi.pBits) {
            std::uint8_t P[6];
            for (int i = 0; i < mi.pBits; ++i) P[i] = static_cast<std::uint8_t>(br.GetBit());
            for (int i = 0; i < nEp; ++i) {
                const int pi = i * mi.pBits / nEp;   // shared (mode 1) or unique
                for (int ch = 0; ch < 4; ++ch)
                    if (mi.prec[ch] != mi.precP[ch])
                        e[i][ch] = static_cast<std::uint8_t>((e[i][ch] << 1) | P[pi]);
            }
        }
        for (int i = 0; i < nEp; ++i) {
            for (int ch = 0; ch < 3; ++ch) e[i][ch] = Unquant(e[i][ch], mi.precP[ch]);
            e[i][3] = mi.precP[3] ? Unquant(e[i][3], mi.precP[3]) : 255;
        }

        // Indices: anchor texels carry one bit less (MSB implied 0).
        std::uint8_t w1[16], w2[16] = {};
        for (int i = 0; i < 16; ++i)
            w1[i] = static_cast<std::uint8_t>(br.GetBits(IsAnchor(mi.subsets, shape, i) ? mi.idxPrec - 1 : mi.idxPrec));
        if (mi.idxPrec2)
            for (int i = 0; i < 16; ++i)
                w2[i] = static_cast<std::uint8_t>(br.GetBits(i ? mi.idxPrec2 : mi.idxPrec2 - 1));

        for (int i = 0; i < 16; ++i) {
            const int s = SubsetOf(mi.subsets, shape, i);
            const std::uint8_t* a = e[s * 2];
            const std::uint8_t* b = e[s * 2 + 1];
            int wc, wa, pc, pa;
            if (!mi.idxPrec2)      { wc = wa = w1[i]; pc = pa = mi.idxPrec; }
            else if (idxMode == 0) { wc = w1[i]; pc = mi.idxPrec;  wa = w2[i]; pa = mi.idxPrec2; }
            else                   { wc = w2[i]; pc = mi.idxPrec2; wa = w1[i]; pa = mi.idxPrec; }
            const int* cw = WeightsFor(pc);
            const int* aw = WeightsFor(pa);
            std::uint8_t o[4];
            for (int ch = 0; ch < 3; ++ch) o[ch] = static_cast<std::uint8_t>(Interp(a[ch], b[ch], cw[wc]));
            o[3] = static_cast<std::uint8_t>(Interp(a[3], b[3], aw[wa]));
            switch (rot) {                     // post-interpolation channel swap
            case 1: std::swap(o[0], o[3]); break;
            case 2: std::swap(o[1], o[3]); break;
            case 3: std::swap(o[2], o[3]); break;
            }
            px[i][0] = o[0]; px[i][1] = o[1]; px[i][2] = o[2]; px[i][3] = o[3];
        }
    }

    void EncodeBlockMode6(const std::uint8_t px[16][4], std::uint8_t* out)
    {
        // Endpoints: most distant RGBA pair (baseline tier, as BC1/BC3).
        int bi = 0, bj = 0; long best = -1;
        for (int i = 0; i < 16; ++i)
            for (int j = i + 1; j < 16; ++j) {
                long d = 0;
                for (int c = 0; c < 4; ++c) { long e = px[i][c] - px[j][c]; d += e * e; }
                if (d > best) { best = d; bi = i; bj = j; }
            }

        // Quantize each endpoint to 7 bits + its own p-bit. Reconstruction is
        // (stored << 1) | p exactly, so per endpoint pick the p in {0,1} that
        // minimizes the summed squared channel error.
        std::uint8_t q[2][4]; std::uint8_t pb[2];
        const std::uint8_t* src[2] = { px[bi], px[bj] };
        for (int epi = 0; epi < 2; ++epi) {
            long bestErr = -1;
            for (int p = 0; p < 2; ++p) {
                std::uint8_t cand[4]; long err = 0;
                for (int c = 0; c < 4; ++c) {
                    int s = (src[epi][c] - p + 1) >> 1;
                    if (s < 0) s = 0; if (s > 127) s = 127;
                    cand[c] = static_cast<std::uint8_t>(s);
                    int r = (s << 1) | p;
                    err += static_cast<long>(src[epi][c] - r) * (src[epi][c] - r);
                }
                if (bestErr < 0 || err < bestErr) {
                    bestErr = err; pb[epi] = static_cast<std::uint8_t>(p);
                    std::memcpy(q[epi], cand, 4);
                }
            }
        }

        // Palette from the reconstructed endpoints; nearest index per pixel.
        std::uint8_t pal[16][4];
        std::uint8_t r0[4], r1[4];
        for (int c = 0; c < 4; ++c) {
            r0[c] = static_cast<std::uint8_t>((q[0][c] << 1) | pb[0]);
            r1[c] = static_cast<std::uint8_t>((q[1][c] << 1) | pb[1]);
        }
        for (int k = 0; k < 16; ++k)
            for (int c = 0; c < 4; ++c)
                pal[k][c] = static_cast<std::uint8_t>(Interp(r0[c], r1[c], kWeights4[k]));

        std::uint8_t idx[16];
        for (int i = 0; i < 16; ++i) {
            int bk = 0; long bd = 1L << 30;
            for (int k = 0; k < 16; ++k) {
                long d = 0;
                for (int c = 0; c < 4; ++c) { long e = px[i][c] - pal[k][c]; d += e * e; }
                if (d < bd) { bd = d; bk = k; }
            }
            idx[i] = static_cast<std::uint8_t>(bk);
        }

        // Anchor constraint: texel 0 stores 3 bits, so its MSB must be 0.
        if (idx[0] & 8) {
            std::swap(q[0][0], q[1][0]); std::swap(q[0][1], q[1][1]);
            std::swap(q[0][2], q[1][2]); std::swap(q[0][3], q[1][3]);
            std::swap(pb[0], pb[1]);
            for (int i = 0; i < 16; ++i) idx[i] = static_cast<std::uint8_t>(15 - idx[i]);
        }

        std::memset(out, 0, 16);
        BitWriter bw{ out };
        bw.Put(1u << 6, 7);                       // mode 6 marker
        for (int c = 0; c < 4; ++c) { bw.Put(q[0][c], 7); bw.Put(q[1][c], 7); }
        bw.Put(pb[0], 1); bw.Put(pb[1], 1);
        bw.Put(idx[0], 3);                        // anchor: implied-0 MSB
        for (int i = 1; i < 16; ++i) bw.Put(idx[i], 4);
    }
}
