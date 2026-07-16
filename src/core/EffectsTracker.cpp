#include "EffectsTracker.h"
#include <RE/Skyrim.h>
#include <bit>

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

        // Accumulate flags into uint bitfields
        uint32_t visionBits = 0;
        uint32_t damageBits = 0;
        uint32_t miscBits   = 0;

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
                visionBits |= (1u << 0);
            if (archetype == AT::kDetectLife)
                visionBits |= (1u << 1);
            if (archetype == AT::kEtherealize)
                visionBits |= (1u << 3);

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
                        damageBits |= (1u << 0);
                    else if (resist == RE::ActorValue::kResistFrost)
                        damageBits |= (1u << 1);
                    else if (resist == RE::ActorValue::kResistShock)
                        damageBits |= (1u << 2);
                    else if (resist == RE::ActorValue::kPoisonResist)
                        damageBits |= (1u << 3);
                }
            }

            // ── Misc effects ────────────────────────────────────────────
            if (archetype == AT::kInvisibility)
                miscBits |= (1u << 0);
            if (archetype == AT::kParalysis)
                miscBits |= (1u << 1);
        }

        // Pack bitfields into .x, zero .yzw
        data.VisionEffects.x = std::bit_cast<float>(visionBits);
        data.VisionEffects.y = 0.0f;
        data.VisionEffects.z = 0.0f;
        data.VisionEffects.w = 0.0f;

        data.DamageEffects.x = std::bit_cast<float>(damageBits);
        data.DamageEffects.y = 0.0f;
        data.DamageEffects.z = 0.0f;
        data.DamageEffects.w = 0.0f;

        data.MiscEffects.x = std::bit_cast<float>(miscBits);
        data.MiscEffects.y = 0.0f;
        data.MiscEffects.z = 0.0f;
        data.MiscEffects.w = 0.0f;

        return data;
    }
}
