#include "WeatherTracker.h"
#include <RE/Skyrim.h>
#include <bit>
#include <cmath>

namespace SB::WeatherTracker
{
    // Persistent state for lightning tracking
    static float s_timeSinceFlash = 99.f;
    static bool  s_wasFlashing    = false;

    // Persistent state for surface accumulation
    static float s_wetness        = 0.f;
    static float s_snowAccum      = 0.f;

    WeatherData Update(float a_deltaTime)
    {
        WeatherData data{};

        auto* sky = RE::Sky::GetSingleton();
        if (!sky || !sky->currentWeather)
            return data;

        auto* w = sky->currentWeather;

        // ── Wind ────────────────────────────────────────────────────────
        data.Wind.x = w->data.windSpeed / 255.0f;  // normalize to [0,1]
        // Wind direction: TESWeather stores as degrees or uint8.
        // Convert to radians.
        data.Wind.y = w->data.windDirection * (3.14159265f / 180.0f);

        // ── Precipitation ───────────────────────────────────────────────
        // TESWeather::Data::flags indicates precipitation type.
        auto flags = w->data.flags;

        bool isRain = flags.any(RE::TESWeather::WeatherDataFlag::kRainy);
        bool isSnow = flags.any(RE::TESWeather::WeatherDataFlag::kSnow);

        if (isSnow)        data.Precipitation.x = 2.0f;
        else if (isRain)   data.Precipitation.x = 1.0f;
        else               data.Precipitation.x = 0.0f;

        // Precipitation intensity from weather transition
        // (fades in/out based on precipitationBeginFadeIn/End)
        data.Precipitation.y = (isRain || isSnow) ? 1.0f : 0.0f;
        // During weather transition, scale by transition %
        if (sky->currentWeatherPct < 1.0f)
            data.Precipitation.y *= sky->currentWeatherPct;

        // ── Lightning ───────────────────────────────────────────────────
        // TESWeather has thunderLightningFrequency.
        data.Lightning.x = w->data.thunderLightningFrequency / 255.0f;

        // Detect lightning flash via sky state.
        // RE::Sky has a flash member or we detect via sudden brightness.
        // For now, we track based on weather flag + random frequency.
        bool thunderActive = flags.any(RE::TESWeather::WeatherDataFlag::kRainy) &&
                             w->data.thunderLightningFrequency > 0;

        // The game handles actual flashes internally.  We provide the
        // frequency parameter so shaders can simulate their own.
        // s_timeSinceFlash tracks time since we last detected one.
        s_timeSinceFlash += a_deltaTime;

        // Check if sky's flash intensity changed (if accessible)
        // Fallback: we expose the frequency and let the shader decide.
        data.Lightning.y = 0.f;  // isFlashing — set by shader or hook
        data.Lightning.z = 0.f;  // flashIntensity
        data.Lightning.w = s_timeSinceFlash;

        // ── Weather flags (packed uint bitfield) ──────────────────────
        uint32_t flagBits = 0;
        if (flags.any(RE::TESWeather::WeatherDataFlag::kPleasant)) flagBits |= (1u << 0);
        if (flags.any(RE::TESWeather::WeatherDataFlag::kCloudy))   flagBits |= (1u << 1);
        if (isRain) flagBits |= (1u << 2);
        if (isSnow) flagBits |= (1u << 3);
        data.Flags.x = std::bit_cast<float>(flagBits);
        data.Flags.y = 0.0f;
        data.Flags.z = 0.0f;
        data.Flags.w = 0.0f;

        // ── Transition ──────────────────────────────────────────────────
        data.Transition.x = sky->currentWeatherPct;

        // Weather form IDs (low 16 bits for shader use)
        if (sky->lastWeather)
            data.Transition.y = static_cast<float>(
                sky->lastWeather->GetFormID() & 0xFFFF);
        data.Transition.z = static_cast<float>(
            w->GetFormID() & 0xFFFF);

        // ── Precipitation surface accumulation ────────────────────────
        // Wetness builds up during rain, dries out otherwise.
        // Snow accumulates during snow, melts otherwise.
        float precipIntensity = data.Precipitation.y;
        if (isRain) {
            s_wetness = std::min(1.0f, s_wetness + precipIntensity * a_deltaTime * 0.1f);
        } else {
            s_wetness = std::max(0.0f, s_wetness - a_deltaTime * 0.02f);
        }
        if (isSnow) {
            s_snowAccum = std::min(1.0f, s_snowAccum + precipIntensity * a_deltaTime * 0.05f);
        } else {
            s_snowAccum = std::max(0.0f, s_snowAccum - a_deltaTime * 0.01f);
        }
        data.PrecipSurface.x = s_wetness;
        data.PrecipSurface.y = s_wetness * 0.3f;  // puddle depth (simplified)
        data.PrecipSurface.z = s_snowAccum;

        // ── Tier A: Live Sky singleton values ─────────────────────────
        // These read the engine's live state, not TESWeather form data.
        data.WindLive.x = sky->windSpeed;
        data.WindLive.y = sky->windAngle;
        data.WindLive.z = std::cos(sky->windAngle);  // wind direction X
        data.WindLive.w = std::sin(sky->windAngle);  // wind direction Z

        // Precipitation particle density (live from engine)
        if (sky->precip) {
            data.PrecipLive.x = sky->precip->currentParticleDensity;
            data.PrecipLive.y = sky->precip->lastParticleDensity;
        }
        data.PrecipLive.z = sky->flash;              // lightning flash intensity
        data.PrecipLive.w = sky->currentGameHour;

        // Cloud coverage (average + max from 32 layers)
        if (sky->clouds) {
            float sumAlpha = 0.0f;
            float maxAlpha = 0.0f;
            int activeLayers = 0;
            for (int i = 0; i < sky->clouds->numLayers && i < 32; ++i) {
                float a = sky->clouds->alphas[i];
                if (a > 0.001f) {
                    sumAlpha += a;
                    activeLayers++;
                    if (a > maxAlpha) maxAlpha = a;
                }
            }
            data.CloudCover.x = activeLayers > 0 ? sumAlpha / static_cast<float>(activeLayers) : 0.0f;
            data.CloudCover.y = static_cast<float>(activeLayers);
            data.CloudCover.z = maxAlpha;
        }
        data.CloudCover.w = sky->currentWeatherPct;

        // Aurora fade curves
        data.AuroraFade.x = sky->auroraIn;
        data.AuroraFade.y = sky->auroraOut;
        data.AuroraFade.z = sky->auroraInStart;
        data.AuroraFade.w = sky->auroraOutStart;

        return data;
    }
}
