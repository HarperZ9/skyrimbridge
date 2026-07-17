#pragma once
//=============================================================================
//  SB_CommandLayout.h — request/response command mailbox layout
//
//  The second shared-memory region (the first, SB_SharedLayout, is a one-way
//  game-state writer). This one is a bidirectional command channel so an
//  EXTERNAL tool can drive SkyrimBridge's engine surface — EngineReflect,
//  RegionWalker, TextureCodec, ModelCodec — without the in-game console.
//
//  Protocol (single-slot, sequence-gated; one in-flight request):
//    client:  fill verb + arg0 + arg1 + argInt, then publish requestSeq (last).
//    plugin:  when requestSeq != responseSeq, read the request, dispatch on the
//             game thread, write status + resultInt + resultText, then publish
//             responseSeq = requestSeq (last).
//    client:  spin/wait until responseSeq == its requestSeq, then read results.
//  The seq fields are the release/acquire fences; every other field is stable
//  before the publishing store on each side.
//
//  NO dependency on SKSE / CommonLibSSE / Windows.h — include from both the
//  plugin and any external client.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <cstdint>

namespace SB
{
    #pragma pack(push, 1)

    struct SB_CommandBlock
    {
        std::uint32_t magic;        // 'SBC1'
        std::uint32_t version;      // protocol version
        std::uint32_t requestSeq;   // client publishes last
        std::uint32_t responseSeq;  // plugin publishes last (== requestSeq when done)
        std::int32_t  argInt;       // request scalar (chance, format enum, dry-run...)
        std::int32_t  status;       // response: 0 ok, negative = error code
        std::int32_t  resultInt;    // response scalar (fields written, ok flag...)
        std::uint32_t reserved;
        char          verb[32];     // request verb, NUL-terminated
        char          arg0[512];    // request string 0 (form id hex / input path)
        char          arg1[512];    // request string 1 (weather id / output path)
        char          resultText[4096];  // response text (dump / listing), NUL-terminated
    };
    static_assert(sizeof(SB_CommandBlock) == 5184, "command block layout is an ABI contract");

    static constexpr std::uint32_t kCommandMagic   = 0x53424331;  // 'SBC1'
    static constexpr std::uint32_t kCommandVersion = 1;
    static constexpr const wchar_t* kCommandMemName = L"SkyrimBridge_Command";
    static constexpr const wchar_t* kCommandEventName = L"SkyrimBridge_CommandReady";

    // Response status codes.
    enum SB_CmdStatus : std::int32_t
    {
        kCmdOK          =  0,
        kCmdUnknownVerb = -1,
        kCmdBadArg      = -2,
        kCmdNotFound    = -3,
        kCmdFailed      = -4,
    };

    // Verbs (case-sensitive). Kept as a documented contract for clients.
    //   ping                                       -> resultInt = 1
    //   reflect.list   arg0 = "0" | "0x<formid>"   -> resultText = listing
    //   reflect.dump   arg0 = "0x<formid>"         -> resultText = INI, resultInt = fields
    //   reflect.apply  arg0 = "0x<formid>", resultText(in) = INI  -> resultInt = fields written
    //   reflect.verify arg0 = "0x<formid>", argInt = 1 -> strict  -> resultInt = fields (0 = fail)
    //   region.dump    arg0 = "0x<region>"         -> resultText
    //   region.weather arg0 = "0x<region>", arg1 = "0x<weather>", argInt = chance -> resultInt
    //   texture.convert arg0 = in, arg1 = out, argInt = 0|1|2 (RGBA8|BC1|BC3)     -> resultInt = ok
    //   texture.scan   argInt = 1 (dry) | 0 (live) -> resultInt = converted
    //   model.convert  arg0 = in, arg1 = out       -> resultInt = ok

    #pragma pack(pop)
}
