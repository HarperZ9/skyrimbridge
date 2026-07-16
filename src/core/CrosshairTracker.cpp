#include "CrosshairTracker.h"
#include <RE/Skyrim.h>
#include <cmath>

namespace SB::CrosshairTracker
{
    CrosshairData Update()
    {
        CrosshairData data{};

        auto* crosshair = RE::CrosshairPickData::GetSingleton();
        if (!crosshair)
            return data;

        // ── Resolve crosshair target ──────────────────────────────────
        auto targetHandle = crosshair->target;
        if (!targetHandle)
            return data;

        auto targetPtr = targetHandle.get();
        if (!targetPtr)
            return data;

        auto* target = targetPtr.get();
        if (!target)
            return data;

        data.Info.x = 1.0f;  // hasTarget

        // ── Distance to target ────────────────────────────────────────
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            auto pPos = player->GetPosition();
            auto tPos = target->GetPosition();
            float dx = tPos.x - pPos.x;
            float dy = tPos.y - pPos.y;
            float dz = tPos.z - pPos.z;
            data.Info.y = std::sqrt(dx*dx + dy*dy + dz*dz);
        }

        // ── Form type ─────────────────────────────────────────────────
        data.Info.z = static_cast<float>(target->GetFormType());

        // ── Target world position ─────────────────────────────────────
        // Use collision point if available, else target position
        auto tPos = target->GetPosition();
        data.Pos.x = tPos.x;
        data.Pos.y = tPos.y;
        data.Pos.z = tPos.z;

        // ── Actor-specific data ───────────────────────────────────────
        auto* targetActor = target->As<RE::Actor>();
        if (targetActor) {
            data.Info.w = 1.0f;  // isActor

            auto* avOwner = targetActor->AsActorValueOwner();
            if (avOwner) {
                float maxHP = avOwner->GetPermanentActorValue(RE::ActorValue::kHealth);
                float curHP = avOwner->GetActorValue(RE::ActorValue::kHealth);
                data.Actor.x = (maxHP > 0.f) ? (curHP / maxHP) : 0.f;
            }

            data.Actor.y = static_cast<float>(targetActor->GetLevel());
            data.Actor.z = (player && targetActor->IsHostileToActor(player)) ? 1.f : 0.f;

            // Essential flag
            bool essential = targetActor->IsEssential();
            data.Actor.w = essential ? 1.f : 0.f;
        }

        return data;
    }
}
