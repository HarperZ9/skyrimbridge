#include "EquipmentTracker.h"
#include <RE/Skyrim.h>

namespace SB::EquipmentTracker
{
    // Map TESObjectWEAP::WEAPON_TYPE to float for shader use
    static float WeaponTypeToFloat(RE::WEAPON_TYPE type)
    {
        return static_cast<float>(type);
        // 0=HandToHand, 1=OneHandSword, 2=OneHandDagger, 3=OneHandAxe,
        // 4=OneHandMace, 5=TwoHandSword, 6=TwoHandAxe, 7=Bow,
        // 8=Staff, 9=Crossbow
    }

    EquipmentData Update()
    {
        EquipmentData data{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player)
            return data;

        RE::Actor* actor = static_cast<RE::Actor*>(player);

        // ── Right hand ────────────────────────────────────────────────
        auto* rightObj = actor->GetEquippedObject(false);  // false = right hand
        if (rightObj) {
            auto* weapon = rightObj->As<RE::TESObjectWEAP>();
            if (weapon) {
                data.Right.x = WeaponTypeToFloat(weapon->GetWeaponType());
                data.Right.y = weapon->GetAttackDamage();

                // Check enchantment
                auto* ench = weapon->formEnchanting;
                if (ench) {
                    data.Right.z = 1.0f;
                }
            }
        }

        // ── Left hand ─────────────────────────────────────────────────
        auto* leftObj = actor->GetEquippedObject(true);  // true = left hand
        if (leftObj) {
            auto* weapon = leftObj->As<RE::TESObjectWEAP>();
            auto* shield = leftObj->As<RE::TESObjectARMO>();
            auto* spell  = leftObj->As<RE::SpellItem>();

            if (weapon) {
                data.Left.x = WeaponTypeToFloat(weapon->GetWeaponType());
                data.Left.y = static_cast<float>(weapon->GetAttackDamage());
            } else if (shield) {
                data.Left.x = 20.0f;  // custom: shield type
                data.Left.y = static_cast<float>(shield->GetArmorRating());
            } else if (spell) {
                data.Left.w = 1.0f;  // isSpell
            }

            if (weapon) {
                auto* ench = weapon->formEnchanting;
                if (ench) data.Left.z = 1.0f;
            }
        }

        // ── Armor ─────────────────────────────────────────────────────
        // Total armor rating from the engine's cached value
        auto* avOwner = actor->AsActorValueOwner();
        if (avOwner) {
            data.Armor.x = avOwner->GetActorValue(RE::ActorValue::kDamageResist);
        }

        // Armor type detection: scan worn armor pieces
        auto inv = actor->GetInventory();
        for (auto& [item, invData] : inv) {
            if (!invData.second || !invData.second->IsWorn())
                continue;

            auto* armor = item->As<RE::TESObjectARMO>();
            if (!armor)
                continue;

            // Check armor weight class
            auto weightClass = armor->GetArmorType();
            if (weightClass == RE::BIPED_MODEL::ArmorType::kHeavyArmor)
                data.Armor.y = 1.0f;
            else if (weightClass == RE::BIPED_MODEL::ArmorType::kLightArmor)
                data.Armor.z = 1.0f;
            else  // clothing/robes
                data.Armor.w = 1.0f;
        }

        // ── Equipment flags (public scalar components) ─────────────────
        auto* actorState = actor->AsActorState();
        bool hasWeaponDrawn = actorState && actorState->IsWeaponDrawn();

        // Check for bow
        bool hasBow = false;
        bool isTwoHanding = false;
        if (rightObj) {
            auto* weapon = rightObj->As<RE::TESObjectWEAP>();
            if (weapon) {
                auto wtype = weapon->GetWeaponType();
                if (wtype == RE::WEAPON_TYPE::kBow || wtype == RE::WEAPON_TYPE::kCrossbow)
                    hasBow = true;
                if (wtype == RE::WEAPON_TYPE::kTwoHandSword ||
                    wtype == RE::WEAPON_TYPE::kTwoHandAxe)
                    isTwoHanding = true;
            }
        }

        // Torch detection: check left hand for torch
        bool hasTorch = false;
        if (leftObj) {
            auto* light = leftObj->As<RE::TESObjectLIGH>();
            if (light)
                hasTorch = true;
        }

        data.Flags.x = hasWeaponDrawn ? 1.0f : 0.0f;
        data.Flags.y = hasBow ? 1.0f : 0.0f;
        data.Flags.z = hasTorch ? 1.0f : 0.0f;
        data.Flags.w = isTwoHanding ? 1.0f : 0.0f;

        return data;
    }
}
