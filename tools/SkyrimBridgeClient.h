#pragma once
//=============================================================================
//  SkyrimBridgeClient.h — external client for the SkyrimBridge command channel
//
//  Drop this into any Windows tool to drive SkyrimBridge's engine surface
//  (EngineReflect / RegionWalker / TextureCodec / ModelCodec) while the game
//  runs with [Native] CommandSurface = true. Header-only, no dependencies
//  beyond Windows.h. The layout mirrors src/SB_CommandLayout.h exactly.
//
//    SkyrimBridgeClient sb;
//    if (sb.Open()) {
//        std::string ini; int fields;
//        sb.ReflectDump(0x0010A232, ini, fields);       // read a record
//        sb.TextureConvert("in.png", "out.dds", 2);     // BC3
//    }
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <string>

class SkyrimBridgeClient
{
public:
#pragma pack(push, 1)
    struct Block
    {
        std::uint32_t magic, version, requestSeq, responseSeq;
        std::int32_t  argInt, status, resultInt;
        std::uint32_t reserved;
        char verb[32], arg0[512], arg1[512], resultText[4096];
    };
#pragma pack(pop)
    static_assert(sizeof(Block) == 5184, "must match SB_CommandBlock");

    bool Open()
    {
        m_map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"SkyrimBridge_Command");
        if (!m_map) return false;
        m_block = static_cast<Block*>(MapViewOfFile(m_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Block)));
        if (!m_block) { CloseHandle(m_map); m_map = nullptr; return false; }
        m_event = OpenEventW(SYNCHRONIZE, FALSE, L"SkyrimBridge_CommandReady");
        return m_block->magic == 0x53424331u;   // 'SBC1'
    }
    void Close()
    {
        if (m_block) { UnmapViewOfFile(m_block); m_block = nullptr; }
        if (m_map) { CloseHandle(m_map); m_map = nullptr; }
        if (m_event) { CloseHandle(m_event); m_event = nullptr; }
    }
    ~SkyrimBridgeClient() { Close(); }

    // Send one command and block until the plugin answers (or timeout ms).
    // Returns the response status (0 = OK). Fills resultInt / resultText.
    std::int32_t Call(const char* verb, const char* a0, const char* a1, std::int32_t argInt,
                      std::int32_t& resultInt, std::string& resultText, DWORD timeoutMs = 5000)
    {
        if (!m_block) return -100;
        std::memset(m_block->verb, 0, sizeof(m_block->verb));
        std::memset(m_block->arg0, 0, sizeof(m_block->arg0));
        std::memset(m_block->arg1, 0, sizeof(m_block->arg1));
        std::strncpy(m_block->verb, verb, sizeof(m_block->verb) - 1);
        if (a0) std::strncpy(m_block->arg0, a0, sizeof(m_block->arg0) - 1);
        if (a1) std::strncpy(m_block->arg1, a1, sizeof(m_block->arg1) - 1);
        m_block->argInt = argInt;
        const std::uint32_t seq = m_block->responseSeq + 1;
        MemoryBarrier();
        m_block->requestSeq = seq;                          // publish last

        const DWORD start = GetTickCount();
        while (m_block->responseSeq != seq) {
            if (GetTickCount() - start > timeoutMs) return -101;   // timeout
            if (m_event) WaitForSingleObject(m_event, 16); else Sleep(1);
        }
        MemoryBarrier();
        resultInt = m_block->resultInt;
        resultText.assign(m_block->resultText, strnlen(m_block->resultText, sizeof(m_block->resultText)));
        return m_block->status;
    }

    bool Ping() { int ri; std::string rt; return Call("ping", nullptr, nullptr, 0, ri, rt) == 0; }
    std::int32_t ReflectDump(std::uint32_t id, std::string& ini, int& fields)
    { char h[16]; std::snprintf(h, sizeof h, "0x%X", id); return Call("reflect.dump", h, nullptr, 0, fields, ini); }
    std::int32_t TextureConvert(const char* in, const char* out, int fmt)
    { int ri; std::string rt; return Call("texture.convert", in, out, fmt, ri, rt); }
    std::int32_t ModelConvert(const char* in, const char* out)
    { int ri; std::string rt; return Call("model.convert", in, out, 0, ri, rt); }

private:
    HANDLE m_map = nullptr, m_event = nullptr;
    Block* m_block = nullptr;
};
