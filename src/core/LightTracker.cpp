#include "LightTracker.h"
#include <RE/Skyrim.h>
#include <cmath>
#include <algorithm>

namespace SB::LightTracker
{
    struct LightCandidate
    {
        float x, y, z;
        float radius;
        float r, g, b;
        float intensity;
        float distSq;  // distance squared to camera
    };

    LightData Update()
    {
        LightData data{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player)
            return data;

        auto pos = player->GetPosition();

        // ── Collect nearby point lights from the player's parent cell ──
        auto* cell = player->GetParentCell();
        if (!cell)
            return data;

        // Use a fixed-size candidate array (avoid allocation per frame)
        static constexpr int kMaxCandidates = 64;
        LightCandidate candidates[kMaxCandidates];
        int candidateCount = 0;

        // Search radius for nearby lights (4096 game units)
        static constexpr float kSearchRadius = 4096.0f;

        cell->ForEachReferenceInRange(pos, kSearchRadius, [&](RE::TESObjectREFR& ref) {
            if (ref.IsDisabled() || ref.IsDeleted())
                return RE::BSContainer::ForEachResult::kContinue;

            auto* baseObj = ref.GetBaseObject();
            if (!baseObj)
                return RE::BSContainer::ForEachResult::kContinue;

            auto* lightForm = baseObj->As<RE::TESObjectLIGH>();
            if (!lightForm)
                return RE::BSContainer::ForEachResult::kContinue;

            auto lightPos = ref.GetPosition();
            float dx = lightPos.x - pos.x;
            float dy = lightPos.y - pos.y;
            float dz = lightPos.z - pos.z;
            float distSq = dx*dx + dy*dy + dz*dz;

            float radius = static_cast<float>(lightForm->data.radius);

            if (candidateCount < kMaxCandidates) {
                auto& c = candidates[candidateCount++];
                c.x = lightPos.x;
                c.y = lightPos.y;
                c.z = lightPos.z;
                c.radius = radius;
                c.r = lightForm->data.color.red / 255.0f;
                c.g = lightForm->data.color.green / 255.0f;
                c.b = lightForm->data.color.blue / 255.0f;
                c.intensity = lightForm->fade;
                c.distSq = distSq;
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        // ── Sort by distance, pick 3 nearest ─────────────────────────
        // Simple selection sort for top 3 (faster than full sort)
        for (int slot = 0; slot < 3 && slot < candidateCount; slot++) {
            int best = slot;
            for (int j = slot + 1; j < candidateCount; j++) {
                if (candidates[j].distSq < candidates[best].distSq)
                    best = j;
            }
            if (best != slot)
                std::swap(candidates[slot], candidates[best]);
        }

        // ── Pack into output ──────────────────────────────────────────
        auto packLight = [](const LightCandidate& c, Float4& posRad, Float4& color) {
            posRad.x = c.x;
            posRad.y = c.y;
            posRad.z = c.z;
            posRad.w = c.radius;
            color.x = c.r;
            color.y = c.g;
            color.z = c.b;
            color.w = c.intensity;
        };

        if (candidateCount > 0) packLight(candidates[0], data.Light0PosRad, data.Light0Color);
        if (candidateCount > 1) packLight(candidates[1], data.Light1PosRad, data.Light1Color);
        if (candidateCount > 2) packLight(candidates[2], data.Light2PosRad, data.Light2Color);

        // ── Summary ───────────────────────────────────────────────────
        data.Summary.x = static_cast<float>(std::min(candidateCount, 255));
        data.Summary.y = (candidateCount > 0) ? std::sqrt(candidates[0].distSq) : 0.0f;

        float totalFlux = 0.f;
        float dominantR = 0.f, dominantG = 0.f, dominantB = 0.f;
        for (int i = 0; i < std::min(candidateCount, 3); i++) {
            float lum = candidates[i].r * 0.2126f + candidates[i].g * 0.7152f + candidates[i].b * 0.0722f;
            totalFlux += lum * candidates[i].intensity;
            dominantR += candidates[i].r * candidates[i].intensity;
            dominantG += candidates[i].g * candidates[i].intensity;
            dominantB += candidates[i].b * candidates[i].intensity;
        }
        data.Summary.z = totalFlux;

        // Dominant hue: simplified — ratio of max channel
        float maxC = std::max({dominantR, dominantG, dominantB, 0.001f});
        data.Summary.w = dominantR / maxC;  // bias toward R as hue proxy

        return data;
    }
}
