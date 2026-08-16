#include "SkyrimBridgeAPI.h"

#include <atomic>
#include <cstring>
#include <mutex>

#include "core/BridgeData.h"

namespace SB::Api
{
    namespace
    {
        std::atomic<uint64_t> g_frameIndex{0};
        std::atomic<bool>     g_frameValid{false};

        std::mutex            g_snapshotMutex;
        SB::AllData           g_snapshots[2]{};
        std::atomic<uint32_t> g_publishedSlot{0};
        uint64_t              g_publishedFrameIndex = 0;
        bool                  g_teardownLatched = false;

        uint64_t GetFrameIndexImpl()
        {
            return g_frameIndex.load(std::memory_order_acquire);
        }

        const SB::AllData* GetFrameDataImpl()
        {
            thread_local SB::AllData callerSnapshot{};

            std::lock_guard lock(g_snapshotMutex);
            if (!g_frameValid.load(std::memory_order_acquire)) {
                return nullptr;
            }

            const uint32_t slot = g_publishedSlot.load(std::memory_order_acquire);
            callerSnapshot = g_snapshots[slot];
            return &callerSnapshot;
        }

        bool IsFrameValidImpl()
        {
            return g_frameValid.load(std::memory_order_acquire);
        }

        bool CopyFrameDataImpl(void* destination, uint32_t destinationSize, uint64_t* frameIndex)
        {
            if (!destination || destinationSize < sizeof(SB::AllData)) {
                return false;
            }

            std::lock_guard lock(g_snapshotMutex);
            if (!g_frameValid.load(std::memory_order_acquire)) {
                return false;
            }

            const uint32_t slot = g_publishedSlot.load(std::memory_order_acquire);
            std::memcpy(destination, &g_snapshots[slot], sizeof(SB::AllData));
            if (frameIndex) {
                *frameIndex = g_publishedFrameIndex;
            }
            return true;
        }

        BridgeInterface g_interface{
            kBridgeInterfaceVersion,
            static_cast<uint32_t>(sizeof(SB::AllData)),
            &GetFrameIndexImpl,
            &GetFrameDataImpl,
            &IsFrameValidImpl,
            &CopyFrameDataImpl,
        };
    }

    void MarkFramePublished(const SB::AllData& publishedData)
    {
        std::lock_guard lock(g_snapshotMutex);
        if (g_teardownLatched) {
            return;
        }

        const uint32_t currentSlot = g_publishedSlot.load(std::memory_order_relaxed);
        const uint32_t nextSlot = 1U - currentSlot;
        g_snapshots[nextSlot] = publishedData;
        g_publishedSlot.store(nextSlot, std::memory_order_release);

        ++g_publishedFrameIndex;
        g_frameIndex.store(g_publishedFrameIndex, std::memory_order_release);
        g_frameValid.store(true, std::memory_order_release);
    }

    void MarkTeardown()
    {
        std::lock_guard lock(g_snapshotMutex);
        g_teardownLatched = true;
        g_frameValid.store(false, std::memory_order_release);
    }
}

extern "C" SB_BRIDGE_API SB::Api::BridgeInterface* SB_GetBridgeInterface()
{
    return &SB::Api::g_interface;
}
