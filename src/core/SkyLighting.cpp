//=============================================================================
//  SkyLighting.cpp — celestial lighting model
//=============================================================================

#include "SkyLighting.h"
#include "SBConfig.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <cmath>

namespace SB
{
    static constexpr float kPi = 3.14159265358979f;
    static constexpr float kDeg = kPi / 180.0f;

    // ── Pure orbital model ───────────────────────────────────────────────
    SkyModel::SunState SkyModel::ComputeSun(float dayOfYear, float hour,
                                            float axialTilt, float latitude,
                                            float summerSolsticeDay)
    {
        float eps = axialTilt * kDeg;
        float lat = latitude * kDeg;

        // Declination peaks at +tilt on the summer solstice, -tilt half a year on.
        float dayAngle = 2.0f * kPi * (dayOfYear - summerSolsticeDay) / 365.0f;
        float decl = eps * std::cos(dayAngle);

        // Hour angle: 0 at local noon, 15 deg per hour.
        float H = (hour - 12.0f) * 15.0f * kDeg;

        float sinAlt = std::sin(lat) * std::sin(decl) +
                       std::cos(lat) * std::cos(decl) * std::cos(H);
        sinAlt = std::clamp(sinAlt, -1.0f, 1.0f);
        float alt = std::asin(sinAlt);

        // Azimuth from the horizontal components (east, north) via atan2:
        // exact at the noon pole (the previous acos form needed an epsilon
        // guard that skewed noon azimuth by ~0.2 deg). Standard solar form.
        float east  = -std::cos(decl) * std::sin(H);
        float north =  std::sin(decl) * std::cos(lat) -
                       std::cos(decl) * std::sin(lat) * std::cos(H);
        float az = std::atan2(east, north);
        if (az < 0.0f) az += 2.0f * kPi;

        // (east, north, sinAlt) is already the unit incident vector: the
        // equatorial -> horizontal transform is a pure rotation.
        SunState s;
        s.altitude = alt;
        s.azimuth = az;
        s.dirX = east;
        s.dirY = north;
        s.dirZ = sinAlt;
        return s;
    }

    float SkyModel::PhaseWeight(float phase01)
    {
        phase01 = std::clamp(phase01, 0.0f, 1.0f);
        return phase01 * phase01;   // emphasise the full moon (exponential-ish)
    }

    // ── Config parsing ───────────────────────────────────────────────────
    LightSource SkyConfig::ParseSource(const std::string& v, LightSource def)
    {
        auto t = Cfg::Trim(v);
        if (t == "Sun")      return LightSource::Sun;
        if (t == "NightSky") return LightSource::NightSky;
        if (t == "Vanilla")  return LightSource::Vanilla;
        if (t == "Zenith")   return LightSource::Zenith;
        if (t == "Custom")   return LightSource::Custom;
        return def;
    }

    static void ReadMoon(const Cfg::Section* s, MoonConfig& m)
    {
        if (!s) return;
        m.fullDirect     = s->Float("FullDirect", m.fullDirect);
        m.fullVolumetric = s->Float("FullVolumetric", m.fullVolumetric);
        m.newDirect      = s->Float("NewDirect", m.newDirect);
        m.newVolumetric  = s->Float("NewVolumetric", m.newVolumetric);
        if (auto* v = s->Find("Color")) m.color = Cfg::AsColor4(*v, m.color);
        m.fadeStart      = s->Float("FadeStart", m.fadeStart);
        m.fadeEnd        = s->Float("FadeEnd", m.fadeEnd);
    }

    SkyConfig SkyLighting::ParseConfig(const std::string& text)
    {
        SkyConfig c;
        auto doc = Cfg::Parse(text);

        if (auto* s = doc.Find("Sky"))
            c.masterEnable = s->Bool("Enable", c.masterEnable);

        if (auto* s = doc.Find("Orbit")) {
            c.orbitEnable     = s->Bool("Enable", c.orbitEnable);
            c.adjustDayLength = s->Bool("AdjustDayLength", c.adjustDayLength);
            c.moveSun         = s->Bool("MoveSun", c.moveSun);
            c.axialTilt       = s->Float("AxialTilt", c.axialTilt);
            c.latitude        = s->Float("Latitude", c.latitude);
            c.twilightAngle   = s->Float("TwilightAngle", c.twilightAngle);
            c.masserTilt      = s->Float("MasserTilt", c.masserTilt);
            c.secundaTilt     = s->Float("SecundaTilt", c.secundaTilt);
            c.summerSolstice  = s->Float("SummerSolstice", c.summerSolstice);
            c.winterSolstice  = s->Float("WinterSolstice", c.winterSolstice);
        }
        if (auto* s = doc.Find("Ambient")) {
            c.ambientEnable = s->Bool("Enable", c.ambientEnable);
            c.tiltScale     = s->Float("TiltScale", c.tiltScale);
        }
        if (auto* s = doc.Find("Light")) {
            c.daySource   = SkyConfig::ParseSource(s->Get("DaySource"), c.daySource);
            c.nightSource = SkyConfig::ParseSource(s->Get("NightSource"), c.nightSource);
            c.minAngle    = s->Float("MinAngle", c.minAngle);
        }
        ReadMoon(doc.Find("Masser"), c.masser);
        ReadMoon(doc.Find("Secunda"), c.secunda);
        if (auto* s = doc.Find("NightSky")) {
            c.nightSkyDirect     = s->Float("Direct", c.nightSkyDirect);
            c.nightSkyVolumetric = s->Float("Volumetric", c.nightSkyVolumetric);
            c.auroraVolumetric   = s->Float("AuroraVolumetric", c.auroraVolumetric);
            if (auto* v = s->Find("Color")) c.nightSkyColor = Cfg::AsColor4(*v, c.nightSkyColor);
        }
        if (auto* s = doc.Find("Tweaks"))
            c.syncWeatherColors = s->Bool("SyncWeatherColors", c.syncWeatherColors);

        // Per-form overrides: [Orbit:ID] [Light:ID] [Weather:ID].
        for (auto& sec : doc.sections) {
            auto id = sec.SuffixID();
            if (id == 0) continue;
            auto base = sec.Base();
            if (base != "Orbit" && base != "Light" && base != "Weather") continue;
            SkyConfig::Override ov{ id, base, sec.entries };
            c.overrides.push_back(std::move(ov));
        }
        return c;
    }

    // ── Lifecycle ────────────────────────────────────────────────────────
    SkyLighting& SkyLighting::Get()
    {
        static SkyLighting instance;
        return instance;
    }

    void SkyLighting::Initialize(const std::filesystem::path& configDir)
    {
        std::ifstream in(configDir / "Sky.ini");
        if (!in) {
            SKSE::log::info("SkyLighting: no Sky.ini, inactive");
            return;
        }
        std::stringstream buf; buf << in.rdbuf();
        m_cfg = ParseConfig(buf.str());
        m_active = true;
        SKSE::log::info("SkyLighting: loaded Sky.ini (masterEnable={}, {} overrides)",
            m_cfg.masterEnable, m_cfg.overrides.size());
        if (m_cfg.masterEnable && m_cfg.moveSun)
            SKSE::log::info("SkyLighting: MoveSun armed (will redirect "
                "Sky->sun->sunBaseNode along the orbital incident vector each "
                "frame, preserving the node's own orbit radius)");
    }

    // ── Per-frame model ──────────────────────────────────────────────────

    // Combined moon brightness 0..1 from the game day (masser dominates).
    static float MoonBrightness()
    {
        auto* cal = RE::Calendar::GetSingleton();
        if (!cal) return 0.5f;
        int day = static_cast<int>(cal->GetDaysPassed());
        auto bright = [](int p) {
            int d = std::min(p % 8, 8 - (p % 8));   // 0 at full, 4 at new
            return 1.0f - static_cast<float>(d) / 4.0f;
        };
        return std::max(bright(day), bright(day + 4));
    }

    // Reposition the visual sun disc along the computed incident vector.
    // Gated by [Sky] Enable + [Orbit] MoveSun (both ship OFF). Preserves the
    // node's current orbit radius (the game's own distance); falls back to
    // 5000 only if the node sits at the origin. Game-bound: whether the
    // directional shadow follows the disc, and whether vanilla Sun::Update
    // rewrites the node later in the frame, is validated in-game
    // (docs/VALIDATION-PROTOCOL.md).
    static void MoveSunNode(RE::Sky* sky, const SkyModel::SunState& s)
    {
        if (!sky->sun) return;
        // NiBillboardNode is only forward-declared in this CommonLib install
        // (no NiBillboardNode.h). It is a NiNode subclass (single-inheritance
        // scene graph, NiAVObject base at offset 0), so address it as one.
        auto* node = reinterpret_cast<RE::NiAVObject*>(sky->sun->sunBaseNode.get());
        if (!node) return;

        float radius = std::sqrt(node->local.translate.SqrLength());
        if (radius < 1.0f) radius = 5000.0f;

        node->local.translate = { s.dirX * radius, s.dirY * radius, s.dirZ * radius };
        RE::NiUpdateData upd{};
        node->Update(upd);

        static bool logged = false;
        if (!logged) {
            logged = true;
            SKSE::log::info("SkyLighting: sun repositioned (alt={:.1f} deg, "
                "az={:.1f} deg, radius={:.0f})",
                s.altitude / kDeg, s.azimuth / kDeg, radius);
        }
    }

    void SkyLighting::Update()
    {
        if (!m_active || !m_cfg.masterEnable) return;

        auto* sky = RE::Sky::GetSingleton();
        auto* cal = RE::Calendar::GetSingleton();
        if (!sky || !cal) return;

        float hour = cal->GetHour();
        float doy  = std::fmod(cal->GetDaysPassed(), 365.0f);

        auto sun = SkyModel::ComputeSun(doy, hour, m_cfg.axialTilt, m_cfg.latitude, m_cfg.summerSolstice);

        if (m_cfg.moveSun)
            MoveSunNode(sky, sun);

        if (!m_cfg.ambientEnable) return;

        // Night factor: 0 with the sun up, ramps to 1 once it is a full
        // twilight-angle below the horizon.
        float nightness = std::clamp((0.0f - sun.altitude) / (m_cfg.twilightAngle * kDeg + 1e-4f), 0.0f, 1.0f);
        if (nightness <= 0.001f) return;   // daytime — leave the weather-driven ambient

        float mb = MoonBrightness();
        float intensity = m_cfg.nightSkyDirect + (m_cfg.masser.fullDirect - m_cfg.masser.newDirect) *
                          SkyModel::PhaseWeight(mb) * 0.25f;
        intensity = std::clamp(intensity, 0.0f, 1.5f);

        RE::NiColor target(m_cfg.nightSkyColor[0] * intensity,
                           m_cfg.nightSkyColor[1] * intensity,
                           m_cfg.nightSkyColor[2] * intensity);

        float w = std::clamp(nightness * m_cfg.tiltScale, 0.0f, 1.0f) * 0.5f;   // bounded blend
        for (int a = 0; a < 3; ++a) {
            for (int e = 0; e < 2; ++e) {
                auto& c = sky->directionalAmbientColors[a][e];
                c.red   += (target.red   - c.red)   * w;
                c.green += (target.green - c.green) * w;
                c.blue  += (target.blue  - c.blue)  * w;
            }
        }
    }
}
