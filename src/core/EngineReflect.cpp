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

// Generic signed/unsigned integer member of C type CTYPE (int16/int32/uint16...).
#define RF_INT(TYPE, NAME, MEMBER, CTYPE) Field{ NAME, Value::Kind::Int, \
    +[](const RE::TESForm* f){ return Value::I(static_cast<long long>(static_cast<const RE::TYPE*>(f)->MEMBER)); }, \
    +[](RE::TESForm* f, const Value& v){ static_cast<RE::TYPE*>(f)->MEMBER = static_cast<CTYPE>(static_cast<long long>(v.num)); } }

#define RF_B(TYPE, NAME, MEMBER) Field{ NAME, Value::Kind::Bool, \
    +[](const RE::TESForm* f){ return Value::B(static_cast<const RE::TYPE*>(f)->MEMBER); }, \
    +[](RE::TESForm* f, const Value& v){ static_cast<RE::TYPE*>(f)->MEMBER = (v.num != 0); } }

// stl::enumeration flag/enum member: raw underlying value as Int.
#define RF_FLAGS(TYPE, NAME, MEMBER, ENUMT) Field{ NAME, Value::Kind::Int, \
    +[](const RE::TESForm* f){ return Value::I(static_cast<long long>(static_cast<const RE::TYPE*>(f)->MEMBER.underlying())); }, \
    +[](RE::TESForm* f, const Value& v){ static_cast<RE::TYPE*>(f)->MEMBER = static_cast<ENUMT>(static_cast<long long>(v.num)); } }

// BSFixedString member (e.g. TESTexture::textureName).
#define RF_S(TYPE, NAME, MEMBER) Field{ NAME, Value::Kind::String, \
    +[](const RE::TESForm* f){ return Value::S(static_cast<const RE::TYPE*>(f)->MEMBER.c_str()); }, \
    +[](RE::TESForm* f, const Value& v){ static_cast<RE::TYPE*>(f)->MEMBER = v.str.c_str(); } }

// Form-pointer member <-> FormID (0 clears; a bad ID is a no-op, never a wild write).
#define RF_LINK(TYPE, NAME, MEMBER, TARGET) Field{ NAME, Value::Kind::FormLink, \
    +[](const RE::TESForm* f){ auto* p = static_cast<const RE::TYPE*>(f)->MEMBER; return Value::Link(p ? p->GetFormID() : 0u); }, \
    +[](RE::TESForm* f, const Value& v){ auto* t = static_cast<RE::TYPE*>(f); if (!v.id) { t->MEMBER = nullptr; return; } \
        if (auto* p = RE::TESForm::LookupByID<RE::TARGET>(v.id)) t->MEMBER = p; } }

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

        // Light (TESObjectLIGH): DATA + FNAM fade + emittance + links.
        R.push_back(Schema{ "Light", RE::TESObjectLIGH::FORMTYPE, {
            RF_INT(TESObjectLIGH, "Time", data.time, std::int32_t),
            RF_INT(TESObjectLIGH, "Radius", data.radius, std::uint32_t),
            RF_C3B(TESObjectLIGH, "Color", data.color),
            RF_FLAGS(TESObjectLIGH, "Flags", data.flags, RE::TES_LIGHT_FLAGS),
            RF_F(TESObjectLIGH, "FalloffExponent", data.fallofExponent),
            RF_F(TESObjectLIGH, "FOV", data.fov),
            RF_F(TESObjectLIGH, "NearDistance", data.nearDistance),
            RF_F(TESObjectLIGH, "FlickerPeriodRecip", data.flickerPeriodRecip),
            RF_F(TESObjectLIGH, "FlickerIntensityAmplitude", data.flickerIntensityAmplitude),
            RF_F(TESObjectLIGH, "FlickerMovementAmplitude", data.flickerMovementAmplitude),
            RF_F(TESObjectLIGH, "Fade", fade),
            RF_C3F(TESObjectLIGH, "EmittanceColor", emittanceColor),
            RF_LINK(TESObjectLIGH, "Sound", sound, BGSSoundDescriptorForm),
            RF_LINK(TESObjectLIGH, "LensFlare", lensFlare, BGSLensFlare),
        }});

        // Water (TESWaterForm): the full DNAM shader block + noise/velocity +
        // links. Unnamed (unkXX) members are excluded, not guessed.
        R.push_back(Schema{ "Water", RE::TESWaterForm::FORMTYPE, {
            RF_INT(TESWaterForm, "Alpha", alpha, std::int8_t),
            RF_FLAGS(TESWaterForm, "Flags", flags, RE::TESWaterForm::Flag),
            RF_F(TESWaterForm, "SunSpecularPower", data.sunSpecularPower),
            RF_F(TESWaterForm, "ReflectionAmount", data.reflectionAmount),
            RF_F(TESWaterForm, "FresnelAmount", data.fresnelAmount),
            RF_F(TESWaterForm, "AboveWaterFogDistNear", data.aboveWaterFogDistNear),
            RF_F(TESWaterForm, "AboveWaterFogDistFar", data.aboveWaterFogDistFar),
            RF_C3B(TESWaterForm, "ShallowWaterColor", data.shallowWaterColor),
            RF_C3B(TESWaterForm, "DeepWaterColor", data.deepWaterColor),
            RF_C3B(TESWaterForm, "ReflectionWaterColor", data.reflectionWaterColor),
            RF_F(TESWaterForm, "DisplacementSize", data.displacementSize),
            RF_F(TESWaterForm, "DisplacementForce", data.displacementForce),
            RF_F(TESWaterForm, "DisplacementVelocity", data.displacementVelocity),
            RF_F(TESWaterForm, "DisplacementFalloff", data.displacementFalloff),
            RF_F(TESWaterForm, "DisplacementDampener", data.displacementDampener),
            RF_F(TESWaterForm, "NoiseFalloff", data.noiseFalloff),
            RF_F(TESWaterForm, "NoiseWindDirection0", data.noiseWindDirectionA[0]),
            RF_F(TESWaterForm, "NoiseWindDirection1", data.noiseWindDirectionA[1]),
            RF_F(TESWaterForm, "NoiseWindDirection2", data.noiseWindDirectionA[2]),
            RF_F(TESWaterForm, "NoiseWindSpeed0", data.noiseWindSpeedA[0]),
            RF_F(TESWaterForm, "NoiseWindSpeed1", data.noiseWindSpeedA[1]),
            RF_F(TESWaterForm, "NoiseWindSpeed2", data.noiseWindSpeedA[2]),
            RF_F(TESWaterForm, "AboveWaterFogAmount", data.aboveWaterFogAmount),
            RF_F(TESWaterForm, "UnderwaterFogAmount", data.underwaterFogAmount),
            RF_F(TESWaterForm, "UnderwaterFogDistNear", data.underwaterFogDistNear),
            RF_F(TESWaterForm, "UnderwaterFogDistFar", data.underwaterFogDistFar),
            RF_F(TESWaterForm, "RefractionMagnitude", data.refractionMagnitude),
            RF_F(TESWaterForm, "SpecularPower", data.specularPower),
            RF_F(TESWaterForm, "SpecularRadius", data.specularRadius),
            RF_F(TESWaterForm, "SpecularBrightness", data.specularBrightness),
            RF_F(TESWaterForm, "UVScale0", data.uvScaleA[0]),
            RF_F(TESWaterForm, "UVScale1", data.uvScaleA[1]),
            RF_F(TESWaterForm, "UVScale2", data.uvScaleA[2]),
            RF_F(TESWaterForm, "AmplitudeScale0", data.amplitudeA[0]),
            RF_F(TESWaterForm, "AmplitudeScale1", data.amplitudeA[1]),
            RF_F(TESWaterForm, "AmplitudeScale2", data.amplitudeA[2]),
            RF_F(TESWaterForm, "ReflectionMagnitude", data.reflectionMagnitude),
            RF_F(TESWaterForm, "SunSparkleMagnitude", data.sunSparkleMagnitude),
            RF_F(TESWaterForm, "SunSpecularMagnitude", data.sunSpecularMagnitude),
            RF_F(TESWaterForm, "DepthReflections", data.depthProperties.reflections),
            RF_F(TESWaterForm, "DepthRefraction", data.depthProperties.refraction),
            RF_F(TESWaterForm, "DepthNormals", data.depthProperties.normals),
            RF_F(TESWaterForm, "DepthSpecularLighting", data.depthProperties.specularLighting),
            RF_F(TESWaterForm, "SunSparklePower", data.sunSparklePower),
            RF_F(TESWaterForm, "FlowmapScale", data.flowmapScale),
            RF_INT(TESWaterForm, "FrequencyX", frequencyX, std::uint32_t),
            RF_INT(TESWaterForm, "FrequencyY", frequencyY, std::uint32_t),
            RF_INT(TESWaterForm, "Octaves", octaves, std::int32_t),
            RF_F(TESWaterForm, "NoiseAmplitude", amplitude),
            RF_F(TESWaterForm, "NoiseLacunarity", lacunarity),
            RF_F(TESWaterForm, "NoiseBias", bias),
            RF_F(TESWaterForm, "NoiseGain", gain),
            RF_F(TESWaterForm, "LinearVelocityX", linearVelocity.x),
            RF_F(TESWaterForm, "LinearVelocityY", linearVelocity.y),
            RF_F(TESWaterForm, "LinearVelocityZ", linearVelocity.z),
            RF_F(TESWaterForm, "AngularVelocityX", angularVelocity.x),
            RF_F(TESWaterForm, "AngularVelocityY", angularVelocity.y),
            RF_F(TESWaterForm, "AngularVelocityZ", angularVelocity.z),
            RF_S(TESWaterForm, "NoiseTexture0", noiseTextures[0].textureName),
            RF_S(TESWaterForm, "NoiseTexture1", noiseTextures[1].textureName),
            RF_S(TESWaterForm, "NoiseTexture2", noiseTextures[2].textureName),
            RF_S(TESWaterForm, "NoiseTexture3", noiseTextures[3].textureName),
            RF_LINK(TESWaterForm, "MaterialType", materialType, BGSMaterialType),
            RF_LINK(TESWaterForm, "WaterSound", waterSound, BGSSoundDescriptorForm),
            RF_LINK(TESWaterForm, "ContactSpell", contactSpell, SpellItem),
            RF_LINK(TESWaterForm, "ImageSpace", imageSpace, TESImageSpace),
        }});

        // EffectShader: fill / edge / particle / color keys / holes / addon
        // models + texture paths and links. Honest exclusion: the membrane and
        // particle blend-mode members (D3DBLEND / D3DBLENDOP) are only
        // forward-declared in CommonLib, so they cannot be reflected.
        R.push_back(Schema{ "EffectShader", RE::TESEffectShader::FORMTYPE, {
            RF_C3B(TESEffectShader, "FillColorKey1", data.fillTextureEffectColorKey1),
            RF_C3B(TESEffectShader, "FillColorKey2", data.fillTextureEffectColorKey2),
            RF_C3B(TESEffectShader, "FillColorKey3", data.fillTextureEffectColorKey3),
            RF_F(TESEffectShader, "FillAlphaFadeInTime", data.fillTextureEffectAlphaFadeInTime),
            RF_F(TESEffectShader, "FillFullAlphaTime", data.fillTextureEffectFullAlphaTime),
            RF_F(TESEffectShader, "FillAlphaFadeOutTime", data.fillTextureEffectAlphaFadeOutTime),
            RF_F(TESEffectShader, "FillPersistentAlphaRatio", data.fillTextureEffectPersistentAlphaRatio),
            RF_F(TESEffectShader, "FillAlphaPulseAmplitude", data.fillTextureEffectAlphaPulseAmplitude),
            RF_F(TESEffectShader, "FillAlphaPulseFrequency", data.fillTextureEffectAlphaPulseFrequency),
            RF_F(TESEffectShader, "FillAnimSpeedU", data.fillTextureEffectTextureAnimationSpeedU),
            RF_F(TESEffectShader, "FillAnimSpeedV", data.fillTextureEffectTextureAnimationSpeedV),
            RF_F(TESEffectShader, "FillFullAlphaRatio", data.fillTextureEffectFullAlphaRatio),
            RF_F(TESEffectShader, "FillColorKey1Scale", data.fillTextureEffectColorKeyScaleTimeColorKey1Scale),
            RF_F(TESEffectShader, "FillColorKey2Scale", data.fillTextureEffectColorKeyScaleTimeColorKey2Scale),
            RF_F(TESEffectShader, "FillColorKey3Scale", data.fillTextureEffectColorKeyScaleTimeColorKey3Scale),
            RF_F(TESEffectShader, "FillColorKey1Time", data.fillTextureEffectColorKeyScaleTimeColorKey1Time),
            RF_F(TESEffectShader, "FillColorKey2Time", data.fillTextureEffectColorKeyScaleTimeColorKey2Time),
            RF_F(TESEffectShader, "FillColorKey3Time", data.fillTextureEffectColorKeyScaleTimeColorKey3Time),
            RF_F(TESEffectShader, "FillTextureScaleU", data.fillTextureEffectTextureScaleU),
            RF_F(TESEffectShader, "FillTextureScaleV", data.fillTextureEffectTextureScaleV),
            RF_F(TESEffectShader, "EdgeFallOff", data.edgeEffectFallOff),
            RF_C3B(TESEffectShader, "EdgeEffectColor", data.edgeEffectColor),
            RF_F(TESEffectShader, "EdgeAlphaFadeInTime", data.edgeEffectAlphaFadeInTime),
            RF_F(TESEffectShader, "EdgeFullAlphaTime", data.edgeEffectFullAlphaTime),
            RF_F(TESEffectShader, "EdgeAlphaFadeOutTime", data.edgeEffectAlphaFadeOutTime),
            RF_F(TESEffectShader, "EdgePersistentAlphaRatio", data.edgeEffectPersistentAlphaRatio),
            RF_F(TESEffectShader, "EdgeAlphaPulseAmplitude", data.edgeEffectAlphaPulseAmplitude),
            RF_F(TESEffectShader, "EdgeAlphaPulseFrequency", data.edgeEffectAlphaPulseFrequency),
            RF_F(TESEffectShader, "EdgeFullAlphaRatio", data.edgeEffectFullAlphaRatio),
            RF_F(TESEffectShader, "EdgeWidthAlphaUnits", data.edgeWidthAlphaUnits),
            RF_C3B(TESEffectShader, "EdgeColor", data.edgeColor),
            RF_F(TESEffectShader, "ParticleBirthRampUpTime", data.particleShaderParticleBirthRampUpTime),
            RF_F(TESEffectShader, "ParticleFullBirthTime", data.particleShaderFullParticleBirthTime),
            RF_F(TESEffectShader, "ParticleBirthRampDownTime", data.particleShaderParticleBirthRampDownTime),
            RF_F(TESEffectShader, "ParticleFullBirthRatio", data.particleShaderFullParticleBirthRatio),
            RF_F(TESEffectShader, "ParticlePersistentCount", data.particleShaderPersistantParticleCount),
            RF_F(TESEffectShader, "ParticleLifetime", data.particleShaderParticleLifetime),
            RF_F(TESEffectShader, "ParticleLifetimeVariance", data.particleShaderParticleLifetimeVariance),
            RF_F(TESEffectShader, "ParticleSpeedAlongNormal", data.particleShaderInitialSpeedAlongNormal),
            RF_F(TESEffectShader, "ParticleAccelAlongNormal", data.particleShaderAccelerationAlongNormal),
            RF_F(TESEffectShader, "ParticleInitialVelocity1", data.particleShaderInitialVelocity1),
            RF_F(TESEffectShader, "ParticleInitialVelocity2", data.particleShaderInitialVelocity2),
            RF_F(TESEffectShader, "ParticleInitialVelocity3", data.particleShaderInitialVelocity3),
            RF_F(TESEffectShader, "ParticleAcceleration1", data.particleShaderAcceleration1),
            RF_F(TESEffectShader, "ParticleAcceleration2", data.particleShaderAcceleration2),
            RF_F(TESEffectShader, "ParticleAcceleration3", data.particleShaderAcceleration3),
            RF_F(TESEffectShader, "ParticleScaleKey1", data.particleShaderScaleKey1),
            RF_F(TESEffectShader, "ParticleScaleKey2", data.particleShaderScaleKey2),
            RF_F(TESEffectShader, "ParticleScaleKey1Time", data.particleShaderScaleKey1Time),
            RF_F(TESEffectShader, "ParticleScaleKey2Time", data.particleShaderScaleKey2Time),
            RF_F(TESEffectShader, "ParticleSpeedNormalVariance", data.particleShaderInitialSpeedAlongNormalVariance),
            RF_F(TESEffectShader, "ParticleInitialRotation", data.particleShaderInitialRotation),
            RF_F(TESEffectShader, "ParticleRotationVariance", data.particleShaderInitialRotationVariance),
            RF_F(TESEffectShader, "ParticleRotationSpeed", data.particleShaderRotationSpeed),
            RF_F(TESEffectShader, "ParticleRotationSpeedVariance", data.particleShaderRotationSpeedVariance),
            RF_F(TESEffectShader, "ParticleAnimStartFrame", data.particleShaderAnimatedStartFrame),
            RF_F(TESEffectShader, "ParticleAnimStartFrameVariance", data.particleShaderAnimatedStartFrameVariance),
            RF_F(TESEffectShader, "ParticleAnimEndFrame", data.particleShaderAnimatedEndFrame),
            RF_F(TESEffectShader, "ParticleAnimLoopStartFrame", data.particleShaderAnimatedLoopStartFrame),
            RF_F(TESEffectShader, "ParticleAnimLoopStartVariance", data.particleShaderAnimatedLoopStartVariance),
            RF_F(TESEffectShader, "ParticleAnimFrameCount", data.particleShaderAnimatedFrameCount),
            RF_F(TESEffectShader, "ParticleAnimFrameCountVariance", data.particleShaderAnimatedFrameCountVariance),
            RF_C3B(TESEffectShader, "ColorKey1", data.colorKey1),
            RF_C3B(TESEffectShader, "ColorKey2", data.colorKey2),
            RF_C3B(TESEffectShader, "ColorKey3", data.colorKey3),
            RF_F(TESEffectShader, "ColorKey1Alpha", data.colorKey1ColorAlpha),
            RF_F(TESEffectShader, "ColorKey2Alpha", data.colorKey2ColorAlpha),
            RF_F(TESEffectShader, "ColorKey3Alpha", data.colorKey3ColorAlpha),
            RF_F(TESEffectShader, "ColorKey1Time", data.colorKey1ColorKeyTime),
            RF_F(TESEffectShader, "ColorKey2Time", data.colorKey2ColorKeyTime),
            RF_F(TESEffectShader, "ColorKey3Time", data.colorKey3ColorKeyTime),
            RF_F(TESEffectShader, "HolesStartTime", data.holesStartTime),
            RF_F(TESEffectShader, "HolesEndTime", data.holesEndTime),
            RF_F(TESEffectShader, "HolesStartVal", data.holesStartVal),
            RF_F(TESEffectShader, "HolesEndVal", data.holesEndVal),
            RF_F(TESEffectShader, "ExplosionWindSpeed", data.explosionWindSpeed),
            RF_F(TESEffectShader, "TextureCountU", data.textureCountU),
            RF_F(TESEffectShader, "TextureCountV", data.textureCountV),
            RF_F(TESEffectShader, "AddonFadeInTime", data.addonModelsFadeInTime),
            RF_F(TESEffectShader, "AddonFadeOutTime", data.addonModelsFadeOutTime),
            RF_F(TESEffectShader, "AddonScaleStart", data.addonModelsScaleStart),
            RF_F(TESEffectShader, "AddonScaleEnd", data.addonModelsScaleEnd),
            RF_F(TESEffectShader, "AddonScaleInTime", data.addonModelsScaleInTime),
            RF_F(TESEffectShader, "AddonScaleOutTime", data.addonModelsScaleOutTime),
            RF_F(TESEffectShader, "ColorScale", data.colorScale),
            RF_F(TESEffectShader, "BirthPositionOffset", data.birthPositionOffset),
            RF_F(TESEffectShader, "BirthPositionOffsetVariance", data.birthPositionOffsetVariance),
            RF_FLAGS(TESEffectShader, "Flags", data.flags, RE::EffectShaderData::Flags),
            RF_LINK(TESEffectShader, "AddonModels", data.addonModels, BGSDebris),
            RF_LINK(TESEffectShader, "AmbientSound", data.ambientSound, BGSSoundDescriptorForm),
            RF_S(TESEffectShader, "FillTexture", fillTexture.textureName),
            RF_S(TESEffectShader, "ParticleTexture", particleShaderTexture.textureName),
            RF_S(TESEffectShader, "HolesTexture", holesTexture.textureName),
            RF_S(TESEffectShader, "MembranePalette", membranePaletteTexture.textureName),
            RF_S(TESEffectShader, "ParticlePalette", particlePaletteTexture.textureName),
        }});

        // ImageSpaceModifier: the DNAM value block. The float mult/add pairs
        // are first-class; the fields CommonLib types as raw uint32 (tint,
        // blur, radial blur, DoF) are exposed as raw Ints, not lossy-decoded.
        R.push_back(Schema{ "ImageSpaceModifier", RE::TESImageSpaceModifier::FORMTYPE, {
            RF_B(TESImageSpaceModifier, "Animatable", data.animatable),
            RF_F(TESImageSpaceModifier, "Duration", data.duration),
            RF_F(TESImageSpaceModifier, "EyeAdaptSpeedMult", data.hdr.eyeAdaptSpeed.mult),
            RF_F(TESImageSpaceModifier, "EyeAdaptSpeedAdd", data.hdr.eyeAdaptSpeed.add),
            RF_F(TESImageSpaceModifier, "BloomBlurRadiusMult", data.hdr.bloomBlurRadius.mult),
            RF_F(TESImageSpaceModifier, "BloomBlurRadiusAdd", data.hdr.bloomBlurRadius.add),
            RF_F(TESImageSpaceModifier, "BloomThresholdMult", data.hdr.bloomThreshold.mult),
            RF_F(TESImageSpaceModifier, "BloomThresholdAdd", data.hdr.bloomThreshold.add),
            RF_F(TESImageSpaceModifier, "BloomScaleMult", data.hdr.bloomScale.mult),
            RF_F(TESImageSpaceModifier, "BloomScaleAdd", data.hdr.bloomScale.add),
            RF_F(TESImageSpaceModifier, "TargetLumMinMult", data.hdr.targetLum.min.mult),
            RF_F(TESImageSpaceModifier, "TargetLumMinAdd", data.hdr.targetLum.min.add),
            RF_F(TESImageSpaceModifier, "TargetLumMaxMult", data.hdr.targetLum.max.mult),
            RF_F(TESImageSpaceModifier, "TargetLumMaxAdd", data.hdr.targetLum.max.add),
            RF_F(TESImageSpaceModifier, "SunlightScaleMult", data.hdr.sunlightScale.mult),
            RF_F(TESImageSpaceModifier, "SunlightScaleAdd", data.hdr.sunlightScale.add),
            RF_F(TESImageSpaceModifier, "SkyScaleMult", data.hdr.skyScale.mult),
            RF_F(TESImageSpaceModifier, "SkyScaleAdd", data.hdr.skyScale.add),
            RF_F(TESImageSpaceModifier, "SaturationMult", data.cinematic.saturation.mult),
            RF_F(TESImageSpaceModifier, "SaturationAdd", data.cinematic.saturation.add),
            RF_F(TESImageSpaceModifier, "BrightnessMult", data.cinematic.brightness.mult),
            RF_F(TESImageSpaceModifier, "BrightnessAdd", data.cinematic.brightness.add),
            RF_F(TESImageSpaceModifier, "ContrastMult", data.cinematic.contrast.mult),
            RF_F(TESImageSpaceModifier, "ContrastAdd", data.cinematic.contrast.add),
            RF_INT(TESImageSpaceModifier, "TintColorRaw", data.tintColor, std::uint32_t),
            RF_INT(TESImageSpaceModifier, "BlurRadiusRaw", data.blurRadius, std::uint32_t),
            RF_INT(TESImageSpaceModifier, "DoubleVisionRaw", data.doubleVisionStrength, std::uint32_t),
            RF_INT(TESImageSpaceModifier, "RadialBlurStrengthRaw", data.radialBlurStrength, std::uint32_t),
            RF_INT(TESImageSpaceModifier, "RadialBlurRampUpRaw", data.radialBlurRampUp, std::uint32_t),
            RF_INT(TESImageSpaceModifier, "RadialBlurStartRaw", data.radialBlurStart, std::uint32_t),
            RF_INT(TESImageSpaceModifier, "RadialBlurRampDownRaw", data.radialBlurRampDown, std::uint32_t),
            RF_INT(TESImageSpaceModifier, "RadialBlurDownStartRaw", data.radialBlurDownStart, std::uint32_t),
            RF_B(TESImageSpaceModifier, "UseTargetForRadialBlur", data.useTargetForRadialBlur),
            RF_F(TESImageSpaceModifier, "RadialBlurCenterX", data.radialBlurCenter.x),
            RF_F(TESImageSpaceModifier, "RadialBlurCenterY", data.radialBlurCenter.y),
            RF_INT(TESImageSpaceModifier, "DofStrengthRaw", data.dof.strength, std::uint32_t),
            RF_INT(TESImageSpaceModifier, "DofDistanceRaw", data.dof.distance, std::uint32_t),
            RF_INT(TESImageSpaceModifier, "DofRangeRaw", data.dof.range, std::uint32_t),
            RF_B(TESImageSpaceModifier, "DofUseTarget", data.dof.useTarget),
            RF_INT(TESImageSpaceModifier, "FadeColorRaw", data.fadeColor, std::uint32_t),
            RF_INT(TESImageSpaceModifier, "MotionBlurRaw", data.motionBlurStrength, std::uint32_t),
        }});

        // WorldSpace: climate/water/lighting links, map framing, LOD + land
        // heights. Runtime containers (cell maps, ref maps) are derived state
        // and stay outside the schema by design.
        R.push_back(Schema{ "WorldSpace", RE::TESWorldSpace::FORMTYPE, {
            RF_LINK(TESWorldSpace, "Climate", climate, TESClimate),
            RF_LINK(TESWorldSpace, "ParentWorld", parentWorld, TESWorldSpace),
            RF_LINK(TESWorldSpace, "LightingTemplate", lightingTemplate, BGSLightingTemplate),
            RF_LINK(TESWorldSpace, "WorldWater", worldWater, TESWaterForm),
            RF_LINK(TESWorldSpace, "LodWater", lodWater, TESWaterForm),
            RF_LINK(TESWorldSpace, "MusicType", musicType, BGSMusicType),
            RF_LINK(TESWorldSpace, "EncounterZone", encounterZone, BGSEncounterZone),
            RF_LINK(TESWorldSpace, "Location", location, BGSLocation),
            RF_F(TESWorldSpace, "LodWaterHeight", lodWaterHeight),
            RF_F(TESWorldSpace, "DefaultLandHeight", defaultLandHeight),
            RF_F(TESWorldSpace, "DefaultWaterHeight", defaultWaterHeight),
            RF_F(TESWorldSpace, "DistantLODMult", distantLODMult),
            RF_F(TESWorldSpace, "NorthRotation", northRotation),
            RF_FLAGS(TESWorldSpace, "Flags", flags, RE::TESWorldSpace::Flag),
            RF_FLAGS(TESWorldSpace, "ParentUseFlags", parentUseFlags, RE::TESWorldSpace::ParentUseFlag),
            RF_INT(TESWorldSpace, "MapUsableWidth", worldMapData.usableWidth, std::uint32_t),
            RF_INT(TESWorldSpace, "MapUsableHeight", worldMapData.usableHeight, std::uint32_t),
            RF_INT(TESWorldSpace, "MapNWCellX", worldMapData.nwCellX, std::int16_t),
            RF_INT(TESWorldSpace, "MapNWCellY", worldMapData.nwCellY, std::int16_t),
            RF_INT(TESWorldSpace, "MapSECellX", worldMapData.seCellX, std::int16_t),
            RF_INT(TESWorldSpace, "MapSECellY", worldMapData.seCellY, std::int16_t),
            RF_F(TESWorldSpace, "MapScale", worldMapOffsetData.mapScale),
            RF_F(TESWorldSpace, "MapOffsetX", worldMapOffsetData.mapOffsetX),
            RF_F(TESWorldSpace, "MapOffsetY", worldMapOffsetData.mapOffsetY),
            RF_F(TESWorldSpace, "MapOffsetZ", worldMapOffsetData.mapOffsetZ),
            RF_F(TESWorldSpace, "MinCoordsX", minimumCoords.x),
            RF_F(TESWorldSpace, "MinCoordsY", minimumCoords.y),
            RF_F(TESWorldSpace, "MaxCoordsX", maximumCoords.x),
            RF_F(TESWorldSpace, "MaxCoordsY", maximumCoords.y),
            RF_S(TESWorldSpace, "CanopyShadowTexture", canopyShadowTexture.textureName),
            RF_S(TESWorldSpace, "WaterEnvMap", waterEnvMap.textureName),
        }});

        // Land texture (LTEX): closes the grass loop the Grass schema opened:
        // WHICH grasses grow on which ground texture. The GNAM grass list is
        // exposed as bounded slots Grass0..Grass7: reads past the list end
        // return 0; writes replace EXISTING entries only (no list surgery),
        // the RegionWalker discipline; writing 0 is a no-op, never a removal.
        {
            Schema s{ "LandTexture", RE::TESLandTexture::FORMTYPE, {
                RF_LINK(TESLandTexture, "TextureSet", textureSet, BGSTextureSet),
                RF_LINK(TESLandTexture, "MaterialType", materialType, BGSMaterialType),
                RF_INT(TESLandTexture, "Friction", havokData.friction, std::int32_t),
                RF_INT(TESLandTexture, "Restitution", havokData.restitution, std::int32_t),
                RF_INT(TESLandTexture, "SpecularExponent", specularExponent, std::int8_t),
                RF_INT(TESLandTexture, "ShaderTextureIndex", shaderTextureIndex, std::int32_t),
            }};
            for (int slot = 0; slot < 8; ++slot) {
                s.fields.push_back(Field{ "Grass" + std::to_string(slot), Value::Kind::FormLink,
                    [slot](const RE::TESForm* f) {
                        // read-only walk; non-const iteration only because this
                        // CommonLib's BSSimpleList const_iterator does not convert
                        auto* lt = const_cast<RE::TESLandTexture*>(
                            static_cast<const RE::TESLandTexture*>(f));
                        int i = 0;
                        for (auto* g : lt->textureGrassList)
                            if (i++ == slot) return Value::Link(g ? g->GetFormID() : 0);
                        return Value::Link(0);
                    },
                    [slot](RE::TESForm* f, const Value& v) {
                        if (!v.id) return;               // no clearing, no surgery
                        auto* g = RE::TESForm::LookupByID<RE::TESGrass>(v.id);
                        if (!g) return;
                        auto* lt = static_cast<RE::TESLandTexture*>(f);
                        int i = 0;
                        for (auto it = lt->textureGrassList.begin();
                             it != lt->textureGrassList.end(); ++it, ++i)
                            if (i == slot) { *it = g; return; }
                    } });
            }
            R.push_back(std::move(s));
        }

        // Grass (GRAS): the full typed DATA block plus the model path. The
        // engine clamps some of these itself (density/slopes 0..100/90); the
        // schema exposes the raw stored values, and the engine's own setters
        // stay the authority on live behavior. Grass placement (which
        // landscape textures grow it) lives on TESLandTexture, not here.
        R.push_back(Schema{ "Grass", RE::TESGrass::FORMTYPE, {
            RF_S(TESGrass, "Model", model),
            RF_INT(TESGrass, "Density", data.density, std::int8_t),
            RF_INT(TESGrass, "MinSlope", data.minSlopeDegrees, std::int8_t),
            RF_INT(TESGrass, "MaxSlope", data.maxSlopeDegrees, std::int8_t),
            RF_INT(TESGrass, "DistanceFromWater", data.distanceFromWaterLevel, std::uint16_t),
            RF_FLAGS(TESGrass, "UnderwaterState", data.underwater, RE::TESGrass::GRASS_WATER_STATE),
            RF_F(TESGrass, "PositionRange", data.positionRange),
            RF_F(TESGrass, "HeightRange", data.heightRange),
            RF_F(TESGrass, "ColorRange", data.colorRange),
            RF_F(TESGrass, "WavePeriod", data.wavePeriod),
            RF_FLAGS(TESGrass, "Flags", data.flags, RE::TESGrass::GRASS_DATA::Flag),
        }});

        SKSE::log::info("EngineReflect: {} schemas registered", R.size());
    }

    std::string SourceChain(RE::FormID id)
    {
        auto* form = RE::TESForm::LookupByID(id);
        if (!form) return {};
        char head[64];
        std::snprintf(head, sizeof head, "0x%08X (%s): ", id,
                      std::string(RE::FormTypeToString(form->GetFormType())).c_str());
        std::string out = head;
        const auto* arr = form->sourceFiles.array;
        if (!arr || arr->empty())
            return out + "(runtime-created; no plugin chain)";
        for (std::uint32_t i = 0; i < arr->size(); ++i) {
            if (i) out += " -> ";
            const auto* f = (*arr)[i];
            out += f ? std::string(f->GetFilename()) : "?";
        }
        out += "   [last = winner]";
        return out;
    }

    // ── Discovery ────────────────────────────────────────────────────────
    static const char* KindName(Value::Kind k)
    {
        switch (k) {
        case Value::Kind::Float:    return "float";
        case Value::Kind::Int:      return "int";
        case Value::Kind::Bool:     return "bool";
        case Value::Kind::Color3:   return "color3";
        case Value::Kind::Color4:   return "color4";
        case Value::Kind::FormLink: return "formlink";
        case Value::Kind::String:   return "string";
        }
        return "?";
    }

    std::string ListSchemas()
    {
        std::ostringstream o;
        for (auto& s : Registry())
            o << s.name << "  (form type " << static_cast<int>(s.formType)
              << ", " << s.fields.size() << " fields)\n";
        return o.str();
    }

    std::string DescribeSchema(const Schema& s)
    {
        std::ostringstream o;
        o << "[" << s.name << "]  " << s.fields.size() << " fields\n";
        for (auto& f : s.fields) o << "  " << f.name << " : " << KindName(f.kind) << "\n";
        return o.str();
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

    VerifyResult VerifyStrict(RE::FormID id)
    {
        // Read -> Write the same values back -> Read again. Witnesses that the
        // setters are idempotent for the record's live values (get/set agree).
        // This MUTATES the form, which is why it is not the default Verify.
        VerifyResult r;
        auto* form = RE::TESForm::LookupByID(id);
        if (!form) { r.detail = "form not found"; return r; }
        auto* s = SchemaFor(form->GetFormType());
        if (!s) { r.detail = "no schema for this form type"; return r; }

        Tree a = Read(form);
        int written = Write(form, a);
        Tree b = Read(form);
        r.fields = written;
        if (a.size() != b.size()) { r.detail = "field count changed across write-back"; return r; }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i].first != b[i].first || !a[i].second.ApproxEquals(b[i].second)) {
                r.detail = "write-back not idempotent at field '" + a[i].first + "'";
                return r;
            }
        }
        r.ok = true;
        return r;
    }
}
