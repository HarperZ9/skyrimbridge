#include <windows.h>

#include "SkyrimBridgeAPI.h"

SB::Api::BridgeInterface* AcquireBridge()
{
    HMODULE mod = GetModuleHandleW(L"SkyrimBridge.dll");
    if (!mod) {
        return nullptr;
    }

    FARPROC proc = GetProcAddress(mod, "SB_GetBridgeInterface");
    if (!proc) {
        return nullptr;
    }

    auto get = reinterpret_cast<SB::Api::BridgeInterface* (*)()>(proc);
    SB::Api::BridgeInterface* api = get();
    if (!api) {
        return nullptr;
    }

    if (api->version != SB::Api::kBridgeInterfaceVersion) {
        return nullptr;
    }

    if (api->allDataSize != sizeof(SB::AllData)) {
        return nullptr;
    }

    if (!api->GetFrameIndex || !api->GetFrameData ||
        !api->IsFrameValid || !api->CopyFrameData) {
        return nullptr;
    }

    return api;
}

bool TryReadSkyrimBridgeFrame(SB::AllData& frame, uint64_t* frameIndex)
{
    SB::Api::BridgeInterface* api = AcquireBridge();
    if (!api) {
        return false;
    }

    if (!api->IsFrameValid()) {
        return false;
    }

    uint64_t copiedFrameIndex = 0;
    if (!api->CopyFrameData(&frame,
            static_cast<uint32_t>(sizeof(frame)),
            &copiedFrameIndex)) {
        return false;
    }

    if (frameIndex) {
        *frameIndex = copiedFrameIndex;
    }

    return true;
}
