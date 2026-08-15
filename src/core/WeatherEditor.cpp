#include "WeatherEditor.h"
#include "CompatDetect.h"
#if SKYRIMBRIDGE_NATIVE_REPLACEMENTS
#include "EditorIDCache.h"
#endif

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace SB
{

// ═════════════════════════════════════════════════════════════════════════════
//  Helpers
// ═════════════════════════════════════════════════════════════════════════════

static float Int8ToNorm(int8_t v)
{
    return static_cast<float>(static_cast<uint8_t>(v)) / 255.0f;
}

static int8_t NormToInt8(float f)
{
    auto u = static_cast<uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
    return static_cast<int8_t>(u);
}

static const char* kColorTypeNames[] = {
    "Sky Upper", "Fog Near", "Unknown", "Ambient",
    "Sunlight", "Sun", "Stars", "Sky Lower",
    "Horizon", "Effect Lighting", "Cloud LOD Diffuse",
    "Cloud LOD Ambient", "Fog Far", "Sky Statics",
    "Water Multiplier", "Sun Glare", "Moon Glare"
};

static const char* kToDNames[] = { "Sunrise", "Day", "Sunset", "Night" };

static std::string TrimWS(const std::string& s)
{
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

// ═════════════════════════════════════════════════════════════════════════════
//  ImageSpaceSnapshot
// ═════════════════════════════════════════════════════════════════════════════

void ImageSpaceSnapshot::ReadFrom(const RE::TESImageSpace* is)
{
    if (!is) { valid = false; return; }
    valid = true;
    formID = is->GetFormID();
    auto& d = is->data;
    eyeAdaptSpeed       = d.hdr.eyeAdaptSpeed;
    bloomBlurRadius     = d.hdr.bloomBlurRadius;
    bloomThreshold      = d.hdr.bloomThreshold;
    bloomScale          = d.hdr.bloomScale;
    receiveBloomThreshold = d.hdr.receiveBloomThreshold;
    white               = d.hdr.white;
    sunlightScale       = d.hdr.sunlightScale;
    skyScale            = d.hdr.skyScale;
    eyeAdaptStrength    = d.hdr.eyeAdaptStrength;
    saturation          = d.cinematic.saturation;
    brightness          = d.cinematic.brightness;
    contrast            = d.cinematic.contrast;
    tintAmount          = d.tint.amount;
    tintR = d.tint.color.red;
    tintG = d.tint.color.green;
    tintB = d.tint.color.blue;
    dofStrength         = d.depthOfField.strength;
    dofDistance          = d.depthOfField.distance;
    dofRange            = d.depthOfField.range;
}

void ImageSpaceSnapshot::WriteTo(RE::TESImageSpace* is) const
{
    if (!is || !valid) return;
    auto& d = is->data;
    d.hdr.eyeAdaptSpeed       = eyeAdaptSpeed;
    d.hdr.bloomBlurRadius     = bloomBlurRadius;
    d.hdr.bloomThreshold      = bloomThreshold;
    d.hdr.bloomScale          = bloomScale;
    d.hdr.receiveBloomThreshold = receiveBloomThreshold;
    d.hdr.white               = white;
    d.hdr.sunlightScale       = sunlightScale;
    d.hdr.skyScale            = skyScale;
    d.hdr.eyeAdaptStrength    = eyeAdaptStrength;
    d.cinematic.saturation    = saturation;
    d.cinematic.brightness    = brightness;
    d.cinematic.contrast      = contrast;
    d.tint.amount             = tintAmount;
    d.tint.color.red   = tintR;
    d.tint.color.green = tintG;
    d.tint.color.blue  = tintB;
    d.depthOfField.strength   = dofStrength;
    d.depthOfField.distance   = dofDistance;
    d.depthOfField.range      = dofRange;
}

// ═════════════════════════════════════════════════════════════════════════════
//  VolumetricSnapshot
// ═════════════════════════════════════════════════════════════════════════════

void VolumetricSnapshot::ReadFrom(const RE::BGSVolumetricLighting* vl)
{
    if (!vl) { valid = false; return; }
    valid = true;
    formID = vl->GetFormID();
    intensity           = vl->intensity;
    customColorContrib  = vl->customColor.contribution;
    colorR = vl->red;   colorG = vl->green;   colorB = vl->blue;
    densityContrib      = vl->density.contribution;
    densitySize         = vl->density.size;
    densityWindSpeed    = vl->density.windSpeed;
    densityFallingSpeed = vl->density.fallingSpeed;
    phaseContrib        = vl->phaseFunction.contribution;
    phaseScattering     = vl->phaseFunction.scattering;
    samplingRangeFactor = vl->samplingRepartition.rangeFactor;
}

void VolumetricSnapshot::WriteTo(RE::BGSVolumetricLighting* vl) const
{
    if (!vl || !valid) return;
    vl->intensity                   = intensity;
    vl->customColor.contribution    = customColorContrib;
    vl->red = colorR;  vl->green = colorG;  vl->blue = colorB;
    vl->density.contribution        = densityContrib;
    vl->density.size                = densitySize;
    vl->density.windSpeed           = densityWindSpeed;
    vl->density.fallingSpeed        = densityFallingSpeed;
    vl->phaseFunction.contribution  = phaseContrib;
    vl->phaseFunction.scattering    = phaseScattering;
    vl->samplingRepartition.rangeFactor = samplingRangeFactor;
}

// ═════════════════════════════════════════════════════════════════════════════
//  DirAmbientSnapshot
// ═════════════════════════════════════════════════════════════════════════════

void DirAmbientSnapshot::ReadFrom(const RE::BGSDirectionalAmbientLightingColors& da)
{
    xMax.FromColor(da.directional.x.max);
    xMin.FromColor(da.directional.x.min);
    yMax.FromColor(da.directional.y.max);
    yMin.FromColor(da.directional.y.min);
    zMax.FromColor(da.directional.z.max);
    zMin.FromColor(da.directional.z.min);
    specular.FromColor(da.specular);
    fresnelPower = da.fresnelPower;
}

void DirAmbientSnapshot::WriteTo(RE::BGSDirectionalAmbientLightingColors& da) const
{
    xMax.ToColor(da.directional.x.max);
    xMin.ToColor(da.directional.x.min);
    yMax.ToColor(da.directional.y.max);
    yMin.ToColor(da.directional.y.min);
    zMax.ToColor(da.directional.z.max);
    zMin.ToColor(da.directional.z.min);
    specular.ToColor(da.specular);
    da.fresnelPower = fresnelPower;
}

// ═════════════════════════════════════════════════════════════════════════════
//  WeatherSnapshot — Read / Write
// ═════════════════════════════════════════════════════════════════════════════

void WeatherSnapshot::ReadFromWeather(RE::TESWeather* w)
{
    if (!w) return;

    formID = w->GetFormID();
#if SKYRIMBRIDGE_NATIVE_REPLACEMENTS
    editorID = EditorIDCache::Get().Lookup(formID);
#else
    // Without the native cache, names come from whichever EditorID provider the
    // user already has (Kitsuune's NativeEditorID Fix, or po3's Tweaks). With
    // neither, the fallback below names the weather by its FormID: the workshop
    // stays usable, presets just key off hex instead of a readable name.
    editorID.clear();
    if (const auto fn = CompatDetect::Get().GetExternalEditorIDFunc()) {
        if (const char* name = fn(formID); name != nullptr)
            editorID = name;
    }
#endif
    if (editorID.empty()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%08X", formID);
        editorID = buf;
    }

    // 17 color types × 4 ToD
    for (int ct = 0; ct < kColorTypeCount; ++ct)
        for (int td = 0; td < kToDCount; ++td)
            colors[ct][td].FromColor(w->colorData[ct][td]);

    // Fog
    fogDayNear   = w->fogData.dayNear;
    fogDayFar    = w->fogData.dayFar;
    fogNightNear = w->fogData.nightNear;
    fogNightFar  = w->fogData.nightFar;
    fogDayPower  = w->fogData.dayPower;
    fogNightPower = w->fogData.nightPower;
    fogDayMax    = w->fogData.dayMax;
    fogNightMax  = w->fogData.nightMax;

    // Weather data
    windSpeed          = Int8ToNorm(w->data.windSpeed);
    windDirection      = Int8ToNorm(w->data.windDirection);
    windDirRange       = Int8ToNorm(w->data.windDirectionRange);
    transDelta         = Int8ToNorm(w->data.transDelta);
    sunGlare           = Int8ToNorm(w->data.sunGlare);
    sunDamage          = Int8ToNorm(w->data.sunDamage);
    precipBeginFadeIn  = Int8ToNorm(w->data.precipitationBeginFadeIn);
    precipEndFadeOut   = Int8ToNorm(w->data.precipitationEndFadeOut);
    thunderBeginFadeIn = Int8ToNorm(w->data.thunderLightningBeginFadeIn);
    thunderEndFadeOut  = Int8ToNorm(w->data.thunderLightningEndFadeOut);
    thunderFrequency   = Int8ToNorm(w->data.thunderLightningFrequency);
    visualEffectBegin  = Int8ToNorm(w->data.visualEffectBegin);
    visualEffectEnd    = Int8ToNorm(w->data.visualEffectEnd);
    flags = w->data.flags.underlying();

    lightningColor.r = Int8ToNorm(w->data.lightningColor.red);
    lightningColor.g = Int8ToNorm(w->data.lightningColor.green);
    lightningColor.b = Int8ToNorm(w->data.lightningColor.blue);
    lightningColor.a = 1.0f;

    // Cloud layers
    numCloudLayers = w->numCloudLayers;
    for (int i = 0; i < 32; ++i) {
        clouds[i].speedX = Int8ToNorm(w->cloudLayerSpeedX[i]);
        clouds[i].speedY = Int8ToNorm(w->cloudLayerSpeedY[i]);
        for (int td = 0; td < kToDCount; ++td)
            clouds[i].alpha[td] = w->cloudAlpha[i][td];
        clouds[i].enabled = !(w->cloudLayerDisabledBits & (1u << i));
    }

    // Directional ambient
    for (int td = 0; td < kToDCount; ++td)
        dirAmbient[td].ReadFrom(w->directionalAmbientLightingColors[td]);

    // ImageSpaces
    for (int td = 0; td < kToDCount; ++td)
        imageSpaces[td].ReadFrom(w->imageSpaces[td]);

    // Volumetric lighting
    for (int td = 0; td < kToDCount; ++td)
        volumetric[td].ReadFrom(w->volumetricLighting[td]);
}

void WeatherSnapshot::WriteToWeather(RE::TESWeather* w) const
{
    if (!w) return;

    // Colors
    for (int ct = 0; ct < kColorTypeCount; ++ct)
        for (int td = 0; td < kToDCount; ++td)
            colors[ct][td].ToColor(w->colorData[ct][td]);

    // Fog
    w->fogData.dayNear    = fogDayNear;
    w->fogData.dayFar     = fogDayFar;
    w->fogData.nightNear  = fogNightNear;
    w->fogData.nightFar   = fogNightFar;
    w->fogData.dayPower   = fogDayPower;
    w->fogData.nightPower = fogNightPower;
    w->fogData.dayMax     = fogDayMax;
    w->fogData.nightMax   = fogNightMax;

    // Weather data
    w->data.windSpeed          = NormToInt8(windSpeed);
    w->data.windDirection      = NormToInt8(windDirection);
    w->data.windDirectionRange = NormToInt8(windDirRange);
    w->data.transDelta         = NormToInt8(transDelta);
    w->data.sunGlare           = NormToInt8(sunGlare);
    w->data.sunDamage          = NormToInt8(sunDamage);
    w->data.precipitationBeginFadeIn   = NormToInt8(precipBeginFadeIn);
    w->data.precipitationEndFadeOut    = NormToInt8(precipEndFadeOut);
    w->data.thunderLightningBeginFadeIn  = NormToInt8(thunderBeginFadeIn);
    w->data.thunderLightningEndFadeOut   = NormToInt8(thunderEndFadeOut);
    w->data.thunderLightningFrequency    = NormToInt8(thunderFrequency);
    w->data.visualEffectBegin  = NormToInt8(visualEffectBegin);
    w->data.visualEffectEnd    = NormToInt8(visualEffectEnd);
    w->data.flags = static_cast<RE::TESWeather::WeatherDataFlag>(flags);

    w->data.lightningColor.red   = NormToInt8(lightningColor.r);
    w->data.lightningColor.green = NormToInt8(lightningColor.g);
    w->data.lightningColor.blue  = NormToInt8(lightningColor.b);

    // Clouds
    w->numCloudLayers = numCloudLayers;
    uint32_t disabledBits = 0;
    for (int i = 0; i < 32; ++i) {
        w->cloudLayerSpeedX[i] = NormToInt8(clouds[i].speedX);
        w->cloudLayerSpeedY[i] = NormToInt8(clouds[i].speedY);
        for (int td = 0; td < kToDCount; ++td)
            w->cloudAlpha[i][td] = clouds[i].alpha[td];
        if (!clouds[i].enabled) disabledBits |= (1u << i);
    }
    w->cloudLayerDisabledBits = disabledBits;

    // Directional ambient
    for (int td = 0; td < kToDCount; ++td)
        dirAmbient[td].WriteTo(w->directionalAmbientLightingColors[td]);

    // ImageSpaces
    for (int td = 0; td < kToDCount; ++td)
        imageSpaces[td].WriteTo(w->imageSpaces[td]);

    // Volumetric lighting
    for (int td = 0; td < kToDCount; ++td)
        volumetric[td].WriteTo(w->volumetricLighting[td]);
}

// ═════════════════════════════════════════════════════════════════════════════
//  WeatherEditor — Singleton + Core
// ═════════════════════════════════════════════════════════════════════════════

WeatherEditor& WeatherEditor::Get()
{
    static WeatherEditor inst;
    return inst;
}

int WeatherEditor::GetCurrentToD() const
{
    auto* cal = RE::Calendar::GetSingleton();
    if (!cal) return kDay;
    float h = cal->GetHour();
    if (h >= 5.0f  && h < 8.0f)  return kSunrise;
    if (h >= 8.0f  && h < 18.0f) return kDay;
    if (h >= 18.0f && h < 21.0f) return kSunset;
    return kNight;
}

void WeatherEditor::Update()
{
    auto* sky = RE::Sky::GetSingleton();
    if (!sky || !sky->currentWeather) return;

    RE::FormID currentID = sky->currentWeather->GetFormID();

    // Detect weather change
    if (currentID != m_lastWeatherID) {
        m_lastWeatherID = currentID;
        if (m_autoCapture) {
            CaptureCurrentWeather();

            // Auto-load preset if one exists
            auto path = GetPresetPath(m_current.editorID);
            if (std::filesystem::exists(path)) {
                LoadSnapshotFromINI(m_current, path);
                m_dirty = true;
                std::error_code ec;
                m_presetMtime = std::filesystem::last_write_time(path, ec);
                SKSE::log::info("WeatherEditor: auto-loaded preset for '{}'", m_current.editorID);
            } else {
                m_presetMtime = {};
            }
        }
    }

    // Preset hot-reload: edit the INI in any editor while the game runs and
    // the change applies within a second. This is the GUI-less workshop loop.
    if (m_active && ++m_reloadCounter >= 60) {
        m_reloadCounter = 0;
        auto path = GetPresetPath(m_current.editorID);
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            auto mtime = std::filesystem::last_write_time(path, ec);
            if (!ec && mtime != m_presetMtime) {
                m_presetMtime = mtime;
                if (LoadSnapshotFromINI(m_current, path)) {
                    m_dirty = true;
                    SKSE::log::info("WeatherEditor: hot-reloaded preset for '{}'",
                        m_current.editorID);
                }
            }
        }
    }

    // Auto-apply edits each frame (skip if A/B compare is active)
    if (m_active && m_autoApply && m_dirty && !m_compareMode) {
        ApplyToGame();
    }
}

void WeatherEditor::CaptureCurrentWeather()
{
    auto* sky = RE::Sky::GetSingleton();
    if (!sky || !sky->currentWeather) return;

    m_targetWeather = sky->currentWeather;
    m_original.ReadFromWeather(m_targetWeather);
    m_current = m_original;
    m_active = true;
    m_dirty = false;

    SKSE::log::info("WeatherEditor: captured '{}' (0x{:08X})",
        m_current.editorID, m_current.formID);
}

void WeatherEditor::ApplyToGame()
{
    if (!m_targetWeather) return;
    m_current.WriteToWeather(m_targetWeather);
    m_dirty = false;
}

void WeatherEditor::RevertToOriginal()
{
    if (!m_targetWeather) return;
    m_current = m_original;
    m_current.WriteToWeather(m_targetWeather);
    m_dirty = false;
    SKSE::log::info("WeatherEditor: reverted '{}' to original", m_current.editorID);
}

void WeatherEditor::ForceWeather(RE::TESWeather* w)
{
    if (!w) return;
    auto* sky = RE::Sky::GetSingleton();
    if (!sky) return;
    sky->ForceWeather(w, true);
    SKSE::log::info("WeatherEditor: forced weather 0x{:08X}", w->GetFormID());
}

void WeatherEditor::ClearForcedWeather()
{
    auto* sky = RE::Sky::GetSingleton();
    if (!sky) return;
    sky->ForceWeather(nullptr, false);
    SKSE::log::info("WeatherEditor: cleared forced weather");
}

// ═════════════════════════════════════════════════════════════════════════════
//  Preset INI Save / Load
// ═════════════════════════════════════════════════════════════════════════════

std::filesystem::path WeatherEditor::GetPresetPath(const std::string& name) const
{
    std::string safeName = name.empty() ? m_current.editorID : name;
    return m_presetDir / (safeName + ".ini");
}

bool WeatherEditor::SavePreset(const std::string& name)
{
    auto path = GetPresetPath(name);
    if (SaveSnapshotToINI(m_current, path)) {
        SKSE::log::info("WeatherEditor: saved preset '{}'", path.string());
        return true;
    }
    return false;
}

bool WeatherEditor::LoadPreset(const std::string& name)
{
    auto path = GetPresetPath(name);
    if (LoadSnapshotFromINI(m_current, path)) {
        m_dirty = true;
        SKSE::log::info("WeatherEditor: loaded preset '{}'", path.string());
        return true;
    }
    return false;
}

bool WeatherEditor::DeletePreset(const std::string& name)
{
    auto path = GetPresetPath(name);
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
        SKSE::log::info("WeatherEditor: deleted preset '{}'", path.string());
        return true;
    }
    return false;
}

std::vector<std::string> WeatherEditor::ListPresets() const
{
    std::vector<std::string> result;
    if (!std::filesystem::exists(m_presetDir)) return result;
    for (auto& entry : std::filesystem::directory_iterator(m_presetDir)) {
        if (entry.path().extension() == ".ini")
            result.push_back(entry.path().stem().string());
    }
    std::sort(result.begin(), result.end());
    return result;
}

// ── INI format ──────────────────────────────────────────────────────────────

bool WeatherEditor::SaveSnapshotToINI(const WeatherSnapshot& s, const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f << "; SkyrimBridge Weather Preset\n";
    f << "; Weather: " << s.editorID << " (0x" << std::hex << s.formID << std::dec << ")\n\n";

    // Identity
    f << "[Identity]\n";
    f << "FormID = 0x" << std::hex << s.formID << std::dec << "\n";
    f << "EditorID = " << s.editorID << "\n\n";

    // Colors (compact: one line per color×tod)
    f << "[Colors]\n";
    for (int ct = 0; ct < kColorTypeCount; ++ct) {
        for (int td = 0; td < kToDCount; ++td) {
            auto& c = s.colors[ct][td];
            f << kColorTypeNames[ct] << "_" << kToDNames[td]
              << " = " << c.r << ", " << c.g << ", " << c.b << ", " << c.a << "\n";
        }
    }
    f << "\n";

    // Fog
    f << "[Fog]\n";
    f << "DayNear = " << s.fogDayNear << "\nDayFar = " << s.fogDayFar << "\n";
    f << "NightNear = " << s.fogNightNear << "\nNightFar = " << s.fogNightFar << "\n";
    f << "DayPower = " << s.fogDayPower << "\nNightPower = " << s.fogNightPower << "\n";
    f << "DayMax = " << s.fogDayMax << "\nNightMax = " << s.fogNightMax << "\n\n";

    // Weather data
    f << "[WeatherData]\n";
    f << "WindSpeed = " << s.windSpeed << "\nWindDirection = " << s.windDirection << "\n";
    f << "WindDirRange = " << s.windDirRange << "\nTransDelta = " << s.transDelta << "\n";
    f << "SunGlare = " << s.sunGlare << "\nSunDamage = " << s.sunDamage << "\n";
    f << "PrecipBeginFadeIn = " << s.precipBeginFadeIn << "\n";
    f << "PrecipEndFadeOut = " << s.precipEndFadeOut << "\n";
    f << "ThunderBeginFadeIn = " << s.thunderBeginFadeIn << "\n";
    f << "ThunderEndFadeOut = " << s.thunderEndFadeOut << "\n";
    f << "ThunderFrequency = " << s.thunderFrequency << "\n";
    f << "VisualEffectBegin = " << s.visualEffectBegin << "\n";
    f << "VisualEffectEnd = " << s.visualEffectEnd << "\n";
    f << "Flags = " << static_cast<int>(s.flags) << "\n";
    f << "LightningColor = " << s.lightningColor.r << ", "
      << s.lightningColor.g << ", " << s.lightningColor.b << "\n\n";

    // Cloud layers
    f << "[Clouds]\n";
    f << "NumLayers = " << s.numCloudLayers << "\n";
    for (int i = 0; i < 32; ++i) {
        auto& cl = s.clouds[i];
        f << "L" << i << "_SpeedX = " << cl.speedX << "\n";
        f << "L" << i << "_SpeedY = " << cl.speedY << "\n";
        f << "L" << i << "_Enabled = " << (cl.enabled ? 1 : 0) << "\n";
        f << "L" << i << "_Alpha = " << cl.alpha[0] << ", " << cl.alpha[1]
          << ", " << cl.alpha[2] << ", " << cl.alpha[3] << "\n";
    }
    f << "\n";

    // Directional ambient (per ToD)
    f << "[DirectionalAmbient]\n";
    for (int td = 0; td < kToDCount; ++td) {
        auto& da = s.dirAmbient[td];
        auto W = [&](const char* axis, const ColorF& c) {
            f << axis << "_" << kToDNames[td] << " = "
              << c.r << ", " << c.g << ", " << c.b << ", " << c.a << "\n";
        };
        W("XMax", da.xMax); W("XMin", da.xMin);
        W("YMax", da.yMax); W("YMin", da.yMin);
        W("ZMax", da.zMax); W("ZMin", da.zMin);
        W("Specular", da.specular);
        f << "FresnelPower_" << kToDNames[td] << " = " << da.fresnelPower << "\n";
    }
    f << "\n";

    // ImageSpace (per ToD)
    for (int td = 0; td < kToDCount; ++td) {
        auto& is = s.imageSpaces[td];
        if (!is.valid) continue;
        f << "[ImageSpace_" << kToDNames[td] << "]\n";
        f << "FormID = 0x" << std::hex << is.formID << std::dec << "\n";
        f << "EyeAdaptSpeed = " << is.eyeAdaptSpeed << "\n";
        f << "BloomBlurRadius = " << is.bloomBlurRadius << "\n";
        f << "BloomThreshold = " << is.bloomThreshold << "\n";
        f << "BloomScale = " << is.bloomScale << "\n";
        f << "ReceiveBloomThreshold = " << is.receiveBloomThreshold << "\n";
        f << "White = " << is.white << "\n";
        f << "SunlightScale = " << is.sunlightScale << "\n";
        f << "SkyScale = " << is.skyScale << "\n";
        f << "EyeAdaptStrength = " << is.eyeAdaptStrength << "\n";
        f << "Saturation = " << is.saturation << "\n";
        f << "Brightness = " << is.brightness << "\n";
        f << "Contrast = " << is.contrast << "\n";
        f << "TintAmount = " << is.tintAmount << "\n";
        f << "TintColor = " << is.tintR << ", " << is.tintG << ", " << is.tintB << "\n";
        f << "DOFStrength = " << is.dofStrength << "\n";
        f << "DOFDistance = " << is.dofDistance << "\n";
        f << "DOFRange = " << is.dofRange << "\n\n";
    }

    // Volumetric (per ToD)
    for (int td = 0; td < kToDCount; ++td) {
        auto& vl = s.volumetric[td];
        if (!vl.valid) continue;
        f << "[Volumetric_" << kToDNames[td] << "]\n";
        f << "FormID = 0x" << std::hex << vl.formID << std::dec << "\n";
        f << "Intensity = " << vl.intensity << "\n";
        f << "CustomColorContrib = " << vl.customColorContrib << "\n";
        f << "Color = " << vl.colorR << ", " << vl.colorG << ", " << vl.colorB << "\n";
        f << "DensityContrib = " << vl.densityContrib << "\n";
        f << "DensitySize = " << vl.densitySize << "\n";
        f << "DensityWindSpeed = " << vl.densityWindSpeed << "\n";
        f << "DensityFallingSpeed = " << vl.densityFallingSpeed << "\n";
        f << "PhaseContrib = " << vl.phaseContrib << "\n";
        f << "PhaseScattering = " << vl.phaseScattering << "\n";
        f << "SamplingRangeFactor = " << vl.samplingRangeFactor << "\n\n";
    }

    return true;
}

static bool ParseFloats(const std::string& val, float* out, int count)
{
    std::istringstream ss(val);
    for (int i = 0; i < count; ++i) {
        if (!(ss >> out[i])) return false;
        char comma;
        ss >> comma;
    }
    return true;
}

static float ReadFloat(const std::string& val)
{
    try { return std::stof(val); }
    catch (...) { return 0.0f; }
}

bool WeatherEditor::LoadSnapshotFromINI(WeatherSnapshot& s, const std::filesystem::path& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string section;
    std::string line;

    while (std::getline(f, line)) {
        auto trimmed = TrimWS(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') continue;

        if (trimmed[0] == '[') {
            auto close = trimmed.find(']');
            section = (close != std::string::npos) ? trimmed.substr(1, close - 1) : trimmed.substr(1);
            continue;
        }

        auto eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        auto key = TrimWS(trimmed.substr(0, eq));
        auto val = TrimWS(trimmed.substr(eq + 1));

        if (section == "Colors") {
            // Parse "Sky Upper_Day = r, g, b, a"
            for (int ct = 0; ct < kColorTypeCount; ++ct) {
                for (int td = 0; td < kToDCount; ++td) {
                    std::string expected = std::string(kColorTypeNames[ct]) + "_" + kToDNames[td];
                    if (key == expected) {
                        float rgba[4];
                        if (ParseFloats(val, rgba, 4)) {
                            s.colors[ct][td] = { rgba[0], rgba[1], rgba[2], rgba[3] };
                        }
                    }
                }
            }
        }
        else if (section == "Fog") {
            if (key == "DayNear")      s.fogDayNear = ReadFloat(val);
            else if (key == "DayFar")  s.fogDayFar = ReadFloat(val);
            else if (key == "NightNear") s.fogNightNear = ReadFloat(val);
            else if (key == "NightFar")  s.fogNightFar = ReadFloat(val);
            else if (key == "DayPower")  s.fogDayPower = ReadFloat(val);
            else if (key == "NightPower") s.fogNightPower = ReadFloat(val);
            else if (key == "DayMax")    s.fogDayMax = ReadFloat(val);
            else if (key == "NightMax")  s.fogNightMax = ReadFloat(val);
        }
        else if (section == "WeatherData") {
            if (key == "WindSpeed")        s.windSpeed = ReadFloat(val);
            else if (key == "WindDirection") s.windDirection = ReadFloat(val);
            else if (key == "WindDirRange")  s.windDirRange = ReadFloat(val);
            else if (key == "TransDelta")    s.transDelta = ReadFloat(val);
            else if (key == "SunGlare")      s.sunGlare = ReadFloat(val);
            else if (key == "SunDamage")     s.sunDamage = ReadFloat(val);
            else if (key == "PrecipBeginFadeIn") s.precipBeginFadeIn = ReadFloat(val);
            else if (key == "PrecipEndFadeOut")  s.precipEndFadeOut = ReadFloat(val);
            else if (key == "ThunderBeginFadeIn") s.thunderBeginFadeIn = ReadFloat(val);
            else if (key == "ThunderEndFadeOut")  s.thunderEndFadeOut = ReadFloat(val);
            else if (key == "ThunderFrequency")   s.thunderFrequency = ReadFloat(val);
            else if (key == "VisualEffectBegin")  s.visualEffectBegin = ReadFloat(val);
            else if (key == "VisualEffectEnd")    s.visualEffectEnd = ReadFloat(val);
            else if (key == "Flags") {
                try { s.flags = static_cast<uint8_t>(std::stoi(val)); } catch (...) {}
            }
            else if (key == "LightningColor") {
                float rgb[3];
                if (ParseFloats(val, rgb, 3))
                    s.lightningColor = { rgb[0], rgb[1], rgb[2], 1.0f };
            }
        }
        else if (section == "Clouds") {
            if (key == "NumLayers") {
                try { s.numCloudLayers = static_cast<uint32_t>(std::stoi(val)); } catch (...) {}
            } else {
                for (int i = 0; i < 32; ++i) {
                    char prefix[8];
                    snprintf(prefix, sizeof(prefix), "L%d_", i);
                    size_t plen = strlen(prefix);
                    if (key.compare(0, plen, prefix) != 0) continue;
                    auto sub = key.substr(plen);
                    if (sub == "SpeedX")       s.clouds[i].speedX = ReadFloat(val);
                    else if (sub == "SpeedY")  s.clouds[i].speedY = ReadFloat(val);
                    else if (sub == "Enabled") s.clouds[i].enabled = (ReadFloat(val) > 0.5f);
                    else if (sub == "Alpha") {
                        float a[4];
                        if (ParseFloats(val, a, 4))
                            for (int td = 0; td < 4; ++td) s.clouds[i].alpha[td] = a[td];
                    }
                    break;
                }
            }
        }
        else if (section == "DirectionalAmbient") {
            for (int td = 0; td < kToDCount; ++td) {
                auto& da = s.dirAmbient[td];
                auto tryColor = [&](const char* axis, ColorF& c) {
                    std::string exp = std::string(axis) + "_" + kToDNames[td];
                    if (key == exp) {
                        float rgba[4];
                        if (ParseFloats(val, rgba, 4))
                            c = { rgba[0], rgba[1], rgba[2], rgba[3] };
                    }
                };
                tryColor("XMax", da.xMax); tryColor("XMin", da.xMin);
                tryColor("YMax", da.yMax); tryColor("YMin", da.yMin);
                tryColor("ZMax", da.zMax); tryColor("ZMin", da.zMin);
                tryColor("Specular", da.specular);
                std::string fp = std::string("FresnelPower_") + kToDNames[td];
                if (key == fp) da.fresnelPower = ReadFloat(val);
            }
        }
        else if (section.rfind("ImageSpace_", 0) == 0) {
            int td = -1;
            for (int i = 0; i < kToDCount; ++i)
                if (section == std::string("ImageSpace_") + kToDNames[i]) td = i;
            if (td < 0) continue;
            auto& is = s.imageSpaces[td];
            is.valid = true;
            if (key == "EyeAdaptSpeed")    is.eyeAdaptSpeed = ReadFloat(val);
            else if (key == "BloomBlurRadius")  is.bloomBlurRadius = ReadFloat(val);
            else if (key == "BloomThreshold")   is.bloomThreshold = ReadFloat(val);
            else if (key == "BloomScale")       is.bloomScale = ReadFloat(val);
            else if (key == "ReceiveBloomThreshold") is.receiveBloomThreshold = ReadFloat(val);
            else if (key == "White")            is.white = ReadFloat(val);
            else if (key == "SunlightScale")    is.sunlightScale = ReadFloat(val);
            else if (key == "SkyScale")         is.skyScale = ReadFloat(val);
            else if (key == "EyeAdaptStrength") is.eyeAdaptStrength = ReadFloat(val);
            else if (key == "Saturation")       is.saturation = ReadFloat(val);
            else if (key == "Brightness")       is.brightness = ReadFloat(val);
            else if (key == "Contrast")         is.contrast = ReadFloat(val);
            else if (key == "TintAmount")       is.tintAmount = ReadFloat(val);
            else if (key == "TintColor") {
                float rgb[3];
                if (ParseFloats(val, rgb, 3)) { is.tintR = rgb[0]; is.tintG = rgb[1]; is.tintB = rgb[2]; }
            }
            else if (key == "DOFStrength")  is.dofStrength = ReadFloat(val);
            else if (key == "DOFDistance")   is.dofDistance = ReadFloat(val);
            else if (key == "DOFRange")      is.dofRange = ReadFloat(val);
        }
        else if (section.rfind("Volumetric_", 0) == 0) {
            int td = -1;
            for (int i = 0; i < kToDCount; ++i)
                if (section == std::string("Volumetric_") + kToDNames[i]) td = i;
            if (td < 0) continue;
            auto& vl = s.volumetric[td];
            vl.valid = true;
            if (key == "Intensity")          vl.intensity = ReadFloat(val);
            else if (key == "CustomColorContrib") vl.customColorContrib = ReadFloat(val);
            else if (key == "Color") {
                float rgb[3];
                if (ParseFloats(val, rgb, 3)) { vl.colorR = rgb[0]; vl.colorG = rgb[1]; vl.colorB = rgb[2]; }
            }
            else if (key == "DensityContrib")      vl.densityContrib = ReadFloat(val);
            else if (key == "DensitySize")          vl.densitySize = ReadFloat(val);
            else if (key == "DensityWindSpeed")     vl.densityWindSpeed = ReadFloat(val);
            else if (key == "DensityFallingSpeed")  vl.densityFallingSpeed = ReadFloat(val);
            else if (key == "PhaseContrib")         vl.phaseContrib = ReadFloat(val);
            else if (key == "PhaseScattering")      vl.phaseScattering = ReadFloat(val);
            else if (key == "SamplingRangeFactor")  vl.samplingRangeFactor = ReadFloat(val);
        }
    }

    return true;
}

}  // namespace SB
