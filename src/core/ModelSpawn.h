#pragma once
//=============================================================================
//  ModelSpawn.h — runtime model spawn core
//
//  The one spawn implementation behind both surfaces (the SpawnModel Papyrus
//  native and the command channel's model.spawn verb): materialize a mesh
//  under meshes\SkyrimBridge\spawn\ (foreign formats convert through
//  ModelCodec, a .nif copies), create a dynamic Static form pointing at it,
//  and place one reference at the player, so the ENGINE's own model loader
//  constructs the NiObject graph (no synthetic engine objects).
//
//  MUTATES the save: the dynamic form and the placed reference persist.
//  Callers are per-op opt-in (console call / external request).
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include <cstdint>
#include <filesystem>
#include <string>

namespace SB::ModelSpawn
{
    // Returns the placed reference's FormID, 0 on failure with the failing
    // stage named in err (also logged). treeMode converts foreign meshes as
    // wind-animated trees; collision adds a convex hull (both apply to the
    // conversion only; a .nif copies as-is).
    std::uint32_t SpawnAtPlayer(const std::filesystem::path& in, std::string& err,
                                bool treeMode = false, bool collision = false);
}
