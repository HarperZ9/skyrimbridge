#pragma once
//=============================================================================
//  RegionWalker.h — structured walk of TESRegionData subrecords
//
//  Regions carry their interesting state in polymorphic TESRegionData
//  subrecords (weather lists, map name, sounds, landscape), which the flat
//  EngineReflect schema cannot express. This walker dumps every subrecord
//  CommonLib types (Weather / Map / Sound / Landscape; Objects, Grass and
//  Imposter have no CommonLib layout and are listed by type+priority only,
//  not decoded) and supports one BOUNDED write: editing the chance of an
//  EXISTING region-weather entry. No list add/remove, no pointer surgery.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <RE/Skyrim.h>

#include <cstdint>
#include <string>

namespace SB::RegionWalker
{
    // Full structured dump (INI-shaped, sectioned per subrecord). Empty
    // string if the form is not a TESRegion.
    std::string Dump(RE::FormID regionID);

    // Set the chance (0-100) of an existing weather entry. Returns entries
    // updated (0 if the region or entry does not exist).
    int SetWeatherChance(RE::FormID regionID, RE::FormID weatherID, std::uint32_t chance);

    // Apply "WeatherChance<i> = 0x<weather>, <chance>" keys from a
    // [RegionWeather:*] section of previously dumped text. Chance edits on
    // existing entries only. Returns entries updated.
    int Apply(RE::FormID regionID, const std::string& text);
}
