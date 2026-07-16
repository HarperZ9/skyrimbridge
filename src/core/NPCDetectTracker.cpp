//=============================================================================
//  NPCDetectTracker.cpp — Nearby actor detection for threat-aware rendering
//
//  Scans actors within a 30m (2048 unit) radius for:
//  - Nearest actor (hostile or not) with position, health, level
//  - Hostile/friendly counts
//  - Overall threat rating for combat-reactive post-processing
//  - Stealth meter state for sneak-aware effects
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "NPCDetectTracker.h"
#include <RE/Skyrim.h>
#include <cmath>
#include <limits>

namespace SB::NPCDetectTracker
{
    static constexpr float kScanRadius = 2048.f;  // ~30 meters in Skyrim units

    NPCDetectData Update()
    {
        NPCDetectData data{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return data;

        auto playerPos = player->GetPosition();

        float nearestDist        = std::numeric_limits<float>::max();
        float nearestHostileDist = std::numeric_limits<float>::max();
        float nearestFriendDist  = std::numeric_limits<float>::max();

        RE::NiPoint3 nearestPos{};
        float nearestHealth = 0.f;
        float nearestLevel  = 0.f;
        bool  nearestHostile = false;
        bool  nearestAlerted = false;

        uint32_t hostileCount  = 0;
        uint32_t friendlyCount = 0;
        float    totalThreat   = 0.f;
        float    maxDetection  = 0.f;

        // Process high actors (loaded, 3D-visible actors near the player)
        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return data;

        for (auto& handle : processLists->highActorHandles) {
            auto actorPtr = handle.get();
            if (!actorPtr) continue;

            auto* actor = actorPtr.get();
            if (!actor || actor->IsDead() || actor == player) continue;

            auto actorPos = actor->GetPosition();
            float dx = actorPos.x - playerPos.x;
            float dy = actorPos.y - playerPos.y;
            float dz = actorPos.z - playerPos.z;
            float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

            if (dist > kScanRadius) continue;

            bool isHostile = actor->IsHostileToActor(player);
            float healthPct = 0.f;
            if (auto* avOwner = actor->AsActorValueOwner()) {
                float cur = avOwner->GetActorValue(RE::ActorValue::kHealth);
                float max = avOwner->GetPermanentActorValue(RE::ActorValue::kHealth);
                healthPct = (max > 0.f) ? (cur / max) : 0.f;
            }

            float level = static_cast<float>(actor->GetLevel());

            if (isHostile) {
                hostileCount++;
                if (dist < nearestHostileDist)
                    nearestHostileDist = dist;
                // Threat contribution: higher level + closer = more threat
                totalThreat += (level / 100.f) * (1.f - dist / kScanRadius);

                // Detection level: how aware this hostile is of the player
                auto detLevel = actor->RequestDetectionLevel(player);
                if (static_cast<float>(detLevel) > maxDetection)
                    maxDetection = static_cast<float>(detLevel);
            } else {
                friendlyCount++;
                if (dist < nearestFriendDist)
                    nearestFriendDist = dist;
            }

            // Track overall nearest actor
            if (dist < nearestDist) {
                nearestDist    = dist;
                nearestPos     = actorPos;
                nearestHealth  = healthPct;
                nearestLevel   = level;
                nearestHostile = isHostile;

                // Check alert state via low process flags
                auto* ai = actor->GetActorRuntimeData().currentProcess;
                if (ai) {
                    nearestAlerted = ai->IsArrested() || actor->IsInCombat();
                }
            }
        }

        // ── Pack results ─────────────────────────────────────────────────
        if (nearestDist < std::numeric_limits<float>::max()) {
            data.Nearest.x = nearestDist;
            data.Nearest.y = nearestHostile ? 1.f : 0.f;
            data.Nearest.z = nearestHealth;
            data.Nearest.w = nearestLevel;

            data.NearestPos.x = nearestPos.x;
            data.NearestPos.y = nearestPos.y;
            data.NearestPos.z = nearestPos.z;
            data.NearestPos.w = nearestAlerted ? 1.f : 0.f;
        }

        data.Summary.x = static_cast<float>(hostileCount);
        data.Summary.y = static_cast<float>(friendlyCount);
        data.Summary.z = nearestHostileDist < std::numeric_limits<float>::max()
                       ? nearestHostileDist : -1.f;
        data.Summary.w = nearestFriendDist < std::numeric_limits<float>::max()
                       ? nearestFriendDist : -1.f;

        // Normalize threat rating to [0,1] (cap at ~5 high-level hostiles)
        data.Threat.x = totalThreat > 1.f ? 1.f : totalThreat;

        // Stealth meter
        if (player->IsSneaking()) {
            // Detection level from player's stealth score
            auto* avOwner = player->AsActorValueOwner();
            if (avOwner) {
                data.Threat.y = avOwner->GetActorValue(RE::ActorValue::kSneak);
            }
        }

        // Total high actor count (engine's NPC density metric)
        data.Threat.z = static_cast<float>(processLists->highActorHandles.size());

        // Max detection level (computed in the main scan loop above)
        data.Threat.w = maxDetection;

        return data;
    }
}
