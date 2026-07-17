//=============================================================================
//  ScriptReport.cpp — live Papyrus VM monitor
//
//  Reads CommonLib's fully-typed BSScript::Internal::VirtualMachine under
//  the VM's own locks (runningStacksLock, attachedScriptsLock), one section
//  at a time with short holds. The overflow/cleanup array sizes are plain
//  u32 reads without their locks: a monitor tolerates a torn count; the
//  alternative (holding more VM locks longer) does not pull its weight.
//=============================================================================

#include "ScriptReport.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

namespace SB::ScriptReport
{
    namespace
    {
        std::atomic<bool> s_requested{ false };

        void TopCounts(std::string& out, const std::map<std::string, int>& m, int limit)
        {
            std::vector<std::pair<std::string, int>> v(m.begin(), m.end());
            std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
            char line[192];
            int shown = 0;
            for (auto& [name, n] : v) {
                if (++shown > limit) { out += "  ...\n"; break; }
                std::snprintf(line, sizeof line, "  %6d  %s\n", n, name.c_str());
                out += line;
            }
            if (v.empty()) out += "  (none)\n";
        }
    }

    std::string Run()
    {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return {};

        // Running stacks: count + which script class each stack's top frame
        // is executing in right now.
        int running = 0, latent = 0;
        std::map<std::string, int> runningBy;
        {
            RE::BSSpinLockGuard l(vm->runningStacksLock);
            for (auto& entry : vm->allRunningStacks) {
                ++running;
                const auto& stack = entry.second;
                if (stack && stack->top && stack->top->owningObjectType)
                    runningBy[stack->top->owningObjectType->GetName()]++;
                else
                    runningBy["(no frame)"]++;
            }
            latent = static_cast<int>(vm->waitingLatentReturns.size());
        }

        // Attached scripts: the save-bloat census.
        std::size_t handles = 0, instances = 0;
        std::map<std::string, int> byClass;
        {
            RE::BSSpinLockGuard l(vm->attachedScriptsLock);
            for (auto& entry : vm->attachedScripts) {
                ++handles;
                for (auto& script : entry.second) {
                    ++instances;
                    auto* obj = script.get();
                    auto* ti = obj ? obj->GetTypeInfo() : nullptr;
                    byClass[ti && ti->GetName() ? ti->GetName() : "(untyped)"]++;
                }
            }
        }

        const std::uint32_t funcMsgs = vm->uiWaitingFunctionMessages;
        const bool overstressed = vm->overstressed;
        const std::uint32_t frozen = vm->frozenStacksCount;
        const auto freeze = vm->freezeState.get();
        std::size_t arrays = 0;
        {
            RE::BSSpinLockGuard l(vm->arraysLock);
            arrays = vm->arrays.size();
        }
        const std::size_t cleanup = vm->objectsAwaitingCleanup.size();   // unlocked u32 read
        const std::size_t reset = vm->objectsAwaitingReset.size();       // unlocked u32 read

        char line[192];
        std::string out;
        out += "[PapyrusVM]\n";
        std::snprintf(line, sizeof line, "overstressed=%s freezeState=%s\n",
                      overstressed ? "YES (the VM itself says it is behind)" : "no",
                      freeze == RE::BSScript::Internal::VirtualMachine::FreezeState::kNotFrozen
                          ? "notFrozen" : "frozen/freezing");
        out += line;
        std::snprintf(line, sizeof line,
                      "waitingFunctionMessages=%u (queue depth; sustained growth = script lag)\n",
                      funcMsgs);
        out += line;
        std::snprintf(line, sizeof line,
                      "runningStacks=%d waitingLatentReturns=%d frozenStacks=%u\n",
                      running, latent, frozen);
        out += line;
        std::snprintf(line, sizeof line,
                      "attachedHandles=%zu scriptInstances=%zu arrays=%zu "
                      "awaitingCleanup=%zu awaitingReset=%zu\n",
                      handles, instances, arrays, cleanup, reset);
        out += line;

        out += "\n[RunningStacksByScript] (executing right now)\n";
        TopCounts(out, runningBy, 15);
        out += "\n[TopScriptClassesByInstances] (the save-bloat census)\n";
        TopCounts(out, byClass, 20);

        std::error_code ec;
        std::filesystem::create_directories("Data/SKSE/Plugins/SkyrimBridge/dumps", ec);
        std::ofstream f("Data/SKSE/Plugins/SkyrimBridge/dumps/scriptreport.txt", std::ios::trunc);
        if (f) f << out;
        SKSE::log::info("ScriptReport: {} running stacks, {} queued messages, "
                        "{} instances across {} handles{}",
                        running, funcMsgs, instances, handles,
                        overstressed ? " [OVERSTRESSED]" : "");
        return out;
    }

    void Request() { s_requested.store(true, std::memory_order_release); }

    void Tick()
    {
        if (s_requested.exchange(false, std::memory_order_acq_rel))
            Run();
    }
}
