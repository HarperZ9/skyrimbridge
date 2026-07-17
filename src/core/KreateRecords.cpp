//=============================================================================
//  KreateRecords.cpp — KreatE record-override appliers
//=============================================================================

#include "KreateRecords.h"
#include "WeatherEditor.h"   // VolumetricSnapshot

#include <SKSE/SKSE.h>

#include <cmath>
#include <fstream>
#include <sstream>

namespace SB::Kreate
{
    // ── shared helpers ───────────────────────────────────────────────────
    static std::uint8_t U8(float f)
    {
        return static_cast<std::uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
    }

    // Overwrite a Color's RGB from an "r, g, b" (0-1) string; alpha untouched.
    static void SetColorRGB(RE::Color& c, const std::string& v)
    {
        auto col = Cfg::AsColor4(v, { c.red / 255.f, c.green / 255.f, c.blue / 255.f, 1.f });
        c.red = U8(col[0]); c.green = U8(col[1]); c.blue = U8(col[2]);
    }

    // Raw 0-255 → engine int8 (the game reads these back through a uint8 cast).
    static std::int8_t RawI8(const std::string& v, std::int8_t def)
    {
        auto t = Cfg::Trim(v);
        if (t.empty()) return def;
        return static_cast<std::int8_t>(static_cast<std::uint8_t>(Cfg::AsInt(t, def)));
    }

    static const char* kToD[4] = { "Sunrise", "Day", "Sunset", "Night" };

    // KreatE color-type names, in TESWeather color-index order (0..16).
    static const char* kCT[17] = {
        "SkyUpper", "FogNear", "Unkown", "Ambient", "Sunlight", "Sun", "Stars",
        "SkyLower", "Horizon", "EffectLighting", "CloudLodDiffuse", "CloudLodAmbient",
        "FogFar", "SkyStatics", "WaterMultiplier", "SunGlare", "MoonGlare"
    };

    // Top/Middle/Bottom → the game's 6-way ambient cube (z up/down, x&y sides).
    static void SetAmbientTMB(RE::BGSDirectionalAmbientLightingColors& da,
                              const std::string* top, const std::string* mid, const std::string* bot)
    {
        if (top) SetColorRGB(da.directional.z.max, *top);
        if (bot) SetColorRGB(da.directional.z.min, *bot);
        if (mid) {
            SetColorRGB(da.directional.x.max, *mid); SetColorRGB(da.directional.x.min, *mid);
            SetColorRGB(da.directional.y.max, *mid); SetColorRGB(da.directional.y.min, *mid);
        }
    }

    // ── ParseOverride ────────────────────────────────────────────────────
    bool ParseOverride(const std::string& text, Override& out)
    {
        out = Override{};
        out.body = Cfg::Parse(text);
        const Cfg::Section* top = out.body.Find("");
        if (!top) return false;
        auto* id = top->Find("ID");
        if (!id) return false;
        out.formID = Cfg::AsFormID(*id);
        auto* opt = top->Find("Optional");
        out.optional = opt ? Cfg::AsBool(*opt, true) : true;
        return out.formID != 0;
    }

    // ── Weather ──────────────────────────────────────────────────────────
    // Resolve a FormID onto a typed form-link slot. id 0 clears the link;
    // an unresolved non-zero id leaves the current link (form not in load order).
    template <class T>
    static void SetLink(T*& slot, std::uint32_t id)
    {
        if (id == 0) { slot = nullptr; return; }
        if (auto* f = RE::TESForm::LookupByID<T>(id)) slot = f;
    }

    static void ApplyWDirAmbient(RE::TESWeather* w, const Cfg::Section& s)
    {
        for (int td = 0; td < 4; ++td)
            SetAmbientTMB(w->directionalAmbientLightingColors[td],
                          s.Find(std::string("Top") + kToD[td]),
                          s.Find(std::string("Middle") + kToD[td]),
                          s.Find(std::string("Bottom") + kToD[td]));
    }

    static void ApplyWColors(RE::TESWeather* w, const Cfg::Section& s)
    {
        for (int ct = 0; ct < 17; ++ct)
            for (int td = 0; td < 4; ++td)
                if (auto* v = s.Find(std::string(kCT[ct]) + kToD[td]))
                    SetColorRGB(w->colorData[ct][td], *v);
    }

    static void ApplyWFog(RE::TESWeather* w, const Cfg::Section& s)
    {
        w->fogData.dayNear    = s.Float("DayNear",    w->fogData.dayNear);
        w->fogData.dayFar     = s.Float("DayFar",     w->fogData.dayFar);
        w->fogData.dayPower   = s.Float("DayCurve",   w->fogData.dayPower);
        w->fogData.dayMax     = s.Float("DayMax",     w->fogData.dayMax);
        w->fogData.nightNear  = s.Float("NightNear",  w->fogData.nightNear);
        w->fogData.nightFar   = s.Float("NightFar",   w->fogData.nightFar);
        w->fogData.nightPower = s.Float("NightCurve", w->fogData.nightPower);
        w->fogData.nightMax   = s.Float("NightMax",   w->fogData.nightMax);
    }

    static void ApplyWClouds(RE::TESWeather* w, const Cfg::Section& s)
    {
        std::uint32_t disabled = w->cloudLayerDisabledBits;
        for (int i = 0; i < 32; ++i) {
            std::string p = "Layer" + std::to_string(i);
            if (auto* v = s.Find(p + "SpeedX")) w->cloudLayerSpeedX[i] = RawI8(*v, w->cloudLayerSpeedX[i]);
            if (auto* v = s.Find(p + "SpeedY")) w->cloudLayerSpeedY[i] = RawI8(*v, w->cloudLayerSpeedY[i]);
            if (auto* v = s.Find(p + "Enabled")) {
                if (Cfg::AsBool(*v)) disabled &= ~(1u << i); else disabled |= (1u << i);
            }
            for (int td = 0; td < 4; ++td)
                if (auto* v = s.Find(p + "Color" + kToD[td])) {
                    SetColorRGB(w->cloudColorData[i][td], *v);          // RGB
                    auto c = Cfg::AsColor4(*v, { 0, 0, 0, w->cloudAlpha[i][td] });
                    w->cloudAlpha[i][td] = c[3];                         // alpha
                }
        }
        w->cloudLayerDisabledBits = disabled;
    }

    static void ApplyWWind(RE::TESWeather* w, const Cfg::Section& s)
    {
        w->data.windSpeed          = RawI8(s.Get("Speed"),     w->data.windSpeed);
        w->data.windDirection      = RawI8(s.Get("Direction"), w->data.windDirection);
        w->data.windDirectionRange = RawI8(s.Get("DirRange"),  w->data.windDirectionRange);
    }

    static void ApplyWMisc(RE::TESWeather* w, const Cfg::Section& s)
    {
        w->data.transDelta = RawI8(s.Get("TransDelta"), w->data.transDelta);
        w->data.sunGlare   = RawI8(s.Get("SunGlare"),   w->data.sunGlare);
        w->data.sunDamage  = RawI8(s.Get("SunDamage"),  w->data.sunDamage);
        w->data.thunderLightningBeginFadeIn = RawI8(s.Get("LightningBeginFadeIn"), w->data.thunderLightningBeginFadeIn);
        w->data.thunderLightningEndFadeOut  = RawI8(s.Get("LightningEndFadeOut"),  w->data.thunderLightningEndFadeOut);
        w->data.thunderLightningFrequency   = RawI8(s.Get("LightningFrequency"),   w->data.thunderLightningFrequency);
        if (auto* v = s.Find("LightningColor")) {
            auto c = Cfg::AsColor4(*v, { 0, 0, 0, 1 });
            w->data.lightningColor.red   = static_cast<std::int8_t>(U8(c[0]));
            w->data.lightningColor.green = static_cast<std::int8_t>(U8(c[1]));
            w->data.lightningColor.blue  = static_cast<std::int8_t>(U8(c[2]));
        }
        // Per-ToD form links + lens flare (KreatE stores these in [Misc]).
        for (int td = 0; td < 4; ++td) {
            if (auto* v = s.Find(std::string("ImageSpaceID") + kToD[td]))
                SetLink(w->imageSpaces[td], Cfg::AsFormID(*v));
            if (auto* v = s.Find(std::string("VolumetricLightingID") + kToD[td]))
                SetLink(w->volumetricLighting[td], Cfg::AsFormID(*v));
        }
        if (auto* v = s.Find("SolarLensFlareID"))
            SetLink(w->sunGlareLensFlare, Cfg::AsFormID(*v));
        // WeatherClassification (data.flags) is deliberately not applied: its
        // KreatE encoding is ambiguous and it drives game weather-selection
        // logic, not the ENB look; writing it risks clobbering the aurora bits.
    }

    static void ApplyWPrecip(RE::TESWeather* w, const Cfg::Section& s)
    {
        w->data.precipitationBeginFadeIn = RawI8(s.Get("BeginFadeIn"), w->data.precipitationBeginFadeIn);
        w->data.precipitationEndFadeOut  = RawI8(s.Get("EndFadeOut"),  w->data.precipitationEndFadeOut);
        if (auto* v = s.Find("ID")) SetLink(w->precipitationData, Cfg::AsFormID(*v));
    }

    static void ApplyWVisualEffect(RE::TESWeather* w, const Cfg::Section& s)
    {
        w->data.visualEffectBegin = RawI8(s.Get("Begin"), w->data.visualEffectBegin);
        w->data.visualEffectEnd   = RawI8(s.Get("End"),   w->data.visualEffectEnd);
        if (auto* v = s.Find("ID")) SetLink(w->referenceEffect, Cfg::AsFormID(*v));
    }

    bool ApplyWeather(const Override& o)
    {
        auto* w = RE::TESForm::LookupByID<RE::TESWeather>(o.formID);
        if (!w) return false;
        if (auto* s = o.body.Find("DirAmbient"))    ApplyWDirAmbient(w, *s);
        if (auto* s = o.body.Find("Colors"))        ApplyWColors(w, *s);
        if (auto* s = o.body.Find("Fog"))           ApplyWFog(w, *s);
        if (auto* s = o.body.Find("Clouds"))        ApplyWClouds(w, *s);
        if (auto* s = o.body.Find("Wind"))          ApplyWWind(w, *s);
        if (auto* s = o.body.Find("Precipitation")) ApplyWPrecip(w, *s);
        if (auto* s = o.body.Find("VisualEffect"))  ApplyWVisualEffect(w, *s);
        if (auto* s = o.body.Find("Misc"))          ApplyWMisc(w, *s);
        return true;
    }

    // ── LightingTemplate ─────────────────────────────────────────────────
    bool ApplyLightingTemplate(const Override& o)
    {
        auto* lt = RE::TESForm::LookupByID<RE::BGSLightingTemplate>(o.formID);
        if (!lt) return false;
        auto& d = lt->data;   // INTERIOR_DATA

        if (const Cfg::Section* s = o.body.Find("")) {
            if (auto* v = s->Find("Ambient"))       SetColorRGB(d.ambient, *v);
            if (auto* v = s->Find("Directional"))   SetColorRGB(d.directional, *v);
            if (auto* v = s->Find("FogColorNear"))  SetColorRGB(d.fogColorNear, *v);
            if (auto* v = s->Find("FogColorFar"))   SetColorRGB(d.fogColorFar, *v);
            d.directionalXY   = static_cast<std::uint32_t>(std::lround(s->Float("DirectionalRotXY", static_cast<float>(d.directionalXY))));
            d.directionalZ    = static_cast<std::uint32_t>(std::lround(s->Float("DirectionalRotZ", static_cast<float>(d.directionalZ))));
            d.directionalFade = s->Float("DirectionalFade", d.directionalFade);
            d.fogNear         = s->Float("FogNear", d.fogNear);
            d.fogFar          = s->Float("FogFar", d.fogFar);
            d.clipDist        = s->Float("FogClipDist", d.clipDist);
            d.fogPower        = s->Float("FogPower", d.fogPower);
            d.fogClamp        = s->Float("FogMax", d.fogClamp);
            d.lightFadeStart  = s->Float("LightFadeStart", d.lightFadeStart);
            d.lightFadeEnd    = s->Float("LightFadeEnd", d.lightFadeEnd);
        }
        if (const Cfg::Section* s = o.body.Find("DirectionalAmbient")) {
            SetAmbientTMB(d.directionalAmbientLightingColors,
                          s->Find("Top"), s->Find("Middle"), s->Find("Bottom"));
        }
        return true;
    }

    // ── Volumetric lighting ──────────────────────────────────────────────
    bool ApplyVolumetric(const Override& o)
    {
        auto* vl = RE::TESForm::LookupByID<RE::BGSVolumetricLighting>(o.formID);
        if (!vl) return false;

        VolumetricSnapshot snap;
        snap.ReadFrom(vl);
        if (const Cfg::Section* s = o.body.Find("")) {
            snap.intensity = s->Float("Intensity", snap.intensity);
            if (auto* v = s->Find("CustomColor")) {
                auto c = Cfg::AsColor4(*v, { snap.colorR, snap.colorG, snap.colorB, snap.customColorContrib });
                snap.colorR = c[0]; snap.colorG = c[1]; snap.colorB = c[2]; snap.customColorContrib = c[3];
            }
            snap.densityContrib      = s->Float("Density", snap.densityContrib);
            snap.densitySize         = s->Float("Size", snap.densitySize);
            snap.densityWindSpeed    = s->Float("WindSpeed", snap.densityWindSpeed);
            snap.densityFallingSpeed = s->Float("FallingSpeed", snap.densityFallingSpeed);
            snap.phaseContrib        = s->Float("Phase", snap.phaseContrib);
            snap.phaseScattering     = s->Float("Scattering", snap.phaseScattering);
            snap.samplingRangeFactor = s->Float("RangeFactor", snap.samplingRangeFactor);
        }
        snap.WriteTo(vl);
        return true;
    }

    // ── Shader particle geometry ─────────────────────────────────────────
    bool ApplyShaderParticle(const Override& o)
    {
        auto* spg = RE::TESForm::LookupByID<RE::BGSShaderParticleGeometryData>(o.formID);
        if (!spg) return false;
        const Cfg::Section* s = o.body.Find("");
        if (!s) return true;

        using D = RE::BGSShaderParticleGeometryData::DataID;
        auto& arr = spg->data;
        auto setf = [&](D id, float val) {
            auto idx = static_cast<std::size_t>(id);
            if (idx < arr.size()) arr[idx].f = val;
        };
        if (auto* v = s->Find("GravityVelocity"))      setf(D::kGravityVelocity, Cfg::AsFloat(*v));
        if (auto* v = s->Find("RotationVelocity"))     setf(D::kRotationVelocity, Cfg::AsFloat(*v));
        if (auto* v = s->Find("ParticleSize")) {
            auto c = Cfg::AsColor4(*v, { 0, 0, 0, 0 });
            setf(D::kParticleSizeX, c[0]); setf(D::kParticleSizeY, c[1]);
        }
        if (auto* v = s->Find("CenterOffsetMin"))      setf(D::kCenterOffsetMin, Cfg::AsFloat(*v));
        if (auto* v = s->Find("CenterOffsetMax"))      setf(D::kCenterOffsetMax, Cfg::AsFloat(*v));
        if (auto* v = s->Find("InitialRotationRange")) setf(D::kStartRotationRange, Cfg::AsFloat(*v));
        if (auto* v = s->Find("SubtextureCountX"))     setf(D::kNumSubtexturesX, Cfg::AsFloat(*v));
        if (auto* v = s->Find("SubtextureCountY"))     setf(D::kNumSubtexturesY, Cfg::AsFloat(*v));
        if (auto* v = s->Find("Shader"))               setf(D::kParticleType, Cfg::AsFloat(*v));
        if (auto* v = s->Find("BoxSize"))              setf(D::kBoxSize, Cfg::AsFloat(*v));
        if (auto* v = s->Find("ParticleDensity"))      setf(D::kParticleDensity, Cfg::AsFloat(*v));
        if (auto* v = s->Find("ParticleTexture")) {
            std::string t = Cfg::Trim(*v);
            if (t.size() >= 2 && t.front() == '"' && t.back() == '"') t = t.substr(1, t.size() - 2);
            spg->particleTexture.textureName = t.c_str();
        }
        return true;
    }

    // ── RecordSet ────────────────────────────────────────────────────────
    void RecordSet::Clear()
    {
        weathers.clear(); lightingTemplates.clear();
        volumetrics.clear(); shaderParticles.clear();
    }

    int RecordSet::Count() const
    {
        return static_cast<int>(weathers.size() + lightingTemplates.size() +
                                volumetrics.size() + shaderParticles.size());
    }

    static void LoadDir(const std::filesystem::path& dir, std::vector<Override>& out)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) return;
        for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".ini") continue;
            std::ifstream in(entry.path());
            if (!in) continue;
            std::stringstream buf; buf << in.rdbuf();
            Override o;
            if (ParseOverride(buf.str(), o)) out.push_back(std::move(o));
        }
    }

    int RecordSet::LoadFrom(const std::filesystem::path& profileDir)
    {
        Clear();
        LoadDir(profileDir / "Weathers", weathers);
        LoadDir(profileDir / "LightingTemplates", lightingTemplates);
        LoadDir(profileDir / "VolumetricLighting", volumetrics);
        LoadDir(profileDir / "ShaderParticleGeometries", shaderParticles);
        return Count();
    }

    int RecordSet::ApplyAll() const
    {
        int n = 0;
        for (auto& o : weathers)          if (ApplyWeather(o)) ++n;
        for (auto& o : lightingTemplates) if (ApplyLightingTemplate(o)) ++n;
        for (auto& o : volumetrics)       if (ApplyVolumetric(o)) ++n;
        for (auto& o : shaderParticles)   if (ApplyShaderParticle(o)) ++n;
        return n;
    }
}
