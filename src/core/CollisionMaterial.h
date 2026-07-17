#pragma once
//=============================================================================
//  CollisionMaterial.h — Skyrim Havok collision materials
//
//  The material on a collision shape drives footstep SOUNDS, weapon/arrow
//  impact effects, and impact decals: walk on a shape tagged SNOW and you
//  get snow footsteps and snow impacts, not the wood default our generated
//  collision has shipped with. The hashes are the SkyrimHavokMaterial enum
//  values (verified against niftools nif.xml and cross-checked against the
//  materials real modlist collision shapes carry; tests/validate_collision_
//  material.py). A curated, footstep-relevant subset is exposed by name and
//  by ordered index (for the compact command-channel encoding).
//
//  Honest null: this sets the material the engine reacts to (sound + impact).
//  Visual snow FOOTPRINT depressions are a separate shader/footprint system,
//  not the collision material.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <cstdint>
#include <string_view>

namespace SB::CollisionMaterial
{
    std::uint32_t Default();                       // WOOD (what has shipped)

    // Case-insensitive friendly name -> hash. 0 if unknown (caller defaults).
    std::uint32_t ByName(std::string_view name);

    // Ordered curated list -> hash. Out of range -> Default(). Index 0 is the
    // default. Used by the command channel (material index in argInt bits).
    std::uint32_t ByIndex(int index);
    const char*   NameByIndex(int index);
    int           Count();
}
