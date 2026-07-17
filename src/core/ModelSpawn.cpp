//=============================================================================
//  ModelSpawn.cpp — runtime model spawn core
//=============================================================================

#include "ModelSpawn.h"
#include "ModelCodec.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace SB::ModelSpawn
{
    std::uint32_t SpawnAtPlayer(const std::filesystem::path& in, std::string& err,
                                bool treeMode, bool collision, int collisionPieces)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded()) {
            err = "needs a loaded player (in-game only)";
            SKSE::log::warn("ModelSpawn: {}", err);
            return 0;
        }

        std::error_code ec;
        std::filesystem::create_directories("Data/meshes/SkyrimBridge/spawn", ec);
        const std::string stem = in.stem().string();
        const std::filesystem::path out =
            std::filesystem::path("Data/meshes/SkyrimBridge/spawn") / (stem + ".nif");

        bool ok;
        if (_stricmp(in.extension().string().c_str(), ".nif") == 0) {
            std::filesystem::copy_file(in, out, std::filesystem::copy_options::overwrite_existing, ec);
            ok = !ec;
        } else {
            ok = ModelCodec::ConvertToNIF(in, out, treeMode, collision, collisionPieces);
        }
        if (!ok) {
            err = "could not materialize " + in.string() + " -> " + out.string();
            SKSE::log::warn("ModelSpawn: {}", err);
            return 0;
        }

        RE::TESObjectSTAT* stat = nullptr;
        if (auto* factory = RE::IFormFactory::GetFormFactoryByType(RE::FormType::Static))
            if (auto* form = factory->Create())
                stat = form->As<RE::TESObjectSTAT>();
        if (!stat) {
            err = "Static form factory unavailable";
            SKSE::log::warn("ModelSpawn: {}", err);
            return 0;
        }
        const std::string rel = "SkyrimBridge\\spawn\\" + stem + ".nif";
        stat->SetModel(rel.c_str());

        auto ref = player->PlaceObjectAtMe(stat, false);
        if (!ref) {
            err = "PlaceObjectAtMe failed for " + rel;
            SKSE::log::warn("ModelSpawn: {}", err);
            return 0;
        }
        SKSE::log::info("ModelSpawn: {} -> meshes\\{} : placed 0x{:08X} (dynamic STAT 0x{:08X})",
                        in.string(), rel, ref->GetFormID(), stat->GetFormID());
        return ref->GetFormID();
    }
}
