#include "ActorValueTracker.h"
#include <RE/Skyrim.h>

namespace SB::ActorValueTracker
{
    ActorValueData Update()
    {
        ActorValueData data{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player)
            return data;

        auto* avOwner = player->AsActorValueOwner();
        if (!avOwner)
            return data;

        using AV = RE::ActorValue;

        // ── Resistances ───────────────────────────────────────────────
        data.Resist.x  = avOwner->GetActorValue(AV::kResistFire);
        data.Resist.y  = avOwner->GetActorValue(AV::kResistFrost);
        data.Resist.z  = avOwner->GetActorValue(AV::kResistShock);
        data.Resist.w  = avOwner->GetActorValue(AV::kResistMagic);

        data.Resist2.x = avOwner->GetActorValue(AV::kPoisonResist);
        data.Resist2.y = avOwner->GetActorValue(AV::kResistDisease);
        data.Resist2.z = avOwner->GetActorValue(AV::kDamageResist);

        // ── Combat stats ──────────────────────────────────────────────
        data.Combat.x  = avOwner->GetActorValue(AV::kAttackDamageMult);
        data.Combat.y  = avOwner->GetActorValue(AV::kWeaponSpeedMult);
        data.Combat.z  = avOwner->GetActorValue(AV::kCriticalChance);
        data.Combat.w  = avOwner->GetActorValue(AV::kUnarmedDamage);

        // ── Movement / encumbrance ────────────────────────────────────
        data.Movement.x = avOwner->GetActorValue(AV::kSpeedMult);
        data.Movement.y = avOwner->GetActorValue(AV::kCarryWeight);
        data.Movement.z = avOwner->GetActorValue(AV::kInventoryWeight);

        float maxCarry = data.Movement.y;
        data.Movement.w = (maxCarry > 0.f) ? (data.Movement.z / maxCarry) : 0.f;

        // ── Combat skills ─────────────────────────────────────────────
        data.SkillCombat.x = avOwner->GetActorValue(AV::kOneHanded);
        data.SkillCombat.y = avOwner->GetActorValue(AV::kTwoHanded);
        data.SkillCombat.z = avOwner->GetActorValue(AV::kArchery);
        data.SkillCombat.w = avOwner->GetActorValue(AV::kBlock);

        // ── Magic skills ──────────────────────────────────────────────
        data.SkillMagic.x  = avOwner->GetActorValue(AV::kAlteration);
        data.SkillMagic.y  = avOwner->GetActorValue(AV::kConjuration);
        data.SkillMagic.z  = avOwner->GetActorValue(AV::kDestruction);
        data.SkillMagic.w  = avOwner->GetActorValue(AV::kIllusion);

        data.SkillMagic2.x = avOwner->GetActorValue(AV::kRestoration);
        data.SkillMagic2.y = avOwner->GetActorValue(AV::kEnchanting);
        data.SkillMagic2.z = avOwner->GetActorValue(AV::kAlchemy);

        // ── Stealth skills ────────────────────────────────────────────
        data.SkillStealth.x = avOwner->GetActorValue(AV::kLightArmor);
        data.SkillStealth.y = avOwner->GetActorValue(AV::kSneak);
        data.SkillStealth.z = avOwner->GetActorValue(AV::kLockpicking);
        data.SkillStealth.w = avOwner->GetActorValue(AV::kPickpocket);

        return data;
    }
}
