//=============================================================================
//  CollisionMaterial.cpp — SkyrimHavokMaterial table
//
//  Hashes are the SkyrimHavokMaterial enum (niftools nif.xml), each
//  cross-checked against the material real modlist collision shapes carry
//  (survey in tests/validate_collision_material.py). Ordered so index 0 is
//  the shipped default and the footstep-relevant materials come first.
//=============================================================================

#include "CollisionMaterial.h"

#include <array>
#include <cctype>
#include <string>

namespace SB::CollisionMaterial
{
    namespace
    {
        struct Entry { const char* name; std::uint32_t hash; };

        // Curated, footstep/effect-relevant subset. index 0 = default.
        constexpr std::array<Entry, 20> kTable = { {
            { "wood",         0x1DD9C611u },   // SKY_HAV_MAT_WOOD (default)
            { "stone",        0xDF02F237u },   // SKY_HAV_MAT_STONE
            { "snow",         0x17C77AAFu },   // SKY_HAV_MAT_SNOW
            { "ice",          0x340E5D1Cu },   // SKY_HAV_MAT_ICE
            { "dirt",         0xB9233EAAu },   // SKY_HAV_MAT_DIRT
            { "grass",        0x6E2F68EEu },   // SKY_HAV_MAT_GRASS
            { "gravel",       0x198BBA58u },   // SKY_HAV_MAT_GRAVEL
            { "sand",         0x813E4D0Du },   // SKY_HAV_MAT_SAND
            { "metal",        0x4CCACC3Bu },   // SKY_HAV_MAT_SOLID_METAL
            { "heavymetal",   0x84E226A3u },   // SKY_HAV_MAT_HEAVY_METAL
            { "glass",        0xDEE94842u },   // SKY_HAV_MAT_GLASS
            { "mud",          0x58987081u },   // SKY_HAV_MAT_MUD
            { "water",        0x3D11E3C7u },   // SKY_HAV_MAT_WATER
            { "bone",         0xB5C27C14u },   // SKY_HAV_MAT_MATERIAL_BONE
            { "organic",      0xB151ADDBu },   // SKY_HAV_MAT_ORGANIC
            { "stairs_stone", 0x359D733Du },   // SKY_HAV_MAT_STAIRS_STONE
            { "stairs_wood",  0x571FF595u },   // SKY_HAV_MAT_STAIRS_WOOD
            { "stairs_snow",  0x5D01492Bu },   // SKY_HAV_MAT_STAIRS_SNOW
            { "heavystone",   0x5DA0D740u },   // SKY_HAV_MAT_HEAVY_STONE
            { "heavywood",    0xB7087047u },   // SKY_HAV_MAT_HEAVY_WOOD
        } };

        std::string Lower(std::string_view s)
        {
            std::string o;
            o.reserve(s.size());
            for (char c : s)
                if (c != '_' && c != ' ' && c != '-')     // fold separators
                    o.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            return o;
        }
    }

    std::uint32_t Default() { return kTable[0].hash; }

    std::uint32_t ByName(std::string_view name)
    {
        const std::string q = Lower(name);
        for (const auto& e : kTable)
            if (Lower(e.name) == q) return e.hash;
        // aliases
        if (q == "solidmetal") return 0x4CCACC3Bu;
        if (q == "stonestairs") return 0x359D733Du;
        if (q == "woodstairs")  return 0x571FF595u;
        if (q == "snowstairs")  return 0x5D01492Bu;
        return 0;
    }

    std::uint32_t ByIndex(int index)
    {
        if (index < 0 || index >= static_cast<int>(kTable.size())) return Default();
        return kTable[static_cast<std::size_t>(index)].hash;
    }

    const char* NameByIndex(int index)
    {
        if (index < 0 || index >= static_cast<int>(kTable.size())) return "wood";
        return kTable[static_cast<std::size_t>(index)].name;
    }

    int Count() { return static_cast<int>(kTable.size()); }
}
