#include "AtmosphereTracker.h"
#include <RE/Skyrim.h>
#include <cmath>

namespace SB::AtmosphereTracker
{
    // ── Color conversion ────────────────────────────────────────────────
    static Float4 ColorToFloat4(const RE::Color& c)
    {
        return {
            c.red   / 255.0f,
            c.green / 255.0f,
            c.blue  / 255.0f,
            c.alpha / 255.0f
        };
    }

    // ── Time-of-day interpolation ───────────────────────────────────────
    // TESWeather stores colors per TOD slot.  We need to interpolate
    // between the current and adjacent slots based on game hour.
    //
    // CommonLibSSE-NG TESWeather::ColorTimes layout:
    //   Indices: Sunrise=0, Day=1, Sunset=2, Night=3
    //   (Some versions have additional slots for early sunrise/late sunset)
    //
    // The game interpolates linearly between adjacent time slots.
    // We replicate that logic here.

    // Get interpolated color from a weather for a given color type.
    // a_hour is game hour [0,24).
    // a_colorType indexes into the weather's color data (an int from ColorTypes enum).
    static Float4 GetInterpolatedColor(
        const RE::TESWeather* a_weather,
        int a_colorType,
        float a_hour)
    {
        if (!a_weather)
            return {};

        // TESWeather::colorData is [ColorTypes::kTotal][ColorTime::kTotal]
        // where the time slots map to approximate hours:
        //   0: Sunrise (~6:00)
        //   1: Day     (~12:00)
        //   2: Sunset  (~18:00)
        //   3: Night   (~0:00/24:00)
        //
        // The exact transition times come from the climate, but
        // for interpolation we use the standard quadrant mapping.

        auto getSlot = [&](int idx) -> Float4 {
            idx = idx & 3;  // wrap to [0,3]
            const auto& c = a_weather->colorData[a_colorType][idx];
            return {
                c.red   / 255.0f,
                c.green / 255.0f,
                c.blue  / 255.0f,
                c.alpha / 255.0f
            };
        };

        // Determine which two slots we're between and the blend factor.
        // Quadrants: Night[21-3] → Sunrise[3-9] → Day[9-15] → Sunset[15-21]
        int slotA, slotB;
        float t;

        if (a_hour < 3.0f) {
            slotA = 3; slotB = 3;  // deep night
            t = 0.f;
        } else if (a_hour < 9.0f) {
            slotA = 3; slotB = 0;  // night → sunrise
            t = (a_hour - 3.0f) / 6.0f;
        } else if (a_hour < 12.0f) {
            slotA = 0; slotB = 1;  // sunrise → day
            t = (a_hour - 9.0f) / 3.0f;
        } else if (a_hour < 15.0f) {
            slotA = 1; slotB = 1;  // midday
            t = 0.f;
        } else if (a_hour < 18.0f) {
            slotA = 1; slotB = 2;  // day → sunset
            t = (a_hour - 15.0f) / 3.0f;
        } else if (a_hour < 21.0f) {
            slotA = 2; slotB = 3;  // sunset → night
            t = (a_hour - 18.0f) / 3.0f;
        } else {
            slotA = 3; slotB = 3;  // deep night
            t = 0.f;
        }

        auto cA = getSlot(slotA);
        auto cB = getSlot(slotB);

        return {
            cA.x + (cB.x - cA.x) * t,
            cA.y + (cB.y - cA.y) * t,
            cA.z + (cB.z - cA.z) * t,
            cA.w + (cB.w - cA.w) * t,
        };
    }

    // Blend between two weather colors based on transition percentage.
    static Float4 BlendWeatherColor(
        const RE::TESWeather* a_current,
        const RE::TESWeather* a_last,
        int a_colorType,
        float a_hour,
        float a_transition)
    {
        auto cur = GetInterpolatedColor(a_current, a_colorType, a_hour);

        if (!a_last || a_transition >= 1.0f)
            return cur;

        auto prev = GetInterpolatedColor(a_last, a_colorType, a_hour);
        float t = a_transition;

        return {
            prev.x + (cur.x - prev.x) * t,
            prev.y + (cur.y - prev.y) * t,
            prev.z + (cur.z - prev.z) * t,
            prev.w + (cur.w - prev.w) * t,
        };
    }

    // ── Main update ─────────────────────────────────────────────────────
    AtmosphereData Update()
    {
        AtmosphereData data{};

        auto* sky = RE::Sky::GetSingleton();
        if (!sky || !sky->currentWeather)
            return data;

        float hour = 12.0f;
        if (auto* cal = RE::Calendar::GetSingleton())
            hour = cal->GetHour();

        float transition = sky->currentWeatherPct;
        auto* current = sky->currentWeather;
        auto* last    = sky->lastWeather;

        using CT = RE::TESWeather::ColorTypes;

        data.SkyUpper        = BlendWeatherColor(current, last, CT::kSkyUpper,        hour, transition);
        data.SkyLower        = BlendWeatherColor(current, last, CT::kSkyLower,        hour, transition);
        data.Horizon         = BlendWeatherColor(current, last, CT::kHorizon,         hour, transition);
        data.Ambient         = BlendWeatherColor(current, last, CT::kAmbient,         hour, transition);
        data.SunlightColor   = BlendWeatherColor(current, last, CT::kSunlight,        hour, transition);
        data.CloudLODDiffuse = BlendWeatherColor(current, last, CT::kCloudLODDiffuse, hour, transition);
        data.CloudLODAmbient = BlendWeatherColor(current, last, CT::kCloudLODAmbient, hour, transition);
        data.EffectLighting  = BlendWeatherColor(current, last, CT::kEffectLighting,  hour, transition);

        // Pack sunlight scale from weather data
        data.SunlightColor.w = current->data.sunGlare;

        return data;
    }
}
