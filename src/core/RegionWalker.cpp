//=============================================================================
//  RegionWalker.cpp — structured walk of TESRegionData subrecords
//=============================================================================

#include "RegionWalker.h"
#include "SBConfig.h"

#include <SKSE/SKSE.h>

#include <cstdio>
#include <sstream>

namespace SB::RegionWalker
{
    namespace
    {
        RE::TESRegion* ResolveRegion(RE::FormID id)
        {
            auto* form = RE::TESForm::LookupByID(id);
            return form ? form->As<RE::TESRegion>() : nullptr;
        }

        const char* TypeName(RE::TESRegionData::Type t)
        {
            switch (t) {
            case RE::TESRegionData::Type::kObjects:  return "Objects";
            case RE::TESRegionData::Type::kWeather:  return "Weather";
            case RE::TESRegionData::Type::kMap:      return "Map";
            case RE::TESRegionData::Type::kLand:     return "Land";
            case RE::TESRegionData::Type::kGrass:    return "Grass";
            case RE::TESRegionData::Type::kSound:    return "Sound";
            case RE::TESRegionData::Type::kImposter: return "Imposter";
            }
            return "Unknown";
        }

        void DumpWeather(std::ostringstream& o, RE::FormID id, RE::TESRegionDataWeather* w)
        {
            o << "[RegionWeather:0x" << std::hex << std::uppercase << id << std::dec << "]\n";
            o << "; WeatherChance<i> = <weather formID>, <chance 0-100>, <global formID or 0>\n";
            int i = 0;
            for (auto* wt : w->weatherTypes) {
                if (!wt) continue;
                char line[96];
                std::snprintf(line, sizeof line, "WeatherChance%d = 0x%X, %u, 0x%X\n", i++,
                              wt->weather ? wt->weather->GetFormID() : 0u,
                              wt->chance,
                              wt->global ? wt->global->GetFormID() : 0u);
                o << line;
            }
        }

        void DumpSound(std::ostringstream& o, RE::FormID id, RE::TESRegionDataSound* s)
        {
            o << "[RegionSound:0x" << std::hex << std::uppercase << id << std::dec << "]\n";
            char line[96];
            std::snprintf(line, sizeof line, "Music = 0x%X\n",
                          s->music ? s->music->GetFormID() : 0u);
            o << line;
            int i = 0;
            for (auto* snd : s->sounds) {
                if (!snd) continue;
                std::snprintf(line, sizeof line, "Sound%d = 0x%X, %u, %.4g\n", i++,
                              snd->sound ? snd->sound->GetFormID() : 0u,
                              snd->flags.underlying(), snd->chance);
                o << line;
            }
        }
    }

    std::string Dump(RE::FormID regionID)
    {
        auto* region = ResolveRegion(regionID);
        if (!region) return {};

        std::ostringstream o;
        o << "[Region:0x" << std::hex << std::uppercase << regionID << std::dec << "]\n";
        {
            char line[96];
            std::snprintf(line, sizeof line, "WorldSpace = 0x%X\nCurrentWeather = 0x%X\n",
                          region->worldSpace ? region->worldSpace->GetFormID() : 0u,
                          region->currentWeather ? region->currentWeather->GetFormID() : 0u);
            o << line;
        }
        if (!region->dataList) return o.str();

        for (auto* rd : region->dataList->regionDataList) {
            if (!rd) continue;
            auto type = rd->GetType();
            o << "; entry: type=" << TypeName(type)
              << " priority=" << static_cast<int>(rd->dataHeader.priority)
              << " override=" << (rd->dataHeader.flags.any(RE::TESRegionData::DataHeader::Flag::kOverride) ? 1 : 0)
              << "\n";
            switch (type) {
            case RE::TESRegionData::Type::kWeather:
                DumpWeather(o, regionID, static_cast<RE::TESRegionDataWeather*>(rd));
                break;
            case RE::TESRegionData::Type::kSound:
                DumpSound(o, regionID, static_cast<RE::TESRegionDataSound*>(rd));
                break;
            case RE::TESRegionData::Type::kMap:
                o << "[RegionMap:0x" << std::hex << std::uppercase << regionID << std::dec << "]\n"
                  << "MapName = " << static_cast<RE::TESRegionDataMap*>(rd)->mapName.c_str() << "\n";
                break;
            case RE::TESRegionData::Type::kLand: {
                auto* icon = static_cast<RE::TESRegionDataLandscape*>(rd)->icon;
                o << "[RegionLand:0x" << std::hex << std::uppercase << regionID << std::dec << "]\n"
                  << "Icon = " << (icon ? icon->textureName.c_str() : "") << "\n";
                break; }
            default:
                // Objects / Grass / Imposter: no CommonLib layout; listed only.
                break;
            }
        }
        return o.str();
    }

    int SetWeatherChance(RE::FormID regionID, RE::FormID weatherID, std::uint32_t chance)
    {
        auto* region = ResolveRegion(regionID);
        if (!region || !region->dataList) return 0;
        if (chance > 100) chance = 100;

        int updated = 0;
        for (auto* rd : region->dataList->regionDataList) {
            if (!rd || rd->GetType() != RE::TESRegionData::Type::kWeather) continue;
            for (auto* wt : static_cast<RE::TESRegionDataWeather*>(rd)->weatherTypes) {
                if (wt && wt->weather && wt->weather->GetFormID() == weatherID) {
                    wt->chance = chance;
                    ++updated;
                }
            }
        }
        return updated;
    }

    int Apply(RE::FormID regionID, const std::string& text)
    {
        int updated = 0;
        auto doc = Cfg::Parse(text);
        for (auto& sec : doc.sections) {
            if (sec.Base() != "RegionWeather") continue;
            for (auto& [key, value] : sec.entries) {
                if (key.rfind("WeatherChance", 0) != 0) continue;
                // value: "<weather formID>, <chance>[, <global>]" (global is read-only)
                auto c1 = value.find(',');
                if (c1 == std::string::npos) continue;
                auto c2 = value.find(',', c1 + 1);
                std::uint32_t weather = Cfg::AsFormID(Cfg::Trim(value.substr(0, c1)));
                int chance = Cfg::AsInt(Cfg::Trim(value.substr(c1 + 1,
                                 (c2 == std::string::npos ? value.size() : c2) - c1 - 1)), -1);
                if (weather && chance >= 0)
                    updated += SetWeatherChance(regionID, weather, static_cast<std::uint32_t>(chance));
            }
        }
        return updated;
    }
}
