#include "FogTracker.h"
#include <RE/Skyrim.h>
#include <cmath>

namespace SB::FogTracker
{
    // Helper: interpolate a single weather's fog near/far colors by TOD
    static Float4 GetFogColor(const RE::TESWeather* a_weather,
                              int a_colorType,
                              float a_hour)
    {
        if (!a_weather) return {};

        // Same quadrant interpolation as AtmosphereTracker
        auto getSlot = [&](int idx) -> Float4 {
            idx &= 3;
            const auto& c = a_weather->colorData[a_colorType][idx];
            return {
                c.red   / 255.0f,
                c.green / 255.0f,
                c.blue  / 255.0f,
                0.f
            };
        };

        int slotA, slotB;
        float t;
        if (a_hour < 3.f)       { slotA = 3; slotB = 3; t = 0.f; }
        else if (a_hour < 9.f)  { slotA = 3; slotB = 0; t = (a_hour - 3.f) / 6.f; }
        else if (a_hour < 12.f) { slotA = 0; slotB = 1; t = (a_hour - 9.f) / 3.f; }
        else if (a_hour < 15.f) { slotA = 1; slotB = 1; t = 0.f; }
        else if (a_hour < 18.f) { slotA = 1; slotB = 2; t = (a_hour - 15.f) / 3.f; }
        else if (a_hour < 21.f) { slotA = 2; slotB = 3; t = (a_hour - 18.f) / 3.f; }
        else                    { slotA = 3; slotB = 3; t = 0.f; }

        auto cA = getSlot(slotA);
        auto cB = getSlot(slotB);
        return {
            cA.x + (cB.x - cA.x) * t,
            cA.y + (cB.y - cA.y) * t,
            cA.z + (cB.z - cA.z) * t,
            0.f
        };
    }

    // Interpolate fog distance/density between day and night.
    // TESWeather::FogData has dayNear/dayFar/dayPower/dayMax and
    // nightNear/nightFar/nightPower/nightMax.
    struct FogDistances {
        float near_ = 0.f, far_ = 0.f, power = 1.f, max_ = 1.f;
    };

    static FogDistances InterpolateFogDistances(
        const RE::TESWeather* a_weather, float a_nightFactor)
    {
        if (!a_weather) return {};

        // The fog distance data in TESWeather.
        // CommonLibSSE-NG: weather->fogData or weather->fogDistance
        // Layout varies by version. We access the struct members directly.
        // RE::TESWeather has a nested FogData struct.
        const auto& fd = a_weather->fogData;
        float dayT = 1.0f - a_nightFactor;
        float nightT = a_nightFactor;

        return {
            fd.dayNear  * dayT + fd.nightNear  * nightT,
            fd.dayFar   * dayT + fd.nightFar   * nightT,
            fd.dayPower * dayT + fd.nightPower * nightT,
            fd.dayMax   * dayT + fd.nightMax   * nightT,
        };
    }

    FogData Update()
    {
        FogData data{};

        auto* sky = RE::Sky::GetSingleton();
        if (!sky || !sky->currentWeather)
            return data;

        float hour = 12.f;
        if (auto* cal = RE::Calendar::GetSingleton())
            hour = cal->GetHour();

        // Night factor: 0 = full day, 1 = full night
        // Approximate from hour: night when sun below horizon
        float nightFactor = 0.f;
        if (hour < 6.f)       nightFactor = 1.f;
        else if (hour < 8.f)  nightFactor = 1.f - (hour - 6.f) / 2.f;
        else if (hour < 18.f) nightFactor = 0.f;
        else if (hour < 20.f) nightFactor = (hour - 18.f) / 2.f;
        else                  nightFactor = 1.f;

        float transition = sky->currentWeatherPct;
        auto* current = sky->currentWeather;
        auto* last    = sky->lastWeather;

        using CT = RE::TESWeather::ColorTypes;

        // ── Fog colors ──────────────────────────────────────────────────
        auto nearCur = GetFogColor(current, CT::kFogNear, hour);
        auto farCur  = GetFogColor(current, CT::kFogFar,  hour);

        if (last && transition < 1.0f) {
            auto nearPrev = GetFogColor(last, CT::kFogNear, hour);
            auto farPrev  = GetFogColor(last, CT::kFogFar,  hour);
            float t = transition;

            nearCur = { nearPrev.x + (nearCur.x - nearPrev.x) * t,
                        nearPrev.y + (nearCur.y - nearPrev.y) * t,
                        nearPrev.z + (nearCur.z - nearPrev.z) * t, 0.f };
            farCur  = { farPrev.x + (farCur.x - farPrev.x) * t,
                        farPrev.y + (farCur.y - farPrev.y) * t,
                        farPrev.z + (farCur.z - farPrev.z) * t, 0.f };
        }

        // ── Fog distances ───────────────────────────────────────────────
        auto distCur = InterpolateFogDistances(current, nightFactor);

        if (last && transition < 1.0f) {
            auto distPrev = InterpolateFogDistances(last, nightFactor);
            float t = transition;
            distCur.near_ = distPrev.near_ + (distCur.near_ - distPrev.near_) * t;
            distCur.far_  = distPrev.far_  + (distCur.far_  - distPrev.far_)  * t;
            distCur.power = distPrev.power + (distCur.power - distPrev.power) * t;
            distCur.max_  = distPrev.max_  + (distCur.max_  - distPrev.max_)  * t;
        }

        // Pack distance into .a channel of color
        data.NearColor = nearCur;
        data.NearColor.w = distCur.near_;
        data.FarColor  = farCur;
        data.FarColor.w  = distCur.far_;
        data.Density   = { distCur.power, distCur.max_, 0.f, 0.f };

        // ── Height fog ──────────────────────────────────────────────────
        float waterZ = 0.f;
        float playerZ = 0.f;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            playerZ = player->GetPosition().z;

            // Try to get water height from current cell
            auto* cell = player->GetParentCell();
            if (cell) {
                waterZ = cell->GetExteriorWaterHeight();
                data.Density.z = cell->IsInteriorCell() ? 1.f : 0.f;
            }
        }

        data.HeightFog = {
            waterZ,           // water surface Z
            playerZ,          // player altitude
            1.0f,             // sea-level density multiplier (shader can tune)
            0.002f            // falloff rate (shader can tune)
        };

        return data;
    }
}
