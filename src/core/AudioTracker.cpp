//=============================================================================
//  AudioTracker.cpp — Music type, ambient state, and audio-reactive data
//
//  Reads BGSMusicType and audio system state for music-reactive shader
//  effects (e.g., combat music → vignette, dungeon music → desaturation).
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "AudioTracker.h"
#include <RE/Skyrim.h>

namespace SB::AudioTracker
{
    AudioData Update()
    {
        AudioData data{};

        // ── Music type ───────────────────────────────────────────────────
        auto* audioMgr = RE::BSAudioManager::GetSingleton();
        if (!audioMgr) return data;

        // Read current music type from the music manager
        // BGSMusicType categories: default, combat, dungeon, explore, etc.
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return data;

        // Check combat music state
        bool inCombat = player->IsInCombat();
        data.Music.z = inCombat ? 1.f : 0.f;

        // Dungeon detection: check if current location has dungeon keyword
        auto* loc = player->GetCurrentLocation();
        if (loc) {
            // Iterate location keywords to detect dungeon types
            auto* kywd = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeDungeon");
            if (kywd && loc->HasKeyword(kywd)) {
                data.Music.w = 1.f;
            }
        }

        // Interior/exterior ambient classification
        auto* cell = player->GetParentCell();
        if (cell) {
            data.Ambient.x = cell->IsInteriorCell() ? 0.f : 1.f;
        }

        // Weather sound active (rain, wind, thunder)
        auto* sky = RE::Sky::GetSingleton();
        if (sky && sky->currentWeather) {
            auto flags = sky->currentWeather->data.flags;
            bool hasWeatherSound = flags.any(
                RE::TESWeather::WeatherDataFlag::kRainy,
                RE::TESWeather::WeatherDataFlag::kSnow
            );
            data.Ambient.z = hasWeatherSound ? 1.f : 0.f;
        }

        return data;
    }
}
