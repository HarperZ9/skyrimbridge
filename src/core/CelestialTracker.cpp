#include "CelestialTracker.h"
#include <RE/Skyrim.h>
#include <cmath>

namespace SB::CelestialTracker
{
    // ── Calculate moon phase from game day ──────────────────────────────
    // Skyrim's moons have different cycles. We approximate using game day.
    // Masser: 24-day cycle (8 phases, 3 days each)
    // Secunda: Could use a different cycle, but we'll use a simple approximation.
    static RE::Moon::Phase GetMoonPhase(bool a_isMasser)
    {
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar)
            return RE::Moon::Phase::kFull;

        // Get current game day (days since game start)
        float gameDay = calendar->GetDaysPassed();
        int dayInt = static_cast<int>(gameDay);

        // Masser: 24-day cycle (8 phases)
        // Secunda: offset by half cycle for variety
        int offset = a_isMasser ? 0 : 4;
        int phase = (dayInt + offset) % 8;

        // Map to Phase enum
        switch (phase) {
        case 0:  return RE::Moon::Phase::kFull;
        case 1:  return RE::Moon::Phase::kWaningGibbous;
        case 2:  return RE::Moon::Phase::kWaningQuarter;
        case 3:  return RE::Moon::Phase::kWaningCrescent;
        case 4:  return RE::Moon::Phase::kNewMoon;
        case 5:  return RE::Moon::Phase::kWaxingCrescent;
        case 6:  return RE::Moon::Phase::kWaxingQuarter;
        case 7:  return RE::Moon::Phase::kWaxingGibbous;
        default: return RE::Moon::Phase::kFull;
        }
    }

    // ── Moon phase to brightness [0,1] ──────────────────────────────────
    static float PhaseBrightness(bool a_isMasser)
    {
        using P = RE::Moon::Phase;
        switch (GetMoonPhase(a_isMasser)) {
        case P::kFull:            return 1.00f;
        case P::kWaningGibbous:
        case P::kWaxingGibbous:   return 0.75f;
        case P::kWaningQuarter:
        case P::kWaxingQuarter:   return 0.50f;
        case P::kWaxingCrescent:
        case P::kWaningCrescent:  return 0.25f;
        case P::kNewMoon:         return 0.00f;
        default:                  return 0.50f;
        }
    }

    // ── Extract position from a SkyObject's NiNode ──────────────────────
    static bool GetSkyObjectPos(const RE::SkyObject* a_obj, RE::NiPoint3& a_out)
    {
        if (!a_obj || !a_obj->root)
            return false;

        // Check if the object is culled (hidden)
        if (a_obj->root->GetAppCulled())
            return false;

        a_out = a_obj->root.get()->world.translate;
        return true;
    }

    // ── Main update ─────────────────────────────────────────────────────
    CelestialData Update()
    {
        CelestialData data{};

        auto* sky = RE::Sky::GetSingleton();
        if (!sky)
            return data;

        // ── Sun ─────────────────────────────────────────────────────────
        // Only direction needed — NDC derivable in shader from dir + VP
        RE::NiPoint3 sunPos{};
        if (sky->sun && GetSkyObjectPos(sky->sun, sunPos)) {
            float len = std::sqrt(sunPos.x*sunPos.x + sunPos.y*sunPos.y + sunPos.z*sunPos.z);
            if (len > 1e-6f) {
                data.SunDirection.x = sunPos.x / len;
                data.SunDirection.y = sunPos.y / len;
                data.SunDirection.z = sunPos.z / len;
            }
            // Elevation angle (rad) — angle above horizon
            data.SunDirection.w = std::asin(std::clamp(data.SunDirection.z, -1.0f, 1.0f));
        }

        // Sun color from current weather
        if (sky->currentWeather) {
            data.SunColor.w = sky->currentWeather->data.sunGlare;
        }

        // ── Masser ──────────────────────────────────────────────────────
        // Direction only — phase brightness packed into .w
        RE::NiPoint3 masserPos{};
        if (sky->masser && GetSkyObjectPos(sky->masser, masserPos)) {
            float len = std::sqrt(masserPos.x*masserPos.x + masserPos.y*masserPos.y + masserPos.z*masserPos.z);
            if (len > 1e-6f) {
                data.MasserDirection.x = masserPos.x / len;
                data.MasserDirection.y = masserPos.y / len;
                data.MasserDirection.z = masserPos.z / len;
            }
            data.MasserDirection.w = PhaseBrightness(true);
        }

        // ── Secunda ─────────────────────────────────────────────────────
        RE::NiPoint3 secundaPos{};
        if (sky->secunda && GetSkyObjectPos(sky->secunda, secundaPos)) {
            float len = std::sqrt(secundaPos.x*secundaPos.x + secundaPos.y*secundaPos.y + secundaPos.z*secundaPos.z);
            if (len > 1e-6f) {
                data.SecundaDirection.x = secundaPos.x / len;
                data.SecundaDirection.y = secundaPos.y / len;
                data.SecundaDirection.z = secundaPos.z / len;
            }
            data.SecundaDirection.w = PhaseBrightness(false);
        }

        // ── Time ────────────────────────────────────────────────────────
        auto* calendar = RE::Calendar::GetSingleton();
        if (calendar) {
            float hour = calendar->GetHour();
            data.TimeData.x = hour;
            data.TimeData.w = hour / 24.0f;  // day progress [0,1]
        }

        // Sunrise/sunset from climate
        float sunriseBegin = 6.0f, sunriseEnd = 8.0f;
        float sunsetBegin = 17.0f, sunsetEnd = 19.5f;

        if (sky->currentClimate) {
            auto* climate = sky->currentClimate;
            auto& timing = climate->timing;
            // uint8_t values: 10-minute intervals (0-143). hour = value / 6.0
            sunriseBegin = static_cast<float>(timing.sunrise.begin) / 6.0f;
            sunriseEnd   = static_cast<float>(timing.sunrise.end)   / 6.0f;
            sunsetBegin  = static_cast<float>(timing.sunset.begin)  / 6.0f;
            sunsetEnd    = static_cast<float>(timing.sunset.end)    / 6.0f;
            data.TimeData.y = sunriseBegin;
            data.TimeData.z = sunsetEnd;
        }

        // ── Time-of-day segments ─────────────────────────────────────────
        float h = data.TimeData.x;  // gameHour [0,24)

        auto smoothstep = [](float edge0, float edge1, float x) -> float {
            float t = (x - edge0) / (edge1 - edge0);
            t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
            return t * t * (3.f - 2.f * t);
        };

        float dawnStart = sunriseBegin - 1.0f;
        float duskEnd   = sunsetEnd + 1.0f;

        // Dawn: ramps before sunrise, fades as sunrise completes
        data.TimeSegments1.x = smoothstep(dawnStart, sunriseBegin, h)
                             * (1.f - smoothstep(sunriseBegin, sunriseEnd, h));
        // Sunrise: active during sunrise period
        data.TimeSegments1.y = smoothstep(sunriseBegin, sunriseBegin + 0.5f, h)
                             * (1.f - smoothstep(sunriseEnd - 0.5f, sunriseEnd, h));
        // Day: full daylight
        data.TimeSegments1.z = smoothstep(sunriseEnd, sunriseEnd + 0.5f, h)
                             * (1.f - smoothstep(sunsetBegin - 0.5f, sunsetBegin, h));
        // Sunset: active during sunset period
        data.TimeSegments1.w = smoothstep(sunsetBegin, sunsetBegin + 0.5f, h)
                             * (1.f - smoothstep(sunsetEnd - 0.5f, sunsetEnd, h));
        // Dusk: fades after sunset
        data.TimeSegments2.x = smoothstep(sunsetEnd - 0.5f, sunsetEnd, h)
                             * (1.f - smoothstep(sunsetEnd, duskEnd, h));
        // Night: dark period (wraps around midnight)
        float nightIn  = smoothstep(duskEnd, duskEnd + 0.5f, h);
        float nightOut = 1.f - smoothstep(dawnStart - 0.5f, dawnStart, h);
        data.TimeSegments2.y = (h > 12.f) ? nightIn : nightOut;
        // Golden hour: ~1h after sunrise + ~1h before sunset
        float ghM = smoothstep(sunriseBegin, sunriseEnd, h)
                  * (1.f - smoothstep(sunriseEnd, sunriseEnd + 1.0f, h));
        float ghE = smoothstep(sunsetBegin - 1.0f, sunsetBegin, h)
                  * (1.f - smoothstep(sunsetBegin, sunsetEnd, h));
        data.TimeSegments2.z = ghM > ghE ? ghM : ghE;
        // Blue hour: twilight fringe before sunrise + after sunset
        float bhM = smoothstep(dawnStart, sunriseBegin, h)
                  * (1.f - smoothstep(sunriseBegin, sunriseBegin + 0.5f, h));
        float bhE = smoothstep(sunsetEnd - 0.5f, sunsetEnd, h)
                  * (1.f - smoothstep(sunsetEnd, duskEnd, h));
        data.TimeSegments2.w = bhM > bhE ? bhM : bhE;

        return data;
    }
}
