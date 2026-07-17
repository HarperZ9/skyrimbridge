#pragma once
//=============================================================================
//  BridgeCommand.h — external command channel (request/response mailbox)
//
//  Plugin-side of SB_CommandLayout: creates the "SkyrimBridge_Command" shared
//  region, and each frame dispatches one pending request to the engine surface
//  (EngineReflect / RegionWalker / TextureCodec / ModelCodec), writing the
//  response back. Lets an external editor drive SkyrimBridge, not only the
//  in-game console. Config-gated ([Native] CommandSurface, default OFF).
//
//  Large payloads (e.g. a full Weather dump) travel through the dumps
//  directory, exactly as the console natives do; the mailbox carries the
//  trigger + a bounded text result. Dispatch runs in the frame-update context
//  (same as the game-state writer) and is SEH-isolated by the caller.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

namespace SB
{
    class BridgeCommand
    {
    public:
        static BridgeCommand& Get();

        bool Initialize();
        void Shutdown();
        bool IsActive() const;

        // Dispatch at most one pending request (call once per frame).
        void Poll();

    private:
        BridgeCommand() = default;
        ~BridgeCommand();

        void* m_hMap = nullptr;      // HANDLE
        void* m_hEvent = nullptr;    // HANDLE
        void* m_block = nullptr;     // SB_CommandBlock*
        unsigned m_lastHandled = 0;
        unsigned m_dispatched = 0;
    };
}
