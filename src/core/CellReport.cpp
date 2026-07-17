//=============================================================================
//  CellReport.cpp — cell performance census
//=============================================================================

#include "CellReport.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

namespace SB::CellReport
{
    namespace
    {
        // Winning (last) source file of a form's override chain.
        const char* WinningPlugin(const RE::TESForm* form)
        {
            if (!form) return "?";
            const auto* arr = form->sourceFiles.array;
            if (!arr || arr->empty()) return "(runtime)";
            const auto* file = (*arr)[arr->size() - 1];
            return file ? file->GetFilename().data() : "?";
        }
    }

    std::string Run()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* cell = player ? player->GetParentCell() : nullptr;
        if (!cell) return {};

        const RE::NiPoint3 ppos = player->GetPosition();

        struct ShadowLight
        {
            RE::FormID ref = 0, base = 0;
            std::string refPlugin, basePlugin;
            float dist = 0;
            bool disabledFlag = false;
        };
        std::map<std::string, int> byType;
        std::map<std::string, int> byPlugin;
        std::vector<ShadowLight> shadows;
        int total = 0, disabledFlagged = 0, lights = 0;

        cell->ForEachReference([&](RE::TESObjectREFR& ref) {
            ++total;
            const bool disFlag = (ref.formFlags & (1u << 11)) != 0;   // initially-disabled record flag
            if (disFlag) ++disabledFlagged;
            auto* base = ref.GetBaseObject();
            if (base) {
                byType[std::string(RE::FormTypeToString(base->GetFormType()))]++;
                byPlugin[WinningPlugin(&ref)]++;
                if (auto* ligh = base->As<RE::TESObjectLIGH>()) {
                    ++lights;
                    // SpotShadow | HemiShadow | OmniShadow
                    if (ligh->data.flags.underlying() & 0x1C00u) {
                        const RE::NiPoint3 p = ref.GetPosition();
                        const float dx = p.x - ppos.x, dy = p.y - ppos.y, dz = p.z - ppos.z;
                        shadows.push_back({ ref.GetFormID(), ligh->GetFormID(),
                                            WinningPlugin(&ref), WinningPlugin(ligh),
                                            std::sqrt(dx * dx + dy * dy + dz * dz), disFlag });
                    }
                }
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });

        std::sort(shadows.begin(), shadows.end(),
                  [](const ShadowLight& a, const ShadowLight& b) { return a.dist < b.dist; });

        // ── report text ───────────────────────────────────────────────────
        char line[256];
        std::string out;
        const char* cellName = cell->GetFormEditorID();
        std::snprintf(line, sizeof line, "[Cell] 0x%08X %s (%s)\n", cell->GetFormID(),
                      cellName && *cellName ? cellName : "(no editor id)",
                      cell->IsInteriorCell() ? "interior" : "exterior");
        out += line;
        std::snprintf(line, sizeof line,
                      "refs=%d disabledFlag=%d lights=%d shadowLights=%zu\n",
                      total, disabledFlagged, lights, shadows.size());
        out += line;

        out += "\n[ShadowLights] (nearest first; the engine renders only a few at once,\n"
               " every one is expensive; this is the classic FPS hunt)\n";
        for (auto& s : shadows) {
            std::snprintf(line, sizeof line,
                          "  ref 0x%08X  base 0x%08X  dist %.0f  %s%s\n",
                          s.ref, s.base, s.dist, s.refPlugin.c_str(),
                          s.disabledFlag ? "  [disabled-flag]" : "");
            out += line;
        }
        if (shadows.empty()) out += "  (none)\n";

        std::vector<std::pair<std::string, int>> plug(byPlugin.begin(), byPlugin.end());
        std::sort(plug.begin(), plug.end(),
                  [](auto& a, auto& b) { return a.second > b.second; });
        out += "\n[RefsByWinningPlugin]\n";
        int shown = 0;
        for (auto& [name, n] : plug) {
            if (++shown > 12) { out += "  ...\n"; break; }
            std::snprintf(line, sizeof line, "  %5d  %s\n", n, name.c_str());
            out += line;
        }

        std::vector<std::pair<std::string, int>> types(byType.begin(), byType.end());
        std::sort(types.begin(), types.end(),
                  [](auto& a, auto& b) { return a.second > b.second; });
        out += "\n[RefsByType]\n";
        for (auto& [name, n] : types) {
            std::snprintf(line, sizeof line, "  %5d  %s\n", n, name.c_str());
            out += line;
        }

        std::error_code ec;
        std::filesystem::create_directories("Data/SKSE/Plugins/SkyrimBridge/dumps", ec);
        std::ofstream f("Data/SKSE/Plugins/SkyrimBridge/dumps/cellreport.txt",
                        std::ios::trunc);
        if (f) f << out;
        SKSE::log::info("CellReport: cell 0x{:08X}: {} refs, {} lights, {} shadow-casting",
                        cell->GetFormID(), total, lights, shadows.size());
        return out;
    }
}
