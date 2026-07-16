#include "ImageSpaceTracker.h"
#include <RE/Skyrim.h>

namespace SB::ImageSpaceTracker
{
    ImageSpaceData Update()
    {
        ImageSpaceData data{};

        auto* imgMgr = RE::ImageSpaceManager::GetSingleton();
        if (!imgMgr)
            return data;

        // ── HDR parameters ────────────────────────────────────────────
        // ImageSpaceManager::data contains the currently active image space
        // settings (blended from base + modifier + overrides).
        auto& isData = imgMgr->data;

        data.HDR.x = isData.baseData.hdr.eyeAdaptSpeed;
        data.HDR.y = isData.baseData.hdr.bloomScale;
        data.HDR.z = isData.baseData.hdr.bloomThreshold;
        data.HDR.w = isData.baseData.hdr.sunlightScale;

        // ── Cinematic ─────────────────────────────────────────────────
        data.Cinematic.x = isData.baseData.cinematic.saturation;
        data.Cinematic.y = isData.baseData.cinematic.brightness;
        data.Cinematic.z = isData.baseData.cinematic.contrast;
        data.Cinematic.w = isData.baseData.tint.amount;

        // ── Cinematic tint color ──────────────────────────────────────
        data.CineTint.x = isData.baseData.tint.color.red / 255.0f;
        data.CineTint.y = isData.baseData.tint.color.green / 255.0f;
        data.CineTint.z = isData.baseData.tint.color.blue / 255.0f;

        // ── Depth of Field ────────────────────────────────────────────
        data.DOF.x = isData.baseData.depthOfField.strength;
        data.DOF.y = isData.baseData.depthOfField.distance;
        data.DOF.z = isData.baseData.depthOfField.range;
        // .w = sky blur radius (enum cast to float as a proxy for vignette)
        data.DOF.w = static_cast<float>(isData.baseData.depthOfField.skyBlurRadius.get());

        // ── Active IMOD (Imagespace Modifier) ─────────────────────────
        // IMODs are scripted imagespace effects (e.g., blood overlay,
        // poison tint, drunk vision). Track the strongest active one.
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            auto* magicTarget = player->AsMagicTarget();
            if (magicTarget) {
                auto* activeEffects = magicTarget->GetActiveEffectList();
                if (activeEffects) {
                    for (auto* ae : *activeEffects) {
                        if (!ae || ae->flags.any(RE::ActiveEffect::Flag::kInactive))
                            continue;
                        auto* effect = ae->GetBaseObject();
                        if (!effect)
                            continue;
                        // Check if this is an IMOD-applying effect
                        if (effect->GetArchetype() == RE::EffectSetting::Archetype::kValueModifier &&
                            effect->HasArchetype(RE::EffectSetting::Archetype::kValueModifier)) {
                            data.IMOD.x = 1.0f;  // hasActiveIMOD
                            data.IMOD.y = ae->magnitude;
                            data.IMOD.w = ae->elapsedSeconds;
                            break;
                        }
                    }
                }
            }
        }

        return data;
    }
}
