#include "EffectsTracker.h"
#include <RE/Skyrim.h>

namespace SB::EffectsTracker
{
    EffectsData Update()
    {
        EffectsData data{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player)
            return data;

        // ── Scan active magic effects ───────────────────────────────────
        // Player inherits MagicTarget, which has a list of ActiveEffect.
        // Each ActiveEffect has a pointer to the EffectSetting (MGEF)
        // which tells us what it does.
        auto* magicTarget = player->AsMagicTarget();
        if (!magicTarget)
            return data;

        auto* activeEffects = magicTarget->GetActiveEffectList();
        if (!activeEffects)
            return data;

        // Accumulate public scalar flag components.
        bool hasNightEye     = false;
        bool hasDetectLife   = false;
        bool hasDetectDead   = false;
        bool hasEthereal     = false;
        bool hasFireDamage   = false;
        bool hasFrostDamage  = false;
        bool hasShockDamage  = false;
        bool hasPoisonDamage = false;
        bool hasInvisibility = false;
        bool hasParalysis    = false;
        bool hasDrunk        = false;

        for (auto* ae : *activeEffects) {
            if (!ae || ae->flags.any(RE::ActiveEffect::Flag::kInactive))
                continue;

            auto* effect = ae->GetBaseObject();
            if (!effect)
                continue;

            auto archetype = effect->GetArchetype();
            using AT = RE::EffectSetting::Archetype;

            // ── Vision effects ──────────────────────────────────────────
            if (archetype == AT::kNightEye)
                hasNightEye = true;
            if (archetype == AT::kDetectLife)
                hasDetectLife = true;
            if (archetype == AT::kEtherealize)
                hasEthereal = true;

            // ── Time effects ────────────────────────────────────────────
            if (archetype == AT::kSlowTime) {
                data.TimeEffects.x = ae->magnitude;
            }

            // ── Damage effects (resistible) ─────────────────────────────
            if (archetype == AT::kValueModifier) {
                auto av = effect->data.primaryAV;
                if (av == RE::ActorValue::kHealth) {
                    auto resist = effect->data.resistVariable;
                    if (resist == RE::ActorValue::kResistFire)
                        hasFireDamage = true;
                    else if (resist == RE::ActorValue::kResistFrost)
                        hasFrostDamage = true;
                    else if (resist == RE::ActorValue::kResistShock)
                        hasShockDamage = true;
                    else if (resist == RE::ActorValue::kPoisonResist)
                        hasPoisonDamage = true;
                }
            }

            // ── Misc effects ────────────────────────────────────────────
            if (archetype == AT::kInvisibility)
                hasInvisibility = true;
            if (archetype == AT::kParalysis)
                hasParalysis = true;
        }

        data.VisionEffects.x = hasNightEye ? 1.0f : 0.0f;
        data.VisionEffects.y = hasDetectLife ? 1.0f : 0.0f;
        data.VisionEffects.z = hasDetectDead ? 1.0f : 0.0f;
        data.VisionEffects.w = hasEthereal ? 1.0f : 0.0f;

        data.DamageEffects.x = hasFireDamage ? 1.0f : 0.0f;
        data.DamageEffects.y = hasFrostDamage ? 1.0f : 0.0f;
        data.DamageEffects.z = hasShockDamage ? 1.0f : 0.0f;
        data.DamageEffects.w = hasPoisonDamage ? 1.0f : 0.0f;

        data.MiscEffects.x = hasInvisibility ? 1.0f : 0.0f;
        data.MiscEffects.y = hasParalysis ? 1.0f : 0.0f;
        data.MiscEffects.z = hasDrunk ? 1.0f : 0.0f;
        data.MiscEffects.w = 0.0f;

        return data;
    }
}
