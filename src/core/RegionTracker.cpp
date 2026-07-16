//=============================================================================
//  RegionTracker.cpp — Location, region, and worldspace data
//
//  Reads BGSLocation, TESRegion, TESWorldSpace for location-aware shading.
//  Enables biome-specific color grading, weather override detection, and
//  worldspace-aware water level for height fog.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "RegionTracker.h"
#include <RE/Skyrim.h>

namespace SB::RegionTracker
{
    RegionData Update()
    {
        RegionData data{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return data;

        auto* cell = player->GetParentCell();
        if (!cell) return data;

        // ── Location hierarchy ───────────────────────────────────────────
        auto* loc = player->GetCurrentLocation();
        if (loc) {
            data.Location.x = static_cast<float>(loc->GetFormID());
            // Walk up the parent chain
            if (auto* parent = loc->parentLoc) {
                data.Location.y = static_cast<float>(parent->GetFormID());
            }
        }

        // ── Worldspace ───────────────────────────────────────────────────
        auto* ws = player->GetWorldspace();
        if (ws) {
            data.Location.z = static_cast<float>(ws->GetFormID());

            // LOD water flag
            data.Worldspace.x = ws->flags.any(RE::TESWorldSpace::Flag::kNoLODWater)
                              ? 0.f : 1.f;
            data.Worldspace.y = ws->defaultWaterHeight;

            // Map center coordinates (ShortPoint: int16_t x, y)
            data.Worldspace.z = static_cast<float>(ws->fixedCenter.x);
            data.Worldspace.w = static_cast<float>(ws->fixedCenter.y);
        }

        // Cell FormID
        data.Location.w = static_cast<float>(cell->GetFormID());

        // ── Region data ──────────────────────────────────────────────────
        // Exterior cells have region lists; interiors typically don't.
        if (!cell->IsInteriorCell()) {
            auto* tes = RE::TES::GetSingleton();
            if (tes) {
                // Get the region list from the current grid cell
                if (auto* regionList = cell->GetRegionList(false)) {
                    for (auto* region : *regionList) {
                        if (!region) continue;

                        data.Region.x = static_cast<float>(region->GetFormID());

                        // Check for weather override
                        if (!region->dataList) break;
                        for (auto* dataEntry : region->dataList->regionDataList) {
                            if (!dataEntry) continue;
                            if (dataEntry->GetType() == RE::TESRegionData::Type::kWeather) {
                                data.Region.y = 1.f;
                            }
                        }
                        break;  // Use first region
                    }
                }
            }
        }

        return data;
    }
}
