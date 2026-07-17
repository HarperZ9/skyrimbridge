//=============================================================================
//  EngineReflect.cpp — schema-driven reflection over engine records
//
//  Schemas are built on CommonLibSSE-NG RE:: types (the community's decompiled
//  engine layout) plus the operator's recovered offsets. Nothing here is
//  re-derived from SkyrimSE.exe: its .text is SteamStub-encrypted on disk, so
//  the reversal lives in CommonLib + Address Library, which this reflects.
//=============================================================================

#include "EngineReflect.h"
#include "SBConfig.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace SB::Reflect
{
    // ── Value ────────────────────────────────────────────────────────────
    std::string Value::ToText() const
    {
        char b[128];
        switch (kind) {
        case Kind::Float:    std::snprintf(b, sizeof b, "%.7g", num); return b;
        case Kind::Int:      std::snprintf(b, sizeof b, "%lld", static_cast<long long>(num)); return b;
        case Kind::Bool:     return num != 0 ? "true" : "false";
        case Kind::Color3:   std::snprintf(b, sizeof b, "%.7g, %.7g, %.7g", col[0], col[1], col[2]); return b;
        case Kind::Color4:   std::snprintf(b, sizeof b, "%.7g, %.7g, %.7g, %.7g", col[0], col[1], col[2], col[3]); return b;
        case Kind::FormLink: std::snprintf(b, sizeof b, "0x%X", id); return b;
        case Kind::String:   return str;
        }
        return {};
    }

    Value Value::FromText(Kind k, const std::string& t)
    {
        switch (k) {
        case Kind::Float:    return F(Cfg::AsFloat(t));
        case Kind::Int:      return I(Cfg::AsInt(t));
        case Kind::Bool:     return B(Cfg::AsBool(t));
        case Kind::Color3:   { auto c = Cfg::AsColor4(t, {0,0,0,1}); return C3(c[0], c[1], c[2]); }
        case Kind::Color4:   { auto c = Cfg::AsColor4(t, {0,0,0,1}); return C4(c[0], c[1], c[2], c[3]); }
        case Kind::FormLink: return Link(Cfg::AsFormID(t));
        case Kind::String:   return S(Cfg::Trim(t));
        }
        return F(0);
    }

    bool Value::ApproxEquals(const Value& o) const
    {
        if (kind != o.kind) return false;
        auto close = [](double a, double b) { return std::fabs(a - b) <= 1e-5 * (1.0 + std::fabs(a) + std::fabs(b)); };
        switch (kind) {
        case Kind::Float: case Kind::Int: case Kind::Bool: return close(num, o.num);
        case Kind::Color3: return close(col[0],o.col[0]) && close(col[1],o.col[1]) && close(col[2],o.col[2]);
        case Kind::Color4: return close(col[0],o.col[0]) && close(col[1],o.col[1]) && close(col[2],o.col[2]) && close(col[3],o.col[3]);
        case Kind::FormLink: return id == o.id;
        case Kind::String: return str == o.str;
        }
        return false;
    }

    // ── Registry ─────────────────────────────────────────────────────────
    static std::vector<Schema>& Registry() { static std::vector<Schema> r; return r; }

    const Field* Schema::Find(std::string_view n) const
    {
        for (auto& f : fields) if (std::string_view(f.name) == n) return &f;
        return nullptr;
    }
    const Schema* SchemaFor(RE::FormType t)
    {
        for (auto& s : Registry()) if (s.formType == t) return &s;
        return nullptr;
    }
    const Schema* SchemaByName(std::string_view name)
    {
        for (auto& s : Registry()) if (s.name == name) return &s;
        return nullptr;
    }
    std::vector<std::string> RegisteredSchemas()
    {
        std::vector<std::string> out;
        for (auto& s : Registry()) out.push_back(s.name);
        return out;
    }

    // ── Field-descriptor macros (non-capturing lambdas -> fn pointers) ────
    static std::uint8_t U8n(float f) { return static_cast<std::uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); }

#define RF_F(TYPE, NAME, MEMBER) Field{ NAME, Value::Kind::Float, \
    +[](const RE::TESForm* f){ return Value::F(static_cast<const RE::TYPE*>(f)->MEMBER); }, \
    +[](RE::TESForm* f, const Value& v){ static_cast<RE::TYPE*>(f)->MEMBER = static_cast<float>(v.num); } }

#define RF_U32(TYPE, NAME, MEMBER) Field{ NAME, Value::Kind::Int, \
    +[](const RE::TESForm* f){ return Value::I(static_cast<const RE::TYPE*>(f)->MEMBER); }, \
    +[](RE::TESForm* f, const Value& v){ static_cast<RE::TYPE*>(f)->MEMBER = static_cast<std::uint32_t>(v.num); } }

#define RF_U8(TYPE, NAME, MEMBER) Field{ NAME, Value::Kind::Int, \
    +[](const RE::TESForm* f){ return Value::I(static_cast<const RE::TYPE*>(f)->MEMBER); }, \
    +[](RE::TESForm* f, const Value& v){ static_cast<RE::TYPE*>(f)->MEMBER = static_cast<std::uint8_t>(std::clamp(static_cast<int>(v.num), 0, 255)); } }

#define RF_C3F(TYPE, NAME, MEMBER) Field{ NAME, Value::Kind::Color3, \
    +[](const RE::TESForm* f){ auto& c = static_cast<const RE::TYPE*>(f)->MEMBER; return Value::C3(c.red, c.green, c.blue); }, \
    +[](RE::TESForm* f, const Value& v){ auto& c = static_cast<RE::TYPE*>(f)->MEMBER; c.red = v.col[0]; c.green = v.col[1]; c.blue = v.col[2]; } }

#define RF_C3B(TYPE, NAME, MEMBER) Field{ NAME, Value::Kind::Color3, \
    +[](const RE::TESForm* f){ auto& c = static_cast<const RE::TYPE*>(f)->MEMBER; return Value::C3(c.red/255.f, c.green/255.f, c.blue/255.f); }, \
    +[](RE::TESForm* f, const Value& v){ auto& c = static_cast<RE::TYPE*>(f)->MEMBER; c.red = U8n(v.col[0]); c.green = U8n(v.col[1]); c.blue = U8n(v.col[2]); } }

    // ── Weather schema builder (indexed arrays -> generated fields) ───────
    static const char* kToD[4] = { "Sunrise", "Day", "Sunset", "Night" };
    static const char* kCT[17] = {
        "SkyUpper", "FogNear", "Unknown", "Ambient", "Sunlight", "Sun", "Stars",
        "SkyLower", "Horizon", "EffectLighting", "CloudLodDiffuse", "CloudLodAmbient",
        "FogFar", "SkyStatics", "WaterMultiplier", "SunGlare", "MoonGlare"
    };

    static void BuildWeatherFields(std::vector<Field>& F)
    {
        using W = RE::TESWeather;

        // Field factories over a captured member-reference selector.
        auto colorB = [&](std::string name, std::function<RE::Color&(W*)> ref) {
            F.push_back(Field{ std::move(name), Value::Kind::Color3,
                [ref](const RE::TESForm* f){ auto& c = ref(const_cast<W*>(static_cast<const W*>(f))); return Value::C3(c.red/255.f, c.green/255.f, c.blue/255.f); },
                [ref](RE::TESForm* f, const Value& v){ auto& c = ref(static_cast<W*>(f)); c.red = U8n(v.col[0]); c.green = U8n(v.col[1]); c.blue = U8n(v.col[2]); } });
        };
        auto fF = [&](std::string name, std::function<float&(W*)> ref) {
            F.push_back(Field{ std::move(name), Value::Kind::Float,
                [ref](const RE::TESForm* f){ return Value::F(ref(const_cast<W*>(static_cast<const W*>(f)))); },
                [ref](RE::TESForm* f, const Value& v){ ref(static_cast<W*>(f)) = static_cast<float>(v.num); } });
        };
        auto i8 = [&](std::string name, std::function<std::int8_t&(W*)> ref) {
            F.push_back(Field{ std::move(name), Value::Kind::Int,
                [ref](const RE::TESForm* f){ return Value::I(static_cast<std::uint8_t>(ref(const_cast<W*>(static_cast<const W*>(f))))); },
                [ref](RE::TESForm* f, const Value& v){ int iv = std::clamp(static_cast<int>(v.num), 0, 255); ref(static_cast<W*>(f)) = static_cast<std::int8_t>(static_cast<std::uint8_t>(iv)); } });
        };
        auto link = [&](std::string name, std::function<std::uint32_t(W*)> get, std::function<void(W*, std::uint32_t)> set) {
            F.push_back(Field{ std::move(name), Value::Kind::FormLink,
                [get](const RE::TESForm* f){ return Value::Link(get(const_cast<W*>(static_cast<const W*>(f)))); },
                [set](RE::TESForm* f, const Value& v){ set(static_cast<W*>(f), v.id); } });
        };

        // Weather colors (17 types x 4 times of day).
        for (int ct = 0; ct < 17; ++ct)
            for (int td = 0; td < 4; ++td) {
                int c = ct, t = td;
                colorB(std::string(kCT[ct]) + kToD[td], [c, t](W* w) -> RE::Color& { return w->colorData[c][t]; });
            }

        // Fog distances / power / max.
        fF("FogDayNear",    [](W* w) -> float& { return w->fogData.dayNear; });
        fF("FogDayFar",     [](W* w) -> float& { return w->fogData.dayFar; });
        fF("FogDayPower",   [](W* w) -> float& { return w->fogData.dayPower; });
        fF("FogDayMax",     [](W* w) -> float& { return w->fogData.dayMax; });
        fF("FogNightNear",  [](W* w) -> float& { return w->fogData.nightNear; });
        fF("FogNightFar",   [](W* w) -> float& { return w->fogData.nightFar; });
        fF("FogNightPower", [](W* w) -> float& { return w->fogData.nightPower; });
        fF("FogNightMax",   [](W* w) -> float& { return w->fogData.nightMax; });

        // Weather DATA (raw 0-255 bytes).
        i8("WindSpeed",          [](W* w) -> std::int8_t& { return w->data.windSpeed; });
        i8("TransDelta",         [](W* w) -> std::int8_t& { return w->data.transDelta; });
        i8("SunGlare",           [](W* w) -> std::int8_t& { return w->data.sunGlare; });
        i8("SunDamage",          [](W* w) -> std::int8_t& { return w->data.sunDamage; });
        i8("PrecipBeginFadeIn",  [](W* w) -> std::int8_t& { return w->data.precipitationBeginFadeIn; });
        i8("PrecipEndFadeOut",   [](W* w) -> std::int8_t& { return w->data.precipitationEndFadeOut; });
        i8("ThunderBeginFadeIn", [](W* w) -> std::int8_t& { return w->data.thunderLightningBeginFadeIn; });
        i8("ThunderEndFadeOut",  [](W* w) -> std::int8_t& { return w->data.thunderLightningEndFadeOut; });
        i8("ThunderFrequency",   [](W* w) -> std::int8_t& { return w->data.thunderLightningFrequency; });
        i8("VisualEffectBegin",  [](W* w) -> std::int8_t& { return w->data.visualEffectBegin; });
        i8("VisualEffectEnd",    [](W* w) -> std::int8_t& { return w->data.visualEffectEnd; });
        i8("WindDirection",      [](W* w) -> std::int8_t& { return w->data.windDirection; });
        i8("WindDirectionRange", [](W* w) -> std::int8_t& { return w->data.windDirectionRange; });

        // Flags + lightning colour.
        F.push_back(Field{ "Flags", Value::Kind::Int,
            [](const RE::TESForm* f){ return Value::I(static_cast<const W*>(f)->data.flags.underlying()); },
            [](RE::TESForm* f, const Value& v){ static_cast<W*>(f)->data.flags = static_cast<W::WeatherDataFlag>(static_cast<std::uint8_t>(static_cast<int>(v.num))); } });
        F.push_back(Field{ "LightningColor", Value::Kind::Color3,
            [](const RE::TESForm* f){ auto& c = static_cast<const W*>(f)->data.lightningColor; return Value::C3(static_cast<std::uint8_t>(c.red)/255.f, static_cast<std::uint8_t>(c.green)/255.f, static_cast<std::uint8_t>(c.blue)/255.f); },
            [](RE::TESForm* f, const Value& v){ auto& c = static_cast<W*>(f)->data.lightningColor; c.red = static_cast<std::int8_t>(U8n(v.col[0])); c.green = static_cast<std::int8_t>(U8n(v.col[1])); c.blue = static_cast<std::int8_t>(U8n(v.col[2])); } });

        // Directional ambient (full 6-way cube + specular + fresnel, per ToD).
        for (int td = 0; td < 4; ++td) {
            int t = td;
            std::string p = std::string("DirAmb") + kToD[td];
            colorB(p + "XMax", [t](W* w) -> RE::Color& { return w->directionalAmbientLightingColors[t].directional.x.max; });
            colorB(p + "XMin", [t](W* w) -> RE::Color& { return w->directionalAmbientLightingColors[t].directional.x.min; });
            colorB(p + "YMax", [t](W* w) -> RE::Color& { return w->directionalAmbientLightingColors[t].directional.y.max; });
            colorB(p + "YMin", [t](W* w) -> RE::Color& { return w->directionalAmbientLightingColors[t].directional.y.min; });
            colorB(p + "ZMax", [t](W* w) -> RE::Color& { return w->directionalAmbientLightingColors[t].directional.z.max; });
            colorB(p + "ZMin", [t](W* w) -> RE::Color& { return w->directionalAmbientLightingColors[t].directional.z.min; });
            colorB(p + "Specular", [t](W* w) -> RE::Color& { return w->directionalAmbientLightingColors[t].specular; });
            fF(p + "Fresnel", [t](W* w) -> float& { return w->directionalAmbientLightingColors[t].fresnelPower; });
        }

        // Cloud layers (all 32: speed, enabled, per-ToD colour + alpha).
        for (int i = 0; i < 32; ++i) {
            int L = i;
            std::string p = "Cloud" + std::to_string(i);
            i8(p + "SpeedX", [L](W* w) -> std::int8_t& { return w->cloudLayerSpeedX[L]; });
            i8(p + "SpeedY", [L](W* w) -> std::int8_t& { return w->cloudLayerSpeedY[L]; });
            F.push_back(Field{ p + "Enabled", Value::Kind::Bool,
                [L](const RE::TESForm* f){ return Value::B((static_cast<const W*>(f)->cloudLayerDisabledBits & (1u << L)) == 0); },
                [L](RE::TESForm* f, const Value& v){ auto* w = static_cast<W*>(f); if (v.num != 0) w->cloudLayerDisabledBits &= ~(1u << L); else w->cloudLayerDisabledBits |= (1u << L); } });
            for (int td = 0; td < 4; ++td) {
                int t = td;
                colorB(p + "Color" + kToD[td], [L, t](W* w) -> RE::Color& { return w->cloudColorData[L][t]; });
                fF(p + "Alpha" + kToD[td], [L, t](W* w) -> float& { return w->cloudAlpha[L][t]; });
            }
        }
        F.push_back(Field{ "NumCloudLayers", Value::Kind::Int,
            [](const RE::TESForm* f){ return Value::I(static_cast<const W*>(f)->numCloudLayers); },
            [](RE::TESForm* f, const Value& v){ static_cast<W*>(f)->numCloudLayers = static_cast<std::uint32_t>(static_cast<int>(v.num)); } });

        // Form links (per-ToD image space + volumetric, plus lens flare / precip / effect).
        for (int td = 0; td < 4; ++td) {
            int t = td;
            link(std::string("ImageSpace") + kToD[td],
                [t](W* w){ return w->imageSpaces[t] ? w->imageSpaces[t]->GetFormID() : 0u; },
                [t](W* w, std::uint32_t id){ if (!id) { w->imageSpaces[t] = nullptr; return; } if (auto* p = RE::TESForm::LookupByID<RE::TESImageSpace>(id)) w->imageSpaces[t] = p; });
            link(std::string("Volumetric") + kToD[td],
                [t](W* w){ return w->volumetricLighting[t] ? w->volumetricLighting[t]->GetFormID() : 0u; },
                [t](W* w, std::uint32_t id){ if (!id) { w->volumetricLighting[t] = nullptr; return; } if (auto* p = RE::TESForm::LookupByID<RE::BGSVolumetricLighting>(id)) w->volumetricLighting[t] = p; });
        }
        link("SunGlareLensFlare",
            [](W* w){ return w->sunGlareLensFlare ? w->sunGlareLensFlare->GetFormID() : 0u; },
            [](W* w, std::uint32_t id){ if (!id) { w->sunGlareLensFlare = nullptr; return; } if (auto* p = RE::TESForm::LookupByID<RE::BGSLensFlare>(id)) w->sunGlareLensFlare = p; });
        link("Precipitation",
            [](W* w){ return w->precipitationData ? w->precipitationData->GetFormID() : 0u; },
            [](W* w, std::uint32_t id){ if (!id) { w->precipitationData = nullptr; return; } if (auto* p = RE::TESForm::LookupByID<RE::BGSShaderParticleGeometryData>(id)) w->precipitationData = p; });
        link("ReferenceEffect",
            [](W* w){ return w->referenceEffect ? w->referenceEffect->GetFormID() : 0u; },
            [](W* w, std::uint32_t id){ if (!id) { w->referenceEffect = nullptr; return; } if (auto* p = RE::TESForm::LookupByID<RE::BGSReferenceEffect>(id)) w->referenceEffect = p; });
    }

    void RegisterBuiltins()
    {
        static bool done = false;
        if (done) return;
        done = true;
        auto& R = Registry();

        // ImageSpace (HDR / cinematic / tint / DoF grading).
        R.push_back(Schema{ "ImageSpace", RE::TESImageSpace::FORMTYPE, {
            RF_F(TESImageSpace, "EyeAdaptSpeed", data.hdr.eyeAdaptSpeed),
            RF_F(TESImageSpace, "BloomBlurRadius", data.hdr.bloomBlurRadius),
            RF_F(TESImageSpace, "BloomThreshold", data.hdr.bloomThreshold),
            RF_F(TESImageSpace, "BloomScale", data.hdr.bloomScale),
            RF_F(TESImageSpace, "ReceiveBloomThreshold", data.hdr.receiveBloomThreshold),
            RF_F(TESImageSpace, "White", data.hdr.white),
            RF_F(TESImageSpace, "SunlightScale", data.hdr.sunlightScale),
            RF_F(TESImageSpace, "SkyScale", data.hdr.skyScale),
            RF_F(TESImageSpace, "EyeAdaptStrength", data.hdr.eyeAdaptStrength),
            RF_F(TESImageSpace, "Saturation", data.cinematic.saturation),
            RF_F(TESImageSpace, "Brightness", data.cinematic.brightness),
            RF_F(TESImageSpace, "Contrast", data.cinematic.contrast),
            RF_F(TESImageSpace, "TintAmount", data.tint.amount),
            RF_C3F(TESImageSpace, "Tint", data.tint.color),
            RF_F(TESImageSpace, "DofStrength", data.depthOfField.strength),
            RF_F(TESImageSpace, "DofDistance", data.depthOfField.distance),
            RF_F(TESImageSpace, "DofRange", data.depthOfField.range),
        }});

        // Volumetric lighting.
        R.push_back(Schema{ "Volumetric", RE::BGSVolumetricLighting::FORMTYPE, {
            RF_F(BGSVolumetricLighting, "Intensity", intensity),
            RF_F(BGSVolumetricLighting, "CustomColorContrib", customColor.contribution),
            RF_F(BGSVolumetricLighting, "ColorR", red),
            RF_F(BGSVolumetricLighting, "ColorG", green),
            RF_F(BGSVolumetricLighting, "ColorB", blue),
            RF_F(BGSVolumetricLighting, "Density", density.contribution),
            RF_F(BGSVolumetricLighting, "Size", density.size),
            RF_F(BGSVolumetricLighting, "WindSpeed", density.windSpeed),
            RF_F(BGSVolumetricLighting, "FallingSpeed", density.fallingSpeed),
            RF_F(BGSVolumetricLighting, "Phase", phaseFunction.contribution),
            RF_F(BGSVolumetricLighting, "Scattering", phaseFunction.scattering),
            RF_F(BGSVolumetricLighting, "RangeFactor", samplingRepartition.rangeFactor),
        }});

        // Lighting template (INTERIOR_DATA).
        R.push_back(Schema{ "LightingTemplate", RE::BGSLightingTemplate::FORMTYPE, {
            RF_C3B(BGSLightingTemplate, "Ambient", data.ambient),
            RF_C3B(BGSLightingTemplate, "Directional", data.directional),
            RF_C3B(BGSLightingTemplate, "FogColorNear", data.fogColorNear),
            RF_C3B(BGSLightingTemplate, "FogColorFar", data.fogColorFar),
            RF_U32(BGSLightingTemplate, "DirectionalRotXY", data.directionalXY),
            RF_U32(BGSLightingTemplate, "DirectionalRotZ", data.directionalZ),
            RF_F(BGSLightingTemplate, "DirectionalFade", data.directionalFade),
            RF_F(BGSLightingTemplate, "FogNear", data.fogNear),
            RF_F(BGSLightingTemplate, "FogFar", data.fogFar),
            RF_F(BGSLightingTemplate, "FogClipDist", data.clipDist),
            RF_F(BGSLightingTemplate, "FogPower", data.fogPower),
            RF_F(BGSLightingTemplate, "FogMax", data.fogClamp),
            RF_F(BGSLightingTemplate, "LightFadeStart", data.lightFadeStart),
            RF_F(BGSLightingTemplate, "LightFadeEnd", data.lightFadeEnd),
        }});

        // Weather (the full record: colors, fog, clouds, dir-ambient, data,
        // form links) built programmatically over its indexed arrays.
        {
            Schema weather{ "Weather", RE::TESWeather::FORMTYPE, {} };
            BuildWeatherFields(weather.fields);
            SKSE::log::info("EngineReflect: Weather schema has {} fields", weather.fields.size());
            R.push_back(std::move(weather));
        }

        // Climate: sunrise/sunset timing, volatility, moon-phase, sky textures.
        R.push_back(Schema{ "Climate", RE::TESClimate::FORMTYPE, {
            RF_U8(TESClimate, "SunriseBegin", timing.sunrise.begin),   // 10-min intervals (hour = /6)
            RF_U8(TESClimate, "SunriseEnd",   timing.sunrise.end),
            RF_U8(TESClimate, "SunsetBegin",  timing.sunset.begin),
            RF_U8(TESClimate, "SunsetEnd",    timing.sunset.end),
            RF_U8(TESClimate, "Volatility",   timing.volatility),
            Field{ "MoonPhaseLength", Value::Kind::Int,
                [](const RE::TESForm* f){ return Value::I(static_cast<const RE::TESClimate*>(f)->timing.moonPhaseLength.underlying()); },
                [](RE::TESForm* f, const Value& v){ static_cast<RE::TESClimate*>(f)->timing.moonPhaseLength = static_cast<RE::TESClimate::Timing::MoonPhaseLength>(static_cast<std::uint8_t>(static_cast<int>(v.num))); } },
            Field{ "SunTexture", Value::Kind::String,
                [](const RE::TESForm* f){ return Value::S(static_cast<const RE::TESClimate*>(f)->skyObjects[RE::TESClimate::SkyObjects::kSun].textureName.c_str()); },
                [](RE::TESForm* f, const Value& v){ static_cast<RE::TESClimate*>(f)->skyObjects[RE::TESClimate::SkyObjects::kSun].textureName = v.str.c_str(); } },
            Field{ "SunGlareTexture", Value::Kind::String,
                [](const RE::TESForm* f){ return Value::S(static_cast<const RE::TESClimate*>(f)->skyObjects[RE::TESClimate::SkyObjects::kSunGlare].textureName.c_str()); },
                [](RE::TESForm* f, const Value& v){ static_cast<RE::TESClimate*>(f)->skyObjects[RE::TESClimate::SkyObjects::kSunGlare].textureName = v.str.c_str(); } },
        }});

        // Region: worldspace + current weather links + emittance colour.
        // (The rich weather-list/map/object data lives in polymorphic
        // TESRegionData subrecords, out of scope for a flat schema.)
        R.push_back(Schema{ "Region", RE::TESRegion::FORMTYPE, {
            RF_C3F(TESRegion, "EmittanceColor", emittanceColor),
            Field{ "WorldSpace", Value::Kind::FormLink,
                [](const RE::TESForm* f){ auto* r = static_cast<const RE::TESRegion*>(f); return Value::Link(r->worldSpace ? r->worldSpace->GetFormID() : 0u); },
                [](RE::TESForm* f, const Value& v){ auto* r = static_cast<RE::TESRegion*>(f); if (!v.id) { r->worldSpace = nullptr; return; } if (auto* p = RE::TESForm::LookupByID<RE::TESWorldSpace>(v.id)) r->worldSpace = p; } },
            Field{ "CurrentWeather", Value::Kind::FormLink,
                [](const RE::TESForm* f){ auto* r = static_cast<const RE::TESRegion*>(f); return Value::Link(r->currentWeather ? r->currentWeather->GetFormID() : 0u); },
                [](RE::TESForm* f, const Value& v){ auto* r = static_cast<RE::TESRegion*>(f); if (!v.id) { r->currentWeather = nullptr; return; } if (auto* p = RE::TESForm::LookupByID<RE::TESWeather>(v.id)) r->currentWeather = p; } },
        }});

        SKSE::log::info("EngineReflect: {} schemas registered", R.size());
    }

    // ── Core ops ─────────────────────────────────────────────────────────
    Tree Read(RE::TESForm* form)
    {
        Tree t;
        if (!form) return t;
        auto* s = SchemaFor(form->GetFormType());
        if (!s) return t;
        for (auto& f : s->fields) t.emplace_back(f.name, f.get(form));
        return t;
    }

    int Write(RE::TESForm* form, const Tree& tree)
    {
        if (!form) return 0;
        auto* s = SchemaFor(form->GetFormType());
        if (!s) return 0;
        int n = 0;
        for (auto& [name, val] : tree)
            if (auto* f = s->Find(name)) { f->set(form, val); ++n; }
        return n;
    }

    std::string ToINI(const Schema& s, RE::FormID id, const Tree& t)
    {
        std::ostringstream o;
        o << "[" << s.name << ":0x" << std::hex << std::uppercase << id << "]\n";
        for (auto& [name, v] : t) o << name << " = " << v.ToText() << "\n";
        return o.str();
    }

    Tree FromINI(const Schema& s, const std::string& text)
    {
        Tree t;
        auto doc = Cfg::Parse(text);
        for (auto& sec : doc.sections) {
            if (sec.Base() != s.name) continue;
            for (auto& [k, v] : sec.entries)
                if (auto* f = s.Find(k)) t.emplace_back(k, Value::FromText(f->kind, v));
        }
        return t;
    }

    // ── High level ───────────────────────────────────────────────────────
    std::string Dump(RE::FormID id)
    {
        auto* form = RE::TESForm::LookupByID(id);
        if (!form) return {};
        auto* s = SchemaFor(form->GetFormType());
        if (!s) return {};
        return ToINI(*s, id, Read(form));
    }

    int Apply(RE::FormID id, const std::string& text)
    {
        auto* form = RE::TESForm::LookupByID(id);
        if (!form) return 0;
        auto* s = SchemaFor(form->GetFormType());
        if (!s) return 0;
        return Write(form, FromINI(*s, text));
    }

    VerifyResult Verify(RE::FormID id)
    {
        VerifyResult r;
        auto* form = RE::TESForm::LookupByID(id);
        if (!form) { r.detail = "form not found"; return r; }
        auto* s = SchemaFor(form->GetFormType());
        if (!s) { r.detail = "no schema for this form type"; return r; }

        Tree a = Read(form);
        std::string ini = ToINI(*s, id, a);
        Tree b = FromINI(*s, ini);
        r.fields = static_cast<int>(a.size());
        if (a.size() != b.size()) { r.detail = "field count mismatch on round-trip"; return r; }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i].first != b[i].first || !a[i].second.ApproxEquals(b[i].second)) {
                r.detail = "round-trip mismatch at field '" + a[i].first + "'";
                return r;
            }
        }
        r.ok = true;
        return r;
    }
}
